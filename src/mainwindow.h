#pragma once

#include <QMainWindow>
#include <QTreeWidgetItem>
#include <QMenu>
#include <QTimer>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class LinuxTermDialog;
class HelpDialog;

// ~ Node.ImageIndex en Lazarus : type d'entrée d'arborescence, stocké dans
// Qt::UserRole+1 (le chemin complet est stocké dans Qt::UserRole, ~ Node.Data).
enum TreeItemType {
    ItemFolder      = 0, // dossier
    ItemImage       = 1, // .png
    ItemNote        = 2, // .md / .txt (arborescence Projet)
    ItemLibraryNote = 3   // .md (arborescence Bibliothèques, "exécutable"/insérable)
};
enum { RolePath = Qt::UserRole, RoleType = Qt::UserRole + 1 };

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

    // --- Grille "Target" ---
    void onTargetCellChanged(int row, int column);      // ~ SG_TargetEditingDone
    void onTargetCurrentCellChanged(int currentRow, int currentColumn,
                                     int previousRow, int previousColumn); // ~ SG_TargetSelectCell

    // --- Timer de sauvegarde automatique ---
    void onUpdateTimerTick();     // ~ Timer_UpdateTimer

private:
    Ui::MainWindow *ui;

    // --- État (miroir des champs "private" de TFMain) ---
    QString pwdApplication;      // ~ PWD_Application
    QString currentProject;      // ~ Current_project
    QStringList softConfiguration; // ~ FSoftConfiguration
    QString currentFileNote;     // ~ Current_fileNote
    QString treeViewOldNameEdit; // ~ TreeViewOldNameEdit

    int linuxTermCounter = 0;    // ~ FLinuxTermCounter
    QString targetOldValue;      // ~ SG_TargetOldValue
    QString targetFileName;      // ~ SG_TargetFileName

    QTimer updateTimer;          // ~ Timer_Update

    QMenu *popupMenuProject = nullptr;   // ~ PopupMenu_Project
    QMenu *popupMenuLibraries = nullptr; // ~ PopupMenu_Libraries

    QList<LinuxTermDialog *> openLinuxTerms; // remplace le Tag/Pointer bricolé en Pascal
    HelpDialog *helpDialog = nullptr; // réutilisé entre les ouvertures (non modale)

    QString treeClipboardPath;   // ~ nouveau : chemin de l'élément copié (fichier OU dossier) dans Project
    int treeClipboardType = -1;  // ~ nouveau : type de l'élément copié (TreeItemType)

    // --- Méthodes internes (~ section "private" de TFMain) ---
    void addDirectoryProjectToTree(const QString &directory, QTreeWidgetItem *parentItem);
    void scanProjectDisk(const QString &startDirectory);
    void addDirectoryLibraryToTree(const QString &directory, QTreeWidgetItem *parentItem);
    void scanLibraryDisk(const QString &startDirectory);
    QString readTreeviewPath(QTreeWidgetItem *item, int *imageIndexOut = nullptr);
    QString readTreeviewFolderPath(QTreeWidgetItem *item);
    void viewTargetInformation(const QString &fileName);
    void searchTargetInformation(const QString &directory);
    void buildContextMenus();

    // Remplace <nomDuParametre> par la valeur correspondante de la grille Target
    // (si affichée), et <attackIP> par l'IP "attaquant" pertinente pour la cible
    // (cf. determineAttackerIp) -- utilisé au double-clic sur une note de bibliothèque.
    QString substitutePlaceholders(const QString &text) const;
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
