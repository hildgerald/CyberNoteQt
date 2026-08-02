#include "markdownhighlighter.h"
#include "markdownkeywords.h"

#include <QFont>
#include <QColor>
#include <QTextBlock>
#include <QTextDocument>

namespace {
const QStringList kLangTable = {
    "",                 // 0 = langage inconnu / générique
    "python", "pascal", "javascript", "c", "powershell",
    "html", "http", "ruby", "bash", "perl", "basic"
};

// ~ Expressions régulières utilisées par le highlighter, regroupées ici plutôt que
// déclarées "static const" à l'intérieur de chaque fonction. Une variable statique
// LOCALE a une construction paresseuse (au premier appel de la fonction), rendue
// thread-safe depuis C++11 par un verrou implicite invisible ("magic statics") :
// coût et non-déterminisme que MISRA C++ demande d'éviter. Ici, la construction a
// lieu une seule fois, de façon déterministe, pendant l'initialisation statique de
// la traduction -- avant l'entrée dans main() -- exactement comme kLangTable ci-dessus.

// --- isHorizontalRule / isBlockQuotePrefix / isListMarkerPrefix / isTableSeparatorLine ---
const QRegularExpression kHorizontalRuleRe(R"(^ {0,3}([-*_])[ \t]*(?:\1[ \t]*){2,}$)");
const QRegularExpression kBlockQuotePrefixRe(R"(^ {0,3}((?:>[ \t]?)+))");
const QRegularExpression kListMarkerPrefixRe(R"(^(\s*)([-*+]|\d{1,9}[.)])(\s+))");
const QRegularExpression kTableSeparatorCellRe(R"(^\s*:?-+:?\s*$)");

// --- highlightCodeLine (schéma générique mot-clé/chaîne, cf. markdownkeywords.h) ---
const QRegularExpression kCodeStringRe(R"(("([^"\\]|\\.)*")|('([^'\\]|\\.)*'))");
const QRegularExpression kCodeWordRe(R"(\b[A-Za-z_][A-Za-z0-9_-]*\b)");

// --- highlightHtmlLine ---
const QRegularExpression kHtmlCommentRe(R"(<!--.*?-->)");
const QRegularExpression kHtmlTagRe(R"(</?[A-Za-z][A-Za-z0-9:-]*)");
const QRegularExpression kHtmlAttrRe(
    R"(\b([A-Za-z_:][A-Za-z0-9_:.-]*)\s*=\s*("([^"\\]|\\.)*"|'([^'\\]|\\.)*'))");

// --- highlightHttpLine ---
const QRegularExpression kHttpRequestRe(
    R"(^(GET|POST|PUT|DELETE|PATCH|HEAD|OPTIONS|CONNECT|TRACE)\s+(\S+)\s+(HTTP/\d\.\d)\s*$)");
const QRegularExpression kHttpStatusRe(R"(^(HTTP/\d\.\d)\s+(\d{3})\s*(.*)$)");
const QRegularExpression kHttpHeaderRe(R"(^([A-Za-z][A-Za-z0-9-]*)\s*:\s*(.*)$)");
const QRegularExpression kHttpBodyStringRe(R"("([^"\\]|\\.)*")");

// --- highlightInlineMarkers ---
const QRegularExpression kImageTagRe(QStringLiteral("!\\[\\[(.+?)\\]\\]\u2028\uFFFC"));
const QRegularExpression kHighlightMarkerRe(R"(==(.+?)==)");
const QRegularExpression kBoldMarkerRe(R"(\*\*(.+?)\*\*)");
const QRegularExpression kStrikeMarkerRe(R"(~~(.+?)~~)");
const QRegularExpression kCommentMarkerRe(R"(%%(.+?)%%)");
const QRegularExpression kUnderlineMarkerRe(R"(__(.+?)__)");
const QRegularExpression kCodeSpanMarkerRe(R"(`([^`]+?)`)");
const QRegularExpression kMathMarkerRe(R"(\$(.+?)\$)");
const QRegularExpression kItalicMarkerRe(R"((?<!\*)\*(?!\*)(.+?)(?<!\*)\*(?!\*))");
}

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    // ~ HeaderFontSize
    const int sizes[6] = {0, 24, 20, 18, 16, 14};
    for (int lvl = 1; lvl <= 5; ++lvl) {
        m_headerFormat[lvl].setFontWeight(QFont::Bold);
        m_headerFormat[lvl].setFontPointSize(sizes[lvl]);
    }

    m_boldFormat.setFontWeight(QFont::Bold);
    m_italicFormat.setFontItalic(true);
    m_underlineFormat.setFontUnderline(true);
    m_strikeFormat.setFontStrikeOut(true);

    m_highlightFormat.setBackground(QColor(150, 120, 0)); // jaune assombri : lisible avec du texte blanc par-dessus
    m_highlightFormat.setForeground(Qt::white);

    m_codeSpanFormat.setFontFamilies({"Monospace"});
    m_codeSpanFormat.setBackground(QColor(230, 230, 230));

    m_mathFormat.setFontFamilies({"Monospace"});
    m_mathFormat.setFontItalic(true);

    m_commentFormat.setForeground(Qt::gray);
    m_commentFormat.setFontItalic(true);

    m_codeBlockBgFormat.setForeground(QColor(220, 220, 220));
    m_codeBlockBgFormat.setFontFamilies({"Monospace"});

    m_fenceFormat.setForeground(Qt::darkGray);
    m_fenceFormat.setFontFamilies({"Monospace"});

    m_keywordFormat.setForeground(Qt::blue);
    m_keywordFormat.setFontWeight(QFont::Bold);
    m_keywordFormat.setFontFamilies({"Monospace"});

    m_cmdletFormat.setForeground(QColor(0, 128, 128));
    m_cmdletFormat.setFontWeight(QFont::Bold);
    m_cmdletFormat.setFontFamilies({"Monospace"});

    m_stringFormat.setForeground(QColor(163, 21, 21));
    m_stringFormat.setFontFamilies({"Monospace"});

    m_commentLineFormat.setForeground(QColor(0, 128, 0));
    m_commentLineFormat.setFontFamilies({"Monospace"});

    // Format "masqué" pour les marqueurs quand le curseur n'est pas dessus/dedans :
    // taille quasi nulle + transparent. Le caractère reste dans le document (donc
    // toujours navigable/éditable/sauvegardé), seul son rendu visuel disparaît.
    m_concealedFormat.setFontPointSize(0.5);
    m_concealedFormat.setForeground(Qt::transparent);

    // --- Structures de bloc (listes, citations, HR, Setext, tableaux) ---
    // Contrairement aux marqueurs d'emphase ci-dessus, ces marqueurs de structure
    // restent TOUJOURS visibles (stylés, jamais masqués/révélés selon le curseur).
    m_listMarkerFormat.setForeground(QColor(86, 156, 214));
    m_listMarkerFormat.setFontWeight(QFont::Bold);

    m_taskCheckboxFormat.setForeground(QColor(86, 156, 214));
    m_taskCheckboxFormat.setFontWeight(QFont::Bold);
    m_taskCheckboxFormat.setFontFamilies({"Monospace"});

    m_taskDoneTextFormat.setFontStrikeOut(true);
    m_taskDoneTextFormat.setForeground(Qt::gray);

    m_blockquoteMarkerFormat.setForeground(QColor(106, 153, 85));
    m_blockquoteMarkerFormat.setFontWeight(QFont::Bold);

    m_blockquoteTextFormat.setForeground(QColor(150, 150, 150));
    m_blockquoteTextFormat.setFontItalic(true);

    m_hrFormat.setForeground(QColor(120, 120, 120));
    m_hrFormat.setFontWeight(QFont::Bold);
    m_hrFormat.setFontFamilies({"Monospace"});

    m_setextUnderlineFormat.setForeground(QColor(120, 120, 120));
    m_setextUnderlineFormat.setFontFamilies({"Monospace"});

    m_tablePipeFormat.setForeground(QColor(120, 170, 220));
    m_tablePipeFormat.setFontWeight(QFont::Bold);

    m_tableHeaderFormat.setFontWeight(QFont::Bold);

    m_tableSeparatorFormat.setForeground(QColor(110, 110, 110));
    m_tableSeparatorFormat.setFontFamilies({"Monospace"});
}

int MarkdownHighlighter::detectHeaderLevel(const QString &text)
{
    // ~ DetectHeaderLevel : compte les '#' consécutifs en tout début de ligne, plafonné à 5
    int c = 0;
    while (c < text.length() && text.at(c) == QLatin1Char('#'))
        ++c;

    int level = 0; // 0 = pas un titre ATX
    if (c > 0 && c < text.length() && text.at(c) == QLatin1Char(' ')) {
        // exige un espace après les '#', comme un titre Markdown standard
        level = qMin(c, 5);
    }
    return level;
}

int MarkdownHighlighter::langIndexFromString(const QString &lang)
{
    const QString l = lang.toLower();
    int idx = 0; // 0 = langage inconnu / générique
    if (l == "python" || l == "py") {
        idx = 1;
    } else if (l == "pascal" || l == "pas" || l == "delphi" || l == "lazarus" ||
               l == "objectpascal" || l == "object pascal") {
        idx = 2;
    } else if (l == "javascript" || l == "js" || l == "typescript" || l == "ts") {
        idx = 3;
    } else if (l == "c" || l == "cpp" || l == "c++" || l == "csharp" || l == "cs") {
        idx = 4;
    } else if (l == "powershell") {
        idx = 5;
    } else if (l == "html" || l == "htm" || l == "xml") {
        idx = 6;
    } else if (l == "http" || l == "https") {
        idx = 7;
    } else if (l == "ruby" || l == "rb") {
        idx = 8;
    } else if (l == "bash" || l == "sh" || l == "shell" || l == "zsh") {
        idx = 9;
    } else if (l == "perl" || l == "pl") {
        idx = 10;
    } else if (l == "basic" || l == "vb" || l == "vbnet" || l == "freebasic" || l == "qbasic") {
        idx = 11;
    }
    return idx;
}

QString MarkdownHighlighter::langNameFromIndex(int idx)
{
    QString name; // chaîne vide si index hors table
    if (idx >= 0 && idx < kLangTable.size())
        name = kLangTable.at(idx);
    return name;
}

int MarkdownHighlighter::setextUnderlineLevel(const QString &trimmedText)
{
    // ~ ligne de soulignement d'un titre Setext : "===...=" (niveau 1) ou "---...-" (niveau 2)
    int level = 0; // 0 = ni l'un ni l'autre
    if (!trimmedText.isEmpty()) {
        bool allEq = true, allDash = true;
        for (const QChar c : trimmedText) {
            if (c != QLatin1Char('='))
                allEq = false;
            if (c != QLatin1Char('-'))
                allDash = false;
        }
        if (allEq)
            level = 1;
        else if (allDash)
            level = 2;
    }
    return level;
}

bool MarkdownHighlighter::isHorizontalRule(const QString &text)
{
    // ~ ---, ***, ___ (au moins 3 occurrences du même caractère, espaces tolérés entre elles)
    return kHorizontalRuleRe.match(text).hasMatch();
}

bool MarkdownHighlighter::isBlockQuotePrefix(const QString &text, int *depthOut, int *contentStartOut)
{
    // ~ >, >>, > > ... (citations imbriquées). Jusqu'à 3 espaces de marge tolérés
    // avant le premier '>', comme le reste de la syntaxe de bloc CommonMark.
    const auto m = kBlockQuotePrefixRe.match(text);
    if (!m.hasMatch())
        return false;
    if (contentStartOut) *contentStartOut = m.capturedLength(0);
    if (depthOut) *depthOut = m.captured(1).count(QLatin1Char('>'));
    return true;
}

bool MarkdownHighlighter::isListMarkerPrefix(const QString &text, int *contentStartOut, bool *isTaskOut,
                                             bool *taskCheckedOut, int *taskContentStartOut)
{
    // ~ listes à puces (-, *, +) et numérotées (1. ou 1)), imbrication gérée naturellement
    // par l'indentation existante (espaces de tête arbitraires, pas seulement 0-3).
    const auto m = kListMarkerPrefixRe.match(text);
    if (!m.hasMatch())
        return false;

    const int contentStart = m.capturedLength(0);
    if (contentStartOut) *contentStartOut = contentStart;

    // ~ case à cocher GFM juste après le marqueur : "[ ]" ou "[x]"/"[X]"
    bool isTask = false, taskChecked = false;
    int taskContentStart = contentStart;
    if (text.length() >= contentStart + 3 &&
        text.at(contentStart) == QLatin1Char('[') &&
        text.at(contentStart + 2) == QLatin1Char(']') &&
        (text.at(contentStart + 1) == QLatin1Char(' ') || text.at(contentStart + 1).toLower() == QLatin1Char('x'))) {
        isTask = true;
        taskChecked = (text.at(contentStart + 1).toLower() == QLatin1Char('x'));
        taskContentStart = contentStart + 3;
        if (text.length() > taskContentStart && text.at(taskContentStart) == QLatin1Char(' '))
            ++taskContentStart;
    }
    if (isTaskOut) *isTaskOut = isTask;
    if (taskCheckedOut) *taskCheckedOut = taskChecked;
    if (taskContentStartOut) *taskContentStartOut = taskContentStart;
    return true;
}

bool MarkdownHighlighter::isPlainParagraphCandidate(const QString &text)
{
    // ~ ligne "normale" (ni titre ATX, ni citation, ni liste, ni séparateur, ni
    // ligne de soulignement) pouvant servir de titre pour un Setext sur la ligne suivante.
    const QString t = text.trimmed();
    if (t.isEmpty())
        return false;
    if (detectHeaderLevel(text) > 0)
        return false;
    if (isFenceLineText(t))
        return false;
    if (isHorizontalRule(text))
        return false;
    if (setextUnderlineLevel(t) > 0)
        return false;
    int cs = 0;
    if (isBlockQuotePrefix(text, nullptr, &cs))
        return false;
    bool it = false, tc = false;
    int tcs = 0, ls = 0;
    if (isListMarkerPrefix(text, &ls, &it, &tc, &tcs))
        return false;
    return true;
}

bool MarkdownHighlighter::isTableRowLine(const QString &text)
{
    return !text.trimmed().isEmpty() && text.contains(QLatin1Char('|'));
}

bool MarkdownHighlighter::isTableSeparatorLine(const QString &text)
{
    // ~ ligne |---|:---:|---:| (structurelle, sépare l'en-tête du corps d'un tableau GFM)
    QString t = text.trimmed();
    if (t.isEmpty())
        return false;
    if (t.startsWith(QLatin1Char('|')))
        t.remove(0, 1);
    if (t.endsWith(QLatin1Char('|')))
        t.chop(1);
    if (t.isEmpty())
        return false;

    const QStringList cells = t.split(QLatin1Char('|'));
    for (const QString &cell : cells) {
        if (!kTableSeparatorCellRe.match(cell).hasMatch())
            return false;
    }
    return true;
}

void MarkdownHighlighter::highlightBlock(const QString &text)
{
    // ~ TMarkdownEdit.ReParse (étape 1 : détection des blocs ``` ) + PaintCodeLine
    const QString trimmed = text.trimmed();
    const bool isFenceLine = trimmed.startsWith(QStringLiteral("```"));
    const int prevState = previousBlockState();

    if (prevState >= StateCodeBlockBase) {
        const int langIdx = prevState - StateCodeBlockBase;
        if (isFenceLine) {
            // Ligne de balise FERMANTE : on cherche la balise ouvrante correspondante en
            // remontant jusqu'à la ligne ``` la plus proche au-dessus (modèle simple, non
            // imbriqué : c'est nécessairement elle, cf. commentaire de fenceRangeCursorInside).
            QTextBlock openBlock = currentBlock().previous();
            while (openBlock.isValid() && !isFenceLineText(openBlock.text().trimmed()))
                openBlock = openBlock.previous();
            const int rangeStart = openBlock.isValid() ? openBlock.position() : currentBlock().position();
            const int rangeEnd = currentBlock().position() + text.length();
            const bool cursorInside = (m_cursorPos >= rangeStart && m_cursorPos <= rangeEnd);

            setFormat(0, text.length(), cursorInside ? m_fenceFormat : m_concealedFormat);
            setCurrentBlockState(StateNormal);
        } else {
            setFormat(0, text.length(), m_codeBlockBgFormat); // police/couleur de base (le fond pleine largeur est géré par MarkdownEdit au niveau bloc)
            highlightCodeLine(text, langNameFromIndex(langIdx));
            setCurrentBlockState(prevState); // on reste dans le même bloc de code
        }
        return; // ~ "Continue" en Pascal : pas de style Markdown inline dans un bloc de code
    }

    if (isFenceLine) {
        // Ligne de balise OUVRANTE : on cherche la balise fermante en descendant jusqu'à
        // la prochaine ligne ``` (ou fin de document si le bloc n'est pas terminé).
        QTextBlock closeBlock = currentBlock().next();
        while (closeBlock.isValid() && !isFenceLineText(closeBlock.text().trimmed()))
            closeBlock = closeBlock.next();
        const int rangeStart = currentBlock().position();
        const int rangeEnd = closeBlock.isValid() ? closeBlock.position() + closeBlock.length()
                                                   : document()->characterCount();
        const bool cursorInside = (m_cursorPos >= rangeStart && m_cursorPos <= rangeEnd);

        const QString langStr = trimmed.mid(3).trimmed();
        setFormat(0, text.length(), cursorInside ? m_fenceFormat : m_concealedFormat);
        setCurrentBlockState(StateCodeBlockBase + langIndexFromString(langStr));
        return;
    }
    setCurrentBlockState(StateNormal);

    // ~ DetectHeaderLevel + PaintHeaderLine (mutuellement exclusif avec PaintNormalLine)
    const int headerLevel = detectHeaderLevel(text);
    if (headerLevel > 0) {
        // Mode "édition live" : le préfixe "#... " n'est visible que si le curseur
        // de texte se trouve sur cette ligne ; sinon seul le titre stylé est affiché.
        const bool cursorOnThisLine = currentBlock().contains(m_cursorPos);
        const int prefixLen = qMin(headerLevel + 1, text.length()); // "#"*niveau + l'espace

        if (cursorOnThisLine) {
            setFormat(0, text.length(), m_headerFormat[headerLevel]);
        } else {
            setFormat(0, prefixLen, m_concealedFormat);
            if (text.length() > prefixLen)
                setFormat(prefixLen, text.length() - prefixLen, m_headerFormat[headerLevel]);
        }
        return;
    }

    // ~ titre Setext : cette ligne est-elle le soulignement (=== ou ---) du paragraphe
    // précédent ? Prioritaire sur l'interprétation "règle horizontale" pour "---",
    // exactement comme CommonMark le prescrit.
    {
        const int lvl = setextUnderlineLevel(trimmed);
        if (lvl > 0) {
            const QTextBlock prev = currentBlock().previous();
            if (prev.isValid() && isPlainParagraphCandidate(prev.text())) {
                setFormat(0, text.length(), m_setextUnderlineFormat);
                return;
            }
            // Sinon (pas de paragraphe valide juste avant) : retombe sur le traitement
            // "règle horizontale" ci-dessous si la ligne y correspond aussi (cas de "---").
        }
    }

    if (isHorizontalRule(text)) {
        setFormat(0, text.length(), m_hrFormat);
        return;
    }

    // ~ titre Setext : cette ligne est-elle un titre suivi d'un soulignement valide ?
    if (isPlainParagraphCandidate(text)) {
        const QTextBlock next = currentBlock().next();
        if (next.isValid()) {
            const int lvl = setextUnderlineLevel(next.text().trimmed());
            if (lvl > 0) {
                setFormat(0, text.length(), m_headerFormat[lvl]);
                return;
            }
        }
    }

    // ~ tableaux GFM : détection + alignement visuel (police à chair fixe, '|' colorés,
    // en-tête en gras), SANS conversion en grille éditable -- un tableau reste du texte
    // Markdown normal dans le document, donc round-trip fichier garanti sans risque.
    {
        const QTextBlock prevBlock = currentBlock().previous();
        const QTextBlock nextBlock = currentBlock().next();
        const QString prevTrimmed = prevBlock.isValid() ? prevBlock.text().trimmed() : QString();
        const QString nextTrimmed = nextBlock.isValid() ? nextBlock.text().trimmed() : QString();

        if (isTableSeparatorLine(trimmed) && prevBlock.isValid() && isTableRowLine(prevBlock.text())) {
            // ligne |---|---| : sépare l'en-tête du corps du tableau
            setFormat(0, text.length(), m_tableSeparatorFormat);
            return;
        }
        if (isTableRowLine(text) && nextBlock.isValid() && isTableSeparatorLine(nextTrimmed)) {
            // ligne d'en-tête (immédiatement suivie d'une ligne séparatrice valide)
            setFormat(0, text.length(), m_tableHeaderFormat);
            highlightTablePipes(text);
            highlightInlineMarkers(text);
            return;
        }
        if (isTableRowLine(text) &&
            ((prevBlock.isValid() && isTableRowLine(prevBlock.text())) ||
             (prevBlock.isValid() && isTableSeparatorLine(prevTrimmed)))) {
            // ligne de données, à la suite de l'en-tête/séparateur ou d'une autre ligne de données
            highlightTablePipes(text);
            highlightInlineMarkers(text);
            return;
        }
    }

    // ~ citation (>, >>, > > ...) : le(s) marqueur(s) restent toujours visibles (stylés,
    // pas masqués/révélés) ; le contenu après le(s) marqueur(s) est italique/atténué.
    int baseOffset = 0;
    {
        int contentStart = 0;
        if (isBlockQuotePrefix(text, nullptr, &contentStart)) {
            setFormat(0, contentStart, m_blockquoteMarkerFormat);
            if (text.length() > contentStart)
                setFormat(contentStart, text.length() - contentStart, m_blockquoteTextFormat);
            baseOffset = contentStart;
        }
    }

    // ~ liste à puces/numérotée (imbrication gérée par l'indentation existante), avec
    // case à cocher GFM optionnelle "- [ ]"/"- [x]" juste après le marqueur.
    {
        const QString rest = text.mid(baseOffset);
        int contentStart = 0, taskContentStart = 0;
        bool isTask = false, taskChecked = false;
        if (isListMarkerPrefix(rest, &contentStart, &isTask, &taskChecked, &taskContentStart)) {
            setFormat(baseOffset, contentStart, m_listMarkerFormat);
            if (isTask) {
                setFormat(baseOffset + contentStart, 3, m_taskCheckboxFormat);
                const int doneStart = baseOffset + taskContentStart;
                if (taskChecked && text.length() > doneStart)
                    setFormat(doneStart, text.length() - doneStart, m_taskDoneTextFormat);
            }
        }
    }

    highlightInlineMarkers(text);
}

void MarkdownHighlighter::highlightCodeLine(const QString &text, const QString &lang)
{
    // ~ TMarkdownEdit.PaintCodeLine
    // HTML et HTTP n'ont pas de mots-clés "de langage de programmation" au sens
    // classique (cf. markdownkeywords.h) -- ils ont chacun leur propre grammaire
    // de surface (balises/attributs pour l'un, requête/statut/en-têtes pour
    // l'autre), traitée à part. Les autres langages (dont ruby/bash/sh/perl/basic,
    // ajoutés en même temps que html/http) suivent le schéma générique ci-dessous.
    const QString l = lang.toLower();
    if (l == "html") {
        highlightHtmlLine(text);
    } else if (l == "http") {
        highlightHttpLine(text);
    } else {
        const QString commentPfx = MarkdownKeywords::commentPrefix(lang);
        if (!commentPfx.isEmpty()) {
            const int idx = text.indexOf(commentPfx);
            if (idx >= 0) {
                setFormat(idx, text.length() - idx, m_commentLineFormat);
                // on continue quand même à colorer ce qui précède le commentaire ci-dessous
            }
        }

        auto it = kCodeStringRe.globalMatch(text);
        QList<QPair<int,int>> stringSpans;
        while (it.hasNext()) {
            const auto m = it.next();
            stringSpans.append({m.capturedStart(), m.capturedLength()});
            setFormat(m.capturedStart(), m.capturedLength(), m_stringFormat);
        }

        auto wIt = kCodeWordRe.globalMatch(text);
        while (wIt.hasNext()) {
            const auto m = wIt.next();
            bool insideString = false;
            for (const auto &span : std::as_const(stringSpans)) {
                if (m.capturedStart() >= span.first && m.capturedStart() < span.first + span.second) {
                    insideString = true;
                    break;
                }
            }
            if (insideString)
                continue;

            const QString word = m.captured();
            if (MarkdownKeywords::isKeyword(lang, word))
                setFormat(m.capturedStart(), m.capturedLength(), m_keywordFormat);
            else if (MarkdownKeywords::isCmdLet(lang, word))
                setFormat(m.capturedStart(), m.capturedLength(), m_cmdletFormat);
        }
    }
}

void MarkdownHighlighter::highlightHtmlLine(const QString &text)
{
    // ~ coloration HTML : commentaires <!-- -->, balises <tag>/</tag>, attributs
    // attr="valeur"/attr='valeur'. Réutilise les formats existants (mot-clé pour
    // les balises, "cmdlet" pour les noms d'attribut, chaîne pour leur valeur)
    // plutôt que d'ajouter de nouveaux QTextCharFormat dédiés.

    // Commentaires (traités ligne par ligne, comme le reste du bloc de code ;
    // un commentaire réparti sur plusieurs lignes sera donc coloré ligne à ligne).
    QList<QPair<int, int>> commentSpans;
    {
        auto it = kHtmlCommentRe.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            commentSpans.append({m.capturedStart(), m.capturedLength()});
            setFormat(m.capturedStart(), m.capturedLength(), m_commentLineFormat);
        }
    }
    auto insideComment = [&commentSpans](int pos) {
        for (const auto &span : std::as_const(commentSpans)) {
            if (pos >= span.first && pos < span.first + span.second)
                return true;
        }
        return false;
    };

    // Balises ouvrantes/fermantes : <tag ...> ou </tag>
    auto tIt = kHtmlTagRe.globalMatch(text);
    while (tIt.hasNext()) {
        const auto m = tIt.next();
        if (insideComment(m.capturedStart()))
            continue;
        setFormat(m.capturedStart(), m.capturedLength(), m_keywordFormat);
    }

    // Attributs : nom="valeur" / nom='valeur' (la valeur doit être entre guillemets,
    // comme en HTML valide ; un attribut booléen sans valeur, ex. "disabled", n'est
    // simplement pas coloré ici, ce qui reste sans conséquence visuelle notable).
    auto aIt = kHtmlAttrRe.globalMatch(text);
    while (aIt.hasNext()) {
        const auto m = aIt.next();
        if (insideComment(m.capturedStart()))
            continue;
        setFormat(m.capturedStart(1), m.capturedLength(1), m_cmdletFormat);
        setFormat(m.capturedStart(2), m.capturedLength(2), m_stringFormat);
    }
}

void MarkdownHighlighter::highlightHttpLine(const QString &text)
{
    // ~ coloration HTTP : ligne de requête ("GET /chemin HTTP/1.1"), ligne de
    // statut ("HTTP/1.1 200 OK"), ligne d'en-tête ("Nom-Entete: valeur"), ou à
    // défaut repli sur la coloration des chaînes (ex. corps JSON).
    //
    // Point de sortie unique : les 3 tentatives de correspondance sont calculées
    // à l'avance (coût négligeable sur une ligne de texte) puis départagées par
    // un if/else, plutôt que par des "return" anticipés dispersés dans la fonction.
    const auto mReq = kHttpRequestRe.match(text);
    const auto mStat = kHttpStatusRe.match(text);
    const auto mHdr = kHttpHeaderRe.match(text);

    if (mReq.hasMatch()) {
        setFormat(mReq.capturedStart(1), mReq.capturedLength(1), m_keywordFormat); // méthode
        setFormat(mReq.capturedStart(2), mReq.capturedLength(2), m_stringFormat);  // chemin
        setFormat(mReq.capturedStart(3), mReq.capturedLength(3), m_cmdletFormat);  // version HTTP
    } else if (mStat.hasMatch()) {
        setFormat(mStat.capturedStart(1), mStat.capturedLength(1), m_cmdletFormat); // version HTTP
        setFormat(mStat.capturedStart(2), mStat.capturedLength(2), m_keywordFormat); // code statut
        if (mStat.capturedLength(3) > 0)
            setFormat(mStat.capturedStart(3), mStat.capturedLength(3), m_stringFormat); // texte statut
    } else if (mHdr.hasMatch()) {
        setFormat(mHdr.capturedStart(1), mHdr.capturedLength(1), m_cmdletFormat); // nom d'en-tête
        if (mHdr.capturedLength(2) > 0)
            setFormat(mHdr.capturedStart(2), mHdr.capturedLength(2), m_stringFormat); // valeur
    } else {
        // Repli (ex. ligne d'un corps JSON) : colore juste les chaînes entre guillemets.
        auto it = kHttpBodyStringRe.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), m_stringFormat);
        }
    }
}

void MarkdownHighlighter::highlightTablePipes(const QString &text, int fromIndex)
{
    for (int i = fromIndex; i < text.length(); ++i) {
        if (text.at(i) == QLatin1Char('|'))
            setFormat(i, 1, m_tablePipeFormat);
    }
}

void MarkdownHighlighter::highlightInlineMarkers(const QString &text)
{
    const int blockPos = currentBlock().position();

    // ~ balise image ![[chemin]] (cf. MarkdownEdit::insertImageTagAndPicture) : le
    // texte réel de la balise est un marqueur comme un autre (masqué par défaut),
    // mais ici il n'y a pas de "contenu" distinct à garder visible séparément -- la
    // balise ENTIÈRE se masque ou se révèle en bloc. L'image qui suit (U+2028 puis
    // U+FFFC) n'est jamais touchée : elle reste toujours visible.
    {
        auto it = kImageTagRe.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            const int start = m.capturedStart();
            const int len = m.capturedLength();               // texte balise + séparateur + image
            const int tagLen = len - 2;                        // longueur du seul texte "![[chemin]]"
            const int absStart = blockPos + start;
            const int absEnd = blockPos + start + len;         // englobe aussi l'image : cliquer sur l'image révèle la balise

            const bool revealed = (m_cursorPos >= absStart && m_cursorPos <= absEnd);
            setFormat(start, tagLen, revealed ? m_fenceFormat : m_concealedFormat);
            // Les 2 derniers caractères (séparateur doux + image) ne sont jamais
            // reformatés ici : le séparateur ne rend rien de visible par lui-même,
            // et l'image doit toujours s'afficher normalement, masquée ou non.
        }
    }

    // ~ TMarkdownEdit.PaintNormalLine, en mode "édition live" : un marqueur (**, __,
    // ~~, ==, `, $, %%) n'est affiché que si le curseur de texte se trouve entre les
    // deux délimiteurs (bornes incluses) ; sinon seul le contenu stylé est visible.
    struct Rule { const QRegularExpression &re; const QTextCharFormat *format; int markerLen; };

    const Rule rules[] = {
        { kHighlightMarkerRe, &m_highlightFormat, 2 },
        { kBoldMarkerRe,      &m_boldFormat,      2 },
        { kStrikeMarkerRe,    &m_strikeFormat,    2 },
        { kCommentMarkerRe,   &m_commentFormat,   2 },
        { kUnderlineMarkerRe, &m_underlineFormat, 2 },
        { kCodeSpanMarkerRe,  &m_codeSpanFormat,  1 },
        { kMathMarkerRe,      &m_mathFormat,      1 },
        { kItalicMarkerRe,    &m_italicFormat,    1 },
    };

    for (const Rule &rule : rules) {
        auto it = rule.re.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            const int start = m.capturedStart();
            const int len = m.capturedLength();
            const int absStart = blockPos + start;
            const int absEnd = blockPos + start + len; // borne incluse : cliquer juste après le marqueur fermant révèle aussi

            const bool revealed = (m_cursorPos >= absStart && m_cursorPos <= absEnd);
            const int ml = rule.markerLen;

            if (revealed || len <= 2 * ml) {
                // curseur dedans (ou entre les deux) -> tout est visible ; ou texte
                // trop court pour distinguer marqueurs/contenu -> on montre tout par sécurité.
                setFormat(start, len, *rule.format);
            } else {
                setFormat(start, ml, m_concealedFormat);                        // marqueur ouvrant
                setFormat(start + ml, len - 2 * ml, *rule.format);              // contenu stylé
                setFormat(start + len - ml, ml, m_concealedFormat);             // marqueur fermant
            }
        }
    }
}
