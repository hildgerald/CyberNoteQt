#pragma once

#include <QMainWindow>
#include <QTreeWidgetItem>
#include <QMenu>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QSet>
#include <QImage>
#include <QMap>
#include <QHash>
#include <QPointer>
#include <QTextCursor>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class LinuxTermDialog;
class NetcatTermDialog;
class HelpDialog;
class HttpFileServer;

// ~ Node.ImageIndex en Lazarus : type d'entrée d'arborescence, stocké dans
// Qt::UserRole+1 (le chemin complet est stocké dans Qt::UserRole, ~ Node.Data).
enum TreeItemType {
    ItemFolder      = 0, // dossier
    ItemImage       = 1, // .png
    ItemNote        = 2, // .md / .txt (arborescence Projet)
    ItemLibraryNote = 3,  // .md (arborescence Bibliothèques, "exécutable"/insérable)
    ItemToolFile    = 4   // v2.4.0 : fichier quelconque de l'onglet "tools"
};
enum { RolePath = Qt::UserRole, RoleType = Qt::UserRole + 1,
       RoleBaseText = Qt::UserRole + 2 }; // v2.3.0 : nom du dossier sans le suffixe ":port" (présent seulement si un serveur HTTP est actif)

/**
 * MainWindow
 *
 * Portage de TFMain (umain.pas). Squelette de la phase 1 :
 * - la disposition des widgets correspond au formulaire umain.frm (mainwindow.ui)
 * - toutes les méthodes existent avec la même responsabilité que l'unité Pascal
 *   d'origine, mais leur CORPS N'EST PAS ENCORE IMPLÉMENTÉ (TODO) : ce sera
 *   fait module par module dans les prochaines étapes.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override; // ~ FormClose

private slots:
    // --- Menu "File" ---
    void onNewProject();          // ~ Menu_NewProjectClick
    void onOpenProject();         // ~ Menu_OpenProjectClick
    void onExit();                // ~ Menu_ExitClick

    // --- Menu "Tools" ---
    void onNewLinuxTerm();        // ~ Start_NewLinuxTermClick
    void onNewNetcatTerm();       // ~ Start_NewNetcatTermClick
    void onTerminalImageForNote(const QImage &image); // Ctrl+Alt+S dans un terminal : insère l'image dans la note ouverte
    void onTerminalTextForNote(const QString &text);  // Ctrl+Alt+C dans un terminal : insère le texte sélectionné dans la note ouverte

    // --- Menu "Help" ---
    void onHelpContents();        // affiche HelpDialog
    void onAbout();               // ~ boîte "About"

    // --- Toolbar / actions projet ---
    void onAddMachine();          // ~ TB_AddMachineClick
    void onAddFolder();           // ~ TB_AddFolderClick
    void onNewNote();             // ~ TB_NewNoteClick
    void onDelete();              // ~ TB_DeleteClick
    void onMakeCopy();            // ~ TB_MakeCopyClick
    void onMoveFileInto();        // ~ TB_MoveFileIntoClick
    void onMergeEntireFileWith(); // ~ TB_MergeEntireFileWithClick
    void onRename();              // ~ TB_RenameClick
    void onTreeCopy();            // ~ nouveau : copier un fichier ou dossier de l'arborescence Project
    void onTreePaste();           // ~ nouveau : coller (copie récursive pour un dossier)

    // --- Toolbar / actions note ---
    void onNoteCopy();            // ~ MenuPop_copyClick / ToolNote_Copy
    void onNoteCut();             // ~ MenuPop_cutClick / ToolNote_Cut
    void onNotePaste();           // ~ MenuPop_PasteClick / ToolNote_Paste
    void onNoteExecute();         // ~ MenuPop_ExecuteClick / ToolNote_Execute

    // --- Arborescence projet ---
    void onTreeProjectItemClicked(QTreeWidgetItem *item, int column);      // ~ TreeView1Click
    void onTreeProjectItemChanged(QTreeWidgetItem *item, int column);      // ~ TreeView1Edited
    void onTreeProjectCurrentItemChanged(QTreeWidgetItem *current,
                                          QTreeWidgetItem *previous);      // ~ TreeView1Editing (capture du nom avant édition)

    // --- Bibliothèque ---
    void onTreeLibraryItemClicked(QTreeWidgetItem *item, int column);      // aperçu Markdown au simple clic
    void onTreeLibraryDoubleClicked(QTreeWidgetItem *item, int column);    // ~ TreeView2DblClick
    void onExportReportDocx();                                             // v2.2.0 : File > Export to a Word report... (fenêtre arbre | Markdown | Word)
    void onInsertFolderNotes();                                            // v2.0.0 : insère les notes d'un dossier (choisies dans une liste) dans la note ouverte
    void onHttpServerConnect();                                            // v2.3.0 : menu contextuel d'un dossier > http server connect
    void onHttpServerDisconnect();                                         // v2.3.0 : menu contextuel d'un dossier > http server disconnect
    void onToolsConnectClicked();                                          // v2.4.0 : bouton connect / unconnect de l'onglet tools
    void onToggleReportExclusion();                                        // v2.0.0 : ajoute/retire le préfixe _ (note ou dossier exclu des rapports)
    void onLibraryCopyToNote();                                            // Ctrl+Alt+C sur un fichier de la bibliothèque : contenu ajouté à la fin de la note ouverte
    void onTerminalCommandFinished(int jobId, int exitCode, const QString &output); // v2.6.0 : résultat d'une commande lancée par "Exécuter"
    bool savePreviewIfModified();                                          // v2.5.0 : enregistre la fiche de l'aperçu si elle a été modifiée
    void onLibraryPreviewCopyToNote(const QString &markdown);              // Ctrl+Alt+C dans l'aperçu : sélection insérée à la ligne suivant le curseur de la note

    // --- Grille "Target" ---
    void onTargetCellChanged(int row, int column);      // ~ SG_TargetEditingDone
    void onTargetCurrentCellChanged(int currentRow, int currentColumn,
                                     int previousRow, int previousColumn); // ~ SG_TargetSelectCell

    // --- Timer de sauvegarde automatique ---
    void onUpdateTimerTick();     // ~ Timer_UpdateTimer

    // --- Explorateur de titres (onglet "Outline") ---
    void refreshOutline();                                        // reconstruit l'arbre si les titres ont changé
    void onOutlineItemClicked(QTreeWidgetItem *item, int column); // amène le titre en haut de l'éditeur

private:
    Ui::MainWindow *ui;

    // --- État (miroir des champs "private" de TFMain) ---
    QString pwdApplication;      // ~ PWD_Application
    QString currentProject;      // ~ Current_project
    QStringList softConfiguration; // ~ FSoftConfiguration
    QString currentFileNote;     // ~ Current_fileNote
    // v2.6.0 : commande lancée par "Exécuter" dont le résultat n'est pas encore revenu. On retient le
    // FICHIER d'origine (l'utilisateur peut changer de fiche pendant l'exécution) et la ligne de la
    // commande, pour y ajouter le résultat à la fin.
    struct PendingCommand {
        QString filePath;            // fiche d'où la commande a été lancée (vide si la note n'a pas de fichier)
        QString command;             // texte de la ligne (trimmed)
        int occurrence = 0;          // rang parmi les lignes identiques (cf. MarkdownEdit::commandOccurrence)
        QTextCursor mark;            // repère dans le document de l'éditeur (valable tant que loadSerial est inchangé)
        quint64 serial = 0;          // MarkdownEdit::loadSerial() au lancement
        QPointer<LinuxTermDialog> term;
    };
    QHash<int, PendingCommand> pendingCommands;
    int nextCommandJobId = 1;
    void launchNoteCommand(LinuxTermDialog *term, const QString &cmd);

    QString previewFilePath;     // v2.5.0 : fiche de la bibliothèque affichée (et éditable) dans l'onglet Preview
    QString treeViewOldNameEdit; // ~ TreeViewOldNameEdit

    int linuxTermCounter = 0;    // ~ FLinuxTermCounter
    int netcatTermCounter = 0;   // ~ nouveau, pendant de linuxTermCounter pour les fenêtres Netcat
    QString targetOldValue;      // ~ SG_TargetOldValue
    QString targetFileName;      // ~ SG_TargetFileName

    QTimer updateTimer;          // ~ Timer_Update

    // --- Explorateur de titres (nouveau) ---
    QTimer outlineTimer;         // regroupe les frappes rapides avant de recalculer les titres
    QStringList outlineSignature; // "niveau|texte" de chaque titre actuellement affiché (détecte un vrai changement)
    QSet<QString> outlineCollapsed; // clés des nœuds repliés par l'utilisateur (conservées entre deux reconstructions)

    QMenu *popupMenuProject = nullptr;   // ~ PopupMenu_Project
    QAction *actionInsertFolderNotes = nullptr;       // v2.0.0
    QAction *actionToggleReportExclusion = nullptr;   // v2.0.0
    QAction *actionHttpConnect = nullptr;             // v2.3.0
    QAction *actionHttpDisconnect = nullptr;          // v2.3.0
    QMenu *popupMenuLibraries = nullptr; // ~ PopupMenu_Libraries

    QList<LinuxTermDialog *> openLinuxTerms; // remplace le Tag/Pointer bricolé en Pascal
    QList<NetcatTermDialog *> openNetcatTerms; // pendant de openLinuxTerms pour les fenêtres Netcat
    HelpDialog *helpDialog = nullptr; // réutilisé entre les ouvertures (non modale)

    // v2.3.0 : serveurs HTTP de distribution de fichiers actifs, un par dossier (clé = chemin nettoyé du dossier)
    QMap<QString, HttpFileServer *> httpServers;

    // v2.4.0 : serveur HTTP de l'onglet "tools" (dossier choisi dans l'arbre tools, ou racine du dossier tools)
    HttpFileServer *toolsServer = nullptr;
    QString toolsServedPath;

    QString treeClipboardPath;   // ~ nouveau : chemin de l'élément copié (fichier OU dossier) dans Project
    int treeClipboardType = -1;  // ~ nouveau : type de l'élément copié (TreeItemType)

    // --- Méthodes internes (~ section "private" de TFMain) ---
    void addDirectoryProjectToTree(const QString &directory, QTreeWidgetItem *parentItem);
    void scanProjectDisk(const QString &startDirectory);
    void addDirectoryLibraryToTree(const QString &directory, QTreeWidgetItem *parentItem);
    void scanLibraryDisk(const QString &startDirectory);
    QString readTreeviewPath(QTreeWidgetItem *item, int *imageIndexOut = nullptr);
    QString readTreeviewFolderPath(QTreeWidgetItem *item);
    QString readTreeviewMachineFolderPath(QTreeWidgetItem *item);
    void viewTargetInformation(const QString &fileName);
    void searchTargetInformation(const QString &directory);
    void buildContextMenus();

    // v2.3.0 : serveurs HTTP
    void stopHttpServer(const QString &folderKey);   // ferme le serveur du dossier (s'il existe) et met l'arbre à jour
    void stopAllHttpServers();                       // ferme toutes les connexions (fermeture du programme)
    QString httpPortConflict(int port) const;        // non vide si un autre serveur de CyberNote utilise déjà ce port
    QString httpListenFailureReason(HttpFileServer *server, int port) const; // explication d'un échec de listen()
    void showHttpRequest(quint16 port, const QString &peer, const QString &method,
                         const QString &path, int status); // trace d'une requête dans la barre d'état

    // v2.4.0 : onglet "tools"
    void addDirectoryToolsToTree(const QString &directory, QTreeWidgetItem *parentItem, int depth);
    void scanToolsDisk();                            // (re)construit l'arbre à partir de <application>/tools/
    void updateToolsConnectionUi();                  // texte/icône du bouton, icône et nom de l'onglet
    void stopToolsServer();
    void refreshHttpServerItems();                   // arrête les serveurs dont le dossier a disparu de l'arbre, puis applique icône + ":port"

    // Remplace <nomDuParametre> par la valeur correspondante de la grille Target
    // (si affichée), et <attackIP> par l'IP "attaquant" pertinente pour la cible
    // (cf. determineAttackerIp) -- utilisé au double-clic sur une note de bibliothèque.
    QString substitutePlaceholders(const QString &text) const;
    bool appendLibraryNoteFile(const QString &path); // ajoute (avec substitution) le contenu d'une note de bibliothèque à la fin de la note courante
    QString determineAttackerIp(const QString &targetIp) const;

    void loadSoftConfiguration();      // ~ partie de TFMain.FormCreate
    void createDefaultSoftConfiguration(); // ~ branche "except" de TFMain.FormCreate
    void saveSoftConfiguration();      // ~ partie de TFMain.FormClose

    QTreeWidgetItem *addTreeChild(QTreeWidgetItem *parent, QTreeWidget *tree,
                                   const QString &text, const QString &fullPath, int type);

    // ~ nouveau : copie récursive (fichier OU dossier entier avec tout son contenu) et
    // génération d'un nom de destination unique (évite d'écraser un élément existant).
    bool copyRecursively(const QString &srcPath, const QString &destPath) const;
    QString uniqueDestinationPath(const QString &destFolder, const QString &baseName, bool isFolder) const;
};
