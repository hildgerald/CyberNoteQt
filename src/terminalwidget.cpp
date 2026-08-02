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
    connect(pty, &Pty::readyRead, this, [this](const QByteArray &data) { screen->feed(data); });
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

    const int scrollBarW = m_scrollBar ? m_scrollBar->sizeHint().width() : 0;
    const int rows = std::max(1, height() / m_cellH);
    const int cols = std::max(1, (width() - scrollBarW) / m_cellW);
    screen->resizeScreen(rows, cols);
    pty->start(shell, {"-i"}, rows, cols, workingDir);
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

    // Ctrl+Shift+S : copie la SÉLECTION en cours sous forme d'image dans le presse-papier
    if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier) && event->key() == Qt::Key_S) {
        copySelectionAsImage();
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

    QStringList lines;
    for (int gl = selMinLine; gl <= selMaxLine; ++gl) {
        const TermLine &line = lineAtGlobal(gl);
        int colStart = 0, colEnd = line.size();
        if (singleLine) { colStart = std::min(lo.x(), hi.x()); colEnd = std::max(lo.x(), hi.x()); }
        else if (gl == selMinLine) { colStart = lo.x(); colEnd = line.size(); }
        else if (gl == selMaxLine) { colStart = 0; colEnd = hi.x(); }

        QString text;
        for (int c = colStart; c < colEnd && c < line.size(); ++c)
            text += line.at(c).ch;
        lines << text.replace(QRegularExpression("\\s+$"), QString()); // rtrim
    }
    return lines.join('\n');
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

void TerminalWidget::copySelectionAsImage()
{
    // Ctrl+Shift+S : rend uniquement les caractères sélectionnés sous forme d'image
    // (les cellules hors sélection sur les lignes concernées restent en fond neutre),
    // conservant l'alignement en colonnes tel qu'affiché à l'écran.
    if (!m_hasSelection)
        return;

    const int selMinLine = std::min(m_selStart.y(), m_selEnd.y());
    const int selMaxLine = std::max(m_selStart.y(), m_selEnd.y());
    const bool singleLine = (selMinLine == selMaxLine);
    const QPoint &lo = (m_selStart.y() <= m_selEnd.y()) ? m_selStart : m_selEnd;
    const QPoint &hi = (m_selStart.y() <= m_selEnd.y()) ? m_selEnd : m_selStart;

    const int nLines = selMaxLine - selMinLine + 1;
    const int cols = screen->cols();
    const QImage::Format fmt = QImage::Format_ARGB32_Premultiplied;
    QImage image(cols * m_cellW, nLines * m_cellH, fmt);
    image.fill(Qt::black);

    QPainter painter(&image);
    painter.setFont(m_font);

    int maxColUsed = 0;
    for (int gl = selMinLine; gl <= selMaxLine; ++gl) {
        const TermLine &line = lineAtGlobal(gl);
        int colStart = 0, colEnd = line.size();
        if (singleLine) { colStart = std::min(lo.x(), hi.x()); colEnd = std::max(lo.x(), hi.x()); }
        else if (gl == selMinLine) { colStart = lo.x(); colEnd = line.size(); }
        else if (gl == selMaxLine) { colStart = 0; colEnd = hi.x(); }

        const int y = (gl - selMinLine) * m_cellH;
        for (int c = colStart; c < colEnd && c < line.size(); ++c) {
            const TermCell &cell = line.at(c);
            QRgb fg = cell.fg, bg = cell.bg;
            if (cell.inverse) std::swap(fg, bg);

            const QRect cellRect((c - colStart) * m_cellW, y, m_cellW, m_cellH);
            painter.fillRect(cellRect, QColor(bg));
            if (cell.ch != QLatin1Char(' ') && !cell.ch.isNull()) {
                QFont f = m_font;
                if (cell.bold) f.setBold(true);
                if (cell.italic) f.setItalic(true);
                painter.setFont(f);
                painter.setPen(QColor(fg));
                painter.drawText(cellRect.x(), y + m_ascent, cell.ch);
            }
            maxColUsed = std::max(maxColUsed, (c - colStart) + 1);
        }
    }
    painter.end();

    if (maxColUsed > 0 && maxColUsed < cols)
        image = image.copy(0, 0, maxColUsed * m_cellW, nLines * m_cellH);

    QGuiApplication::clipboard()->setImage(image);
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
