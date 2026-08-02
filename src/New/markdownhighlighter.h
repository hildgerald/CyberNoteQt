#ifndef MARKDOWNHIGHLIGHTER_H
#define MARKDOWNHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>

/**
 * MarkdownHighlighter
 *
 * Équivalent "fonctionnel" (pas pixel-perfect) du moteur de rendu maison de
 * TMarkdownEdit.Paint / PaintNormalLine / PaintHeaderLine / PaintCodeLine
 * (markdownedit.pas). Gère :
 *  - les titres ATX # à ##### et les titres Setext (Titre\n=== ou \n---)
 *  - **gras**, *italique*, __souligné__, ~~barré~~, ==surlignage==, `code en ligne`, $math$
 *  - les blocs de code fencés ```lang ... ``` avec coloration des mots-clés
 *    (cf. markdownkeywords.h, portage de IsKeyword/IsCmdLets/CommentPrefix) ;
 *    langages supportés : python, pascal, javascript/typescript, c/c++/c#,
 *    powershell, ruby, bash/sh, perl, basic (schéma générique mot-clé/chaîne/
 *    commentaire), plus html et http qui ont leur propre schéma dédié (cf.
 *    highlightHtmlLine/highlightHttpLine dans le .cpp : balises/attributs pour
 *    l'un, ligne de requête/statut + en-têtes pour l'autre)
 *  - les structures de bloc CommonMark/GFM : listes à puces/numérotées imbriquées,
 *    cases à cocher GFM (- [ ]/- [x]), citations imbriquées (>, >>...), règles
 *    horizontales (---, ***, ___). Les tableaux GFM sont désormais convertis en
 *    vrais QTextTable éditables par MarkdownEdit (cf. son .cpp) ; ce highlighter
 *    ne garde qu'un repli de style texte brut pour un tableau pas encore
 *    converti (juste tapé, avant rechargement).
 *
 * Mode "édition live" : les marqueurs d'emphase (**, __, #, etc.) ne sont visibles
 * que lorsque le curseur de texte se trouve entre eux (ou sur la ligne, pour un
 * titre) ; sinon ils sont masqués (taille quasi nulle + couleur transparente,
 * le caractère reste présent dans le document, seul son rendu est réduit :
 * QSyntaxHighlighter ne peut pas faire disparaître du texte du flux de mise
 * en page, seulement changer son apparence). MarkdownEdit appelle
 * setCursorPosition() puis rehighlightBlock() à chaque déplacement du curseur.
 * Les marqueurs de structure de bloc (>, -, 1., |, ---) restent en revanche
 * TOUJOURS visibles (stylés, pas masqués) : ce sont des repères structurels,
 * pas des décorations à cacher.
 */
class MarkdownHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit MarkdownHighlighter(QTextDocument *parent = nullptr);

    // Position absolue (dans le document) du curseur de texte ; détermine quels
    // marqueurs sont "révélés" lors du prochain (re)highlight.
    void setCursorPosition(int absolutePosition) { m_cursorPos = absolutePosition; }

    // Expose le seuil d'état "dans un bloc de code" (utilisé par MarkdownEdit pour
    // savoir si un rehighlight plus large est nécessaire au déplacement du curseur).
    static bool isCodeBlockState(int blockUserState) { return blockUserState >= StateCodeBlockBase; }
    static bool isFenceLineText(const QString &trimmedText) { return trimmedText.startsWith(QStringLiteral("```")); }

    // Exposés pour MarkdownEdit (indentation visuelle des citations, cf. updateBlockDecorations,
    // et détection des blocs de tableau GFM à convertir en vrai QTextTable au chargement).
    static bool isBlockQuotePrefix(const QString &text, int *depthOut, int *contentStartOut);
    static bool isHorizontalRule(const QString &text);
    static bool isTableRowLine(const QString &text);
    static bool isTableSeparatorLine(const QString &text);

protected:
    void highlightBlock(const QString &text) override;

private:
    // ~ StateNormal / StateCodeBlockBase+langIndex, encodés dans setCurrentBlockState()
    enum { StateNormal = -1, StateCodeBlockBase = 100 };

    QTextCharFormat m_headerFormat[6]; // index 1..5 utilisés
    QTextCharFormat m_boldFormat;
    QTextCharFormat m_italicFormat;
    QTextCharFormat m_underlineFormat;
    QTextCharFormat m_strikeFormat;
    QTextCharFormat m_highlightFormat;
    QTextCharFormat m_codeSpanFormat;
    QTextCharFormat m_mathFormat;
    QTextCharFormat m_commentFormat;
    QTextCharFormat m_codeBlockBgFormat;
    QTextCharFormat m_fenceFormat;
    QTextCharFormat m_keywordFormat;
    QTextCharFormat m_cmdletFormat;
    QTextCharFormat m_stringFormat;
    QTextCharFormat m_commentLineFormat;
    QTextCharFormat m_concealedFormat; // marqueur masqué (taille ~0 + transparent)

    QTextCharFormat m_listMarkerFormat;      // -, *, +, 1. ...
    QTextCharFormat m_taskCheckboxFormat;    // [ ] / [x]
    QTextCharFormat m_taskDoneTextFormat;    // texte d'une tâche cochée (barré, atténué)
    QTextCharFormat m_blockquoteMarkerFormat;// > (visible, toujours)
    QTextCharFormat m_blockquoteTextFormat;  // contenu d'une citation (italique, atténué)
    QTextCharFormat m_hrFormat;              // ---, ***, ___
    QTextCharFormat m_setextUnderlineFormat; // ligne de soulignement === / --- d'un titre Setext
    QTextCharFormat m_tablePipeFormat;       // caractères '|' (repli texte brut, cf. .cpp)
    QTextCharFormat m_tableHeaderFormat;     // ligne d'en-tête d'un tableau GFM (repli texte brut)
    QTextCharFormat m_tableSeparatorFormat;  // ligne |---|---| (repli texte brut)

    int m_cursorPos = -1; // position absolue courante du curseur (-1 = inconnue -> tout masqué)

    static int detectHeaderLevel(const QString &text);       // ~ DetectHeaderLevel
    static int langIndexFromString(const QString &lang);     // encode le langage dans le blockState
    static QString langNameFromIndex(int idx);

    // --- Structures de bloc (nouveau) ---
    static int setextUnderlineLevel(const QString &trimmedText); // 1 (===), 2 (---), 0 (ni l'un ni l'autre)
    static bool isPlainParagraphCandidate(const QString &text);  // ligne "normale" pouvant servir de titre Setext
    static bool isListMarkerPrefix(const QString &text, int *contentStartOut, bool *isTaskOut,
                                    bool *taskCheckedOut, int *taskContentStartOut);

    void highlightInlineMarkers(const QString &text);
    void highlightCodeLine(const QString &text, const QString &lang); // ~ PaintCodeLine
    // ~ nouveau : HTML et HTTP n'ont pas de liste de mots-clés classique (cf.
    // markdownkeywords.h) -- HTML est structuré en balises/attributs, HTTP en
    // ligne de requête/statut + en-têtes "Nom: valeur". Schéma dédié pour chacun,
    // appelé depuis highlightCodeLine() selon le langage du bloc ```.
    void highlightHtmlLine(const QString &text);
    void highlightHttpLine(const QString &text);
    void highlightTablePipes(const QString &text, int fromIndex = 0);
};

#endif // MARKDOWNHIGHLIGHTER_H
