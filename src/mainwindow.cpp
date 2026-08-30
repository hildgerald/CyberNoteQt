#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "linuxtermdialog.h"
#include "configfile.h"
#include "helpdialog.h"

#include <QCloseEvent>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QCoreApplication>
#include <QSignalBlocker>
#include <QProcess>
#include <QRegularExpression>
#include <QNetworkInterface>
#include <QIcon>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    buildContextMenus();

    // --- Connexions menu ---
    connect(ui->actionNewProject,  &QAction::triggered, this, &MainWindow::onNewProject);
    connect(ui->actionOpenProject, &QAction::triggered, this, &MainWindow::onOpenProject);
    connect(ui->actionExit,        &QAction::triggered, this, &MainWindow::onExit);
    connect(ui->actionNewLinuxTerm,&QAction::triggered, this, &MainWindow::onNewLinuxTerm);

    // --- Connexions menu Help ---
    connect(ui->actionHelpContents, &QAction::triggered, this, &MainWindow::onHelpContents);
    connect(ui->actionAbout,        &QAction::triggered, this, &MainWindow::onAbout);

    // --- Connexions toolbar projet ---
    connect(ui->actionAddMachine,          &QAction::triggered, this, &MainWindow::onAddMachine);
    connect(ui->actionAddFolder,           &QAction::triggered, this, &MainWindow::onAddFolder);
    connect(ui->actionNewNote,             &QAction::triggered, this, &MainWindow::onNewNote);
    connect(ui->actionDelete,              &QAction::triggered, this, &MainWindow::onDelete);
    connect(ui->actionMakeCopy,            &QAction::triggered, this, &MainWindow::onMakeCopy);
    connect(ui->actionMoveFileInto,        &QAction::triggered, this, &MainWindow::onMoveFileInto);
    connect(ui->actionMergeEntireFileWith, &QAction::triggered, this, &MainWindow::onMergeEntireFileWith);
    connect(ui->actionRename,              &QAction::triggered, this, &MainWindow::onRename);
    connect(ui->actionTreeCopy,            &QAction::triggered, this, &MainWindow::onTreeCopy);
    connect(ui->actionTreePaste,           &QAction::triggered, this, &MainWindow::onTreePaste);
    connect(ui->actionNewProjectTb,        &QAction::triggered, this, &MainWindow::onNewProject);
    connect(ui->actionOpenProjectTb,       &QAction::triggered, this, &MainWindow::onOpenProject);

    // --- Connexions toolbar note ---
    connect(ui->actionNoteCopy,    &QAction::triggered, this, &MainWindow::onNoteCopy);
    connect(ui->actionNoteCut,     &QAction::triggered, this, &MainWindow::onNoteCut);
    connect(ui->actionNotePaste,   &QAction::triggered, this, &MainWindow::onNotePaste);
    connect(ui->actionNoteExecute, &QAction::triggered, this, &MainWindow::onNoteExecute);

    // --- Arborescences ---
    connect(ui->treeProject, &QTreeWidget::itemClicked, this, &MainWindow::onTreeProjectItemClicked);
    connect(ui->treeProject, &QTreeWidget::itemChanged, this, &MainWindow::onTreeProjectItemChanged);
    connect(ui->treeProject, &QTreeWidget::currentItemChanged, this, &MainWindow::onTreeProjectCurrentItemChanged);
    connect(ui->treeLibrary, &QTreeWidget::itemClicked, this, &MainWindow::onTreeLibraryItemClicked);
    connect(ui->treeLibrary, &QTreeWidget::itemDoubleClicked, this, &MainWindow::onTreeLibraryDoubleClicked);

    // Renommage inline : uniquement via F2 (évite le conflit avec le simple-clic qui
    // ouvre déjà la note dans TreeView1Click / onTreeProjectItemClicked).
    ui->treeProject->setEditTriggers(QAbstractItemView::EditKeyPressed);

    // --- Grille Target ---
    connect(ui->tableTarget, &QTableWidget::cellChanged, this, &MainWindow::onTargetCellChanged);
    connect(ui->tableTarget, &QTableWidget::currentCellChanged, this, &MainWindow::onTargetCurrentCellChanged);
    ui->gbTarget->setVisible(false); // ~ GB_Target.Visible := findTargetFile (par défaut caché)

    // --- Timer de sauvegarde auto (1s, comme Timer_Update en Pascal) ---
    updateTimer.setInterval(1000);
    connect(&updateTimer, &QTimer::timeout, this, &MainWindow::onUpdateTimerTick);
    updateTimer.start();

    // ~ TFMain.FormCreate
    pwdApplication = QCoreApplication::applicationDirPath() + "/";
    scanLibraryDisk(pwdApplication + "libraries/");
    loadSoftConfiguration();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::buildContextMenus()
{
    // ~ PopupMenu_Project / PopupMenu_Note / PopupMenu_Libraries
    // En Lazarus ces popups sont posés depuis le formulaire ; en Qt on les construit
    // en code et on les affiche via QWidget::customContextMenuRequested.
    popupMenuProject = new QMenu(this);
    popupMenuProject->addAction(ui->actionNewProjectTb);
    popupMenuProject->addAction(ui->actionOpenProjectTb);
    popupMenuProject->addSeparator();
    popupMenuProject->addAction(ui->actionAddMachine);  // ~ MenuPopProject_NewComputer
    popupMenuProject->addAction(ui->actionAddFolder);   // ~ MenuPopProject_NewFolder
    popupMenuProject->addAction(ui->actionNewNote);
    popupMenuProject->addSeparator();
    popupMenuProject->addAction(ui->actionTreeCopy);
    popupMenuProject->addAction(ui->actionTreePaste);
    popupMenuProject->addSeparator();
    popupMenuProject->addAction(ui->actionRename);
    popupMenuProject->addAction(ui->actionMakeCopy);
    popupMenuProject->addAction(ui->actionMoveFileInto);
    popupMenuProject->addAction(ui->actionMergeEntireFileWith);
    popupMenuProject->addSeparator();
    popupMenuProject->addAction(ui->actionDelete);

    // ~ NB : contrairement à treeProject/treeLibrary, mdEditor n'utilise PAS
    // Qt::CustomContextMenu ici : MarkdownEdit surcharge déjà contextMenuEvent()
    // (menu standard Copier/Coller + sous-menu "Table" quand le curseur est dans un
    // tableau). Passer par CustomContextMenu court-circuiterait ce menu (il ne serait
    // alors JAMAIS appelé) -- c'était le bug : le clic droit dans un tableau n'ouvrait
    // jamais les actions ajouter/supprimer/copier/coller ligne/colonne. On se contente
    // donc d'injecter l'action "Exécuter" dans SON menu, via setExtraContextMenuActions.
    ui->mdEditor->setExtraContextMenuActions({ui->actionNoteExecute});

    popupMenuLibraries = new QMenu(this); // ~ PopupMenu_Libraries (vide côté Pascal)

    ui->treeProject->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->treeLibrary->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(ui->treeProject, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        popupMenuProject->exec(ui->treeProject->mapToGlobal(pos));
    });
    connect(ui->treeLibrary, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        popupMenuLibraries->exec(ui->treeLibrary->mapToGlobal(pos));
    });
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // ~ TFMain.FormClose
    saveSoftConfiguration();
    event->accept();
}

// ======================= Menu File =======================

void MainWindow::onNewProject()
{
    // ~ TFMain.Menu_NewProjectClick
    const QString dir = QFileDialog::getExistingDirectory(this, tr("New Project"));
    if (dir.isEmpty())
        return;

    QFile projectFile(dir + "/.project");
    if (projectFile.open(QIODevice::WriteOnly | QIODevice::Text))
        projectFile.close(); // fichier vide, réservé à une config globale future

    currentProject = dir;
    scanProjectDisk(currentProject);
}

void MainWindow::onOpenProject()
{
    // ~ TFMain.Menu_OpenProjectClick
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Open Project"));
    if (dir.isEmpty())
        return;
    currentProject = dir;
    scanProjectDisk(currentProject);
}

void MainWindow::onExit()
{
    close();
}

// ======================= Menu Tools =======================

void MainWindow::onNewLinuxTerm()
{
    // ~ TFMain.Start_NewLinuxTermClick : ouvre une nouvelle fenêtre terminal et
    // ajoute une entrée dynamique dans le menu Tools pour lui redonner le focus.
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Enter the New Terminal Name"),
                                                tr("Name :"), QLineEdit::Normal, QString(), &ok);

    // ~ dossier de démarrage du terminal : le dossier actuellement sélectionné dans
    // l'arborescence Project, sinon la racine du projet ouvert, sinon le dossier de
    // l'application (CyberNoteQt), dans cet ordre de priorité.
    QString startDir = readTreeviewFolderPath(ui->treeProject->currentItem());
    if (startDir.isEmpty() || !QDir(startDir).exists())
        startDir = currentProject;
    if (startDir.isEmpty() || !QDir(startDir).exists())
        startDir = pwdApplication;

    auto *term = new LinuxTermDialog(this, startDir);
    term->setAttribute(Qt::WA_DeleteOnClose);
    term->resize(900, 600); // taille par défaut confortable pour une fenêtre de terminal

    if (!ok || name.trimmed().isEmpty()) {
        ++linuxTermCounter;
        term->setFixedTitle(tr("Linux Terminal %1").arg(linuxTermCounter));
    } else {
        term->setFixedTitle(tr("Linux Terminal - %1").arg(name));
    }
    openLinuxTerms.append(term);

    QAction *menuEntry = ui->menuTools->addAction(term->windowTitle());
    connect(menuEntry, &QAction::triggered, term, [term]() {
        term->show();
        term->raise();
        term->activateWindow();
    });
    // ~ garde le libellé du menu Tools synchronisé si le terminal est renommé
    // ultérieurement (clic droit > Rename Terminal...), pas seulement à la création.
    connect(term, &QWidget::windowTitleChanged, menuEntry, &QAction::setText);
    connect(term, &QObject::destroyed, menuEntry, &QObject::deleteLater);
    connect(term, &QObject::destroyed, this, [this](QObject *obj) {
        // ~ retire la fenêtre fermée de la liste (WA_DeleteOnClose la détruit tout seul) ;
        // comparaison de pointeur uniquement, obj peut déjà être partiellement détruit.
        openLinuxTerms.removeAll(static_cast<LinuxTermDialog *>(obj));
    });

    term->show();
}

// ======================= Menu Help =======================

void MainWindow::onHelpContents()
{
    if (!helpDialog) {
        helpDialog = new HelpDialog(this);
        helpDialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(helpDialog, &QObject::destroyed, this, [this]() { helpDialog = nullptr; });
    }
    helpDialog->show();
    helpDialog->raise();
    helpDialog->activateWindow();
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("About CyberNotePas"),
        tr("<h3>CyberNotePas v1.0 (portage Qt6/C++)</h3>"
           "<p>Outil de prise de notes structuré par projet pour le pentest / la sécurité "
           "offensive : arborescence de machines cibles, notes Markdown avec aperçu en direct, "
           "fiches d'information par cible, bibliothèque de commandes, et terminaux Linux "
           "intégrés (vrais pseudo-terminaux).</p>"
           "<p>Portage Qt6/C++ (Kali/Linux) du logiciel Lazarus/Free Pascal CyberNotePas d'origine.</p>"
           "<p>Voir <b>Help &gt; Help Contents</b> pour le guide d'utilisation complet.</p>"));
}

// ======================= Toolbar projet =======================

void MainWindow::onAddFolder()
{
    // ~ TFMain.TB_AddFolderClick
    const QString selectedPath = readTreeviewFolderPath(ui->treeProject->currentItem());
    if (!QDir(selectedPath).exists())
        return;

    bool ok = false;
    const QString folderName = QInputDialog::getText(this, tr("Create a new folder"),
                                                       tr("Name :"), QLineEdit::Normal, QString(), &ok);
    if (!ok || folderName.isEmpty())
        return;

    if (!QDir(selectedPath).mkdir(folderName))
        QMessageBox::critical(this, tr("Create a new folder"), tr("Impossible to create the folder"));

    scanProjectDisk(currentProject);
}

void MainWindow::onAddMachine()
{
    // ~ TFMain.TB_AddMachineClick
    QString selectedPath = readTreeviewFolderPath(ui->treeProject->currentItem());
    if (!QDir(selectedPath).exists())
        selectedPath = currentProject;
    if (!QDir(selectedPath).exists())
        return;

    bool ok = false;
    const QString computerName = QInputDialog::getText(this, tr("Target computer"),
                                                         tr("Name :"), QLineEdit::Normal, QString(), &ok);
    if (!ok || computerName.isEmpty())
        return;

    const QString machineDir = selectedPath + "/" + computerName;
    if (!QDir(selectedPath).mkdir(computerName)) {
        QMessageBox::critical(this, tr("Target computer"), tr("Impossible to create the computer folder"));
        scanProjectDisk(currentProject);
        return;
    }

    // Fichier .target avec les 3 clés attendues par ViewTargetInformation
    QFile targetFile(machineDir + "/.target");
    if (targetFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&targetFile);
        out.setEncoding(QStringConverter::Utf8);
        out << "targetIP=\n" << "targetName=\n" << "targetComment=\n";
        targetFile.close();
    }

    // Lit FSoftConfiguration entre <AddNewMachine> et </AddNewMachine> pour créer les
    // sous-dossiers "mkdir(...)" (les entrées "mkfile(...)" existent dans le fichier de
    // config mais ne sont pas traitées ici, fidèle au comportement Pascal d'origine).
    bool inComputerZone = false;
    for (const QString &line : std::as_const(softConfiguration)) {
        if (line == "<AddNewMachine>") {
            inComputerZone = true;
        } else if (line == "</AddNewMachine>") {
            inComputerZone = false;
        } else if (inComputerZone) {
            if (line.startsWith("mkdir(") && line.endsWith(")")) {
                const QString dir = line.mid(6, line.length() - 7);
                if (!dir.isEmpty())
                    QDir(machineDir).mkdir(dir);
            }
        }
    }

    scanProjectDisk(currentProject);
}

void MainWindow::onNewNote()
{
    // ~ TFMain.TB_NewNoteClick
    const QString selectedPath = readTreeviewFolderPath(ui->treeProject->currentItem());
    if (!QDir(selectedPath).exists())
        return;

    bool ok = false;
    const QString noteName = QInputDialog::getText(this, tr("Create a new note"),
                                                     tr("Name :"), QLineEdit::Normal, QString(), &ok);
    if (!ok || noteName.isEmpty())
        return;

    QFile f(selectedPath + "/" + noteName + ".md");
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) // Rewrite Pascal = écrase/crée vide
        f.close();

    scanProjectDisk(currentProject);
}

void MainWindow::onDelete()
{
    // ~ TFMain.TB_DeleteClick
    int type = -1;
    const QString oldName = readTreeviewPath(ui->treeProject->currentItem(), &type);

    if (type == ItemFolder) {
        if (QDir(oldName).exists()) {
            const auto answer = QMessageBox::warning(this, tr("Confirmation"),
                tr("Do you really want to delete permanently \"%1\" and all its contents ?").arg(oldName),
                QMessageBox::Yes | QMessageBox::No);
            if (answer == QMessageBox::Yes) {
                if (!QDir(oldName).removeRecursively())
                    QMessageBox::critical(this, tr("Remove"), tr("Error when delete the folder !"));
            }
        } else {
            QMessageBox::critical(this, tr("Rename"), tr("We can't rename the folder because the it doesn't exists !"));
        }
    } else if (type > ItemFolder) {
        if (QFile::exists(oldName)) {
            const auto answer = QMessageBox::warning(this, tr("Confirmation"),
                tr("Do you really want to delete permanently \"%1\" ?").arg(oldName),
                QMessageBox::Yes | QMessageBox::No);
            if (answer == QMessageBox::Yes) {
                if (!QFile::remove(oldName))
                    QMessageBox::critical(this, tr("Remove"), tr("Error when delete the file !"));
            }
        } else {
            QMessageBox::critical(this, tr("Rename"), tr("We can't rename the file because the file doesn't exists !"));
        }
    }
    scanProjectDisk(currentProject);
}

void MainWindow::onMakeCopy()
{
    // ~ TFMain.TB_MakeCopyClick
    int fileType = -1;
    const QString selectedFile = readTreeviewPath(ui->treeProject->currentItem(), &fileType);
    if (fileType != ItemNote) // On ne duplique que les fichiers .md/.txt (ImageIndex 2)
        return;

    const QString selectedPath = readTreeviewFolderPath(ui->treeProject->currentItem());
    if (!QDir(selectedPath).exists())
        return;

    while (true) {
        bool ok = false;
        const QString noteName = QInputDialog::getText(this, tr("Duplicate a note"),
                                                         tr("Name :"), QLineEdit::Normal, QString(), &ok);
        if (!ok || noteName.isEmpty())
            break; // ~ NoteName vide -> on sort de la boucle "repeat...until length(NoteName)=0"

        const QString destPath = selectedPath + "/" + noteName + ".md";
        if (QFile::exists(destPath)) {
            QMessageBox::critical(this, tr("Duplicate note"), tr("We can't duplicate the file because the file exists !"));
            continue;
        }

        if (QFile::copy(selectedFile, destPath)) {
            ui->mdEditor->loadFromMdFile(destPath);
            currentFileNote = destPath;
            ui->tabNote->setTabText(0, noteName + ".md");
            scanProjectDisk(currentProject);
        }
        break;
    }
}

void MainWindow::onMergeEntireFileWith()
{
    // ~ TFMain.TB_MergeEntireFileWithClick
    int fileType = -1;
    const QString selectedFile = readTreeviewPath(ui->treeProject->currentItem(), &fileType);
    if (fileType != ItemNote)
        return;

    if (!QFile::exists(selectedFile))
        return;

    const QString destFile = QFileDialog::getOpenFileName(this, tr("Merge entire file with..."),
                                                            currentProject);
    if (destFile.isEmpty())
        return;

    QFile source(selectedFile);
    QFile dest(destFile);
    if (!source.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    QString sourceText = QTextStream(&source).readAll();
    source.close();

    QString destText;
    if (dest.open(QIODevice::ReadOnly | QIODevice::Text)) {
        destText = QTextStream(&dest).readAll();
        dest.close();
    }

    destText += "\n" + sourceText;

    if (dest.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&dest);
        out.setEncoding(QStringConverter::Utf8);
        out << destText;
        dest.close();
    }
    QFile::remove(selectedFile);

    ui->mdEditor->loadFromMdFile(destFile);
    currentFileNote = destFile;
    ui->tabNote->setTabText(0, QFileInfo(destFile).fileName());
    scanProjectDisk(currentProject);
}

void MainWindow::onMoveFileInto()
{
    // ~ TFMain.TB_MoveFileIntoClick
    int fileType = -1;
    const QString selectedFile = readTreeviewPath(ui->treeProject->currentItem(), &fileType);
    if (fileType != ItemNote)
        return;

    if (!QFile::exists(selectedFile))
        return;

    const QString destDir = QFileDialog::getExistingDirectory(this, tr("Move file into"));
    if (destDir.isEmpty())
        return;

    const QString fileName = QFileInfo(selectedFile).fileName();
    const QString destPath = destDir + "/" + fileName;

    if (QFile::copy(selectedFile, destPath)) {
        QFile::remove(selectedFile);
        ui->mdEditor->loadFromMdFile(destPath);
        currentFileNote = destPath;
        ui->tabNote->setTabText(0, fileName);
        scanProjectDisk(currentProject);
    }
}

void MainWindow::onRename()
{
    // ~ TFMain.TB_RenameClick
    int typeFichier = -1;
    const QString oldName = readTreeviewPath(ui->treeProject->currentItem(), &typeFichier);

    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Rename a file or directory"),
                                                    tr("New name :"), QLineEdit::Normal, QString(), &ok);
    if (!ok || newName.trimmed().isEmpty())
        return;

    const QFileInfo oldInfo(oldName);
    const QString parentDir = oldInfo.absolutePath();
    const QString newPath = parentDir + "/" + newName;

    if (typeFichier == ItemFolder) {
        if (QDir(oldName).exists()) {
            if (QDir(newPath).exists()) {
                QMessageBox::critical(this, tr("Rename"), tr("A folder with the same name exists !"));
                return;
            }
            if (!QDir().rename(oldName, newPath))
                QMessageBox::critical(this, tr("Rename"), tr("Error when rename the folder !"));
        } else {
            QMessageBox::critical(this, tr("Rename"), tr("We can't rename the folder because the it doesn't exists !"));
        }
    } else if (typeFichier > ItemFolder) {
        if (QFile::exists(oldName)) {
            if (QFile::exists(newPath)) {
                QMessageBox::critical(this, tr("Rename"), tr("A folder with the same name exists !"));
                return;
            }
            if (!QFile::rename(oldName, newPath))
                QMessageBox::critical(this, tr("Rename"), tr("Error when rename the folder !"));
        } else {
            QMessageBox::critical(this, tr("Rename"), tr("We can't rename the file because the file doesn't exists !"));
        }
    }
    scanProjectDisk(currentProject);
}

void MainWindow::onTreeCopy()
{
    // ~ nouveau : mémorise l'élément sélectionné (fichier OU dossier entier) pour un
    // collage ultérieur (onTreePaste), à ne pas confondre avec "Make a copy" qui
    // duplique une note sur place et ne gère pas les dossiers.
    int type = -1;
    const QString path = readTreeviewPath(ui->treeProject->currentItem(), &type);
    if (path.isEmpty() || type < 0 || !QFileInfo::exists(path))
        return;

    treeClipboardPath = path;
    treeClipboardType = type;
}

void MainWindow::onTreePaste()
{
    // ~ nouveau : colle l'élément précédemment copié dans le dossier sélectionné (ou
    // le dossier parent si un fichier est sélectionné). Pour un dossier, TOUT son
    // contenu est copié récursivement (sous-dossiers compris), pas seulement le dossier vide.
    if (treeClipboardPath.isEmpty() || !QFileInfo::exists(treeClipboardPath))
        return;

    QString destFolder = readTreeviewFolderPath(ui->treeProject->currentItem());
    if (destFolder.isEmpty() || !QDir(destFolder).exists())
        destFolder = currentProject;
    if (destFolder.isEmpty() || !QDir(destFolder).exists())
        return;

    const bool isFolder = (treeClipboardType == ItemFolder);

    if (isFolder) {
        // Sécurité : on ne peut pas coller un dossier dans lui-même ni dans un de ses
        // propres sous-dossiers (boucle infinie / corruption des données).
        const QString srcAbs = QDir(treeClipboardPath).absolutePath();
        const QString destAbs = QDir(destFolder).absolutePath();
        if (destAbs == srcAbs || (destAbs + "/").startsWith(srcAbs + "/")) {
            QMessageBox::critical(this, tr("Paste"),
                tr("Cannot paste a folder into itself or one of its own subfolders."));
            return;
        }
    }

    const QString baseName = QFileInfo(treeClipboardPath).fileName();
    const QString destPath = uniqueDestinationPath(destFolder, baseName, isFolder);

    const bool ok = isFolder ? copyRecursively(treeClipboardPath, destPath)
                              : QFile::copy(treeClipboardPath, destPath);
    if (!ok)
        QMessageBox::critical(this, tr("Paste"), tr("Impossible to paste the copied item here."));

    scanProjectDisk(currentProject);
}

bool MainWindow::copyRecursively(const QString &srcPath, const QString &destPath) const
{
    // ~ nouveau : copie récursive d'un dossier (avec tout son contenu, sous-dossiers
    // compris) ou d'un simple fichier ; QDir/QFile n'offrent pas cette fonction telle quelle.
    const QFileInfo srcInfo(srcPath);
    if (srcInfo.isDir()) {
        if (!QDir().mkpath(destPath))
            return false;
        const QDir srcDir(srcPath);
        const QFileInfoList entries = srcDir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries) {
            const QString childDest = QDir(destPath).filePath(entry.fileName());
            if (!copyRecursively(entry.absoluteFilePath(), childDest))
                return false;
        }
        return true;
    }
    return QFile::copy(srcPath, destPath);
}

QString MainWindow::uniqueDestinationPath(const QString &destFolder, const QString &baseName, bool isFolder) const
{
    // ~ nouveau : évite d'écraser un élément existant en ajoutant un suffixe
    // " - copy", " - copy (2)", etc. jusqu'à trouver un nom disponible (comportement
    // classique d'un gestionnaire de fichiers, sans interrompre l'utilisateur).
    QString candidate = QDir(destFolder).filePath(baseName);
    if (!QFileInfo::exists(candidate))
        return candidate;

    QString stem = baseName;
    QString ext;
    if (!isFolder) {
        const QFileInfo fi(baseName);
        const QString suffix = fi.suffix();
        if (!suffix.isEmpty() && !fi.completeBaseName().isEmpty()) {
            stem = fi.completeBaseName();
            ext = "." + suffix;
        }
    }

    int counter = 1;
    do {
        const QString newName = (counter == 1)
            ? QStringLiteral("%1 - copy%2").arg(stem, ext)
            : QStringLiteral("%1 - copy (%2)%3").arg(stem).arg(counter).arg(ext);
        candidate = QDir(destFolder).filePath(newName);
        ++counter;
    } while (QFileInfo::exists(candidate));

    return candidate;
}

// ======================= Toolbar note =======================

void MainWindow::onNoteCopy()  { ui->mdEditor->copy(); }   // ~ MenuPop_copyClick
void MainWindow::onNoteCut()   { ui->mdEditor->cut(); }    // ~ MenuPop_cutClick
void MainWindow::onNotePaste() { ui->mdEditor->paste(); }  // ~ MenuPop_PasteClick

void MainWindow::onNoteExecute()
{
    // ~ TFMain.MenuPop_ExecuteClick : envoie la ligne courante de la note au terminal.
    // Si un seul terminal est ouvert, on y envoie directement ; s'il y en a plusieurs,
    // on demande à l'utilisateur lequel choisir ; s'il n'y en a aucun, on l'invite à
    // en ouvrir un (Tools > Start New Linux Term).
    const QString cmdExe = ui->mdEditor->getCurrentLineTextContent().trimmed();
    if (cmdExe.isEmpty())
        return;

    if (openLinuxTerms.isEmpty()) {
        QMessageBox::information(this, tr("Execute"),
            tr("No terminal is open. Please open one first (Tools > Start New Linux Term)."));
        return;
    }

    if (openLinuxTerms.size() == 1) {
        openLinuxTerms.first()->sendCommand(cmdExe);
        return;
    }

    QStringList names;
    for (LinuxTermDialog *t : std::as_const(openLinuxTerms))
        names << t->windowTitle();

    bool ok = false;
    const QString choice = QInputDialog::getItem(this, tr("Choose a terminal"),
                                                  tr("Send the command to which terminal?"),
                                                  names, 0, false, &ok);
    if (!ok)
        return;

    const int idx = names.indexOf(choice);
    if (idx >= 0)
        openLinuxTerms.at(idx)->sendCommand(cmdExe);
}

// ======================= Arborescence projet =======================

void MainWindow::onTreeProjectItemClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    if (!item)
        return;

    // ~ TFMain.TreeView1Click
    QString path = item->data(0, RolePath).toString();
    const int type = item->data(0, RoleType).toInt();

    if (type == ItemNote) {
        ui->mdEditor->loadFromMdFile(path);
        currentFileNote = path;
        ui->tabNote->setTabText(0, item->text(0));
    }

    if (type != ItemFolder)
        path = QFileInfo(path).absolutePath();

    searchTargetInformation(path);
}

void MainWindow::onTreeProjectItemChanged(QTreeWidgetItem *item, int column)
{
    // ~ TFMain.TreeView1Edited
    if (column != 0 || !item)
        return;
    if (ui->treeProject->signalsBlocked())
        return;

    const QString newName = item->text(0);
    if (newName.isEmpty() || treeViewOldNameEdit.isEmpty())
        return;

    const int type = item->data(0, RoleType).toInt();
    if (type == ItemFolder) {
        // ~ "we rename a folder" : non implémenté côté Pascal d'origine, laissé volontairement vide.
    } else if (type > ItemFolder) {
        const QString newPath = QFileInfo(treeViewOldNameEdit).absolutePath() + "/" + newName;
        if (!QFile::rename(treeViewOldNameEdit, newPath))
            QMessageBox::critical(this, tr("Rename a file"), tr("Impossible to rename the file"));
        scanProjectDisk(currentProject);
    }
}

void MainWindow::onTreeProjectCurrentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous)
{
    Q_UNUSED(previous);
    // ~ TFMain.TreeView1Editing : mémorise le chemin avant une éventuelle édition inline
    treeViewOldNameEdit = readTreeviewPath(current, nullptr);
}

// ======================= Bibliothèque =======================

void MainWindow::onTreeLibraryItemClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    if (!item)
        return;

    const int type = item->data(0, RoleType).toInt();
    if (type != ItemLibraryNote)
        return;

    const QString path = item->data(0, RolePath).toString();
    if (path.isEmpty() || !QFileInfo(path).isFile())
        return;

    // L'onglet Preview est dédié à la note sélectionnée dans la bibliothèque.
    // On recharge le même MarkdownEdit afin de bénéficier du rendu Markdown,
    // des tableaux et des images relatives au dossier de la note.
    ui->mdPreview->setReadOnly(true);
    ui->mdPreview->loadFromMdFile(path);

    const QString fileName = QFileInfo(path).fileName();
    ui->tabLibraries->setTabText(1, QStringLiteral("Preview-%1").arg(fileName));
    ui->tabLibraries->setCurrentIndex(1);
}

void MainWindow::onTreeLibraryDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    if (!item)
        return;
    // ~ TFMain.TreeView2DblClick. NB : le code Pascal d'origine relit TreeView1.Selected
    // au lieu de TreeView2.Selected (bug probable, un item bibliothèque ImageIndex=3
    // n'existe jamais dans l'arbre Projet). On applique ici le comportement visiblement
    // voulu : double-clic sur une note de bibliothèque -> ajoutée à la note courante,
    // avec substitution des paramètres <nomDuParametre> / <attackIP> au passage.
    const int type = item->data(0, RoleType).toInt();
    if (type == ItemLibraryNote) {
        const QString path = item->data(0, RolePath).toString();

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return;
        QTextStream in(&f);
        in.setEncoding(QStringConverter::Utf8);
        const QString content = in.readAll();

        ui->mdEditor->appendMarkdownText(substitutePlaceholders(content));
    }
}

// ======================= Grille Target =======================

void MainWindow::onTargetCellChanged(int row, int column)
{
    Q_UNUSED(column);
    // ~ TFMain.SG_TargetEditingDone
    if (row < 0 || targetFileName.isEmpty())
        return;

    QTableWidgetItem *valueItem = ui->tableTarget->item(row, 1);
    const QString newValue = valueItem ? valueItem->text() : QString();
    if (newValue == targetOldValue)
        return;

    QFile f(targetFileName);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        for (int i = 0; i < ui->tableTarget->rowCount(); ++i) {
            QTableWidgetItem *nameIt = ui->tableTarget->item(i, 0);
            QTableWidgetItem *valIt  = ui->tableTarget->item(i, 1);
            out << (nameIt ? nameIt->text() : QString()) << "="
                << (valIt ? valIt->text() : QString()) << '\n';
        }
    }
    targetOldValue = newValue;
}

void MainWindow::onTargetCurrentCellChanged(int currentRow, int currentColumn,
                                             int previousRow, int previousColumn)
{
    Q_UNUSED(currentColumn); Q_UNUSED(previousRow); Q_UNUSED(previousColumn);
    // ~ TFMain.SG_TargetSelectCell
    QTableWidgetItem *item = ui->tableTarget->item(currentRow, 1);
    targetOldValue = item ? item->text() : QString();
}

// ======================= Timer =======================

void MainWindow::onUpdateTimerTick()
{
    // ~ TFMain.Timer_UpdateTimer
    if (ui->mdEditor->isNoteModified() && !currentFileNote.isEmpty())
        ui->mdEditor->saveToMdFile(currentFileNote);
}

// ======================= Méthodes internes =======================

QTreeWidgetItem *MainWindow::addTreeChild(QTreeWidgetItem *parent, QTreeWidget *tree,
                                           const QString &text, const QString &fullPath, int type)
{
    QTreeWidgetItem *node = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
    node->setText(0, text);
    node->setData(0, RolePath, fullPath);
    node->setData(0, RoleType, type);
    node->setFlags(node->flags() | Qt::ItemIsEditable);

    // ~ icône différente selon le type d'entrée (dossier / image / note Markdown),
    // pour les arborescences Project ET Libraries.
    switch (type) {
    case ItemFolder:
        node->setIcon(0, QIcon(QStringLiteral(":/icons/folder_plain.svg")));
        break;
    case ItemImage:
        node->setIcon(0, QIcon(QStringLiteral(":/icons/image_file.svg")));
        break;
    case ItemNote:
    case ItemLibraryNote:
        node->setIcon(0, QIcon(QStringLiteral(":/icons/markdown_file.svg")));
        break;
    default:
        break;
    }

    return node;
}

void MainWindow::addDirectoryProjectToTree(const QString &directory, QTreeWidgetItem *parentItem)
{
    // ~ TFMain.AddDirectoryProjectToTree
    QDir dir(directory);
    const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);

    for (const QFileInfo &entry : entries) {
        const QString name = entry.fileName();
        const QString fullPath = entry.absoluteFilePath();
        QTreeWidgetItem *node = nullptr;

        if (entry.isDir()) {
            node = addTreeChild(parentItem, ui->treeProject, name, fullPath, ItemFolder);
        } else {
            if (name.contains(".png"))
                node = addTreeChild(parentItem, ui->treeProject, name, fullPath, ItemImage);
            else if (name.contains(".txt") || name.contains(".md"))
                node = addTreeChild(parentItem, ui->treeProject, name, fullPath, ItemNote);
        }

        if (entry.isDir() && node)
            addDirectoryProjectToTree(fullPath, node);
    }
}

void MainWindow::scanProjectDisk(const QString &startDirectory)
{
    // ~ TFMain.scan_project_disk
    if (startDirectory.isEmpty())
        return;

    const QSignalBlocker blocker(ui->treeProject);
    ui->treeProject->clear();

    auto *rootItem = new QTreeWidgetItem(ui->treeProject);
    rootItem->setText(0, QDir(startDirectory).absolutePath());
    rootItem->setData(0, RolePath, QDir(startDirectory).absolutePath());
    rootItem->setData(0, RoleType, ItemFolder);
    rootItem->setIcon(0, QIcon(QStringLiteral(":/icons/folder_plain.svg")));

    addDirectoryProjectToTree(startDirectory, rootItem);

    rootItem->setExpanded(true);
    ui->treeProject->sortItems(0, Qt::AscendingOrder);
}

void MainWindow::addDirectoryLibraryToTree(const QString &directory, QTreeWidgetItem *parentItem)
{
    // ~ TFMain.AddDirectoryLibraryToTree
    QDir dir(directory);
    const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);

    for (const QFileInfo &entry : entries) {
        const QString name = entry.fileName();
        const QString fullPath = entry.absoluteFilePath();
        QTreeWidgetItem *node = nullptr;

        if (entry.isDir()) {
            node = addTreeChild(parentItem, ui->treeLibrary, name, fullPath, ItemFolder);
        } else if (name.contains(".md")) {
            node = addTreeChild(parentItem, ui->treeLibrary, name, fullPath, ItemLibraryNote);
        }

        if (entry.isDir() && node)
            addDirectoryLibraryToTree(fullPath, node);
    }
}

void MainWindow::scanLibraryDisk(const QString &startDirectory)
{
    // ~ TFMain.scan_Library_disk
    // NB : le code Pascal d'origine ré-utilise par erreur TreeView1 lors de l'appel
    // récursif (AddDirectoryLibraryToTree(..., TreeView1)) ; on utilise ici correctement
    // treeLibrary de bout en bout.
    if (!QDir(startDirectory).exists())
        return;

    const QSignalBlocker blocker(ui->treeLibrary);
    ui->treeLibrary->clear();

    auto *rootItem = new QTreeWidgetItem(ui->treeLibrary);
    rootItem->setText(0, QDir(startDirectory).absolutePath());
    rootItem->setData(0, RolePath, QDir(startDirectory).absolutePath());
    rootItem->setData(0, RoleType, ItemFolder);
    rootItem->setIcon(0, QIcon(QStringLiteral(":/icons/folder_plain.svg")));

    addDirectoryLibraryToTree(startDirectory, rootItem);
    rootItem->setExpanded(true);
}

QString MainWindow::readTreeviewPath(QTreeWidgetItem *item, int *imageIndexOut)
{
    // ~ TFMain.ReadTreeviewPath
    if (!item) {
        if (imageIndexOut) *imageIndexOut = -1;
        return QString();
    }
    if (imageIndexOut) *imageIndexOut = item->data(0, RoleType).toInt();
    return item->data(0, RolePath).toString();
}

QString MainWindow::readTreeviewFolderPath(QTreeWidgetItem *item)
{
    // ~ TFMain.ReadTreeviewFolderPath
    if (!item)
        return QString();

    QString result = item->data(0, RolePath).toString();
    if (item->data(0, RoleType).toInt() > ItemFolder) {
        QTreeWidgetItem *parent = item->parent();
        if (parent)
            result = parent->data(0, RolePath).toString();
    }
    return result;
}

void MainWindow::viewTargetInformation(const QString &fileName)
{
    // ~ TFMain.ViewTargetInformation
    if (!QFile::exists(fileName))
        return;

    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    QStringList lines;
    while (!in.atEnd())
        lines << in.readLine();

    const QSignalBlocker blocker(ui->tableTarget);
    ui->tableTarget->setRowCount(0);
    ui->tableTarget->setRowCount(lines.count());

    for (int i = 0; i < lines.count(); ++i) {
        const QString &lstr = lines.at(i);
        const int iEgal = lstr.indexOf('=');
        if (iEgal > 1) { // ~ iEgal>2 en Pascal (index 1-based) -> >1 en 0-based
            ui->tableTarget->setItem(i, 0, new QTableWidgetItem(lstr.left(iEgal)));
            ui->tableTarget->setItem(i, 1, new QTableWidgetItem(lstr.mid(iEgal + 1)));
        }
    }
}

void MainWindow::searchTargetInformation(const QString &directory)
{
    // ~ TFMain.SearchTargetInformation
    QString path = directory;
    bool findTargetFile = false;
    targetFileName.clear();

    while (!path.isEmpty()) {
        const QString fileTarget = path + "/.target";
        if (QFile::exists(fileTarget)) {
            findTargetFile = true;
            viewTargetInformation(fileTarget);
            targetFileName = fileTarget;
            path.clear();
        } else {
            const QString parent = QFileInfo(path).absolutePath();
            if (parent == path || path.length() < currentProject.length()) {
                path.clear();
            } else {
                path = parent;
            }
        }
    }

    ui->gbTarget->setVisible(findTargetFile);
}

QString MainWindow::substitutePlaceholders(const QString &text) const
{
    // ~ nouvelle fonctionnalité demandée : remplace <nomDuParametre> par la valeur
    // correspondante de la grille Target (si affichée), et <attackIP> par l'IP
    // "attaquant" pertinente pour la cible (cf. determineAttackerIp).
    QString result = text;

    if (!ui->gbTarget->isVisible())
        return result; // pas de fichier .target actif pour cette note -> rien à substituer

    QString targetIp;
    for (int i = 0; i < ui->tableTarget->rowCount(); ++i) {
        QTableWidgetItem *nameIt = ui->tableTarget->item(i, 0);
        if (!nameIt)
            continue;
        const QString name = nameIt->text().trimmed();
        if (name.isEmpty())
            continue;

        QTableWidgetItem *valIt = ui->tableTarget->item(i, 1);
        const QString value = valIt ? valIt->text() : QString();
        result.replace(QStringLiteral("<%1>").arg(name), value);

        if (name.compare(QStringLiteral("targetIP"), Qt::CaseInsensitive) == 0)
            targetIp = value.trimmed();
    }

    if (result.contains(QStringLiteral("<attackIP>")))
        result.replace(QStringLiteral("<attackIP>"), determineAttackerIp(targetIp));

    return result;
}

QString MainWindow::determineAttackerIp(const QString &targetIp) const
{
    // Détermine l'IP "attaquant" pertinente pour joindre targetIp : on interroge
    // directement le noyau via "ip route get", qui choisit la bonne interface/IP
    // source exactement comme le ferait une vraie connexion réseau -- ce qui gère
    // correctement le cas d'interfaces VPN (tun0...) prioritaires sur la route
    // par défaut, plutôt que de deviner nous-mêmes une interface "par défaut".
    auto runIpRouteGet = [](const QString &destination) -> QString {
        QProcess proc;
        proc.start(QStringLiteral("ip"), {QStringLiteral("route"), QStringLiteral("get"), destination});
        if (!proc.waitForFinished(1000))
            return QString();
        const QString output = QString::fromUtf8(proc.readAllStandardOutput());
        static const QRegularExpression re(R"(\bsrc\s+(\d{1,3}(?:\.\d{1,3}){3}))");
        const auto m = re.match(output);
        return m.hasMatch() ? m.captured(1) : QString();
    };

    if (!targetIp.isEmpty()) {
        const QString ip = runIpRouteGet(targetIp);
        if (!ip.isEmpty())
            return ip;
    }

    // Repli : IP cible inconnue ou "ip route get" indisponible -> IP source qu'utiliserait
    // la route par défaut (vers Internet). Comme ci-dessus, cela reflète correctement un
    // VPN actif si c'est lui qui porte la route par défaut.
    const QString viaDefault = runIpRouteGet(QStringLiteral("1.1.1.1"));
    if (!viaDefault.isEmpty())
        return viaDefault;

    // Dernier repli (ex: pas de connectivité réseau du tout) : première interface
    // active non-loopback trouvée via QNetworkInterface.
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || (iface.flags() & QNetworkInterface::IsLoopBack))
            continue;
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)
                return entry.ip().toString();
        }
    }
    return QString();
}

// ======================= Configuration logicielle =======================

void MainWindow::loadSoftConfiguration()
{
    // ~ TFMain.FormCreate (partie chargement de cybernotepas.conf)
    const QString confPath = pwdApplication + "cybernotepas.conf";
    softConfiguration = ConfigFile::load(confPath);

    if (softConfiguration.isEmpty()) {
        createDefaultSoftConfiguration();
        return;
    }

    bool loadProject = false, loadNote = false;
    for (const QString &line : std::as_const(softConfiguration)) {
        if (line.startsWith("lastproject=")) {
            loadProject = true;
            currentProject = line.mid(QStringLiteral("lastproject=").length());
        } else if (line.startsWith("lastnote=")) {
            loadNote = true;
            currentFileNote = line.mid(QStringLiteral("lastnote=").length());
        }
    }

    if (loadProject && currentProject.length() > 1) {
        scanProjectDisk(currentProject);
        if (loadNote && currentFileNote.length() > 1)
            ui->mdEditor->loadFromMdFile(currentFileNote);
    }
}

void MainWindow::createDefaultSoftConfiguration()
{
    // ~ branche "except" de TFMain.FormCreate
    softConfiguration.clear();
    softConfiguration << "<General>"
                       << "lastproject="
                       << "lastnote="
                       << "</General>"
                       << "<AddNewMachine>"
                       << "mkdir(01-scanning)"
                       << "mkdir(02-enumeration)"
                       << "mkdir(03-exploitation)"
                       << "mkdir(04-privesc)"
                       << "mkdir(05-post_exploitation)"
                       << "mkfile(findings.md)"
                       << "</AddNewMachine>";
    ConfigFile::save(pwdApplication + "cybernotepas.conf", softConfiguration);
}

void MainWindow::saveSoftConfiguration()
{
    // ~ TFMain.FormClose
    bool save2File = false;
    for (QString &line : softConfiguration) {
        if (line.startsWith("lastproject=")) {
            save2File = true;
            line = "lastproject=" + currentProject;
        } else if (line.startsWith("lastnote=")) {
            save2File = true;
            line = "lastnote=" + currentFileNote;
        }
    }
    if (save2File)
        ConfigFile::save(pwdApplication + "cybernotepas.conf", softConfiguration);
}
