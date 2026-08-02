#include "markdownedit.h"
#include "markdownhighlighter.h"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QMimeData>
#include <QImage>
#include <QTextBlock>
#include <QTextTable>
#include <QTextTableFormat>
#include <QTextTableCell>
#include <QRegularExpression>
#include <QUrl>
#include <QMenu>
#include <QContextMenuEvent>
#include <QKeyEvent>

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

QString MarkdownEdit::getCurrentLineTextContent() const
{
    QTextCursor c = textCursor();
    c.select(QTextCursor::LineUnderCursor);
    return c.selectedText();
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

void MarkdownEdit::contextMenuEvent(QContextMenuEvent *event)
{
    // ~ menu contextuel standard (Copier/Coller...) + actions supplémentaires fournies
    // par MainWindow (ex. "Exécuter", cf. setExtraContextMenuActions) + un sous-menu
    // "Table" (façon Obsidian) quand le clic droit a lieu dans une cellule de tableau.
    QMenu *menu = createStandardContextMenu();

    if (!m_extraContextMenuActions.isEmpty()) {
        menu->addSeparator();
        menu->addActions(m_extraContextMenuActions);
    }

    const QTextCursor cursor = cursorForPosition(event->pos());
    if (QTextTable *table = cursor.currentTable()) {
        menu->addSeparator();
        QMenu *tableMenu = menu->addMenu(tr("Table"));

        tableMenu->addAction(tr("Insert Row Above"), this, [this, table, cursor]() { insertTableRow(table, cursor, false); });
        tableMenu->addAction(tr("Insert Row Below"), this, [this, table, cursor]() { insertTableRow(table, cursor, true); });
        tableMenu->addAction(tr("Duplicate Row"), this, [this, table, cursor]() { duplicateTableRow(table, cursor); });
        tableMenu->addAction(tr("Delete Row"), this, [this, table, cursor]() { deleteTableRow(table, cursor); });
        tableMenu->addSeparator();
        tableMenu->addAction(tr("Insert Column Left"), this, [this, table, cursor]() { insertTableColumn(table, cursor, false); });
        tableMenu->addAction(tr("Insert Column Right"), this, [this, table, cursor]() { insertTableColumn(table, cursor, true); });
        tableMenu->addAction(tr("Duplicate Column"), this, [this, table, cursor]() { duplicateTableColumn(table, cursor); });
        tableMenu->addAction(tr("Delete Column"), this, [this, table, cursor]() { deleteTableColumn(table, cursor); });
    }

    menu->exec(event->globalPos());
    delete menu;
}

void MarkdownEdit::insertTableRow(QTextTable *table, const QTextCursor &at, bool below)
{
    const QTextTableCell cell = table->cellAt(at);
    table->insertRows(cell.row() + (below ? 1 : 0), 1);
}

void MarkdownEdit::deleteTableRow(QTextTable *table, const QTextCursor &at)
{
    if (table->rows() <= 1)
        return; // garde toujours au moins une ligne
    const QTextTableCell cell = table->cellAt(at);
    table->removeRows(cell.row(), 1);
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
    table->insertColumns(cell.column() + (right ? 1 : 0), 1);
}

void MarkdownEdit::deleteTableColumn(QTextTable *table, const QTextCursor &at)
{
    if (table->columns() <= 1)
        return; // garde toujours au moins une colonne
    const QTextTableCell cell = table->cellAt(at);
    table->removeColumns(cell.column(), 1);
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
