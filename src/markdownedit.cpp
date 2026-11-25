#include "markdownedit.h"
#include "markdownhighlighter.h"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QMimeData>
#include <QImage>
#include <algorithm>
#include <QTextBlock>
#include <QTextTable>
#include <QTextTableFormat>
#include <QTextTableCell>
#include <QRegularExpression>
#include <QUrl>
#include <QMenu>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QPainter>
#include <QAbstractTextDocumentLayout>
#include <climits>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QKeyEvent>
#include <QSet>
#include <QScrollBar>
#include <QAbstractTextDocumentLayout>

namespace {
const QString kImageScheme = QStringLiteral("mdimg:");
const QColor kCodeBlockBgColor(60, 60, 60);
const QColor kTableBorderColor(100, 100, 100);

bool isFenceLineText(const QString &trimmed)
{
    return trimmed.startsWith(QStringLiteral("```"));
}

// Motif utilisé pour repérer, à la sauvegarde, le couple "séparateur doux + image"
// inséré juste après le texte réel d'une balise ![[..]] (cf. insertImageTagAndPicture).
const QString kSeparatorPlusImage = QString(QChar(QChar::LineSeparator)) + QChar(QChar::ObjectReplacementCharacter);

// ~ découpe une ligne "| a | b |" en cellules, en gérant l'échappement "\|" et les
// barres verticales de bord optionnelles (GFM les autorise sans elles : "a | b").
QStringList splitTableRow(const QString &line)
{
    QString t = line.trimmed();
    if (t.startsWith('|'))
        t.remove(0, 1);
    if (t.endsWith('|') && !t.endsWith("\\|"))
        t.chop(1);

    QStringList cells;
    QString cur;
    for (int i = 0; i < t.size(); ++i) {
        if (t.at(i) == '\\' && i + 1 < t.size() && t.at(i + 1) == '|') {
            cur += '|';
            ++i;
        } else if (t.at(i) == '|') {
            cells << cur.trimmed();
            cur.clear();
        } else {
            cur += t.at(i);
        }
    }
    cells << cur.trimmed();
    return cells;
}

// ~ estime, à partir d'une position de caractère dans une ligne "| a | b |", l'index de
// colonne correspondant (comptage des '|' non finaux avant cette position). Utilisé pour
// replacer le curseur dans la bonne cellule après conversion texte -> QTextTable.
int estimateColumnFromLine(const QString &line, int posInLine)
{
    int start = 0;
    if (line.startsWith('|'))
        start = 1;
    int end = line.length();
    if (line.endsWith('|'))
        --end;
    posInLine = qBound(0, posInLine, line.length());

    int col = 0;
    for (int i = start; i < posInLine && i < end; ++i) {
        if (line.at(i) == '|')
            ++col;
    }
    return col;
}
}

MarkdownEdit::MarkdownEdit(QWidget *parent)
    : QTextEdit(parent)
{
    highlighter = new MarkdownHighlighter(document());

    connect(this, &QTextEdit::textChanged, this, [this]() {
        if (!m_updatingBlockBackgrounds)
            noteModified = true;
    });

    // Mode "édition live" : relaie la position du curseur au highlighter à chaque
    // déplacement, et ne ré-highlighte que les blocs concernés (pas tout le document).
    connect(this, &QTextEdit::cursorPositionChanged, this, &MarkdownEdit::onCursorPositionChanged);

    // Fond de bloc pleine largeur des zones de code (recalculé à chaque frappe).
    connect(this, &QTextEdit::textChanged, this, &MarkdownEdit::onTextChangedUpdateCodeBackgrounds);
}

void MarkdownEdit::onCursorPositionChanged()
{
    const int pos = textCursor().position();
    highlighter->setCursorPosition(pos);

    // On ne ré-highlighte que le(s) bloc(s) concerné(s) : celui que le curseur
    // vient de quitter (ses marqueurs doivent se re-masquer) et celui où il arrive
    // (ses marqueurs doivent se révéler). Un déplacement au sein d'un même bloc
    // (ex: sortir de **gras** sans changer de ligne) est bien couvert puisque
    // l'un des deux rehighlightBlock() ci-dessous porte alors sur ce même bloc.
    QTextBlock oldBlock = document()->findBlock(qBound(0, m_lastCursorPos, document()->characterCount() - 1));
    QTextBlock newBlock = document()->findBlock(pos);
    if (oldBlock.isValid())
        highlighter->rehighlightBlock(oldBlock);
    if (newBlock.isValid() && newBlock != oldBlock)
        highlighter->rehighlightBlock(newBlock);

    // Cas particulier des blocs de code : la visibilité des lignes ``` dépend de la
    // position du curseur n'IMPORTE OÙ dans tout le bloc (pas seulement sur la ligne
    // ``` elle-même), donc un simple rehighlight du bloc quitté/atteint ne suffit pas
    // à mettre à jour l'apparence des balises ouvrante/fermante potentiellement
    // ailleurs dans le document. On détecte ce cas et on ré-highlighte alors tout le
    // document (solution simple et fiable, le coût reste raisonnable pour une note).
    auto touchesCodeFence = [](const QTextBlock &b) {
        if (!b.isValid())
            return false;
        if (MarkdownHighlighter::isFenceLineText(b.text().trimmed()))
            return true;
        if (MarkdownHighlighter::isCodeBlockState(b.userState()))
            return true;
        const QTextBlock prev = b.previous();
        return prev.isValid() && MarkdownHighlighter::isCodeBlockState(prev.userState());
    };
    if (touchesCodeFence(oldBlock) || touchesCodeFence(newBlock))
        highlighter->rehighlight();

    m_lastCursorPos = pos;
}

void MarkdownEdit::onTextChangedUpdateCodeBackgrounds()
{
    if (m_updatingBlockBackgrounds)
        return;
    updateBlockDecorations();

    // Édition live des tableaux façon Obsidian : convertit en vrai QTextTable dès que
    // possible un tableau GFM en cours de frappe manuelle (cf. maybeConvertTypedTable).
    if (!m_convertingTypedTable)
        maybeConvertTypedTable();
}

void MarkdownEdit::maybeConvertTypedTable()
{
    // ~ nouveau : dès qu'un tableau GFM tapé À LA MAIN devient syntaxiquement valide
    // (ligne d'en-tête suivie d'une ligne séparatrice |---|---| correcte), on le
    // convertit immédiatement en vrai QTextTable, exactement comme le fait
    // insertMarkdownContent au chargement d'un fichier -- pour que la grille, la
    // navigation Tab et le menu clic-droit "Table" fonctionnent aussi pour un tableau
    // composé directement dans l'éditeur (et pas seulement chargé depuis un .md).
    QTextCursor cur = textCursor();
    if (cur.currentTable())
        return; // déjà dans un vrai tableau : rien à convertir

    QTextBlock block = cur.block();
    if (!MarkdownHighlighter::isTableRowLine(block.text())) {
        // Le curseur peut se trouver juste après la ligne séparatrice qui vient d'être
        // validée (ex: après un Entrée) : on regarde alors le bloc précédent.
        if (block.previous().isValid() && MarkdownHighlighter::isTableRowLine(block.previous().text()))
            block = block.previous();
        else
            return;
    }

    QTextBlock first = block;
    while (first.previous().isValid() && MarkdownHighlighter::isTableRowLine(first.previous().text())
           && !QTextCursor(first.previous()).currentTable())
        first = first.previous();

    QTextBlock last = block;
    while (last.next().isValid() && MarkdownHighlighter::isTableRowLine(last.next().text())
           && !QTextCursor(last.next()).currentTable())
        last = last.next();

    if (first == last)
        return; // il faut au moins l'en-tête ET la ligne séparatrice

    const QTextBlock second = first.next();
    if (!MarkdownHighlighter::isTableSeparatorLine(second.text().trimmed()))
        return; // pas encore une ligne séparatrice valide -> l'utilisateur est encore en train de la taper

    // Rassemble les lignes candidates, et repère où se trouvait le curseur dedans (pour
    // le replacer dans la bonne cellule une fois le texte remplacé par un QTextTable).
    QStringList lines;
    int cursorRelRow = 0;
    int cursorCol = 0;
    int rowIdx = 0;
    for (QTextBlock b = first; ; b = b.next()) {
        lines << b.text();
        if (b == cur.block()) {
            cursorRelRow = rowIdx;
            cursorCol = estimateColumnFromLine(b.text(), cur.position() - b.position());
        }
        ++rowIdx;
        if (b == last)
            break;
    }

    // La ligne séparatrice (index 1 dans "lines") disparaît à la conversion ; les
    // lignes de données décalent donc d'un cran (ligne 2 -> ligne de table 1, etc.).
    const int targetTableRow = (cursorRelRow <= 1) ? 0 : cursorRelRow - 1;

    m_convertingTypedTable = true;

    QTextCursor edit(document());
    edit.beginEditBlock();
    edit.setPosition(first.position());
    edit.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
    edit.removeSelectedText();
    insertMarkdownTable(edit, lines); // repositionne "edit" juste après le tableau inséré
    edit.endEditBlock();

    QTextCursor probe(document());
    probe.setPosition(qMax(0, edit.position() - 1));
    if (QTextTable *table = probe.currentTable()) {
        const int row = qBound(0, targetTableRow, table->rows() - 1);
        const int col = qBound(0, cursorCol, table->columns() - 1);
        setTextCursor(table->cellAt(row, col).firstCursorPosition());
    } else {
        setTextCursor(edit);
    }

    highlighter->rehighlight();
    updateBlockDecorations();

    m_convertingTypedTable = false;
    noteModified = true;
}

void MarkdownEdit::updateBlockDecorations()
{
    // ~ fond de bloc PLEINE LARGEUR pour les zones de code, et marge gauche
    // proportionnelle à la profondeur d'imbrication pour les citations : deux
    // décorations au niveau bloc (QTextBlockFormat) que QSyntaxHighlighter seul ne
    // peut pas appliquer (il ne gère que le format des caractères, pas le fond
    // pleine largeur ni les marges de paragraphe).
    m_updatingBlockBackgrounds = true;

    bool inCode = false;
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        const QString blockText = block.text();
        const QString trimmed = blockText.trimmed();
        const bool fence = isFenceLineText(trimmed);
        const bool shouldColorCode = inCode || fence; // la ligne de balise fermante fait partie du bloc coloré

        int quoteDepth = 0;
        const bool isQuote = !shouldColorCode && !inCode &&
                              MarkdownHighlighter::isBlockQuotePrefix(blockText, &quoteDepth, nullptr);

        QTextBlockFormat fmt = block.blockFormat();
        bool changed = false;

        const bool wantCodeBg = shouldColorCode;
        const bool hasCodeBg = fmt.background().color() == kCodeBlockBgColor;
        if (wantCodeBg && !hasCodeBg) {
            fmt.setBackground(kCodeBlockBgColor);
            changed = true;
        } else if (!wantCodeBg && fmt.background().color().isValid()) {
            fmt.clearBackground();
            changed = true;
        }

        const qreal wantMargin = isQuote ? qreal(quoteDepth) * 20.0 : 0.0;
        if (fmt.leftMargin() != wantMargin) {
            fmt.setLeftMargin(wantMargin);
            changed = true;
        }

        if (changed) {
            cursor.setPosition(block.position());
            cursor.setBlockFormat(fmt);
        }

        if (fence)
            inCode = !inCode;
    }
    cursor.endEditBlock();

    m_updatingBlockBackgrounds = false;
}

namespace {
// Retire du texte de titre les marqueurs d'emphase / de code inline, pour un affichage
// propre dans l'explorateur ("## **Gras** et `code`" -> "Gras et code").
QString cleanHeadingText(QString t)
{
    t = t.trimmed();
    // marqueurs de fermeture ATX optionnels : "## Titre ##"
    static const QRegularExpression closingHashes(QStringLiteral(R"(\s+#+\s*$)"));
    t.remove(closingHashes);
    static const QRegularExpression emphasis(QStringLiteral(R"(\*\*|__|~~|==|`)"));
    t.remove(emphasis);
    return t.trimmed();
}

bool blockIsInsideTable(const QTextBlock &block)
{
    return QTextCursor(block).currentTable() != nullptr;
}
} // namespace

QList<MarkdownHeading> MarkdownEdit::headings() const
{
    // ~ nouveau : mêmes règles que MarkdownHighlighter (ATX plafonné à 5 niveaux, titres Setext)
    // et même gestion des blocs de code que updateBlockDecorations() (bascule sur chaque
    // ligne ```), pour qu'un "# commentaire" dans un bloc de code ne soit pas pris pour un titre.
    QList<MarkdownHeading> result;
    bool inCode = false;

    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        const QString text = block.text();
        const QString trimmed = text.trimmed();

        if (MarkdownHighlighter::isFenceLineText(trimmed)) {
            inCode = !inCode;
            continue;
        }
        if (inCode)
            continue;

        MarkdownHeading h;
        const int atxLevel = MarkdownHighlighter::detectHeaderLevel(text);
        if (atxLevel > 0) {
            int hashes = 0;
            while (hashes < text.length() && text.at(hashes) == QLatin1Char('#'))
                ++hashes;
            h.level = atxLevel;
            h.text = cleanHeadingText(text.mid(hashes + 1));
        } else if (MarkdownHighlighter::isPlainParagraphCandidate(text)) {
            const QTextBlock next = block.next();
            if (!next.isValid())
                continue;
            const int setextLevel = MarkdownHighlighter::setextUnderlineLevel(next.text().trimmed());
            if (setextLevel == 0)
                continue;
            h.level = setextLevel;
            h.text = cleanHeadingText(trimmed);
        } else {
            continue;
        }

        if (h.text.isEmpty())
            continue; // "# " en cours de frappe : pas encore de titre à afficher
        if (blockIsInsideTable(block))
            continue; // "# x" dans une cellule de tableau : ce n'est pas un titre

        h.position = block.position();
        result << h;
    }
    return result;
}

bool MarkdownEdit::revealHeading(int index)
{
    const QList<MarkdownHeading> list = headings();
    if (index < 0 || index >= list.size())
        return false;

    const QTextBlock block = document()->findBlock(list.at(index).position);
    if (!block.isValid())
        return false;

    // Le curseur se place sur le titre (on continue ensuite à éditer à cet endroit).
    // setTextCursor() déclenche un ensureCursorVisible() ; on règle donc le défilement
    // APRÈS, pour que notre positionnement soit le dernier mot.
    QTextCursor cursor(block);
    setTextCursor(cursor);

    // Ordonnée du haut du bloc dans le document = valeur de défilement qui l'amène tout en
    // haut de la zone visible. QScrollBar borne à maximum() : si le titre est trop près de
    // la fin du document pour pouvoir être placé en haut, on s'arrête au défilement maximal,
    // c'est-à-dire que la fin du document reste calée sur le bas de la vue (pas de vide).
    const int wanted = qRound(document()->documentLayout()->blockBoundingRect(block).top());
    QScrollBar *bar = verticalScrollBar();
    bar->setValue(qBound(bar->minimum(), wanted, bar->maximum()));

    setFocus();
    return true;
}

QString MarkdownEdit::getCurrentLineTextContent() const
{
    // ~ GetCurrentLineTextContent : texte de la VRAIE ligne (paragraphe/bloc) où se trouve le
    // curseur. Ne pas utiliser QTextCursor::LineUnderCursor : il désigne la ligne VISUELLE, c'est-à-
    // dire seulement le morceau affiché sur une ligne lorsque l'éditeur replie un texte plus long
    // que sa largeur (v1.8.0 : une commande longue n'était envoyée qu'en partie au terminal).
    QString t = textCursor().block().text();
    t.replace(kSeparatorPlusImage, QString()); // image intégrée : seule sa balise ![[..]] reste dans le texte
    t.remove(QChar(QChar::ObjectReplacementCharacter));
    t.remove(QChar(QChar::LineSeparator));
    return t;
}

QString MarkdownEdit::resolveImagePath(const QString &path) const
{
    // ~ TMarkdownEdit.ResolveImagePath
    if (path.isEmpty())
        return path;

    if (path.startsWith("~/"))
        return QDir::homePath() + "/" + path.mid(2);

    if (!path.startsWith('/')) {
        if (!basePath.isEmpty())
            return QDir(basePath).filePath(path);
        return QDir::current().filePath(path);
    }
    return path;
}

QString MarkdownEdit::generateImageFileName() const
{
    // ~ TMarkdownEdit.GenerateImageFileName
    return QDateTime::currentDateTime().toString("yyyyMMddhhmmss") + ".png";
}

void MarkdownEdit::insertImageTagAndPicture(QTextCursor &cursor, const QString &path, const QImage &image)
{
    // Insère le triplet permanent : texte réel "![[chemin]]" (~ marqueur, masqué par
    // défaut par le highlighter, révélé quand le curseur est dessus ou sur l'image) +
    // un saut de ligne "doux" (ne crée pas de nouveau paragraphe, donc invisible à la
    // sauvegarde) + l'image elle-même, qui elle reste TOUJOURS visible en dessous.
    cursor.insertText(QStringLiteral("![[%1]]").arg(path));
    cursor.insertText(QString(QChar(QChar::LineSeparator)));

    const QString resourceName = kImageScheme + path;
    document()->addResource(QTextDocument::ImageResource, QUrl(resourceName), image);
    cursor.insertImage(resourceName);
}

void MarkdownEdit::insertMarkdownText(QTextCursor &cursor, const QString &text)
{
    // ~ TMarkdownEdit.LoadFromMdFile / DetectImageOnLine : pour chaque balise
    // ![[chemin]] rencontrée, insère le triplet texte+séparateur+image (cf.
    // insertImageTagAndPicture) si l'image est trouvée, sinon le texte brut seul.
    static const QRegularExpression imgRe(R"(!\[\[(.+?)\]\])");

    int pos = 0;
    auto it = imgRe.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        if (m.capturedStart() > pos)
            cursor.insertText(text.mid(pos, m.capturedStart() - pos));

        const QString imgPath = m.captured(1).trimmed();
        const QString resolved = resolveImagePath(imgPath);
        const QImage image(resolved);
        if (!image.isNull()) {
            insertImageTagAndPicture(cursor, imgPath, image);
        } else {
            // ~ comportement Pascal : si le fichier est introuvable, rien n'est chargé
            // (GetCachedImage retourne nil) ; on garde ici une trace textuelle visible
            // pour ne pas perdre discrètement l'information au moment de la sauvegarde.
            cursor.insertText(QStringLiteral("![[%1]]").arg(imgPath));
        }
        pos = m.capturedEnd();
    }
    if (pos < text.length())
        cursor.insertText(text.mid(pos));
}

void MarkdownEdit::insertMarkdownTable(QTextCursor &cursor, const QStringList &tableLines)
{
    // ~ nouveau (façon Obsidian) : convertit un bloc GFM (ligne d'en-tête + ligne
    // séparatrice + lignes de données) en un vrai QTextTable éditable, au lieu d'un
    // simple texte stylé. L'alignement de colonne (:---, :---:, ---:) est conservé
    // comme alignement de paragraphe sur chaque cellule de la colonne, ce qui permet
    // de le relire tel quel à la sérialisation (cf. serializeTable).
    const QStringList headerCells = splitTableRow(tableLines.value(0));
    const QStringList sepCells = splitTableRow(tableLines.value(1));

    int cols = qMax(headerCells.size(), sepCells.size());

    QVector<Qt::Alignment> aligns(cols, Qt::AlignLeft);
    for (int c = 0; c < sepCells.size(); ++c) {
        const QString s = sepCells.at(c).trimmed();
        const bool left = s.startsWith(':');
        const bool right = s.endsWith(':');
        if (left && right)
            aligns[c] = Qt::AlignHCenter;
        else if (right)
            aligns[c] = Qt::AlignRight;
        else
            aligns[c] = Qt::AlignLeft;
    }

    QList<QStringList> dataRows;
    for (int i = 2; i < tableLines.size(); ++i) {
        const QStringList row = splitTableRow(tableLines.at(i));
        dataRows << row;
        cols = qMax(cols, row.size());
    }

    const int rows = 1 + dataRows.size();

    QTextTableFormat fmt;
    fmt.setBorder(1);
    fmt.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    fmt.setBorderBrush(kTableBorderColor);
    fmt.setCellPadding(4);
    fmt.setCellSpacing(0);
    fmt.setHeaderRowCount(1);

    QTextTable *table = cursor.insertTable(rows, cols, fmt);

    auto fillCell = [&](int r, int c, const QString &text, bool header) {
        QTextTableCell cell = table->cellAt(r, c);
        QTextCursor cellCursor = cell.firstCursorPosition();
        QTextBlockFormat bf = cellCursor.blockFormat();
        bf.setAlignment(aligns.value(c, Qt::AlignLeft));
        cellCursor.setBlockFormat(bf);
        if (header) {
            QTextCharFormat cf = cellCursor.charFormat();
            cf.setFontWeight(QFont::Bold);
            cellCursor.setCharFormat(cf);
        }
        insertMarkdownText(cellCursor, text); // gère aussi une éventuelle image dans la cellule
    };

    for (int c = 0; c < cols; ++c)
        fillCell(0, c, headerCells.value(c), true);
    for (int r = 0; r < dataRows.size(); ++r)
        for (int c = 0; c < cols; ++c)
            fillCell(r + 1, c, dataRows.at(r).value(c), false);

    // IMPORTANT : QTextCursor::insertTable() laisse le curseur positionné DANS la
    // première cellule du tableau qui vient d'être créé. Sans repositionnement, le
    // contenu inséré juste après (cf. insertMarkdownContent) se retrouverait collé À
    // L'INTÉRIEUR de cette cellule au lieu de continuer après le tableau (bug détecté
    // et corrigé pendant les tests : "Texte après." se retrouvait fusionné avec "Nom").
    cursor.setPosition(table->lastPosition() + 1);
}

void MarkdownEdit::insertMarkdownContent(QTextCursor &cursor, const QStringList &lines)
{
    // ~ point d'entrée de chargement/ajout : détecte les blocs de tableau GFM
    // (ligne contenant '|' immédiatement suivie d'une ligne séparatrice valide) et
    // les convertit en vrai QTextTable (insertMarkdownTable) ; le reste du texte suit
    // le traitement ligne à ligne habituel (insertMarkdownText).
    //
    // Point d'attention (bug détecté et corrigé pendant les tests) : QTextCursor::
    // insertTable() gère lui-même sa séparation avec le contenu précédent/suivant ;
    // pré-insérer un bloc séparateur juste avant (via cursor.insertBlock(), comme on
    // le fait entre deux lignes de texte normales) laisserait un paragraphe vide
    // parasite avant le tableau. On ne pré-insère donc CE séparateur que lorsque le
    // prochain élément n'est PAS lui-même un début de tableau.
    auto isTableStartAt = [&lines](int idx) {
        return idx + 1 < lines.size() &&
               MarkdownHighlighter::isTableRowLine(lines.at(idx)) &&
               MarkdownHighlighter::isTableSeparatorLine(lines.at(idx + 1).trimmed());
    };

    int i = 0;
    while (i < lines.size()) {
        if (isTableStartAt(i)) {
            int j = i + 2;
            while (j < lines.size() && MarkdownHighlighter::isTableRowLine(lines.at(j)))
                ++j;
            insertMarkdownTable(cursor, lines.mid(i, j - i));
            i = j;
            continue; // pas de insertBlock() ici non plus : même raison qu'avant le tableau
        }

        insertMarkdownText(cursor, lines.at(i));
        ++i;

        if (i < lines.size() && !isTableStartAt(i))
            cursor.insertBlock();
    }
}

QString MarkdownEdit::serializeTable(QTextTable *table) const
{
    // ~ reconstruit un QTextTable en syntaxe GFM, avec colonnes alignées visuellement
    // (comme le ferait Obsidian lui-même en sauvegardant), alignement (:---/:---:/---:)
    // relu depuis l'alignement de paragraphe appliqué à chaque cellule (cf. insertMarkdownTable).
    const int rows = table->rows();
    const int cols = table->columns();

    QVector<QVector<QString>> cells(rows, QVector<QString>(cols));
    QVector<Qt::Alignment> aligns(cols, Qt::AlignLeft);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const QTextTableCell cell = table->cellAt(r, c);
            QString text;
            for (auto it = cell.begin(); !it.atEnd(); ++it) {
                const QTextBlock b = it.currentBlock();
                if (!b.isValid())
                    continue;
                QString t = b.text();
                t.replace(kSeparatorPlusImage, QString());
                if (t.contains(QChar(QChar::ObjectReplacementCharacter))) {
                    // cellule contenant une image : reconstruit depuis son format (cf. documentToMarkdownText)
                    QString rebuilt;
                    for (auto fit = b.begin(); !fit.atEnd(); ++fit) {
                        const QTextFragment frag = fit.fragment();
                        if (!frag.isValid())
                            continue;
                        const QTextCharFormat cf = frag.charFormat();
                        if (cf.isImageFormat()) {
                            QString name = cf.toImageFormat().name();
                            if (name.startsWith(kImageScheme))
                                name = name.mid(kImageScheme.length());
                            rebuilt += QStringLiteral("![[%1]]").arg(name);
                        } else {
                            QString ft = frag.text();
                            ft.remove(QChar(QChar::LineSeparator));
                            rebuilt += ft;
                        }
                    }
                    t = rebuilt;
                }
                text += t;
                if (r == 0)
                    aligns[c] = b.blockFormat().alignment();
            }
            cells[r][c] = text.trimmed().replace('|', "\\|");
        }
    }

    QVector<int> widths(cols, 3);
    for (int c = 0; c < cols; ++c)
        for (int r = 0; r < rows; ++r)
            widths[c] = qMax(widths[c], cells[r][c].length());

    auto padCell = [](const QString &s, int w, Qt::Alignment a) {
        const int pad = qMax(0, w - s.length());
        if (a & Qt::AlignRight)
            return QString(pad, ' ') + s;
        if (a & Qt::AlignHCenter) {
            const int left = pad / 2, right = pad - left;
            return QString(left, ' ') + s + QString(right, ' ');
        }
        return s + QString(pad, ' ');
    };

    QStringList lines;
    for (int r = 0; r < rows; ++r) {
        QStringList rowCells;
        for (int c = 0; c < cols; ++c)
            rowCells << padCell(cells[r][c], widths[c], aligns.value(c, Qt::AlignLeft));
        lines << "| " + rowCells.join(" | ") + " |";

        if (r == 0) {
            QStringList sepCells;
            for (int c = 0; c < cols; ++c) {
                const Qt::Alignment a = aligns.value(c, Qt::AlignLeft);
                const QString dashes(qMax(3, widths[c]), '-');
                if (a & Qt::AlignHCenter)
                    sepCells << (":" + dashes.mid(0, dashes.length() - 2) + ":");
                else if (a & Qt::AlignRight)
                    sepCells << (dashes.mid(0, dashes.length() - 1) + ":");
                else
                    sepCells << dashes;
            }
            lines << "| " + sepCells.join(" | ") + " |";
        }
    }
    return lines.join('\n');
}

QString MarkdownEdit::documentToMarkdownText() const
{
    // ~ TMarkdownEdit.GetTextContent. Parcourt le document image par image (frame)
    // via QTextFrame::iterator pour distinguer les paragraphes normaux des tableaux
    // (un QTextTable compte comme UNE seule entrée, pas une par cellule) et
    // reconstruire chacun avec la sérialisation appropriée.
    QStringList outLines;
    QTextFrame *root = document()->rootFrame();
    auto it = root->begin();

    // Cas particulier (détecté pendant les tests) : Qt conserve toujours un
    // paragraphe vide par défaut en tout début de document, même quand un tableau
    // est la toute première chose insérée (cf. insertMarkdownTable) -- ce paragraphe
    // fantôme n'a jamais existé dans le fichier .md d'origine. On l'ignore ici, à la
    // sérialisation, plutôt que de risquer de perturber la structure du tableau en
    // tentant de le supprimer du document lui-même (une sélection qui touche la
    // limite d'un cadre/tableau est étendue par Qt à TOUT le cadre : une tentative
    // précédente de nettoyage à l'aide d'un curseur a fini par supprimer le tableau
    // entier -- piège à ne pas reproduire).
    if (!it.atEnd()) {
        const QTextBlock firstBlock = it.currentBlock();
        if (firstBlock.isValid() && firstBlock.text().isEmpty()) {
            auto next = it;
            ++next;
            if (!next.atEnd() && qobject_cast<QTextTable *>(next.currentFrame()))
                ++it;
        }
    }

    for (; !it.atEnd(); ++it) {
        if (QTextTable *table = qobject_cast<QTextTable *>(it.currentFrame())) {
            outLines << serializeTable(table);
            continue;
        }

        const QTextBlock block = it.currentBlock();
        if (!block.isValid())
            continue;

        QString line = block.text(); // U+2028 pour le séparateur, U+FFFC pour l'image
        line.replace(kSeparatorPlusImage, QString());

        // Filet de sécurité : une image isolée (sans le couple séparateur+texte
        // attendu devant elle) ne devrait normalement pas se produire avec le flux
        // normal, mais on la reconstruit depuis son format plutôt que de la perdre.
        if (line.contains(QChar(QChar::ObjectReplacementCharacter))) {
            QString rebuilt;
            for (auto fit = block.begin(); !fit.atEnd(); ++fit) {
                const QTextFragment frag = fit.fragment();
                if (!frag.isValid())
                    continue;
                const QTextCharFormat fmt = frag.charFormat();
                if (fmt.isImageFormat()) {
                    QString name = fmt.toImageFormat().name();
                    if (name.startsWith(kImageScheme))
                        name = name.mid(kImageScheme.length());
                    rebuilt += QStringLiteral("![[%1]]").arg(name);
                } else {
                    QString t = frag.text();
                    t.remove(QChar(QChar::LineSeparator));
                    rebuilt += t;
                }
            }
            line = rebuilt;
        }
        outLines << line;
    }
    return outLines.join('\n');
}

void MarkdownEdit::loadFromMdFile(const QString &fileName)
{
    // ~ TMarkdownEdit.LoadFromMdFile
    basePath = QFileInfo(fileName).absolutePath();
    ++m_loadSerial; // les repères posés avant ce chargement ne sont plus valables (cf. markCommandLine)

    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    const QStringList lines = in.readAll().split('\n');

    clear();
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    insertMarkdownContent(cursor, lines);
    cursor.endEditBlock();

    resetCursorTrackingAfterLoad();
    noteModified = false;
}

void MarkdownEdit::resetCursorTrackingAfterLoad()
{
    // Après un chargement complet, le curseur "de travail" utilisé pour insérer le
    // texte se retrouve en fin de document : on le replace au début, et on force un
    // rehighlight complet pour repartir sur un état cohérent (sinon les blocs auraient
    // été highlightés avec une position de curseur transitoire, non pertinente).
    QTextCursor start(document());
    start.movePosition(QTextCursor::Start);
    setTextCursor(start);
    m_lastCursorPos = 0;
    highlighter->setCursorPosition(0);
    highlighter->rehighlight();
    updateBlockDecorations();
}

void MarkdownEdit::saveToMdFile(const QString &fileName)
{
    // ~ TMarkdownEdit.SaveToMdFile
    QFile f(fileName);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << documentToMarkdownText();
    noteModified = false;
}

void MarkdownEdit::appendFromMdFile(const QString &fileName)
{
    // ~ TMarkdownEdit.AppendFromMdFile
    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    appendMarkdownText(in.readAll());
}

void MarkdownEdit::appendMarkdownText(const QString &content)
{
    // Cœur de AppendFromMdFile, mais à partir d'une chaîne déjà en mémoire (permet
    // par exemple à MainWindow d'y substituer des valeurs de la grille Target avant
    // l'ajout, cf. onTreeLibraryDoubleClicked).
    const QStringList lines = content.split('\n');

    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.beginEditBlock();
    if (!document()->isEmpty())
        cursor.insertBlock();
    insertMarkdownContent(cursor, lines);
    cursor.endEditBlock();

    setTextCursor(cursor); // le curseur suit le contenu ajouté -> déclenche cursorPositionChanged
    noteModified = true;
}

void MarkdownEdit::openNewLineAfterCursor(QTextCursor &cursor)
{
    // À appeler dans un beginEditBlock() : crée une ligne vide neutre (sans fond de
    // code/citation hérité) juste après la ligne du curseur, ou après le tableau si le
    // curseur est dans un tableau, et y place `cursor`.
    if (QTextTable *table = cursor.currentTable()) {
        cursor.setPosition(std::min(table->lastPosition() + 1, document()->characterCount() - 1));
        cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
        cursor.movePosition(QTextCursor::PreviousBlock);
    } else {
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
    }
}

bool MarkdownEdit::insertTextOnNextLine(const QString &text)
{
    // Le texte est inséré dans un bloc de code Markdown : une ligne ```, le texte (une ligne
    // du texte par ligne de la note, sans interprétation Markdown), puis une ligne ```.
    // Les lignes vides en début/fin de sélection (fréquentes dans un terminal) sont retirées.
    QStringList lines = QString(text).replace("\r\n", "\n").replace('\r', '\n').split('\n');
    while (!lines.isEmpty() && lines.first().trimmed().isEmpty())
        lines.removeFirst();
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    if (lines.isEmpty())
        return false;

    lines.prepend(QStringLiteral("```"));
    lines.append(QStringLiteral("```"));

    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    openNewLineAfterCursor(cursor);
    for (int i = 0; i < lines.size(); ++i) {
        if (i > 0)
            cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
        cursor.insertText(lines.at(i));
    }
    cursor.endEditBlock();

    setTextCursor(cursor);
    ensureCursorVisible();
    noteModified = true;
    return true;
}

QString MarkdownEdit::selectedMarkdownText() const
{
    const QTextCursor sel = textCursor();
    if (!sel.hasSelection())
        return QString();

    const int selStart = sel.selectionStart();
    const int selEnd = sel.selectionEnd();

    QStringList lines;
    QSet<QTextTable *> doneTables;
    for (QTextBlock block = document()->findBlock(selStart);
         block.isValid() && block.position() < selEnd; block = block.next()) {
        if (QTextTable *table = QTextCursor(block).currentTable()) {
            // Un tableau touché par la sélection est repris en entier (une seule fois).
            if (!doneTables.contains(table)) {
                doneTables.insert(table);
                lines << serializeTable(table);
            }
            continue;
        }

        const int blockStart = block.position();
        const int from = std::max(selStart, blockStart) - blockStart;
        const int to = std::min(selEnd, blockStart + block.length() - 1) - blockStart;
        QString t = block.text().mid(from, std::max(0, to - from));
        t.replace(kSeparatorPlusImage, QString()); // l'image elle-même : seule sa balise ![[..]] est reprise
        t.remove(QChar(QChar::ObjectReplacementCharacter));
        t.remove(QChar(QChar::LineSeparator));
        lines << t;
    }

    return lines.join('\n');
}

QString MarkdownEdit::imageBaseFolder() const
{
    return basePath.isEmpty() ? QDir::currentPath() : basePath;
}

QString MarkdownEdit::copyReferencedImages(const QString &markdown, const QString &sourceFolder,
                                           const QString &destFolder)
{
    static const QRegularExpression imgTag(QStringLiteral("!\\[\\[([^\\]]+)\\]\\]"));

    auto sameContent = [](const QString &a, const QString &b) {
        if (QFileInfo(a).size() != QFileInfo(b).size())
            return false;
        QFile fa(a), fb(b);
        return fa.open(QIODevice::ReadOnly) && fb.open(QIODevice::ReadOnly) && fa.readAll() == fb.readAll();
    };

    // Copie `src` dans destFolder sous le nom relatif `rel` (ou une variante _1, _2... en cas de
    // fichier différent déjà présent) ; renvoie le nom relatif retenu, vide en cas d'échec.
    auto importOne = [&](const QString &src, const QString &rel) -> QString {
        const QFileInfo ri(rel);
        const QString dir = (ri.path() == QLatin1String(".")) ? QString() : ri.path() + QLatin1Char('/');
        const QString stem = ri.completeBaseName();
        const QString suffix = ri.suffix().isEmpty() ? QString() : QLatin1Char('.') + ri.suffix();
        const QString srcCanon = QFileInfo(src).canonicalFilePath();

        for (int n = 0; n < 1000; ++n) {
            const QString candidate = dir + stem + (n ? QStringLiteral("_%1").arg(n) : QString()) + suffix;
            const QString dest = QDir(destFolder).filePath(candidate);
            if (!QFileInfo::exists(dest)) {
                QDir().mkpath(QFileInfo(dest).absolutePath());
                return QFile::copy(src, dest) ? candidate : QString();
            }
            if (QFileInfo(dest).canonicalFilePath() == srcCanon || sameContent(src, dest))
                return candidate; // déjà là (même fichier ou contenu identique)
        }
        return QString();
    };

    QString result;
    int last = 0;
    QRegularExpressionMatchIterator it = imgTag.globalMatch(markdown);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        result += markdown.mid(last, m.capturedStart() - last);
        last = m.capturedEnd();

        const QString tagPath = m.captured(1);
        const bool isHome = tagPath.startsWith(QStringLiteral("~/"));
        const bool isAbs = QDir::isAbsolutePath(tagPath);
        const QString src = isHome ? QDir::homePath() + QLatin1Char('/') + tagPath.mid(2)
                                   : (isAbs ? tagPath : QDir(sourceFolder).filePath(tagPath));
        if (!QFileInfo(src).isFile()) {
            result += m.captured(0); // image introuvable : balise inchangée
            continue;
        }

        // Chemin relatif dans la note de destination : on garde le sous-dossier d'une balise
        // relative, sinon (absolue, ~/, ou remontant par ..) on ne garde que le nom du fichier.
        QString rel = QDir::cleanPath(tagPath);
        if (isHome || isAbs || rel.startsWith(QStringLiteral("..")))
            rel = QFileInfo(src).fileName();

        const QString newRel = importOne(src, rel);
        result += newRel.isEmpty() ? m.captured(0) : QStringLiteral("![[%1]]").arg(newRel);
    }
    result += markdown.mid(last);
    return result;
}

bool MarkdownEdit::insertMarkdownOnNextLine(const QString &markdown)
{
    QStringList lines = QString(markdown).replace("\r\n", "\n").replace('\r', '\n').split('\n');
    while (!lines.isEmpty() && lines.first().trimmed().isEmpty())
        lines.removeFirst();
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    if (lines.isEmpty())
        return false;

    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    openNewLineAfterCursor(cursor);
    insertMarkdownContent(cursor, lines);
    cursor.endEditBlock();

    setTextCursor(cursor);
    ensureCursorVisible();
    noteModified = true;
    return true;
}

bool MarkdownEdit::insertImageOnNextLine(const QImage &image)
{
    if (image.isNull())
        return false;

    // Enregistre l'image dans le dossier de la note (comme le collage Ctrl+V), sans écraser
    // un fichier existant (deux captures dans la même seconde ont le même nom horodaté).
    QString folder = basePath.isEmpty() ? QDir::currentPath() : basePath;
    if (!QDir(folder).exists())
        QDir().mkpath(folder);
    QString fname = generateImageFileName();
    const QString stem = QFileInfo(fname).completeBaseName();
    for (int n = 1; QFile::exists(QDir(folder).filePath(fname)); ++n)
        fname = QStringLiteral("%1_%2.png").arg(stem).arg(n);
    if (!image.save(QDir(folder).filePath(fname), "PNG"))
        return false;

    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();

    openNewLineAfterCursor(cursor);

    insertImageTagAndPicture(cursor, fname, image);
    cursor.endEditBlock();

    setTextCursor(cursor);
    ensureCursorVisible();
    noteModified = true;
    return true;
}

void MarkdownEdit::insertFromMimeData(const QMimeData *source)
{
    // ~ TMarkdownEdit.PasteFromClipboard / PasteImageFromClipboard : si le presse-papier
    // contient une image, elle est enregistrée sur disque (nom horodaté) dans FBasePath,
    // puis le triplet texte+séparateur+image est inséré (cf. insertImageTagAndPicture),
    // pour un comportement identique à une image chargée depuis un fichier .md.
    if (source->hasImage()) {
        const QImage image = qvariant_cast<QImage>(source->imageData());
        if (!image.isNull()) {
            const QString fname = generateImageFileName();
            QString folder = basePath.isEmpty() ? QDir::currentPath() : basePath;
            if (!QDir(folder).exists())
                QDir().mkpath(folder);
            const QString fullPath = QDir(folder).filePath(fname);

            if (image.save(fullPath, "PNG")) {
                QTextCursor cursor = textCursor();
                insertImageTagAndPicture(cursor, fname, image);
                setTextCursor(cursor);
                noteModified = true;
                return;
            }
        }
    }

    if (source->hasText()) {
        QTextEdit::insertFromMimeData(source);
        noteModified = true;
    }
}

// ======================= Édition de tableau façon Obsidian =======================

void MarkdownEdit::keyPressEvent(QKeyEvent *event)
{
    // Ctrl+Alt+C dans l'aperçu (lecture seule) de la bibliothèque : demande l'insertion de la
    // sélection dans la note en cours d'édition (traitée par MainWindow). Sans effet dans l'éditeur.
    if ((isReadOnly() || m_copyToNoteShortcut) && (event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::AltModifier)
        && !(event->modifiers() & Qt::ShiftModifier) && event->key() == Qt::Key_C) {
        const QString markdown = selectedMarkdownText();
        if (!markdown.trimmed().isEmpty())
            emit copyToNoteRequested(markdown);
        event->accept();
        return;
    }

    // ~ Tab/Maj+Tab navigue de cellule en cellule dans un tableau, comme Obsidian.
    // Tab dans la toute dernière cellule ajoute une nouvelle ligne (aussi comme Obsidian).
    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        const QTextCursor cursor = textCursor();
        if (QTextTable *table = cursor.currentTable()) {
            const QTextTableCell cell = table->cellAt(cursor);
            int row = cell.row(), col = cell.column();

            if (event->key() == Qt::Key_Tab) {
                ++col;
                if (col >= table->columns()) {
                    col = 0;
                    ++row;
                }
                if (row >= table->rows())
                    table->insertRows(table->rows(), 1);
            } else {
                --col;
                if (col < 0) {
                    col = table->columns() - 1;
                    --row;
                }
                if (row < 0) {
                    event->accept();
                    return;
                }
            }
            setTextCursor(table->cellAt(row, col).firstCursorPosition());
            event->accept();
            return;
        }
    }
    QTextEdit::keyPressEvent(event);
}

void MarkdownEdit::setExtraContextMenuActions(const QList<QAction *> &actions)
{
    m_extraContextMenuActions = actions;
}

void MarkdownEdit::focusOutEvent(QFocusEvent *event)
{
    QTextEdit::focusOutEvent(event);
    // L'ouverture d'un menu contextuel prend le focus (PopupFocusReason) : ce n'est pas "quitter".
    if (event->reason() != Qt::PopupFocusReason)
        emit focusLeft();
}

namespace {
// ---- aides pour les formats de texte appliqués à une sélection ----

struct TextSegment
{
    QTextBlock block;
    int s = 0; // début dans le bloc
    int e = 0; // fin (exclue) dans le bloc
};

// Découpe la sélection [selStart, selEnd) en un segment par bloc touché (les blocs des cellules
// de tableau sont des blocs comme les autres pour QTextDocument).
QList<TextSegment> selectionSegments(QTextDocument *doc, int selStart, int selEnd)
{
    QList<TextSegment> out;
    for (QTextBlock b = doc->findBlock(selStart); b.isValid() && b.position() < selEnd; b = b.next()) {
        const int bs = b.position();
        const int s = qMax(selStart, bs) - bs;
        const int e = qMin(selEnd, bs + b.length() - 1) - bs;
        if (e > s)
            out.append({b, s, e});
    }
    return out;
}

// Zone de code (lignes ``` comprises) : le Markdown inline n'y a pas de sens.
bool isCodeLikeBlock(const QTextBlock &b)
{
    return MarkdownHighlighter::isCodeBlockState(b.userState())
        || MarkdownHighlighter::isFenceLineText(b.text().trimmed());
}

int starRunBefore(const QString &t, int pos)
{
    int n = 0;
    while (pos - 1 - n >= 0 && t.at(pos - 1 - n) == QLatin1Char('*'))
        ++n;
    return n;
}

int starRunAfter(const QString &t, int pos)
{
    int n = 0;
    while (pos + n < t.size() && t.at(pos + n) == QLatin1Char('*'))
        ++n;
    return n;
}

// Préfixe de bloc Markdown : titre "## ", citation "> ", puce "- ", case "- [ ] ", numéro "1. ".
const QRegularExpression &blockPrefixRe()
{
    static const QRegularExpression re(QStringLiteral(
        R"(^(?:#{1,6}[ \t]+|(?:>[ \t]?)+|[-*+][ \t]+(?:\[[ xX]\][ \t]+)?|\d+[.)][ \t]+))"));
    return re;
}
} // namespace

void MarkdownEdit::contextMenuEvent(QContextMenuEvent *event)
{
    // Clic droit : la sélection est conservée si le clic tombe dedans ; sinon le curseur
    // est placé au point cliqué (les insertions du menu se font donc "là où on a cliqué").
    const QTextCursor click = cursorForPosition(event->pos());
    {
        const QTextCursor sel = textCursor();
        const bool clickInSelection = sel.hasSelection()
            && click.position() >= sel.selectionStart() && click.position() <= sel.selectionEnd();
        if (!clickInSelection && !isReadOnly())
            setTextCursor(click);
    }

    // ~ menu standard (Copier/Coller...) + actions de MainWindow (ex. "Exécuter")
    QMenu *menu = createStandardContextMenu();
    if (!m_extraContextMenuActions.isEmpty()) {
        menu->addSeparator();
        menu->addActions(m_extraContextMenuActions);
    }

    if (!isReadOnly()) {
        const QTextCursor cur = textCursor();

        // --- Format : tous les formats Markdown applicables à la sélection ---
        if (cur.hasSelection()) {
            const int s = cur.selectionStart();
            const int e = cur.selectionEnd();
            menu->addSeparator();
            QMenu *fmt = menu->addMenu(tr("Format"));

            auto addInline = [this, fmt, s, e](const QString &label, const QString &marker) {
                fmt->addAction(label, this, [this, marker, s, e]() { applyInlineFormat(marker, s, e); });
            };
            addInline(tr("Bold"), QStringLiteral("**"));
            addInline(tr("Italic"), QStringLiteral("*"));
            addInline(tr("Underline"), QStringLiteral("__"));
            addInline(tr("Strikethrough"), QStringLiteral("~~"));
            addInline(tr("Highlight"), QStringLiteral("=="));
            addInline(tr("Inline Code"), QStringLiteral("`"));
            addInline(tr("Math"), QStringLiteral("$"));

            fmt->addSeparator();
            QMenu *heading = fmt->addMenu(tr("Heading"));
            for (int lvl = 1; lvl <= 5; ++lvl) {
                heading->addAction(tr("Heading %1").arg(lvl), this, [this, lvl, s, e]() {
                    applyBlockFormat(static_cast<BlockFormatKind>(lvl), s, e);
                });
            }
            heading->addSeparator();
            heading->addAction(tr("Normal Text"), this, [this, s, e]() { applyBlockFormat(BlockParagraph, s, e); });

            auto addBlock = [this, fmt, s, e](const QString &label, BlockFormatKind kind) {
                fmt->addAction(label, this, [this, kind, s, e]() { applyBlockFormat(kind, s, e); });
            };
            addBlock(tr("Quote"), BlockQuote);
            addBlock(tr("Bulleted List"), BlockBullet);
            addBlock(tr("Numbered List"), BlockNumbered);
            addBlock(tr("Task List"), BlockTask);

            fmt->addSeparator();
            fmt->addAction(tr("Clear Formatting"), this, [this, s, e]() { clearTextFormatting(s, e); });
        }

        // --- Table : insertion d'un tableau + édition (actif quand le clic est dans un tableau) ---
        menu->addSeparator();
        QMenu *tableMenu = menu->addMenu(tr("Table"));
        tableMenu->addAction(tr("Insert Table..."), this, [this]() {
            int rows = 2, cols = 3;
            if (askTableSize(&rows, &cols))
                insertEmptyTable(rows, cols);
        });
        tableMenu->addSeparator();

        QTextTable *table = click.currentTable();
        const int curRow = table ? table->cellAt(click).row() : -1;

        auto addTableAction = [this, tableMenu, table, click](const QString &label, bool enabled, auto op) {
            QAction *a = tableMenu->addAction(label, this, [this, table, click, op]() { (this->*op)(table, click); });
            a->setEnabled(enabled);
        };
        QAction *rowAbove = tableMenu->addAction(tr("Insert Row Above"), this, [this, table, click]() { insertTableRow(table, click, false); });
        rowAbove->setEnabled(table && curRow > 0); // pas au-dessus de la ligne d'en-tête
        QAction *rowBelow = tableMenu->addAction(tr("Insert Row Below"), this, [this, table, click]() { insertTableRow(table, click, true); });
        rowBelow->setEnabled(table != nullptr);
        addTableAction(tr("Duplicate Row"), table != nullptr, &MarkdownEdit::duplicateTableRow);
        addTableAction(tr("Delete Row"), table && table->rows() > 1, &MarkdownEdit::deleteTableRow);
        tableMenu->addSeparator();
        QAction *colLeft = tableMenu->addAction(tr("Insert Column Left"), this, [this, table, click]() { insertTableColumn(table, click, false); });
        colLeft->setEnabled(table != nullptr);
        QAction *colRight = tableMenu->addAction(tr("Insert Column Right"), this, [this, table, click]() { insertTableColumn(table, click, true); });
        colRight->setEnabled(table != nullptr);
        addTableAction(tr("Duplicate Column"), table != nullptr, &MarkdownEdit::duplicateTableColumn);
        addTableAction(tr("Delete Column"), table && table->columns() > 1, &MarkdownEdit::deleteTableColumn);

        // --- Zone de code ``` ---
        QAction *codeAct = menu->addAction(tr("Insert Code Block"), this, [this]() { insertCodeBlockOnNextLine(); });
        codeAct->setEnabled(!isCodeLikeBlock(textCursor().block()));
    }

    menu->exec(event->globalPos());
    delete menu;
}

// ======================= Édition de tableau =======================

void MarkdownEdit::insertTableRow(QTextTable *table, const QTextCursor &at, bool below)
{
    const QTextTableCell cell = table->cellAt(at);
    const int row = cell.row();
    if (!below && row == 0)
        return; // la ligne 0 est l'en-tête GFM : on n'insère jamais au-dessus
    const int col = cell.column();
    const int newRow = row + (below ? 1 : 0);
    const int srcRow = below ? row : row + 1; // la ligne d'origine, une fois la nouvelle insérée

    QTextCursor edit(document());
    edit.beginEditBlock();
    table->insertRows(newRow, 1);
    for (int c = 0; c < table->columns(); ++c) {
        // La nouvelle ligne reprend l'alignement de colonne de la ligne voisine.
        QTextCursor src = table->cellAt(srcRow, c).firstCursorPosition();
        QTextCursor dst = table->cellAt(newRow, c).firstCursorPosition();
        QTextBlockFormat bf = dst.blockFormat();
        bf.setAlignment(src.blockFormat().alignment());
        dst.setBlockFormat(bf);
    }
    edit.endEditBlock();
    setTextCursor(table->cellAt(newRow, col).firstCursorPosition());
}

void MarkdownEdit::deleteTableRow(QTextTable *table, const QTextCursor &at)
{
    if (table->rows() <= 1)
        return; // garde toujours au moins une ligne
    const QTextTableCell cell = table->cellAt(at);
    const int row = cell.row();
    const int col = cell.column();
    table->removeRows(row, 1);
    setTextCursor(table->cellAt(qMin(row, table->rows() - 1), qMin(col, table->columns() - 1)).firstCursorPosition());
}

void MarkdownEdit::duplicateTableRow(QTextTable *table, const QTextCursor &at)
{
    const QTextTableCell cell = table->cellAt(at);
    const int row = cell.row();

    QStringList texts;
    for (int c = 0; c < table->columns(); ++c)
        texts << table->cellAt(row, c).firstCursorPosition().block().text();

    table->insertRows(row + 1, 1);
    for (int c = 0; c < table->columns(); ++c) {
        QTextCursor cc = table->cellAt(row + 1, c).firstCursorPosition();
        cc.insertText(texts.value(c));
    }
}

void MarkdownEdit::insertTableColumn(QTextTable *table, const QTextCursor &at, bool right)
{
    const QTextTableCell cell = table->cellAt(at);
    const int col = cell.column();
    const int row = cell.row();
    const int newCol = col + (right ? 1 : 0);
    const int srcCol = right ? col : col + 1; // la colonne d'origine, une fois la nouvelle insérée

    QTextCursor edit(document());
    edit.beginEditBlock();
    table->insertColumns(newCol, 1);
    for (int r = 0; r < table->rows(); ++r) {
        // La nouvelle colonne reprend l'alignement de sa voisine ; sa cellule d'en-tête est en gras.
        QTextCursor src = table->cellAt(r, srcCol).firstCursorPosition();
        QTextCursor dst = table->cellAt(r, newCol).firstCursorPosition();
        QTextBlockFormat bf = dst.blockFormat();
        bf.setAlignment(src.blockFormat().alignment());
        dst.setBlockFormat(bf);
        if (r == 0) {
            QTextCharFormat cf;
            cf.setFontWeight(QFont::Bold);
            dst.mergeBlockCharFormat(cf);
        }
    }
    edit.endEditBlock();
    setTextCursor(table->cellAt(row, newCol).firstCursorPosition());
}

void MarkdownEdit::deleteTableColumn(QTextTable *table, const QTextCursor &at)
{
    if (table->columns() <= 1)
        return; // garde toujours au moins une colonne
    const QTextTableCell cell = table->cellAt(at);
    const int row = cell.row();
    const int col = cell.column();
    table->removeColumns(col, 1);
    setTextCursor(table->cellAt(qMin(row, table->rows() - 1), qMin(col, table->columns() - 1)).firstCursorPosition());
}

void MarkdownEdit::duplicateTableColumn(QTextTable *table, const QTextCursor &at)
{
    const QTextTableCell cell = table->cellAt(at);
    const int col = cell.column();

    QStringList texts;
    for (int r = 0; r < table->rows(); ++r)
        texts << table->cellAt(r, col).firstCursorPosition().block().text();

    table->insertColumns(col + 1, 1);
    for (int r = 0; r < table->rows(); ++r) {
        QTextCursor cc = table->cellAt(r, col + 1).firstCursorPosition();
        cc.insertText(texts.value(r));
    }
}

bool MarkdownEdit::askTableSize(int *rows, int *cols)
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Insert Table"));
    auto *form = new QFormLayout(&dlg);

    auto *colSpin = new QSpinBox(&dlg);
    colSpin->setRange(1, 20);
    colSpin->setValue(*cols);
    auto *rowSpin = new QSpinBox(&dlg);
    rowSpin->setRange(1, 200);
    rowSpin->setValue(*rows);
    form->addRow(tr("Columns:"), colSpin);
    form->addRow(tr("Rows (excluding header):"), rowSpin);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return false;
    *cols = colSpin->value();
    *rows = rowSpin->value();
    return true;
}

void MarkdownEdit::insertEmptyTable(int bodyRows, int cols)
{
    // Nouveau tableau GFM vide (en-tête "Column N" + lignes vides) sur une NOUVELLE ligne située
    // après la ligne du curseur (ou après le tableau si le curseur est déjà dans un tableau).
    cols = qBound(1, cols, 50);
    bodyRows = qBound(1, bodyRows, 500);

    QStringList header, sep, empty;
    for (int c = 0; c < cols; ++c) {
        header << QStringLiteral("Column %1").arg(c + 1);
        sep << QStringLiteral("---");
        empty << QString();
    }
    auto line = [](const QStringList &cells) { return QStringLiteral("| ") + cells.join(QStringLiteral(" | ")) + QStringLiteral(" |"); };

    QStringList lines{line(header), line(sep)};
    for (int r = 0; r < bodyRows; ++r)
        lines << line(empty);

    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    openNewLineAfterCursor(cursor);
    insertMarkdownTable(cursor, lines);
    cursor.endEditBlock();

    // Place le curseur dans la première cellule (même repérage que maybeConvertTypedTable).
    QTextCursor probe(document());
    probe.setPosition(qMax(0, cursor.position() - 1));
    if (QTextTable *table = probe.currentTable())
        setTextCursor(table->cellAt(0, 0).firstCursorPosition());
    else
        setTextCursor(cursor);

    ensureCursorVisible();
    noteModified = true;
}

void MarkdownEdit::insertCodeBlockOnNextLine()
{
    // Zone de code vide : ```, une ligne vide pour le contenu, ``` -- sur de nouvelles lignes
    // après la ligne du curseur ; le curseur est placé dans la zone, prêt à taper.
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    openNewLineAfterCursor(cursor);
    cursor.insertText(QStringLiteral("```"));
    cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
    const int inner = cursor.position();
    cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
    cursor.insertText(QStringLiteral("```"));
    cursor.endEditBlock();

    QTextCursor inside(document());
    inside.setPosition(inner);
    setTextCursor(inside);
    ensureCursorVisible();
    noteModified = true;
}

// ======================= Formats de texte (menu "Format") =======================

void MarkdownEdit::applyInlineFormat(const QString &marker, int selStart, int selEnd)
{
    // Bascule un marqueur Markdown (**, *, __, ~~, ==, `, $) autour de la sélection, bloc par
    // bloc : déjà entouré -> retire ; sinon -> entoure. Les espaces de bord sont laissés dehors
    // (un "** gras **" ne serait pas interprété), les zones de code sont ignorées.
    const QList<TextSegment> segs = selectionSegments(document(), selStart, selEnd);
    if (segs.isEmpty())
        return;

    const int ml = marker.size();
    const bool isStar = (marker == QLatin1String("*"));
    const QChar objChar(QChar::ObjectReplacementCharacter);
    const QChar sepChar(QChar::LineSeparator);

    QTextCursor c(document());
    int newStart = -1, newEnd = -1;
    c.beginEditBlock();
    for (const TextSegment &sg : segs) {
        const QTextBlock b = sg.block;
        if (isCodeLikeBlock(b))
            continue;
        const QString text = b.text();
        int s = sg.s, e = sg.e;
        while (s < e && text.at(s).isSpace())
            ++s;
        while (e > s && text.at(e - 1).isSpace())
            --e;
        if (e <= s)
            continue;
        const QString seg = text.mid(s, e - s);
        if (seg.contains(objChar) || seg.contains(sepChar))
            continue; // image / balise ![[..]] : on n'y touche pas

        // Pour l'italique "*", un nombre PAIR d'étoiles collées appartient à un gras (**) : on ne
        // le prend pas pour de l'italique.
        const bool wrappedInside = seg.size() >= 2 * ml && seg.startsWith(marker) && seg.endsWith(marker)
            && (!isStar || (starRunAfter(seg, 0) % 2 == 1 && starRunBefore(seg, seg.size()) % 2 == 1));
        const bool wrappedOutside = !wrappedInside && s >= ml && e + ml <= text.size()
            && text.mid(s - ml, ml) == marker && text.mid(e, ml) == marker
            && (!isStar || (starRunBefore(text, s) % 2 == 1 && starRunAfter(text, e) % 2 == 1));

        const int base = b.position();
        auto replaceRange = [&](int from, int to, const QString &str) {
            c.setPosition(base + from);
            c.setPosition(base + to, QTextCursor::KeepAnchor);
            c.insertText(str);
        };

        int ns, ne; // nouvelle plage du texte (sans marqueurs), relative au bloc
        if (wrappedInside) {
            const QString inner = seg.mid(ml, seg.size() - 2 * ml);
            replaceRange(s, e, inner);
            ns = s;
            ne = s + inner.size();
        } else if (wrappedOutside) {
            replaceRange(s - ml, e + ml, seg);
            ns = s - ml;
            ne = ns + seg.size();
        } else {
            replaceRange(s, e, marker + seg + marker);
            ns = s + ml;
            ne = ns + seg.size();
        }
        if (newStart < 0)
            newStart = base + ns;
        newEnd = base + ne;
    }
    c.endEditBlock();

    if (newStart >= 0) {
        QTextCursor sel(document());
        sel.setPosition(newStart);
        sel.setPosition(newEnd, QTextCursor::KeepAnchor);
        setTextCursor(sel);
    }
}

void MarkdownEdit::applyBlockFormat(BlockFormatKind kind, int selStart, int selEnd)
{
    // Titre (# .. #####), citation, liste à puces / numérotée / de tâches : préfixe de ligne
    // des blocs touchés par la sélection. Même format déjà présent partout -> il est retiré.
    struct Item { QTextBlock block; int prefixLen; QString prefix; };
    QList<Item> items;
    for (const TextSegment &sg : selectionSegments(document(), selStart, selEnd)) {
        if (isCodeLikeBlock(sg.block) || QTextCursor(sg.block).currentTable())
            continue; // pas de titre/liste dans un code ni dans une cellule de tableau
        const QRegularExpressionMatch m = blockPrefixRe().match(sg.block.text());
        items.append({sg.block, m.hasMatch() ? int(m.capturedLength()) : 0, m.hasMatch() ? m.captured(0) : QString()});
    }
    if (items.isEmpty())
        return;

    auto matchesKind = [kind](const QString &prefix) {
        const QString p = prefix.trimmed();
        if (kind >= BlockH1 && kind <= BlockH5)
            return p == QString(int(kind), QLatin1Char('#'));
        switch (kind) {
        case BlockQuote:    return p.startsWith(QLatin1Char('>'));
        case BlockBullet:   return p == QLatin1String("-") || p == QLatin1String("*") || p == QLatin1String("+");
        case BlockNumbered: return !p.isEmpty() && p.at(0).isDigit();
        case BlockTask:     return p.startsWith(QLatin1Char('-')) && p.contains(QLatin1Char('['));
        default:            return false;
        }
    };
    bool allMatch = (kind != BlockParagraph);
    for (const Item &it : items)
        allMatch = allMatch && matchesKind(it.prefix);
    const bool remove = (kind == BlockParagraph) || allMatch;

    QTextCursor c(document());
    c.beginEditBlock();
    int number = 1;
    for (const Item &it : items) {
        QString newPrefix;
        if (!remove) {
            if (kind >= BlockH1 && kind <= BlockH5)
                newPrefix = QString(int(kind), QLatin1Char('#')) + QLatin1Char(' ');
            else if (kind == BlockQuote)
                newPrefix = QStringLiteral("> ");
            else if (kind == BlockBullet)
                newPrefix = QStringLiteral("- ");
            else if (kind == BlockNumbered)
                newPrefix = QStringLiteral("%1. ").arg(number++);
            else if (kind == BlockTask)
                newPrefix = QStringLiteral("- [ ] ");
        }
        if (it.prefixLen == 0 && newPrefix.isEmpty())
            continue;
        const int pos = it.block.position();
        c.setPosition(pos);
        c.setPosition(pos + it.prefixLen, QTextCursor::KeepAnchor);
        c.insertText(newPrefix);
    }
    c.endEditBlock();

    QTextCursor sel(document());
    sel.setPosition(items.first().block.position());
    const QTextBlock last = items.last().block;
    sel.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
    setTextCursor(sel);
}

void MarkdownEdit::clearTextFormatting(int selStart, int selEnd)
{
    // Retire les marqueurs inline (gras, italique, souligné, barré, surligné, code) contenus dans
    // la sélection, ainsi que le préfixe de bloc (titre, citation, liste) des lignes dont le
    // début est sélectionné.
    static const QList<QRegularExpression> inlineRes = {
        QRegularExpression(QStringLiteral(R"(\*\*\*(.+?)\*\*\*)")),
        QRegularExpression(QStringLiteral(R"(\*\*(.+?)\*\*)")),
        QRegularExpression(QStringLiteral(R"(__(.+?)__)")),
        QRegularExpression(QStringLiteral(R"(~~(.+?)~~)")),
        QRegularExpression(QStringLiteral(R"(==(.+?)==)")),
        QRegularExpression(QStringLiteral(R"(\*(.+?)\*)")),
        QRegularExpression(QStringLiteral(R"(`(.+?)`)")),
    };
    const QChar objChar(QChar::ObjectReplacementCharacter);
    const QChar sepChar(QChar::LineSeparator);

    QTextCursor c(document());
    int newStart = -1, newEnd = -1;
    c.beginEditBlock();
    for (const TextSegment &sg : selectionSegments(document(), selStart, selEnd)) {
        const QTextBlock b = sg.block;
        if (isCodeLikeBlock(b))
            continue;
        const QString seg = b.text().mid(sg.s, sg.e - sg.s);
        if (seg.contains(objChar) || seg.contains(sepChar))
            continue;

        QString out = seg;
        for (const QRegularExpression &re : inlineRes)
            out.replace(re, QStringLiteral("\\1"));
        if (sg.s == 0 && !QTextCursor(b).currentTable()) {
            const QRegularExpressionMatch m = blockPrefixRe().match(out);
            if (m.hasMatch())
                out.remove(0, m.capturedLength());
        }

        const int base = b.position();
        if (out != seg) {
            c.setPosition(base + sg.s);
            c.setPosition(base + sg.e, QTextCursor::KeepAnchor);
            c.insertText(out);
        }
        if (newStart < 0)
            newStart = base + sg.s;
        newEnd = base + sg.s + out.size();
    }
    c.endEditBlock();

    if (newStart >= 0) {
        QTextCursor sel(document());
        sel.setPosition(newStart);
        sel.setPosition(newEnd, QTextCursor::KeepAnchor);
        setTextCursor(sel);
    }
}

// ======================= Résultat d'une commande exécutée depuis la note (v2.6.0) =======================

namespace {
// Lignes du résultat, prêtes à être insérées. Une ligne qui commencerait par ``` fermerait la zone de
// code : on lui préfixe une espace de largeur nulle (qui n'est pas retirée par trimmed()).
QStringList commandOutputLines(const QString &output)
{
    QStringList lines = QString(output).replace(QStringLiteral("\r\n"), QStringLiteral("\n"))
                            .replace(QLatin1Char('\r'), QLatin1Char('\n')).split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.first().trimmed().isEmpty())
        lines.removeFirst();
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    for (QString &l : lines) {
        if (l.trimmed().startsWith(QStringLiteral("```")))
            l.prepend(QChar(0x200B));
    }
    return lines;
}
}

QTextCursor MarkdownEdit::markCommandLine() const
{
    return QTextCursor(textCursor().block());
}

int MarkdownEdit::commandOccurrence() const
{
    const QTextBlock cur = textCursor().block();
    const QString cmd = cur.text().trimmed();
    int n = 0;
    for (QTextBlock b = document()->begin(); b.isValid() && b != cur; b = b.next()) {
        if (b.text().trimmed() == cmd)
            ++n;
    }
    return n;
}

bool MarkdownEdit::insertCommandOutputAfter(const QTextCursor &mark, const QString &command, const QString &output)
{
    const QStringList lines = commandOutputLines(output);
    if (lines.isEmpty())
        return false;

    // La ligne repérée a pu être modifiée ou déplacée pendant l'exécution : si son texte n'est
    // plus la commande, on prend la ligne identique la plus proche ; à défaut, on garde le repère.
    const QString cmd = command.trimmed();
    QTextBlock b = mark.block();
    if (!b.isValid() || b.text().trimmed() != cmd) {
        const int ref = b.isValid() ? b.position() : 0;
        QTextBlock best;
        int bestDist = INT_MAX;
        for (QTextBlock x = document()->begin(); x.isValid(); x = x.next()) {
            if (x.text().trimmed() != cmd)
                continue;
            const int d = qAbs(x.position() - ref);
            if (d < bestDist) {
                best = x;
                bestDist = d;
            }
        }
        if (best.isValid())
            b = best;
        else if (!b.isValid())
            return false;
    }

    // Commande déjà dans une zone ``` : le résultat s'ajoute tel quel (pas de ``` imbriqués).
    const bool inCode = MarkdownHighlighter::isCodeBlockState(b.userState())
                        && !MarkdownHighlighter::isFenceLineText(b.text().trimmed());
    QStringList all = lines;
    if (!inCode) {
        all.prepend(QStringLiteral("```"));
        all.append(QStringLiteral("```"));
    }

    // Point d'insertion : fin de la ligne (ou du tableau qui la contient).
    QTextCursor cursor(b);
    int insertAt = b.position() + b.length() - 1;
    if (QTextTable *table = cursor.currentTable())
        insertAt = table->lastPosition() + 1;

    // Curseur / défilement de l'utilisateur : conservés (un curseur situé en fin de ligne serait
    // sinon repoussé après le texte inséré).
    const QTextCursor user = textCursor();
    int userPos = user.position(), userAnchor = user.anchor();
    const int scrollValue = verticalScrollBar()->value();
    const int before = document()->characterCount();

    cursor.beginEditBlock();
    openNewLineAfterCursor(cursor);
    for (int i = 0; i < all.size(); ++i) {
        if (i > 0)
            cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
        cursor.insertText(all.at(i));
    }
    cursor.endEditBlock();

    const int delta = document()->characterCount() - before;
    if (userPos > insertAt)
        userPos += delta;
    if (userAnchor > insertAt)
        userAnchor += delta;
    // (un curseur situé exactement en fin de ligne est repoussé par Qt après le texte inséré :
    // on remet donc toujours la position calculée si elle diffère)
    if (textCursor().position() != userPos || textCursor().anchor() != userAnchor) {
        QTextCursor restored(document());
        restored.setPosition(userAnchor);
        restored.setPosition(userPos, QTextCursor::KeepAnchor);
        setTextCursor(restored);
    }
    verticalScrollBar()->setValue(scrollValue);
    noteModified = true;
    return true;
}

bool MarkdownEdit::insertCommandOutputInFile(const QString &fileName, const QString &command,
                                             int occurrence, const QString &output)
{
    const QStringList outLines = commandOutputLines(output);
    if (outLines.isEmpty())
        return false;

    QFile in(fileName);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    QStringList lines;
    {
        QTextStream ts(&in);
        ts.setEncoding(QStringConverter::Utf8);
        lines = ts.readAll().split(QLatin1Char('\n'));
    }
    in.close();

    // La commande est retrouvée par son texte (le fichier a pu être modifié depuis) : on prend la
    // n-ième ligne identique (n = rang au moment du lancement), sinon la dernière ; si la ligne
    // n'existe plus du tout, le résultat est ajouté à la fin de la fiche.
    const QString cmd = command.trimmed();
    QList<int> hits;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).trimmed() == cmd)
            hits << i;
    }

    QStringList block = outLines;
    int insertAfter;
    if (hits.isEmpty()) {
        if (!lines.isEmpty() && !lines.last().trimmed().isEmpty())
            lines << QString();
        insertAfter = lines.size() - 1;
        block.prepend(QStringLiteral("```"));
        block.append(QStringLiteral("```"));
    } else {
        const int idx = hits.at(qMin(qMax(occurrence, 0), hits.size() - 1));
        bool inCode = false;
        for (int i = 0; i < idx; ++i) {
            if (MarkdownHighlighter::isFenceLineText(lines.at(i).trimmed()))
                inCode = !inCode;
        }
        insertAfter = idx;
        if (!inCode) {
            // Ligne de tableau : le résultat va après le tableau, pas au milieu.
            if (MarkdownHighlighter::isTableRowLine(lines.at(idx))) {
                while (insertAfter + 1 < lines.size() && MarkdownHighlighter::isTableRowLine(lines.at(insertAfter + 1)))
                    ++insertAfter;
            }
            block.prepend(QStringLiteral("```"));
            block.append(QStringLiteral("```"));
        }
    }
    for (int k = 0; k < block.size(); ++k)
        lines.insert(insertAfter + 1 + k, block.at(k));

    QFile out(fileName);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    QTextStream os(&out);
    os.setEncoding(QStringConverter::Utf8);
    os << lines.join(QLatin1Char('\n'));
    return true;
}

// ======================= Nom du langage d'une zone ``` (balises masquées) =======================

void MarkdownEdit::paintEvent(QPaintEvent *event)
{
    QTextEdit::paintEvent(event);

    // Zone ```bash dont le curseur est EN DEHORS : les ``` et "bash" sont transparents
    // (cf. MarkdownHighlighter) ; on dessine ici "bash" en petit, à droite de la zone. Quand le
    // curseur est dans la zone, la ligne "```bash" est affichée normalement, à gauche.
    const int cursorPos = textCursor().position();
    QAbstractTextDocumentLayout *layout = document()->documentLayout();
    const QPointF offset(-horizontalScrollBar()->value(), -verticalScrollBar()->value());

    QPainter p(viewport());
    QFont f(QStringLiteral("Monospace"));
    f.setStyleHint(QFont::Monospace);
    f.setPointSizeF(7);
    f.setItalic(true);
    p.setFont(f);
    p.setPen(QColor(150, 150, 150));

    for (QTextBlock b = document()->findBlock(cursorForPosition(QPoint(0, 0)).position()); b.isValid(); b = b.next()) {
        const QRectF r = layout->blockBoundingRect(b).translated(offset);
        if (r.top() > viewport()->height())
            break;
        const QString t = b.text().trimmed();
        // ligne OUVRANTE : ligne ``` dont l'état est "dans un bloc de code" (la fermante est Normal)
        if (r.bottom() < 0 || !MarkdownHighlighter::isFenceLineText(t)
            || !MarkdownHighlighter::isCodeBlockState(b.userState()))
            continue;

        int i = 0;
        while (i < t.size() && t.at(i) == QLatin1Char('`'))
            ++i;
        const QString lang = t.mid(i).trimmed();
        if (lang.isEmpty())
            continue;

        // plage de la zone : de la ligne ouvrante à la fin de la ligne fermante (incluse)
        QTextBlock close = b.next();
        while (close.isValid() && !MarkdownHighlighter::isFenceLineText(close.text().trimmed()))
            close = close.next();
        const int rangeEnd = close.isValid() ? close.position() + close.length() - 1 : document()->characterCount();
        if (cursorPos >= b.position() && cursorPos <= rangeEnd)
            continue; // curseur dans la zone : "```bash" est affiché normalement

        const qreal right = qMin(r.right(), qreal(viewport()->width())) - 6;
        p.drawText(QRectF(r.left(), r.top(), right - r.left(), r.height()),
                   Qt::AlignRight | Qt::AlignVCenter, lang);
    }
}
