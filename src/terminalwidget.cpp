#include "terminalwidget.h"
#include "ptyprocess.h"
#include "terminalscreen.h"

#include <QPainter>
#include <QFontMetrics>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QApplication>
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QTimer>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScrollBar>
#include <algorithm>

TerminalWidget::TerminalWidget(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::IBeamCursor);

    m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_font.setPointSize(11);
    recomputeCellMetrics();

    screen = new TerminalScreen(24, 80, this);
    connect(screen, &TerminalScreen::screenChanged, this, &TerminalWidget::onScreenChanged);
    connect(screen, &TerminalScreen::titleChanged, this, &TerminalWidget::titleChanged);
    connect(screen, &TerminalScreen::bell, this, [this]() { QApplication::beep(); });
    connect(screen, &TerminalScreen::hostResponse, this, [this](const QByteArray &data) { sendToShell(data); });

    pty = new Pty(this);
    connect(pty, &Pty::readyRead, this, [this](const QByteArray &data) {
        screen->feed(data);
        if (!m_captures.isEmpty())
            feedCaptures(data);
    });
    connect(pty, &Pty::finished, this, &TerminalWidget::onPtyFinished);

    // Ascenseur vertical (scrollback), à droite du widget. Positionné manuellement dans
    // resizeEvent() (TerminalWidget dessine tout lui-même via paintEvent, pas de layout).
    m_scrollBar = new QScrollBar(Qt::Vertical, this);
    m_scrollBar->setRange(0, 0);
    connect(m_scrollBar, &QScrollBar::valueChanged, this, &TerminalWidget::onScrollBarValueChanged);

    auto *blinkTimer = new QTimer(this);
    connect(blinkTimer, &QTimer::timeout, this, &TerminalWidget::onCursorBlinkTimeout);
    blinkTimer->start(600);
}

TerminalWidget::~TerminalWidget() = default;

void TerminalWidget::recomputeCellMetrics()
{
    const QFontMetrics fm(m_font);
    m_cellW = std::max(1, fm.horizontalAdvance(QLatin1Char('M')));
    m_cellH = std::max(1, fm.height());
    m_ascent = fm.ascent();
}

void TerminalWidget::startShell(const QString &workingDir)
{
    // ~ TFLinuxTerm.CreateShell, mais sur un vrai pty : le shell voit un terminal
    // normal (isatty=true), donc plus besoin de FAwaitingSudoPassword / RewriteSudoCommand
    // etc. côté applicatif -- sudo gère nativement la saisie du mot de passe via le pty.
    QString shell = QProcessEnvironment::systemEnvironment().value("SHELL");
    if (shell.isEmpty())
        shell = "/bin/bash";

    startProgram(shell, {"-i"}, workingDir);
}

void TerminalWidget::startProgram(const QString &program, const QStringList &args,
                                   const QString &workingDir)
{
    // Si un programme précédent tourne encore sur ce pty (ex: reconnexion sans avoir
    // laissé le temps au signal `finished` de nettoyer), on le termine proprement avant
    // d'en relancer un nouveau.
    if (pty->isRunning())
        pty->terminate();

    const int scrollBarW = m_scrollBar ? m_scrollBar->sizeHint().width() : 0;
    const int rows = std::max(1, height() / m_cellH);
    const int cols = std::max(1, (width() - scrollBarW) / m_cellW);
    screen->resizeScreen(rows, cols);
    pty->start(program, args, rows, cols, workingDir);
}

void TerminalWidget::stopProgram()
{
    if (pty->isRunning())
        pty->terminate();
}

bool TerminalWidget::isRunning() const
{
    return pty && pty->isRunning();
}

void TerminalWidget::resetTerminal()
{
    // ESC c = RIS (Reset to Initial State) : géré nativement par TerminalScreen,
    // efface écran + scrollback et remet les attributs par défaut.
    // Note : "\x1b" et "c" sont séparés en deux littéraux concaténés, car 'c' est un
    // chiffre hexadécimal valide -- "\x1bc" serait lu comme un seul échappement hors
    // plage (\x1bc) plutôt que ESC suivi du caractère 'c'.
    screen->feed(QByteArrayLiteral("\x1b" "c"));
}

void TerminalWidget::applySizeToScreenAndPty()
{
    const int scrollBarW = m_scrollBar ? m_scrollBar->sizeHint().width() : 0;
    const int rows = std::max(1, height() / m_cellH);
    const int cols = std::max(1, (width() - scrollBarW) / m_cellW);
    if (rows != screen->rows() || cols != screen->cols()) {
        screen->resizeScreen(rows, cols);
        if (pty->isRunning())
            pty->resize(rows, cols);
    }
}

void TerminalWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_scrollBar) {
        const int w = m_scrollBar->sizeHint().width();
        m_scrollBar->setGeometry(width() - w, 0, w, height());
    }
    applySizeToScreenAndPty();
}

void TerminalWidget::onScreenChanged()
{
    syncScrollBar();
    update();
}

void TerminalWidget::onPtyFinished()
{
    emit shellFinished();
}

void TerminalWidget::onCursorBlinkTimeout()
{
    m_cursorBlinkOn = !m_cursorBlinkOn;
    if (m_scrollOffset == 0)
        update();
}

int TerminalWidget::firstVisibleGlobalLine() const
{
    if (screen->isAlternateScreen())
        return 0;
    const int sbSize = screen->scrollback().size();
    return std::max(0, sbSize - m_scrollOffset);
}

const TermLine &TerminalWidget::lineAtGlobal(int global) const
{
    static const TermLine empty;
    if (screen->isAlternateScreen())
        return screen->lineAt(global);

    const auto &sb = screen->scrollback();
    if (global < 0)
        return empty;
    if (global < sb.size())
        return sb.at(global);
    return screen->lineAt(global - sb.size());
}

void TerminalWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setFont(m_font);
    painter.fillRect(rect(), QColor(0, 0, 0));

    const int firstGlobal = firstVisibleGlobalLine();
    const int rows = screen->rows();
    const int cols = screen->cols();

    const int selMinLine = std::min(m_selStart.y(), m_selEnd.y());
    const int selMaxLine = std::max(m_selStart.y(), m_selEnd.y());

    for (int r = 0; r < rows; ++r) {
        const int globalLine = firstGlobal + r;
        const TermLine &line = lineAtGlobal(globalLine);
        const int y = r * m_cellH;

        // sélection sur cette ligne ?
        int selColStart = -1, selColEnd = -1;
        if (m_hasSelection && globalLine >= selMinLine && globalLine <= selMaxLine) {
            const bool singleLine = (selMinLine == selMaxLine);
            const QPoint &lo = (m_selStart.y() <= m_selEnd.y()) ? m_selStart : m_selEnd;
            const QPoint &hi = (m_selStart.y() <= m_selEnd.y()) ? m_selEnd : m_selStart;
            if (singleLine) { selColStart = std::min(lo.x(), hi.x()); selColEnd = std::max(lo.x(), hi.x()); }
            else if (globalLine == selMinLine) { selColStart = lo.x(); selColEnd = cols; }
            else if (globalLine == selMaxLine) { selColStart = 0; selColEnd = hi.x(); }
            else { selColStart = 0; selColEnd = cols; }
        }

        for (int c = 0; c < cols && c < line.size(); ++c) {
            const TermCell &cell = line.at(c);
            QRgb fg = cell.fg, bg = cell.bg;
            if (cell.inverse) std::swap(fg, bg);
            if (selColStart >= 0 && c >= selColStart && c < selColEnd) {
                bg = qRgb(80, 120, 200);
                fg = qRgb(255, 255, 255);
            }

            const QRect cellRect(c * m_cellW, y, m_cellW, m_cellH);
            if (bg != qRgb(0, 0, 0))
                painter.fillRect(cellRect, QColor(bg));

            if (cell.ch != QLatin1Char(' ') && !cell.ch.isNull()) {
                QFont f = m_font;
                if (cell.bold) f.setBold(true);
                if (cell.italic) f.setItalic(true);
                if (cell.underline) f.setUnderline(true);
                if (cell.strike) f.setStrikeOut(true);
                painter.setFont(f);
                painter.setPen(QColor(fg));
                painter.drawText(cellRect.x(), y + m_ascent, cell.ch);
            }
        }

        // curseur (uniquement visible quand on regarde le bas "temps réel", pas le scrollback)
        if (m_scrollOffset == 0 && r == screen->cursorRow() && screen->cursorVisible()) {
            const QRect cursorRect(screen->cursorCol() * m_cellW, y, m_cellW, m_cellH);
            if (hasFocus()) {
                if (m_cursorBlinkOn)
                    painter.fillRect(cursorRect, QColor(200, 200, 200, 160));
            } else {
                painter.setPen(QColor(200, 200, 200));
                painter.drawRect(cursorRect.adjusted(0, 0, -1, -1));
            }
        }
    }
}

QPoint TerminalWidget::posToCell(const QPoint &pos) const
{
    const int col = std::clamp(pos.x() / m_cellW, 0, screen->cols() - 1);
    const int row = std::clamp(pos.y() / m_cellH, 0, screen->rows() - 1);
    return QPoint(col, firstVisibleGlobalLine() + row);
}

void TerminalWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    if (event->button() == Qt::LeftButton) {
        m_selecting = true;
        m_hasSelection = false;
        m_selStart = m_selEnd = posToCell(event->pos());
        update();
    }
    QWidget::mousePressEvent(event);
}

void TerminalWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_selecting) {
        m_selEnd = posToCell(event->pos());
        m_hasSelection = (m_selStart != m_selEnd);
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_selecting = false;
    QWidget::mouseReleaseEvent(event);
}

void TerminalWidget::wheelEvent(QWheelEvent *event)
{
    const int steps = event->angleDelta().y() / 120;
    if (screen->isAlternateScreen()) {
        // Dans une appli plein écran (less/vim/htop...), on relaie la molette
        // comme des touches flèche, à l'image d'un vrai terminal.
        const QByteArray seq = screen->appCursorKeys() ? "\x1bOA" : "\x1b[A";
        const QByteArray seqDown = screen->appCursorKeys() ? "\x1bOB" : "\x1b[B";
        for (int i = 0; i < std::abs(steps) * 3; ++i)
            sendToShell(steps > 0 ? seq : seqDown);
    } else {
        // molette vers le haut (steps > 0) -> remonte dans l'historique (defile vers le
        // haut) ; molette vers le bas (steps < 0) -> redescend vers le direct. Le signe
        // était inversé auparavant (bug corrigé).
        scrollViewport(steps * 3);
    }
    event->accept();
}

void TerminalWidget::scrollViewport(int deltaLines)
{
    const int maxOffset = screen->scrollback().size();
    m_scrollOffset = std::clamp(m_scrollOffset + deltaLines, 0, maxOffset);
    syncScrollBar();
    update();
}

void TerminalWidget::syncScrollBar()
{
    if (!m_scrollBar)
        return;
    m_updatingScrollBar = true;
    const int maxOffset = screen->scrollback().size();
    if (screen->isAlternateScreen()) {
        // Pas de scrollback pertinent pendant une appli plein écran (vim/htop/less...) :
        // ascenseur désactivé plutôt que masqué, pour éviter un redimensionnement du
        // texte affiché (le nombre de colonnes reste stable, l'ascenseur garde sa place).
        m_scrollBar->setEnabled(false);
        m_scrollBar->setRange(0, 0);
    } else {
        m_scrollBar->setEnabled(maxOffset > 0);
        m_scrollBar->setRange(0, maxOffset);
        m_scrollBar->setPageStep(std::max(1, screen->rows()));
        // Convention ascenseur standard : valeur minimale = haut (plus ancien), valeur
        // maximale = bas (direct). m_scrollOffset=0 (direct) doit donc correspondre à
        // la valeur MAXIMALE de l'ascenseur.
        m_scrollBar->setValue(maxOffset - m_scrollOffset);
    }
    m_updatingScrollBar = false;
}

void TerminalWidget::onScrollBarValueChanged(int value)
{
    if (m_updatingScrollBar)
        return; // évite la boucle : notre propre syncScrollBar() déclenche aussi ce signal
    const int maxOffset = screen->scrollback().size();
    m_scrollOffset = std::clamp(maxOffset - value, 0, maxOffset);
    update();
}

void TerminalWidget::resetScrollToBottom()
{
    if (m_scrollOffset != 0) {
        m_scrollOffset = 0;
        syncScrollBar();
        update();
    }
}

void TerminalWidget::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    update();
}

void TerminalWidget::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    update();
}

void TerminalWidget::sendToShell(const QByteArray &bytes)
{
    if (pty && pty->isRunning())
        pty->writeData(bytes);
}

void TerminalWidget::keyPressEvent(QKeyEvent *event)
{
    const auto mods = event->modifiers();

    // Ctrl+C : la commande suivie est interrompue, le shell n'affichera jamais le marqueur de fin
    // (bash abandonne le reste de la ligne) -> on rend tout de suite la sortie partielle.
    if (!m_captures.isEmpty() && (mods & Qt::ControlModifier) && !(mods & (Qt::ShiftModifier | Qt::AltModifier))
        && event->key() == Qt::Key_C)
        finishAllCaptures(130);

    // Ctrl+Shift+S : copie la SÉLECTION en cours sous forme d'image dans le presse-papier
    if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier) && event->key() == Qt::Key_S) {
        copySelectionAsImage();
        event->accept();
        return;
    }
    // Ctrl+Alt+S : même image, mais insérée directement dans la note ouverte (v1.4.0)
    if ((mods & Qt::ControlModifier) && (mods & Qt::AltModifier) && !(mods & Qt::ShiftModifier)
        && event->key() == Qt::Key_S) {
        copySelectionAsImageToNote();
        event->accept();
        return;
    }
    // Ctrl+Alt+C : texte sélectionné inséré directement dans la note ouverte (v1.5.0)
    if ((mods & Qt::ControlModifier) && (mods & Qt::AltModifier) && !(mods & Qt::ShiftModifier)
        && event->key() == Qt::Key_C) {
        copySelectionTextToNote();
        event->accept();
        return;
    }
    if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier) && event->key() == Qt::Key_C) {
        copySelection();
        event->accept();
        return;
    }
    if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier) && event->key() == Qt::Key_V) {
        pasteFromClipboard();
        event->accept();
        return;
    }

    const bool appMode = screen->appCursorKeys();
    QByteArray toSend;

    switch (event->key()) {
    case Qt::Key_Up:    toSend = appMode ? "\x1bOA" : "\x1b[A"; break;
    case Qt::Key_Down:  toSend = appMode ? "\x1bOB" : "\x1b[B"; break;
    case Qt::Key_Right: toSend = appMode ? "\x1bOC" : "\x1b[C"; break;
    case Qt::Key_Left:  toSend = appMode ? "\x1bOD" : "\x1b[D"; break;
    case Qt::Key_Home:  toSend = "\x1b[H"; break;
    case Qt::Key_End:   toSend = "\x1b[F"; break;
    case Qt::Key_PageUp:
        if (mods & Qt::ShiftModifier) { scrollViewport(screen->rows()); event->accept(); return; }
        toSend = "\x1b[5~";
        break;
    case Qt::Key_PageDown:
        if (mods & Qt::ShiftModifier) { scrollViewport(-screen->rows()); event->accept(); return; }
        toSend = "\x1b[6~";
        break;
    case Qt::Key_Insert:    toSend = "\x1b[2~"; break;
    case Qt::Key_Delete:    toSend = "\x1b[3~"; break;
    case Qt::Key_Backspace: toSend = "\x7f"; break;
    case Qt::Key_Tab:       toSend = "\t"; break;
    case Qt::Key_Return:
    case Qt::Key_Enter:     toSend = "\r"; break;
    case Qt::Key_Escape:    toSend = "\x1b"; break;
    case Qt::Key_F1:  toSend = "\x1bOP"; break;
    case Qt::Key_F2:  toSend = "\x1bOQ"; break;
    case Qt::Key_F3:  toSend = "\x1bOR"; break;
    case Qt::Key_F4:  toSend = "\x1bOS"; break;
    case Qt::Key_F5:  toSend = "\x1b[15~"; break;
    case Qt::Key_F6:  toSend = "\x1b[17~"; break;
    case Qt::Key_F7:  toSend = "\x1b[18~"; break;
    case Qt::Key_F8:  toSend = "\x1b[19~"; break;
    case Qt::Key_F9:  toSend = "\x1b[20~"; break;
    case Qt::Key_F10: toSend = "\x1b[21~"; break;
    case Qt::Key_F11: toSend = "\x1b[23~"; break;
    case Qt::Key_F12: toSend = "\x1b[24~"; break;
    default: break;
    }

    if (toSend.isEmpty()) {
        if ((mods & Qt::ControlModifier) && !(mods & Qt::ShiftModifier) &&
            event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z) {
            const char code = static_cast<char>(event->key() - Qt::Key_A + 1);
            toSend = QByteArray(1, code);
        } else {
            const QString text = event->text();
            if (!text.isEmpty())
                toSend = text.toUtf8();
        }
    }

    if (!toSend.isEmpty()) {
        resetScrollToBottom();
        sendToShell(toSend);
    }
    event->accept();
}

bool TerminalWidget::hasSelection() const
{
    return m_hasSelection;
}

QString TerminalWidget::selectedText() const
{
    if (!m_hasSelection)
        return QString();

    const int selMinLine = std::min(m_selStart.y(), m_selEnd.y());
    const int selMaxLine = std::max(m_selStart.y(), m_selEnd.y());
    const bool singleLine = (selMinLine == selMaxLine);
    const QPoint &lo = (m_selStart.y() <= m_selEnd.y()) ? m_selStart : m_selEnd;
    const QPoint &hi = (m_selStart.y() <= m_selEnd.y()) ? m_selEnd : m_selStart;

    // Une ligne affichée qui n'est que la SUITE d'un retour à la ligne automatique (texte
    // plus long que la largeur du terminal) est re-jointe à la précédente : le texte copié
    // ne contient un saut de ligne que là où il y en a eu un vrai.
    QString out;
    for (int gl = selMinLine; gl <= selMaxLine; ++gl) {
        const TermLine &line = lineAtGlobal(gl);
        int colStart = 0, colEnd = line.size();
        if (singleLine) { colStart = std::min(lo.x(), hi.x()); colEnd = std::max(lo.x(), hi.x()); }
        else if (gl == selMinLine) { colStart = lo.x(); colEnd = line.size(); }
        else if (gl == selMaxLine) { colStart = 0; colEnd = hi.x(); }

        QString text;
        for (int c = colStart; c < colEnd && c < line.size(); ++c)
            text += line.at(c).ch;

        const bool continuesOnNextLine = (gl < selMaxLine) && !line.isEmpty() && line.last().wrapped;
        if (!continuesOnNextLine)
            text.replace(QRegularExpression("\\s+$"), QString()); // rtrim (sauf ligne coupée : les espaces y sont réels)
        out += text;
        if (gl < selMaxLine && !continuesOnNextLine)
            out += QLatin1Char('\n');
    }
    return out;
}

void TerminalWidget::copySelection()
{
    const QString text = selectedText();
    if (!text.isEmpty())
        QGuiApplication::clipboard()->setText(text);
}

void TerminalWidget::pasteFromClipboard()
{
    const QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty())
        return;
    QByteArray data = text.toUtf8();
    if (screen->bracketedPaste()) {
        QByteArray wrapped = "\x1b[200~";
        wrapped += data;
        wrapped += "\x1b[201~";
        data = wrapped;
    }
    resetScrollToBottom();
    sendToShell(data);
}

QImage TerminalWidget::renderSelectionImage() const
{
    // Ctrl+Shift+S : rend uniquement les caractères sélectionnés sous forme d'image
    // (les cellules hors sélection sur les lignes concernées restent en fond neutre),
    // conservant l'alignement en colonnes tel qu'affiché à l'écran.
    //
    // Depuis la v1.3.0 : la largeur de l'image n'est plus celle du terminal, mais celle du
    // texte le plus long de la sélection (dernier caractère non blanc) + une marge totale
    // de 16 pixels (8 px de chaque côté), pour éviter des captures inutilement larges.
    if (!m_hasSelection)
        return QImage();

    constexpr int kMarginPx = 16; // marge horizontale totale (répartie moitié gauche / moitié droite)

    const int selMinLine = std::min(m_selStart.y(), m_selEnd.y());
    const int selMaxLine = std::max(m_selStart.y(), m_selEnd.y());
    const bool singleLine = (selMinLine == selMaxLine);
    const QPoint &lo = (m_selStart.y() <= m_selEnd.y()) ? m_selStart : m_selEnd;
    const QPoint &hi = (m_selStart.y() <= m_selEnd.y()) ? m_selEnd : m_selStart;

    const int nLines = selMaxLine - selMinLine + 1;

    // Plage de colonnes sélectionnée [colStart, colEnd) pour une ligne globale donnée.
    auto columnRange = [&](int gl, const TermLine &line, int &colStart, int &colEnd) {
        colStart = 0;
        colEnd = line.size();
        if (singleLine) { colStart = std::min(lo.x(), hi.x()); colEnd = std::max(lo.x(), hi.x()); }
        else if (gl == selMinLine) { colStart = lo.x(); colEnd = line.size(); }
        else if (gl == selMaxLine) { colStart = 0; colEnd = hi.x(); }
        colEnd = std::min(colEnd, static_cast<int>(line.size()));
    };

    // Passe 1 : largeur (en colonnes) du texte le plus long = dernier caractère non blanc
    // de chaque ligne, relativement au début de sélection de cette ligne.
    int maxTextCols = 0;
    for (int gl = selMinLine; gl <= selMaxLine; ++gl) {
        const TermLine &line = lineAtGlobal(gl);
        int colStart, colEnd;
        columnRange(gl, line, colStart, colEnd);
        for (int c = colEnd - 1; c >= colStart; --c) {
            const QChar ch = line.at(c).ch;
            if (!ch.isNull() && !ch.isSpace()) {
                maxTextCols = std::max(maxTextCols, (c - colStart) + 1);
                break;
            }
        }
    }
    if (maxTextCols == 0)
        return QImage(); // sélection entièrement blanche : rien d'utile à copier

    const QImage::Format fmt = QImage::Format_ARGB32_Premultiplied;
    QImage image(maxTextCols * m_cellW + kMarginPx, nLines * m_cellH, fmt);
    image.fill(Qt::black);

    // Passe 2 : rendu, décalé de la demi-marge à gauche.
    QPainter painter(&image);
    painter.setFont(m_font);
    const int xOffset = kMarginPx / 2;

    for (int gl = selMinLine; gl <= selMaxLine; ++gl) {
        const TermLine &line = lineAtGlobal(gl);
        int colStart, colEnd;
        columnRange(gl, line, colStart, colEnd);

        const int y = (gl - selMinLine) * m_cellH;
        for (int c = colStart; c < colEnd; ++c) {
            if ((c - colStart) >= maxTextCols)
                break; // au-delà du texte le plus long : espaces de fin, non rendus
            const TermCell &cell = line.at(c);
            QRgb fg = cell.fg, bg = cell.bg;
            if (cell.inverse) std::swap(fg, bg);

            const QRect cellRect(xOffset + (c - colStart) * m_cellW, y, m_cellW, m_cellH);
            painter.fillRect(cellRect, QColor(bg));
            if (cell.ch != QLatin1Char(' ') && !cell.ch.isNull()) {
                QFont f = m_font;
                if (cell.bold) f.setBold(true);
                if (cell.italic) f.setItalic(true);
                painter.setFont(f);
                painter.setPen(QColor(fg));
                painter.drawText(cellRect.x(), y + m_ascent, cell.ch);
            }
        }
    }
    painter.end();

    return image;
}

void TerminalWidget::copySelectionAsImage()
{
    // Ctrl+Shift+S : image de la sélection dans le presse-papier.
    const QImage image = renderSelectionImage();
    if (!image.isNull())
        QGuiApplication::clipboard()->setImage(image);
}

void TerminalWidget::copySelectionAsImageToNote()
{
    // Ctrl+Alt+S : même image que Ctrl+Shift+S, mais au lieu du presse-papier elle est
    // transmise (signal) à MainWindow qui l'insère dans la note ouverte, à la ligne
    // suivant la position du curseur.
    const QImage image = renderSelectionImage();
    if (!image.isNull())
        emit selectionImageForNote(image);
}

void TerminalWidget::copySelectionTextToNote()
{
    // Ctrl+Alt+C : le texte sélectionné (même contenu que Ctrl+Shift+C) est transmis
    // (signal) à MainWindow qui l'insère dans la note ouverte, à la ligne suivant la
    // position du curseur.
    const QString text = selectedText();
    if (!text.isEmpty())
        emit selectionTextForNote(text);
}

void TerminalWidget::clearSelection()
{
    m_hasSelection = false;
    update();
}

void TerminalWidget::sendCommand(const QString &command)
{
    // ~ envoie une commande depuis l'extérieur (menu Execute de l'éditeur Markdown),
    // comme si l'utilisateur l'avait tapée puis avait appuyé sur Entrée.
    resetScrollToBottom();
    sendToShell(command.toUtf8());
    sendToShell(QByteArrayLiteral("\r"));
}

// ======================= Capture de la sortie d'une commande (v2.6.0) =======================

namespace {
// Retire le commentaire shell final ("cmd # note") : un '#' hors guillemets, en début de ligne ou
// précédé d'un espace. Sans ça, le marqueur de fin ajouté après la commande serait avalé par le
// commentaire.
QString stripShellComment(const QString &s)
{
    bool sq = false, dq = false;
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('\\') && !sq) {
            ++i;
            continue;
        }
        if (c == QLatin1Char('\'') && !dq)
            sq = !sq;
        else if (c == QLatin1Char('"') && !sq)
            dq = !dq;
        else if (c == QLatin1Char('#') && !sq && !dq && (i == 0 || s.at(i - 1).isSpace()))
            return s.left(i);
    }
    return s;
}

constexpr int kCaptureMaxBytes = 8 * 1024 * 1024;
}

bool TerminalWidget::sendCommandCapture(const QString &command, int jobId)
{
    QString cmd = stripShellComment(command).trimmed();
    while (cmd.endsWith(QLatin1Char(';')))
        cmd = cmd.left(cmd.size() - 1).trimmed();

    if (cmd.isEmpty() || cmd.endsWith(QLatin1Char('\\')) || cmd.endsWith(QLatin1Char('|'))
        || cmd.endsWith(QLatin1String("&&"))) {
        sendCommand(command);
        return false;
    }

    // "cmd &" (arrière-plan) ne peut pas être suivi de "; " (erreur de syntaxe) : simple espace.
    const QString sep = cmd.endsWith(QLatin1Char('&')) ? QStringLiteral(" ") : QStringLiteral("; ");
    const QString id = QString::number(jobId);
    const QString line = QStringLiteral("printf '\\033]777;S;") + id + QStringLiteral("\\007'; ")
                         + cmd + sep
                         + QStringLiteral("printf '\\033]777;D;") + id + QStringLiteral(";%d\\007' $?");

    Capture c;
    c.id = jobId;
    m_captures.append(c);
    sendCommand(line);
    return true;
}

void TerminalWidget::feedCaptures(const QByteArray &data)
{
    for (int i = 0; i < m_captures.size();) {
        Capture &c = m_captures[i];
        const QByteArray id = QByteArray::number(c.id);
        c.buf += data;

        if (!c.started) {
            // Le marqueur de début n'est émis qu'au lancement de la commande : ce qui précède est
            // l'écho de la ligne tapée (qui contient le TEXTE du marqueur, pas l'octet ESC).
            const QByteArray startMark = QByteArrayLiteral("\x1b]777;S;") + id + QByteArrayLiteral("\x07");
            const int ix = c.buf.indexOf(startMark);
            if (ix >= 0) {
                c.started = true;
                c.buf = c.buf.mid(ix + startMark.size());
            } else if (c.buf.size() > 65536) {
                c.buf = c.buf.right(startMark.size()); // jamais démarré : on ne garde que de quoi recoller un marqueur coupé
            }
        }

        bool done = false;
        int status = 0;
        if (c.started) {
            const QByteArray endMark = QByteArrayLiteral("\x1b]777;D;") + id + QByteArrayLiteral(";");
            const int ix = c.buf.indexOf(endMark);
            if (ix >= 0) {
                const int bel = c.buf.indexOf('\x07', ix + endMark.size());
                if (bel >= 0) {
                    status = c.buf.mid(ix + endMark.size(), bel - ix - endMark.size()).toInt();
                    c.buf.truncate(ix);
                    done = true;
                }
            } else if (c.buf.size() > kCaptureMaxBytes) {
                c.buf = c.buf.right(kCaptureMaxBytes); // sortie énorme : on garde la fin
            }
        }

        if (done) {
            const Capture fin = m_captures.takeAt(i);
            emit commandCaptured(fin.id, status, renderCapturedOutput(fin.buf));
        } else {
            ++i;
        }
    }
}

void TerminalWidget::finishAllCaptures(int exitCode)
{
    const QList<Capture> pending = m_captures;
    m_captures.clear();
    for (const Capture &c : pending) {
        // Pas encore démarrée (ex. attente d'un mot de passe sudo) : rien à rendre.
        emit commandCaptured(c.id, exitCode, c.started ? renderCapturedOutput(c.buf) : QString());
    }
}

QString TerminalWidget::renderCapturedOutput(const QByteArray &raw) const
{
    // Rejoue les octets bruts (couleurs, retours chariot, effacements...) dans un écran
    // temporaire de même largeur que le terminal, puis lit le texte obtenu (scrollback + écran).
    TerminalScreen tmp(200, screen->cols());
    tmp.feed(raw);

    QStringList lines;
    QString current;
    auto pushLine = [&](const TermLine &l) {
        QString text;
        text.reserve(l.size());
        for (const TermCell &cell : l)
            text += cell.ch;
        const bool wrapped = !l.isEmpty() && l.last().wrapped;
        current += text;
        if (!wrapped) {
            while (current.endsWith(QLatin1Char(' ')))
                current.chop(1);
            lines << current;
            current.clear();
        }
    };
    for (const TermLine &l : tmp.scrollback())
        pushLine(l);
    for (int r = 0; r < tmp.rows(); ++r)
        pushLine(tmp.lineAt(r));
    if (!current.isEmpty())
        lines << current;

    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    int first = 0;
    while (first < lines.size() && lines.at(first).trimmed().isEmpty())
        ++first;
    return lines.mid(first).join(QLatin1Char('\n'));
}
