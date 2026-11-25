#ifndef MARKDOWNEDIT_H
#define MARKDOWNEDIT_H

#include <QTextEdit>
#include <QString>
#include <QList>
#include <QTextCursor>
#include <Qt>

class QAction;
class MarkdownHighlighter;
class QTextTable;
class QTextBlock;

// Un titre du document (ATX "# ..." ou Setext "texte / ===") tel qu'affiché dans
// l'explorateur de titres (onglet "Outline").
struct MarkdownHeading
{
    int level = 1;      // 1..5 (même plafond que MarkdownHighlighter)
    QString text;       // texte du titre, sans les marqueurs "#" ni l'emphase (**, __, `...`)
    int position = 0;   // position absolue du début du bloc dans le QTextDocument
};

/**
 * MarkdownEdit
 *
 * Portage de TMarkdownEdit (markdownedit.pas).
 *
 * Choix retenu (validé avec l'utilisateur) : "fonctionnel équivalent" plutôt que
 * réécriture pixel-perfect du moteur de peinture maison Lazarus. On s'appuie donc
 * sur QTextEdit (rich text natif Qt) + MarkdownHighlighter (QSyntaxHighlighter,
 * cf. markdownhighlighter.h) pour la coloration syntaxique.
 *
 * Mode "édition live" : les marqueurs Markdown (**, __, #, ```, etc.) ne sont
 * visibles que lorsque le curseur de texte est dessus/dedans (cf.
 * MarkdownHighlighter) ; cette classe relaie la position du curseur au
 * highlighter à chaque déplacement (onCursorPositionChanged) et déclenche un
 * ré-highlight ciblé (seulement les blocs concernés, pas tout le document).
 *
 * Images ![[chemin]] (~ DetectImageOnLine/ResolveImagePath/PasteImageFromClipboard) :
 * à l'ouverture d'un fichier .md, chaque balise ![[chemin]] est transformée en
 * un triplet permanent dans le document : le TEXTE RÉEL "![[chemin]]" (jamais
 * supprimé), un saut de ligne "doux" (QChar::LineSeparator -- invisible à la
 * sauvegarde), puis l'IMAGE réellement intégrée. Le texte de la balise suit la
 * même règle d'édition live que les autres marqueurs (masqué par défaut,
 * révélé -- affiché AU-DESSUS de l'image qui reste TOUJOURS visible en dessous --
 * quand le curseur est dessus). Coller une image (Ctrl+V) suit le même principe.
 *
 * Blocs de code fencés ```lang ... ``` : fond de bloc PLEINE LARGEUR
 * (QTextBlockFormat, cf. updateBlockDecorations) et balises ``` en édition live.
 *
 * Tableaux GFM (| a | b |) : convertis en VRAIS QTextTable éditables à l'ouverture
 * du fichier (et à l'ajout depuis la bibliothèque), avec un fonctionnement calqué
 * sur Obsidian :
 *  - Tab / Maj+Tab navigue de cellule en cellule ; Tab dans la dernière cellule
 *    de la dernière ligne ajoute une nouvelle ligne (comme Obsidian).
 *  - Clic droit dans une cellule : menu "Table" (insérer/dupliquer/supprimer une
 *    ligne ou une colonne), en plus du menu standard Copier/Coller.
 *  - L'alignement de colonne (:---, :---:, ---:) est conservé (appliqué comme
 *    alignement de paragraphe sur chaque cellule) et resérialisé à la sauvegarde.
 *  - Sérialisation (documentToMarkdownText) : un QTextTable est reconstruit en
 *    syntaxe GFM avec colonnes alignées visuellement (comme le ferait Obsidian
 *    lui-même en sauvegardant), pas de conversion perdue au format texte.
 */
class MarkdownEdit : public QTextEdit
{
    Q_OBJECT

public:
    explicit MarkdownEdit(QWidget *parent = nullptr);

    // --- API publique équivalente à TMarkdownEdit ---
    void loadFromMdFile(const QString &fileName);   // ~ LoadFromMdFile
    void saveToMdFile(const QString &fileName);      // ~ SaveToMdFile
    void appendFromMdFile(const QString &fileName);  // ~ AppendFromMdFile
    void appendMarkdownText(const QString &content);  // cœur de AppendFromMdFile, depuis une chaîne déjà en mémoire

    bool isNoteModified() const { return noteModified; } // ~ NoteModified

    // ~ nouveau (v1.4.0) : insère une image (ex. capture d'une sélection de terminal) sur une
    // NOUVELLE ligne située juste après la ligne du curseur (après le tableau si le curseur est
    // dans un tableau). L'image est enregistrée en PNG dans le dossier de la note (nom
    // horodaté, rendu unique si besoin) et insérée comme pour un collage Ctrl+V. Le curseur
    // reste à la fin de l'image, donc des insertions successives s'empilent les unes sous les
    // autres. Renvoie false si l'image est nulle ou ne peut pas être enregistrée.
    bool insertImageOnNextLine(const QImage &image);

    // ~ nouveau (v1.5.0) : insère un texte (ex. sélection d'un terminal) sur de NOUVELLES lignes
    // situées juste après la ligne du curseur (après le tableau si le curseur est dans un
    // tableau), ENCADRÉ par deux lignes ``` pour qu'il soit pris comme un extrait de code.
    // Une ligne du texte = une ligne de la note, sans interprétation Markdown ; les lignes
    // vides en début/fin de texte sont retirées. Le curseur reste sur la ``` de fermeture :
    // des insertions successives s'empilent. Renvoie false si le texte est vide ou blanc.
    bool insertTextOnNextLine(const QString &text);

    // ~ nouveau (v1.7.0) : texte Markdown de la SÉLECTION courante (ex. dans l'aperçu de la
    // bibliothèque). Les lignes sont reprises telles quelles (les tableaux touchés par la
    // sélection sont resérialisés en GFM, les balises d'image ![[x.png]] sont conservées). Pour que
    // les images suivent le texte dans une autre note, voir copyReferencedImages(). Vide s'il n'y a
    // pas de sélection.
    QString selectedMarkdownText() const;

    // Dossier de référence des images de cet éditeur (dossier de la note chargée ; à défaut,
    // dossier courant), c'est-à-dire là où les balises ![[x.png]] relatives sont résolues.
    QString imageBaseFolder() const;
    // Met à jour ce dossier sans recharger la note (ex. note ou dossier renommé pendant qu'elle est ouverte).
    void setImageBaseFolder(const QString &folder) { basePath = folder; }

    // ~ nouveau (v1.7.0) : pour chaque balise ![[x.png]] de `markdown` (relative à sourceFolder,
    // absolue ou ~/), COPIE l'image dans destFolder (le dossier de la note de destination) pour que
    // la note reste autonome quand on copie son dossier. La balise garde son nom relatif
    // (sous-dossier conservé s'il y en avait un) ; si un fichier de même nom mais de contenu
    // différent existe déjà, l'image est copiée sous un autre nom (x_1.png...) et la balise
    // est ajustée ; un fichier identique déjà présent est réutilisé. Une balise dont l'image est
    // introuvable est laissée telle quelle. Renvoie le Markdown avec les balises éventuellement ajustées.
    static QString copyReferencedImages(const QString &markdown, const QString &sourceFolder,
                                        const QString &destFolder);

    // ~ nouveau (v1.7.0) : insère du texte Markdown (titres, tableaux, images, code...) sur de
    // NOUVELLES lignes situées juste après la ligne du curseur (après le tableau si le curseur
    // est dans un tableau), avec le même traitement qu'un chargement de fichier (vrais
    // tableaux, images). Renvoie false si le texte est vide ou blanc.
    bool insertMarkdownOnNextLine(const QString &markdown);

    QString getCurrentLineTextContent() const; // ~ GetCurrentLineTextContent (vraie ligne/paragraphe où se trouve le curseur, même si l'affichage la replie sur plusieurs lignes)

    // ~ nouveau : explorateur de titres (onglet "Outline").
    // headings() parcourt le document courant et renvoie les titres dans l'ordre ; les
    // lignes situées dans un bloc de code ``` ou dans un tableau sont ignorées.
    QList<MarkdownHeading> headings() const;
    // Place le curseur sur le titre n°index (index dans headings()) et le fait remonter en
    // haut de la zone visible -- sauf si cela laisserait du vide sous la fin du document,
    // auquel cas on s'arrête au défilement maximal (la fin du document reste alors tout en
    // bas de la vue). Renvoie false si l'index est invalide.
    bool revealHeading(int index);

    // ~ nouveau : actions supplémentaires (ex. "Exécuter") ajoutées au menu contextuel
    // standard construit par contextMenuEvent(), en plus du sous-menu "Table". Permet à
    // MainWindow de garder son action "Exécuter" sans avoir à ré-implémenter tout le
    // menu contextuel (qui gère déjà le cas tableau) via Qt::CustomContextMenu.
    void setExtraContextMenuActions(const QList<QAction *> &actions);

    // ~ nouveau (v2.6.0) : résultat d'une commande exécutée depuis la note (menu Exécuter).
    //  - markCommandLine()       : repère (QTextCursor, qui suit les modifications du document) la
    //                              ligne courante, à garder pendant l'exécution.
    //  - commandOccurrence()     : rang de cette ligne parmi les lignes identiques du document
    //                              (0 = la première), pour la retrouver dans le FICHIER si la note
    //                              n'est plus affichée à la fin de la commande.
    //  - loadSerial()            : change à chaque loadFromMdFile() ; si elle a changé, le repère
    //                              n'est plus valable (autre fiche chargée entre-temps).
    //  - insertCommandOutputAfter: insère `output` sur de nouvelles lignes juste après la ligne
    //                              repérée (entouré de ``` sauf si la commande est déjà dans une
    //                              zone ```, auquel cas les lignes sont ajoutées telles quelles).
    //                              Le curseur et le défilement de l'utilisateur ne bougent pas.
    //  - insertCommandOutputInFile: même insertion directement dans un fichier .md sur disque.
    QTextCursor markCommandLine() const;
    int commandOccurrence() const;
    quint64 loadSerial() const { return m_loadSerial; }
    bool insertCommandOutputAfter(const QTextCursor &mark, const QString &command, const QString &output);
    static bool insertCommandOutputInFile(const QString &fileName, const QString &command,
                                          int occurrence, const QString &output);

    // ~ nouveau (v2.5.0) : active Ctrl+Alt+C (sélection -> copyToNoteRequested). Auparavant
    // réservé aux éditeurs en lecture seule ; l'aperçu de la bibliothèque étant désormais
    // éditable, le raccourci est activé explicitement par MainWindow (désactivé par défaut).
    void setCopyToNoteShortcutEnabled(bool on) { m_copyToNoteShortcut = on; }

signals:
    // ~ nouveau (v2.5.0) : l'éditeur perd le focus (hors ouverture d'un menu contextuel).
    // Utilisé pour enregistrer automatiquement une fiche de la bibliothèque modifiée.
    void focusLeft();

    // Émis quand on appuie sur Ctrl+Alt+C dans un éditeur en LECTURE SEULE (l'aperçu de la
    // bibliothèque) avec une sélection : MainWindow l'insère dans la note en cours d'édition.
    void copyToNoteRequested(const QString &markdown);

protected:
    void insertFromMimeData(const QMimeData *source) override; // ~ PasteFromClipboard/PasteImageFromClipboard
    void keyPressEvent(QKeyEvent *event) override;              // Tab/Maj+Tab dans les tableaux (~ Obsidian)
    void contextMenuEvent(QContextMenuEvent *event) override;    // menu "Table", "Format", bloc de code...
    void paintEvent(QPaintEvent *event) override;                // nom du langage ```bash dessiné à droite (balises masquées)
    void focusOutEvent(QFocusEvent *event) override;             // signal focusLeft()

private slots:
    void onCursorPositionChanged();
    void onTextChangedUpdateCodeBackgrounds();

private:
    QString basePath;      // ~ FBasePath (dossier de référence pour les images relatives)
    bool noteModified = false; // ~ FNoteModified
    MarkdownHighlighter *highlighter = nullptr;

    int m_lastCursorPos = 0; // dernière position connue, pour ré-highlighter le bloc quitté
    bool m_updatingBlockBackgrounds = false; // garde de ré-entrance pour updateBlockDecorations

    QList<QAction *> m_extraContextMenuActions; // ex. "Exécuter" (cf. setExtraContextMenuActions)
    quint64 m_loadSerial = 0;                   // cf. loadSerial()
    bool m_copyToNoteShortcut = false;          // cf. setCopyToNoteShortcutEnabled

    // --- Insertions depuis le menu contextuel (v2.5.0) ---
    bool askTableSize(int *rows, int *cols);     // petite boîte de dialogue (colonnes / lignes)
    void insertEmptyTable(int bodyRows, int cols); // nouveau tableau sur une nouvelle ligne après le curseur
    void insertCodeBlockOnNextLine();             // zone ``` vide sur une nouvelle ligne, curseur à l'intérieur

    // --- Formats de texte sur une sélection (menu "Format") : tous en Markdown ---
    // Les positions sont absolues dans le QTextDocument.
    void applyInlineFormat(const QString &marker, int selStart, int selEnd); // bascule **, *, __, ~~, ==, `, $
    enum BlockFormatKind { BlockParagraph = 0, BlockH1 = 1, BlockH2, BlockH3, BlockH4, BlockH5,
                           BlockQuote = 10, BlockBullet, BlockNumbered, BlockTask };
    void applyBlockFormat(BlockFormatKind kind, int selStart, int selEnd);   // titre, citation, listes
    void clearTextFormatting(int selStart, int selEnd);                      // retire marqueurs + préfixe de bloc

    // ~ nouveau : conversion "live" d'un tableau GFM tapé à la main (pas chargé depuis
    // un fichier) en vrai QTextTable, dès que la ligne séparatrice |---|---| devient
    // valide -- même principe qu'Obsidian. Sans ça, un tableau tapé à la main restait
    // du texte brut coloré, sans grille ni menu clic-droit "Table" (celui-ci ne
    // s'active que sur un QTextCursor::currentTable() valide).
    bool m_convertingTypedTable = false;
    void maybeConvertTypedTable();

    QString resolveImagePath(const QString &path) const; // ~ ResolveImagePath
    QString generateImageFileName() const;                // ~ GenerateImageFileName
    void openNewLineAfterCursor(QTextCursor &cursor);     // crée une ligne vide neutre après la ligne (ou le tableau) du curseur, y place `cursor`

    void insertMarkdownContent(QTextCursor &cursor, const QStringList &lines); // point d'entrée : détecte les tableaux et délègue le reste
    void insertMarkdownText(QTextCursor &cursor, const QString &text); // insère texte+séparateur+image pour chaque ![[..]] (une seule ligne, hors tableau)
    void insertImageTagAndPicture(QTextCursor &cursor, const QString &path, const QImage &image); // le triplet texte/séparateur/image
    void insertMarkdownTable(QTextCursor &cursor, const QStringList &tableLines); // construit un vrai QTextTable depuis un bloc GFM

    QString documentToMarkdownText() const; // sérialise le QTextDocument -> texte .md
    QString serializeTable(QTextTable *table) const; // reconstruit la syntaxe GFM (colonnes alignées) d'un QTextTable

    void resetCursorTrackingAfterLoad(); // repositionne le curseur au début + rehighlight complet après chargement
    void updateBlockDecorations(); // fond pleine largeur (code) + marge (citations)

    // --- Édition de tableau façon Obsidian (menu contextuel clic droit) ---
    void insertTableRow(QTextTable *table, const QTextCursor &at, bool below);
    void deleteTableRow(QTextTable *table, const QTextCursor &at);
    void duplicateTableRow(QTextTable *table, const QTextCursor &at);
    void insertTableColumn(QTextTable *table, const QTextCursor &at, bool right);
    void deleteTableColumn(QTextTable *table, const QTextCursor &at);
    void duplicateTableColumn(QTextTable *table, const QTextCursor &at);
};

#endif // MARKDOWNEDIT_H
