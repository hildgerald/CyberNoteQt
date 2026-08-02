#ifndef MARKDOWNEDIT_H
#define MARKDOWNEDIT_H

#include <QTextEdit>
#include <QString>
#include <QList>
#include <Qt>

class QAction;
class MarkdownHighlighter;
class QTextTable;

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

    QString getCurrentLineTextContent() const; // ~ GetCurrentLineTextContent (ligne où se trouve le curseur)

    // ~ nouveau : actions supplémentaires (ex. "Exécuter") ajoutées au menu contextuel
    // standard construit par contextMenuEvent(), en plus du sous-menu "Table". Permet à
    // MainWindow de garder son action "Exécuter" sans avoir à ré-implémenter tout le
    // menu contextuel (qui gère déjà le cas tableau) via Qt::CustomContextMenu.
    void setExtraContextMenuActions(const QList<QAction *> &actions);

protected:
    void insertFromMimeData(const QMimeData *source) override; // ~ PasteFromClipboard/PasteImageFromClipboard
    void keyPressEvent(QKeyEvent *event) override;              // Tab/Maj+Tab dans les tableaux (~ Obsidian)
    void contextMenuEvent(QContextMenuEvent *event) override;    // menu "Table" quand le curseur est dans un tableau

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

    // ~ nouveau : conversion "live" d'un tableau GFM tapé à la main (pas chargé depuis
    // un fichier) en vrai QTextTable, dès que la ligne séparatrice |---|---| devient
    // valide -- même principe qu'Obsidian. Sans ça, un tableau tapé à la main restait
    // du texte brut coloré, sans grille ni menu clic-droit "Table" (celui-ci ne
    // s'active que sur un QTextCursor::currentTable() valide).
    bool m_convertingTypedTable = false;
    void maybeConvertTypedTable();

    QString resolveImagePath(const QString &path) const; // ~ ResolveImagePath
    QString generateImageFileName() const;                // ~ GenerateImageFileName

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
