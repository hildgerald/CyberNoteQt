#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "linuxtermdialog.h"
#include "netcattermdialog.h"
#include "configfile.h"
#include "helpdialog.h"
#include "insertnotesdialog.h"
#include "reportwindow.h"
#include "markdownedit.h"
#include "httpfileserver.h"

#include <QCloseEvent>
#include <QAbstractSocket>
#include <QTreeWidgetItemIterator>
#include <QSet>
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
#include <QScrollBar>
#include <QHash>
#include <QShortcut>
#include <QApplication>
#include <QSaveFile>
#include <QBuffer>
#include <QImage>
#include <QDesktopServices>
#include <QUrl>
#include <algorithm>

// ~ nouveau (v1.10.0) : garantit qu'un nom de fichier markdown se termine par l'extension .md
// (insensible à la casse), sans la doubler si l'utilisateur l'a déjà saisie ("note" -> "note.md",
// "note.md" -> "note.md", "note.MD" -> "note.MD").
static QString ensureMdExtension(const QString &fileName)
{
    const QString name = fileName.trimmed();
    return name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive) ? name : name + QStringLiteral(".md");
}

// ~ nouveau (v2.0.0) : décale de `shift` niveaux les titres Markdown (# ... ######, plafonnés à 6)
// d'une note, sans toucher aux lignes situées dans un bloc de code ```...```. Sert à emboîter les
// titres d'une note insérée sous le titre qui la précède dans le rapport.
static QString shiftMarkdownHeadings(const QString &markdown, int shift)
{
    if (shift <= 0)
        return markdown;
    static const QRegularExpression headingRe(QStringLiteral("^(#{1,6})([ \\t].*)$"));
    QStringList lines = markdown.split(QLatin1Char('\n'));
    bool inFence = false;
    for (QString &line : lines) {
        if (line.trimmed().startsWith(QStringLiteral("```"))) {
            inFence = !inFence;
            continue;
        }
        if (inFence)
            continue;
        const QRegularExpressionMatch m = headingRe.match(line);
        if (!m.hasMatch())
            continue;
        const int level = std::min(6, static_cast<int>(m.captured(1).size()) + shift);
        line = QString(level, QLatin1Char('#')) + m.captured(2);
    }
    return lines.join(QLatin1Char('\n'));
}

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
    connect(ui->actionExportDocx,  &QAction::triggered, this, &MainWindow::onExportReportDocx);
    connect(ui->actionNewLinuxTerm,&QAction::triggered, this, &MainWindow::onNewLinuxTerm);
    connect(ui->actionNewNetcatTerm,&QAction::triggered, this, &MainWindow::onNewNetcatTerm);

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

    // Ctrl+Alt+C dans la bibliothèque : sur un fichier (arbre) -> son contenu est ajouté à la fin
    // de la note ouverte ; dans l'aperçu -> la sélection est insérée à la ligne suivant le curseur.
    auto *libCopyShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+C")), ui->treeLibrary);
    libCopyShortcut->setContext(Qt::WidgetShortcut);
    connect(libCopyShortcut, &QShortcut::activated, this, &MainWindow::onLibraryCopyToNote);
    connect(ui->mdPreview, &MarkdownEdit::copyToNoteRequested, this, &MainWindow::onLibraryPreviewCopyToNote);

    // v2.5.0 : l'aperçu de la bibliothèque est éditable ; la fiche modifiée est enregistrée dès
    // qu'on la quitte (focus perdu, changement d'onglet, autre fiche sélectionnée, fermeture).
    ui->mdPreview->setReadOnly(false);
    ui->mdPreview->setCopyToNoteShortcutEnabled(true); // Ctrl+Alt+C reste actif bien que l'aperçu soit éditable
    connect(ui->mdPreview, &MarkdownEdit::focusLeft, this, [this]() { savePreviewIfModified(); });
    connect(ui->tabLibraries, &QTabWidget::currentChanged, this, [this](int) { savePreviewIfModified(); });

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

    // --- Explorateur de titres (onglet "Outline") ---
    // Toute modification du texte (frappe, chargement d'une note, ajout depuis la
    // bibliothèque...) relance un recalcul différé : 250 ms après la dernière modification.
    outlineTimer.setSingleShot(true);
    outlineTimer.setInterval(250);
    connect(&outlineTimer, &QTimer::timeout, this, &MainWindow::refreshOutline);
    connect(ui->mdEditor, &QTextEdit::textChanged, &outlineTimer, qOverload<>(&QTimer::start));
    connect(ui->treeOutline, &QTreeWidget::itemClicked,   this, &MainWindow::onOutlineItemClicked);
    connect(ui->treeOutline, &QTreeWidget::itemActivated, this, &MainWindow::onOutlineItemClicked); // Entrée au clavier
    // On mémorise les nœuds repliés par l'utilisateur pour les retrouver après chaque reconstruction.
    auto outlineKey = [](QTreeWidgetItem *it) { return it->data(0, Qt::UserRole + 1).toString(); };
    connect(ui->treeOutline, &QTreeWidget::itemCollapsed, this, [this, outlineKey](QTreeWidgetItem *it) {
        outlineCollapsed.insert(outlineKey(it));
    });
    connect(ui->treeOutline, &QTreeWidget::itemExpanded, this, [this, outlineKey](QTreeWidgetItem *it) {
        outlineCollapsed.remove(outlineKey(it));
    });

    // v2.3.0 : quel que soit le chemin de sortie du programme, toutes les connexions HTTP sont fermées
    connect(qApp, &QCoreApplication::aboutToQuit, this, &MainWindow::stopAllHttpServers);

    // ~ TFMain.FormCreate
    pwdApplication = QCoreApplication::applicationDirPath() + "/";
    scanLibraryDisk(pwdApplication + "libraries/");

    // v2.4.0 : onglet "tools" (arbre de <application>/tools/ + serveur HTTP)
    ui->treeTools->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(ui->toolsConnectButton, &QPushButton::clicked, this, &MainWindow::onToolsConnectClicked);
    connect(ui->tabProject, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == ui->tabProject->indexOf(ui->tabToolsPage))
            scanToolsDisk(); // le contenu du dossier peut avoir changé depuis le dernier affichage
    });
    updateToolsConnectionUi();
    scanToolsDisk();

    loadSoftConfiguration();
}

MainWindow::~MainWindow()
{
    stopAllHttpServers();
    delete ui;
}

// ---------------------------------------------------------------------------
// Explorateur de titres (onglet "Outline")
// ---------------------------------------------------------------------------

void MainWindow::refreshOutline()
{
    const QList<MarkdownHeading> headings = ui->mdEditor->headings();

    // Si aucun titre n'a changé de niveau ni de texte (ex: on a juste tapé dans un
    // paragraphe), l'arbre est déjà à jour : on ne le reconstruit pas (pas de scintillement,
    // la sélection et le défilement de l'arbre restent intacts).
    QStringList signature;
    signature.reserve(headings.size());
    for (const MarkdownHeading &h : headings)
        signature << QString::number(h.level) + QLatin1Char('|') + h.text;
    if (signature == outlineSignature)
        return;
    outlineSignature = signature;

    QTreeWidget *tree = ui->treeOutline;
    const int scrollValue = tree->verticalScrollBar()->value();

    // La sélection est retrouvée par son rang dans la liste plate des titres.
    int selectedIndex = -1;
    if (QTreeWidgetItem *cur = tree->currentItem())
        selectedIndex = cur->data(0, Qt::UserRole).toInt();

    QSignalBlocker blocker(tree); // setExpanded() émettrait itemExpanded/itemCollapsed
    tree->clear();

    // Construction de la hiérarchie : un titre devient l'enfant du titre précédent de
    // niveau strictement inférieur (un "###" juste après un "#" se range sous ce "#").
    QList<QTreeWidgetItem *> stack;
    QList<int> stackLevels;
    QHash<QString, int> occurrences; // pour distinguer deux titres identiques
    QList<QTreeWidgetItem *> allItems;

    for (int i = 0; i < headings.size(); ++i) {
        const MarkdownHeading &h = headings.at(i);
        while (!stack.isEmpty() && stackLevels.last() >= h.level) {
            stack.removeLast();
            stackLevels.removeLast();
        }

        QTreeWidgetItem *item = stack.isEmpty() ? new QTreeWidgetItem(tree)
                                                : new QTreeWidgetItem(stack.last());
        item->setText(0, h.text);
        item->setToolTip(0, h.text);
        item->setData(0, Qt::UserRole, i); // rang du titre dans headings()

        const QString base = QString::number(h.level) + QLatin1Char('|') + h.text;
        const int n = occurrences[base]++;
        item->setData(0, Qt::UserRole + 1, base + QLatin1Char('#') + QString::number(n)); // clé de repli
        allItems << item;

        stack << item;
        stackLevels << h.level;
    }

    for (QTreeWidgetItem *item : std::as_const(allItems)) {
        item->setExpanded(!outlineCollapsed.contains(item->data(0, Qt::UserRole + 1).toString()));
        if (item->data(0, Qt::UserRole).toInt() == selectedIndex)
            tree->setCurrentItem(item);
    }

    tree->verticalScrollBar()->setValue(scrollValue);
}

void MainWindow::onOutlineItemClicked(QTreeWidgetItem *item, int)
{
    if (!item)
        return;
    const int index = item->data(0, Qt::UserRole).toInt(); // à lire AVANT un éventuel refresh (qui détruit les items)

    // Si une modification récente du texte n'a pas encore été répercutée dans l'arbre,
    // on le met à jour d'abord (le rang du titre est relatif à la liste courante).
    if (outlineTimer.isActive()) {
        outlineTimer.stop();
        refreshOutline();
    }

    ui->mdEditor->revealHeading(index);
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

    // v2.0.0 : préparation d'un rapport à partir des notes d'un dossier
    actionInsertFolderNotes = new QAction(tr("Insert the notes of this folder into the open note..."), this);
    actionToggleReportExclusion = new QAction(tr("Exclude from the report (add a _ prefix)"), this);
    connect(actionInsertFolderNotes, &QAction::triggered, this, &MainWindow::onInsertFolderNotes);
    connect(actionToggleReportExclusion, &QAction::triggered, this, &MainWindow::onToggleReportExclusion);
    popupMenuProject->addAction(actionInsertFolderNotes);
    popupMenuProject->addAction(ui->actionExportDocx);
    popupMenuProject->addAction(actionToggleReportExclusion);

    // v2.3.0 : serveur HTTP de distribution de fichiers sur un dossier
    popupMenuProject->addSeparator();
    actionHttpConnect = new QAction(tr("http server connect"), this);
    actionHttpDisconnect = new QAction(tr("http server disconnect"), this);
    connect(actionHttpConnect, &QAction::triggered, this, &MainWindow::onHttpServerConnect);
    connect(actionHttpDisconnect, &QAction::triggered, this, &MainWindow::onHttpServerDisconnect);
    popupMenuProject->addAction(actionHttpConnect);
    popupMenuProject->addAction(actionHttpDisconnect);

    connect(popupMenuProject, &QMenu::aboutToShow, this, [this] {
        QTreeWidgetItem *it = ui->treeProject->currentItem();
        int type = -1;
        const QString path = it ? readTreeviewPath(it, &type) : QString();
        const bool isRoot = it && !it->parent();
        actionInsertFolderNotes->setEnabled(type == ItemFolder);
        actionToggleReportExclusion->setEnabled((type == ItemFolder || type == ItemNote) && !isRoot);
        const bool serverActive = (type == ItemFolder) && httpServers.contains(QDir::cleanPath(path));
        actionHttpConnect->setEnabled(type == ItemFolder && !serverActive);
        actionHttpDisconnect->setEnabled(serverActive);
        actionToggleReportExclusion->setText(QFileInfo(path).fileName().startsWith(QLatin1Char('_'))
            ? tr("Include in the report (remove the _ prefix)")
            : tr("Exclude from the report (add a _ prefix)"));
    });

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
    savePreviewIfModified(); // v2.5.0 : fiche de bibliothèque en cours d'édition
    stopAllHttpServers(); // v2.3.0 : ferme tous les serveurs HTTP et leurs connexions
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

    // ~ dossier de démarrage du terminal : toujours <projet>/<machine>/, déterminé
    // en remontant l'arborescence Project à partir de l'élément sélectionné (quel
    // que soit le sous-dossier ouvert). Si aucun dossier <machine> ne peut être
    // déduit (rien sélectionné, ou racine du projet sélectionnée), on retombe sur
    // la racine du projet ouvert, puis le dossier personnel (home) de la machine
    // courante, et en tout dernier recours le dossier de l'application (CyberNoteQt).
    QString startDir = readTreeviewMachineFolderPath(ui->treeProject->currentItem());
    if (startDir.isEmpty() || !QDir(startDir).exists())
        startDir = currentProject;
    if (startDir.isEmpty() || !QDir(startDir).exists())
        startDir = QDir::homePath();
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
    connect(term, &LinuxTermDialog::imageForNoteReady, this, &MainWindow::onTerminalImageForNote);
    connect(term, &LinuxTermDialog::textForNoteReady, this, &MainWindow::onTerminalTextForNote);

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
        // v2.6.0 : commandes en cours dans ce terminal : plus de résultat à attendre
        for (auto it = pendingCommands.begin(); it != pendingCommands.end();) {
            if (it.value().term.isNull() || it.value().term.data() == static_cast<LinuxTermDialog *>(obj))
                it = pendingCommands.erase(it);
            else
                ++it;
        }
    });

    term->show();
}

void MainWindow::onNewNetcatTerm()
{
    // ~ pendant de onNewLinuxTerm pour "Tools > Start New Netcat Term" : ouvre une
    // fenêtre Netcat (déconnectée par défaut, port 443) et ajoute une entrée dynamique
    // dans le menu Tools pour lui redonner le focus.
    auto *term = new NetcatTermDialog(this);
    term->setAttribute(Qt::WA_DeleteOnClose);
    term->resize(900, 600);

    ++netcatTermCounter;
    term->setFixedTitle(tr("Netcat Terminal %1").arg(netcatTermCounter));
    openNetcatTerms.append(term);
    connect(term, &NetcatTermDialog::imageForNoteReady, this, &MainWindow::onTerminalImageForNote);
    connect(term, &NetcatTermDialog::textForNoteReady, this, &MainWindow::onTerminalTextForNote);

    QAction *menuEntry = ui->menuTools->addAction(term->windowTitle());
    connect(menuEntry, &QAction::triggered, term, [term]() {
        term->show();
        term->raise();
        term->activateWindow();
    });
    connect(term, &QWidget::windowTitleChanged, menuEntry, &QAction::setText);
    connect(term, &QObject::destroyed, menuEntry, &QObject::deleteLater);
    connect(term, &QObject::destroyed, this, [this](QObject *obj) {
        openNetcatTerms.removeAll(static_cast<NetcatTermDialog *>(obj));
    });

    term->show();
}

void MainWindow::onTerminalImageForNote(const QImage &image)
{
    // Ctrl+Alt+S dans un terminal Linux / console netcat : insère l'image de la sélection
    // dans la note déjà ouverte, à la ligne suivant la position du curseur.
    if (currentFileNote.isEmpty()) {
        QMessageBox::information(this, tr("Insert image"),
            tr("No note is open. Please open a note first, then try again."));
        return;
    }
    if (ui->mdEditor->insertImageOnNextLine(image))
        statusBar()->showMessage(tr("Image inserted into the note."), 3000);
    else
        QMessageBox::warning(this, tr("Insert image"),
            tr("The image could not be saved next to the note."));
}

void MainWindow::onTerminalTextForNote(const QString &text)
{
    // Ctrl+Alt+C dans un terminal Linux / console netcat : insère le texte sélectionné,
    // encadré par ```, dans la note déjà ouverte, à la ligne suivant la position du curseur.
    if (currentFileNote.isEmpty()) {
        QMessageBox::information(this, tr("Insert text"),
            tr("No note is open. Please open a note first, then try again."));
        return;
    }
    if (ui->mdEditor->insertTextOnNextLine(text))
        statusBar()->showMessage(tr("Text inserted into the note as a code block."), 3000);
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
        tr("<h3>CyberNotePas v2.6.2 (portage Qt6/C++)</h3>"
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

    // v1.11.0 : une note existante n'est plus écrasée. Si le nom est déjà pris, on refuse et on
    // redemande un nouveau nom (le dernier nom saisi est reproposé pour être modifié).
    QString noteName;
    while (true) {
        bool ok = false;
        noteName = QInputDialog::getText(this, tr("Create a new note"),
                                          tr("Name :"), QLineEdit::Normal, noteName, &ok);
        if (!ok || noteName.trimmed().isEmpty())
            return;

        if (QFile::exists(selectedPath + "/" + ensureMdExtension(noteName))) {
            QMessageBox::critical(this, tr("Create a new note"),
                tr("We can't create the note because a file with this name already exists ! Please choose another name."));
            continue;
        }
        break;
    }

    QFile f(selectedPath + "/" + ensureMdExtension(noteName));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
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

    // ~ nouveau (v1.9.0) : un DOSSIER est dupliqué avec tout son contenu (sous-dossiers, notes,
    // images...), à côté de l'original, c'est-à-dire dans le même dossier parent.
    if (fileType == ItemFolder) {
        const QString srcFolder = QDir::cleanPath(selectedFile);
        if (srcFolder.isEmpty() || !QDir(srcFolder).exists())
            return;
        if (!currentProject.isEmpty() && srcFolder == QDir::cleanPath(currentProject)) {
            QMessageBox::information(this, tr("Duplicate a folder"),
                tr("The project root folder cannot be duplicated here."));
            return;
        }
        const QFileInfo srcInfo(srcFolder);
        const QString parentFolder = srcInfo.absolutePath();

        while (true) {
            bool ok = false;
            const QString folderName = QInputDialog::getText(this, tr("Duplicate a folder"),
                                                               tr("Name :"), QLineEdit::Normal,
                                                               srcInfo.fileName() + QStringLiteral(" - copy"), &ok).trimmed();
            if (!ok || folderName.isEmpty())
                break;

            // Le nouveau dossier doit rester à côté de l'original : pas de séparateur de chemin.
            if (folderName.contains('/') || folderName == QLatin1String(".") || folderName == QLatin1String("..")) {
                QMessageBox::critical(this, tr("Duplicate folder"), tr("Invalid folder name."));
                continue;
            }

            const QString destPath = QDir(parentFolder).filePath(folderName);
            if (QFileInfo::exists(destPath)) {
                QMessageBox::critical(this, tr("Duplicate folder"),
                                      tr("We can't duplicate the folder because this name already exists !"));
                continue;
            }

            if (!copyRecursively(srcFolder, destPath)) {
                QDir(destPath).removeRecursively(); // ne laisse pas une copie partielle
                QMessageBox::critical(this, tr("Duplicate folder"), tr("Impossible to duplicate the folder."));
            }
            scanProjectDisk(currentProject);
            break;
        }
        return;
    }

    if (fileType != ItemNote) // Sinon, on ne duplique que les fichiers .md/.txt (ImageIndex 2)
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

        const QString destFileName = ensureMdExtension(noteName);
        const QString destPath = selectedPath + "/" + destFileName;
        if (QFile::exists(destPath)) {
            QMessageBox::critical(this, tr("Duplicate note"), tr("We can't duplicate the file because the file exists !"));
            continue;
        }

        if (QFile::copy(selectedFile, destPath)) {
            ui->mdEditor->loadFromMdFile(destPath);
            currentFileNote = destPath;
            ui->tabNote->setTabText(0, destFileName);
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
                                                    tr("New name :"), QLineEdit::Normal,
                                                    QFileInfo(oldName).fileName(), &ok); // v1.11.0 : nom actuel prérempli
    if (!ok || newName.trimmed().isEmpty())
        return;

    const QFileInfo oldInfo(oldName);
    const QString parentDir = oldInfo.absolutePath();

    // Une note markdown (.md) renommée doit garder l'extension .md à la fin de son nom.
    QString finalName = newName;
    if (typeFichier == ItemNote && oldInfo.suffix().compare(QStringLiteral("md"), Qt::CaseInsensitive) == 0)
        finalName = ensureMdExtension(newName);
    if (finalName == oldInfo.fileName())
        return; // nom inchangé : rien à renommer
    const QString newPath = parentDir + "/" + finalName;

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
                QMessageBox::critical(this, tr("Rename"), tr("A file with the same name exists !"));
                return;
            }
            if (!QFile::rename(oldName, newPath))
                QMessageBox::critical(this, tr("Rename"), tr("Error when rename the file !"));
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
    // v2.6.0 : le résultat de la commande est ajouté à la note (cf. launchNoteCommand).
    const QString cmdExe = ui->mdEditor->getCurrentLineTextContent().trimmed();
    if (cmdExe.isEmpty())
        return;

    if (openLinuxTerms.isEmpty()) {
        QMessageBox::information(this, tr("Execute"),
            tr("No terminal is open. Please open one first (Tools > Start New Linux Term)."));
        return;
    }

    if (openLinuxTerms.size() == 1) {
        launchNoteCommand(openLinuxTerms.first(), cmdExe);
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
        launchNoteCommand(openLinuxTerms.at(idx), cmdExe);
}

void MainWindow::launchNoteCommand(LinuxTermDialog *term, const QString &cmd)
{
    // v2.6.0 : lance `cmd` dans le terminal choisi et mémorise d'où elle vient. Quand elle se
    // termine, son résultat est ajouté à la ligne suivant la commande DANS LA FICHE D'ORIGINE,
    // même si l'utilisateur a ouvert une autre fiche entre-temps (cf. onTerminalCommandFinished).
    const int jobId = nextCommandJobId++;

    PendingCommand job;
    job.filePath = currentFileNote;
    job.command = cmd;
    job.occurrence = ui->mdEditor->commandOccurrence();
    job.mark = ui->mdEditor->markCommandLine();
    job.serial = ui->mdEditor->loadSerial();
    job.term = term;

    connect(term, &LinuxTermDialog::commandFinished, this, &MainWindow::onTerminalCommandFinished,
            Qt::UniqueConnection);

    if (term->sendCommandCapture(cmd, jobId))
        pendingCommands.insert(jobId, job);
    else
        statusBar()->showMessage(tr("Command sent (its result cannot be captured)."), 4000);
}

void MainWindow::onTerminalCommandFinished(int jobId, int exitCode, const QString &output)
{
    // v2.6.0 : la commande lancée par "Exécuter" est terminée (exitCode 130 = interrompue par Ctrl+C).
    const auto it = pendingCommands.find(jobId);
    if (it == pendingCommands.end())
        return; // job d'un autre terminal / déjà traité
    const PendingCommand job = it.value();
    pendingCommands.erase(it);

    if (output.trimmed().isEmpty()) {
        statusBar()->showMessage(tr("Command finished (exit code %1), no output.").arg(exitCode), 4000);
        return;
    }

    const bool sameFileOpen = (job.filePath == currentFileNote);
    QString shownName = job.filePath.isEmpty() ? ui->tabNote->tabText(0) : QFileInfo(job.filePath).fileName();

    if (sameFileOpen && job.serial == ui->mdEditor->loadSerial()) {
        // La fiche d'origine est toujours affichée, telle qu'elle a été chargée : on insère dans le
        // document (curseur, défilement et annulation de l'utilisateur sont préservés) ; l'enregistrement
        // se fait par le timer habituel.
        if (ui->mdEditor->insertCommandOutputAfter(job.mark, job.command, output))
            statusBar()->showMessage(tr("Command result added to %1.").arg(shownName), 4000);
        return;
    }

    if (job.filePath.isEmpty() || !QFileInfo(job.filePath).isFile()) {
        statusBar()->showMessage(tr("Command finished, but the note it came from no longer exists : result not added."), 6000);
        return;
    }

    // La fiche d'origine n'est plus (ou plus telle quelle) dans l'éditeur : le résultat est écrit
    // directement dans son fichier. Si elle est de nouveau ouverte, ses éventuelles modifications non
    // enregistrées sont d'abord sauvegardées, puis l'affichage est rechargé.
    if (sameFileOpen && ui->mdEditor->isNoteModified())
        ui->mdEditor->saveToMdFile(currentFileNote);

    if (!MarkdownEdit::insertCommandOutputInFile(job.filePath, job.command, job.occurrence, output)) {
        statusBar()->showMessage(tr("Command finished, but the result could not be written to %1.").arg(shownName), 6000);
        return;
    }

    if (sameFileOpen) {
        const int scroll = ui->mdEditor->verticalScrollBar()->value();
        ui->mdEditor->loadFromMdFile(currentFileNote);
        ui->mdEditor->verticalScrollBar()->setValue(scroll);
    }
    statusBar()->showMessage(tr("Command result added to %1.").arg(shownName), 4000);
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
        // Une note markdown (.md) renommée doit garder l'extension .md à la fin de son nom.
        QString finalName = newName;
        if (type == ItemNote && QFileInfo(treeViewOldNameEdit).suffix().compare(QStringLiteral("md"), Qt::CaseInsensitive) == 0)
            finalName = ensureMdExtension(newName);
        const QString newPath = QFileInfo(treeViewOldNameEdit).absolutePath() + "/" + finalName;
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
    // v2.5.0 : l'aperçu est éditable -> la fiche qu'on quitte est d'abord enregistrée si elle a
    // été modifiée (y compris quand on re-clique la même fiche, qui serait sinon rechargée
    // depuis le disque en perdant les modifications).
    savePreviewIfModified();
    ui->mdPreview->loadFromMdFile(path);
    previewFilePath = path;

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
    if (type == ItemLibraryNote)
        appendLibraryNoteFile(item->data(0, RolePath).toString());
}

bool MainWindow::appendLibraryNoteFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    const QString content = in.readAll();

    // Les images référencées (![[x.png]], relatives au dossier de la bibliothèque) sont copiées
    // dans le dossier de la note : copier le dossier des notes ne fait ainsi perdre aucune image.
    const QString withImages = MarkdownEdit::copyReferencedImages(
        substitutePlaceholders(content), QFileInfo(path).absolutePath(), ui->mdEditor->imageBaseFolder());
    ui->mdEditor->appendMarkdownText(withImages);
    return true;
}

void MainWindow::onExportReportDocx()
{
    // v2.2.0 : ouvre la fenêtre de rédaction du rapport (arbre des notes | éditeur Markdown | éditeur Word)
    // pour le dossier sélectionné dans l'arbre du projet, ou pour le projet entier si aucun dossier n'est
    // sélectionné. Le rapport est le fichier report.docx situé à la racine de ce dossier.
    QString folder = currentProject;
    if (QTreeWidgetItem *item = ui->treeProject->currentItem()) {
        int type = -1;
        const QString path = readTreeviewPath(item, &type);
        if (type == ItemFolder && !path.isEmpty())
            folder = path;
    }
    if (folder.isEmpty() || !QDir(folder).exists()) {
        QMessageBox::information(this, tr("Report"), tr("Please open a project first."));
        return;
    }

    ReportWindow window(folder, this);
    if (!window.initialize())
        return;
    window.setWindowState(Qt::WindowMaximized);
    window.exec();
}

void MainWindow::onInsertFolderNotes()
{
    // v2.0.0 : liste les notes du dossier sélectionné (sous-dossiers compris), laisse choisir
    // lesquelles garder (les noms commençant par "_" = fausses pistes sont décochés) et leur ordre,
    // puis les insère à la ligne suivant le curseur de la note ouverte (le gabarit du rapport).
    QTreeWidgetItem *item = ui->treeProject->currentItem();
    int type = -1;
    const QString folder = item ? readTreeviewPath(item, &type) : QString();
    if (!item || type != ItemFolder) {
        QMessageBox::information(this, tr("Insert notes"), tr("Please select a folder in the project tree."));
        return;
    }
    if (currentFileNote.isEmpty()) {
        QMessageBox::information(this, tr("Insert notes"),
            tr("No note is open. Please open the report template first, then try again."));
        return;
    }
    if (ui->mdEditor->isNoteModified())
        ui->mdEditor->saveToMdFile(currentFileNote);

    InsertNotesDialog dlg(folder, QFileInfo(currentFileNote).canonicalFilePath(), this);
    if (!dlg.hasNotes()) {
        QMessageBox::information(this, tr("Insert notes"), tr("No note was found in this folder."));
        return;
    }
    if (dlg.exec() != QDialog::Accepted)
        return;

    const bool titles = dlg.addTitles();
    const int level = dlg.titleLevel();
    const int shift = titles ? level : level - 1; // les titres de la note passent sous le titre ajouté
    const QString destFolder = ui->mdEditor->imageBaseFolder();

    QStringList parts;
    for (const QString &file : dlg.selectedFiles()) {
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        QTextStream in(&f);
        in.setEncoding(QStringConverter::Utf8);
        QString content = in.readAll();
        content.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        if (content.trimmed().isEmpty())
            continue;

        content = shiftMarkdownHeadings(content, shift);
        // Les images de la note sont copiées dans le dossier du rapport (cf. v1.7.0).
        content = MarkdownEdit::copyReferencedImages(content, QFileInfo(file).absolutePath(), destFolder);
        while (content.startsWith(QLatin1Char('\n')))
            content.remove(0, 1);
        while (content.endsWith(QLatin1Char('\n')) || content.endsWith(QLatin1Char(' ')))
            content.chop(1);

        QString block;
        if (titles) {
            QString rel = QDir(folder).relativeFilePath(file);
            const int dot = rel.lastIndexOf(QLatin1Char('.'));
            if (dot > 0)
                rel.truncate(dot);
            block = QString(level, QLatin1Char('#')) + QLatin1Char(' ') + rel + QStringLiteral("\n\n");
        }
        parts << block + content;
    }

    if (parts.isEmpty() || !ui->mdEditor->insertMarkdownOnNextLine(parts.join(QStringLiteral("\n\n")))) {
        QMessageBox::information(this, tr("Insert notes"), tr("Nothing was inserted (the selected notes are empty)."));
        return;
    }
    statusBar()->showMessage(tr("%1 note(s) inserted.").arg(parts.size()), 4000);
}

void MainWindow::onToggleReportExclusion()
{
    // v2.0.0 : marque une note/un dossier comme "à ne pas mettre dans le rapport" (fausse piste)
    // en ajoutant le préfixe "_" à son nom, ou le retire s'il y est déjà.
    QTreeWidgetItem *item = ui->treeProject->currentItem();
    int type = -1;
    const QString oldPath = item ? QDir::cleanPath(readTreeviewPath(item, &type)) : QString();
    if (oldPath.isEmpty() || (type != ItemFolder && type != ItemNote))
        return;
    if (!item->parent()) {
        QMessageBox::information(this, tr("Report"), tr("The project root folder cannot be excluded."));
        return;
    }

    const QFileInfo info(oldPath);
    const QString name = info.fileName();
    const QString newName = name.startsWith(QLatin1Char('_')) ? name.mid(1) : QStringLiteral("_") + name;
    if (newName.isEmpty())
        return;
    const QString newPath = info.absolutePath() + QLatin1Char('/') + newName;
    if (QFileInfo::exists(newPath)) {
        QMessageBox::critical(this, tr("Report"),
            tr("Impossible : an element named \"%1\" already exists here.").arg(newName));
        return;
    }

    // La note ouverte est enregistrée avant tout changement de chemin.
    if (ui->mdEditor->isNoteModified() && !currentFileNote.isEmpty())
        ui->mdEditor->saveToMdFile(currentFileNote);

    const bool ok = (type == ItemFolder) ? QDir().rename(oldPath, newPath) : QFile::rename(oldPath, newPath);
    if (!ok) {
        QMessageBox::critical(this, tr("Report"), tr("Error while renaming."));
        return;
    }

    // Si la note ouverte est (dans) l'élément renommé, on suit son nouveau chemin.
    if (!currentFileNote.isEmpty()) {
        const QString cur = QDir::cleanPath(currentFileNote);
        QString newCur;
        if (cur == oldPath)
            newCur = newPath;
        else if (cur.startsWith(oldPath + QLatin1Char('/')))
            newCur = newPath + cur.mid(oldPath.size());
        if (!newCur.isEmpty()) {
            currentFileNote = newCur;
            ui->mdEditor->setImageBaseFolder(QFileInfo(newCur).absolutePath());
            ui->tabNote->setTabText(0, QFileInfo(newCur).fileName());
        }
    }

    scanProjectDisk(currentProject);
    statusBar()->showMessage(newName.startsWith(QLatin1Char('_'))
        ? tr("Excluded from the report : %1").arg(newName)
        : tr("Included in the report : %1").arg(newName), 4000);
}

// ======================= Serveur HTTP de distribution de fichiers (v2.3.0) =======================

void MainWindow::onHttpServerConnect()
{
    QTreeWidgetItem *item = ui->treeProject->currentItem();
    int type = -1;
    const QString path = item ? readTreeviewPath(item, &type) : QString();
    if (!item || type != ItemFolder || path.isEmpty())
        return;

    const QString key = QDir::cleanPath(path);
    if (httpServers.contains(key)) {
        QMessageBox::information(this, tr("http server connect"),
            tr("A HTTP server is already listening for this folder (port %1).").arg(httpServers.value(key)->port()));
        return;
    }
    if (!QDir(key).exists()) {
        QMessageBox::critical(this, tr("http server connect"), tr("This folder doesn't exist anymore !"));
        return;
    }

    const QString folderName = QFileInfo(key).fileName().isEmpty() ? key : QFileInfo(key).fileName();
    int port = 80; // port par défaut
    for (;;) {
        bool ok = false;
        port = QInputDialog::getInt(this, tr("http server connect"),
                                    tr("Listening port for the folder \"%1\" :").arg(folderName),
                                    port, 1, 65535, 1, &ok);
        if (!ok)
            return; // demande annulée

        QString reason;
        HttpFileServer *server = nullptr;

        // port déjà pris par un autre serveur HTTP de CyberNote ?
        reason = httpPortConflict(port);

        if (reason.isEmpty()) {
            server = new HttpFileServer(key, this);
            if (server->start(static_cast<quint16>(port))) {
                // l'écoute est réellement active : on enregistre, puis l'icône et ":port" changent
                const quint16 listenPort = server->port();
                httpServers.insert(key, server);
                connect(server, &HttpFileServer::requestServed, this,
                        [this, listenPort](const QString &peer, const QString &method, const QString &reqPath, int status) {
                    showHttpRequest(listenPort, peer, method, reqPath, status);
                });
                refreshHttpServerItems();
                statusBar()->showMessage(tr("HTTP server started : \"%1\" is shared on port %2.")
                                             .arg(folderName).arg(listenPort), 5000);
                return;
            }

            reason = httpListenFailureReason(server, port);
            delete server;
        }

        // écoute impossible : on prévient l'utilisateur, qui choisit un autre port ou annule
        QMessageBox box(QMessageBox::Warning, tr("http server connect"),
                        reason + QStringLiteral("\n\n") + tr("Do you want to choose another listening port ?"),
                        QMessageBox::Retry | QMessageBox::Cancel, this);
        box.button(QMessageBox::Retry)->setText(tr("Choose another port"));
        if (box.exec() != QMessageBox::Retry)
            return;

        port = (port < 1024) ? 8080 : (port < 65535 ? port + 1 : 8080); // proposition pour la prochaine saisie
    }
}

void MainWindow::onHttpServerDisconnect()
{
    QTreeWidgetItem *item = ui->treeProject->currentItem();
    int type = -1;
    const QString path = item ? readTreeviewPath(item, &type) : QString();
    if (!item || type != ItemFolder)
        return;
    stopHttpServer(QDir::cleanPath(path));
}

void MainWindow::stopHttpServer(const QString &folderKey)
{
    HttpFileServer *server = httpServers.take(folderKey);
    if (!server)
        return;
    const quint16 port = server->port();
    server->stop(); // ferme l'écoute et les connexions en cours
    delete server;
    refreshHttpServerItems(); // rend au dossier son icône et son nom d'origine
    statusBar()->showMessage(tr("HTTP server stopped (port %1).").arg(port), 5000);
}

void MainWindow::stopAllHttpServers()
{
    if (toolsServer) { // v2.4.0
        toolsServer->stop();
        delete toolsServer;
        toolsServer = nullptr;
    }
    const QList<HttpFileServer *> servers = httpServers.values();
    httpServers.clear();
    for (HttpFileServer *server : servers) {
        server->stop();
        delete server;
    }
}

void MainWindow::refreshHttpServerItems()
{
    // 1) dossiers actuellement présents dans l'arbre
    QList<QTreeWidgetItem *> folders;
    QSet<QString> present;
    for (QTreeWidgetItemIterator it(ui->treeProject); *it; ++it) {
        if ((*it)->data(0, RoleType).toInt() == ItemFolder) {
            folders << *it;
            present.insert(QDir::cleanPath((*it)->data(0, RolePath).toString()));
        }
    }

    // 2) un dossier supprimé, renommé, déplacé ou sorti du projet ouvert ne peut plus être servi
    const QStringList keys = httpServers.keys();
    for (const QString &key : keys) {
        if (present.contains(key))
            continue;
        HttpFileServer *server = httpServers.take(key);
        const quint16 port = server->port();
        server->stop();
        delete server;
        statusBar()->showMessage(tr("HTTP server stopped (port %1) : the folder \"%2\" is no longer in the project.")
                                     .arg(port).arg(QFileInfo(key).fileName()), 8000);
    }

    // 3) icône + ":port" pour les dossiers servis ; nom et icône d'origine pour les autres
    const QSignalBlocker blocker(ui->treeProject);
    for (QTreeWidgetItem *item : std::as_const(folders)) {
        const QString key = QDir::cleanPath(item->data(0, RolePath).toString());
        const auto found = httpServers.constFind(key);
        if (found != httpServers.cend()) {
            if (!item->data(0, RoleBaseText).isValid())
                item->setData(0, RoleBaseText, item->text(0));
            const quint16 port = found.value()->port();
            item->setText(0, item->data(0, RoleBaseText).toString() + QLatin1Char(':') + QString::number(port));
            item->setIcon(0, QIcon(QStringLiteral(":/icons/folder_http.svg")));
            item->setToolTip(0, tr("HTTP server listening on port %1").arg(port));
        } else if (item->data(0, RoleBaseText).isValid()) {
            item->setText(0, item->data(0, RoleBaseText).toString());
            item->setData(0, RoleBaseText, QVariant());
            item->setIcon(0, QIcon(QStringLiteral(":/icons/folder_plain.svg")));
            item->setToolTip(0, QString());
        }
    }
}

QString MainWindow::httpPortConflict(int port) const
{
    for (auto it = httpServers.cbegin(); it != httpServers.cend(); ++it) {
        if (it.value()->port() == port)
            return tr("The port %1 is already used by the HTTP server of the folder \"%2\".")
                       .arg(port).arg(QFileInfo(it.key()).fileName());
    }
    if (toolsServer && toolsServer->port() == port)
        return tr("The port %1 is already used by the HTTP server of the \"tools\" tab.").arg(port);
    return QString();
}

QString MainWindow::httpListenFailureReason(HttpFileServer *server, int port) const
{
    switch (server->serverError()) {
    case QAbstractSocket::AddressInUseError:
        return tr("The port %1 is already in use by another program.").arg(port);
    case QAbstractSocket::SocketAccessError:
        return tr("Permission denied for the port %1 (the ports below 1024 normally require administrator privileges).").arg(port);
    default:
        return tr("Impossible to listen on the port %1 : %2").arg(port).arg(server->errorString());
    }
}

void MainWindow::showHttpRequest(quint16 port, const QString &peer, const QString &method,
                                 const QString &path, int status)
{
    // concaténation (et non arg() en cascade) : le chemin demandé vient du réseau
    statusBar()->showMessage(QLatin1Char(':') + QString::number(port) + QStringLiteral("  ")
                                 + method + QLatin1Char(' ') + path.left(120) + QStringLiteral(" -> ")
                                 + QString::number(status) + QStringLiteral("  (") + peer + QLatin1Char(')'), 6000);
}

// ======================= Onglet "tools" (v2.4.0) =======================

void MainWindow::addDirectoryToolsToTree(const QString &directory, QTreeWidgetItem *parentItem, int depth)
{
    const QFileInfoList entries = QDir(directory).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot,
                                                                QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &entry : entries) {
        const QString fullPath = entry.absoluteFilePath();
        if (entry.isDir()) {
            QTreeWidgetItem *node = addTreeChild(parentItem, ui->treeTools, entry.fileName(), fullPath, ItemFolder);
            // pas de descente dans un lien symbolique (boucle possible) ni au-delà de 32 niveaux
            if (!entry.isSymLink() && depth < 32)
                addDirectoryToolsToTree(fullPath, node, depth + 1);
        } else {
            addTreeChild(parentItem, ui->treeTools, entry.fileName(), fullPath, ItemToolFile);
        }
    }
}

void MainWindow::scanToolsDisk()
{
    const QString toolsRoot = QDir(pwdApplication + QStringLiteral("tools")).absolutePath();

    // on mémorise l'état de l'arbre (dossiers dépliés, sélection) pour le retrouver après la reconstruction
    QSet<QString> expanded;
    for (QTreeWidgetItemIterator it(ui->treeTools); *it; ++it)
        if ((*it)->isExpanded())
            expanded.insert((*it)->data(0, RolePath).toString());
    const QString selected = ui->treeTools->currentItem()
                                 ? ui->treeTools->currentItem()->data(0, RolePath).toString() : QString();

    {
        const QSignalBlocker blocker(ui->treeTools);
        ui->treeTools->clear();
        if (QDir(toolsRoot).exists()) {
            auto *rootItem = new QTreeWidgetItem(ui->treeTools);
            rootItem->setText(0, toolsRoot);
            rootItem->setData(0, RolePath, toolsRoot);
            rootItem->setData(0, RoleType, ItemFolder);
            rootItem->setIcon(0, QIcon(QStringLiteral(":/icons/folder_plain.svg")));
            addDirectoryToolsToTree(toolsRoot, rootItem, 0);

            for (QTreeWidgetItemIterator it(ui->treeTools); *it; ++it) {
                const QString path = (*it)->data(0, RolePath).toString();
                if (!(*it)->parent() || expanded.contains(path))
                    (*it)->setExpanded(true);
                if (!selected.isEmpty() && path == selected)
                    ui->treeTools->setCurrentItem(*it);
            }
        }
    }

    // le dossier servi a disparu : le serveur ne peut plus continuer
    if (toolsServer && !QDir(toolsServedPath).exists()) {
        stopToolsServer();
        statusBar()->showMessage(tr("The tools HTTP server was stopped : the served folder no longer exists."), 8000);
    }
}

void MainWindow::updateToolsConnectionUi()
{
    const bool connected = (toolsServer != nullptr);
    const QIcon icon(connected ? QStringLiteral(":/icons/status_connected.svg")
                               : QStringLiteral(":/icons/status_disconnected.svg"));

    ui->toolsConnectButton->setText(connected ? tr("unconnect") : tr("connect"));
    ui->toolsConnectButton->setIcon(icon);
    ui->toolsPortSpin->setEnabled(!connected);

    const int index = ui->tabProject->indexOf(ui->tabToolsPage);
    if (index >= 0) {
        ui->tabProject->setTabIcon(index, icon);
        ui->tabProject->setTabText(index, connected ? tr("tools") + QLatin1Char(':') + QString::number(toolsServer->port())
                                                    : tr("tools"));
        ui->tabProject->setTabToolTip(index, connected
            ? tr("HTTP server listening on port %1 for \"%2\"").arg(toolsServer->port()).arg(toolsServedPath)
            : tr("Not connected"));
    }
}

void MainWindow::stopToolsServer()
{
    if (toolsServer) {
        toolsServer->stop(); // ferme l'écoute et les connexions en cours
        delete toolsServer;
        toolsServer = nullptr;
    }
    updateToolsConnectionUi();
}

void MainWindow::onToolsConnectClicked()
{
    if (toolsServer) { // déjà connecté : le bouton ("unconnect") déconnecte
        stopToolsServer();
        statusBar()->showMessage(tr("Tools HTTP server stopped."), 5000);
        return;
    }

    const QString toolsRoot = QDir(pwdApplication + QStringLiteral("tools")).absolutePath();
    if (!QDir(toolsRoot).exists()) {
        QMessageBox::critical(this, tr("tools"), tr("The folder \"%1\" doesn't exist !").arg(toolsRoot));
        return;
    }

    // dossier servi : le dossier sélectionné dans l'arbre tools (pour un fichier : son dossier),
    // ou à défaut la racine du dossier tools
    QString folder = toolsRoot;
    if (QTreeWidgetItem *item = ui->treeTools->currentItem()) {
        QString path = item->data(0, RolePath).toString();
        if (item->data(0, RoleType).toInt() != ItemFolder)
            path = QFileInfo(path).absolutePath();
        if (!path.isEmpty() && QDir(path).exists())
            folder = path;
    }
    folder = QDir::cleanPath(folder);

    const int port = ui->toolsPortSpin->value();
    QString reason = httpPortConflict(port);
    if (reason.isEmpty()) {
        auto *server = new HttpFileServer(folder, this);
        if (server->start(static_cast<quint16>(port))) {
            const quint16 listenPort = server->port();
            toolsServer = server;
            toolsServedPath = folder;
            connect(server, &HttpFileServer::requestServed, this,
                    [this, listenPort](const QString &peer, const QString &method, const QString &reqPath, int status) {
                showHttpRequest(listenPort, peer, method, reqPath, status);
            });
            updateToolsConnectionUi();
            statusBar()->showMessage(tr("Tools HTTP server started : \"%1\" is shared on port %2.")
                                         .arg(QFileInfo(folder).fileName()).arg(listenPort), 5000);
            return;
        }
        reason = httpListenFailureReason(server, port);
        delete server;
    }
    QMessageBox::critical(this, tr("tools"), reason);
}

void MainWindow::onLibraryCopyToNote()
{
    // Ctrl+Alt+C sur un fichier de la bibliothèque : même résultat que le double-clic
    // (contenu ajouté à la fin de la note ouverte, paramètres <...> substitués).
    QTreeWidgetItem *item = ui->treeLibrary->currentItem();
    if (!item || item->data(0, RoleType).toInt() != ItemLibraryNote)
        return;
    if (currentFileNote.isEmpty()) {
        QMessageBox::information(this, tr("Library"),
            tr("No note is open. Please open a note first, then try again."));
        return;
    }
    if (appendLibraryNoteFile(item->data(0, RolePath).toString()))
        statusBar()->showMessage(tr("Library note added at the end of the note."), 3000);
}

bool MainWindow::savePreviewIfModified()
{
    // v2.5.0 : enregistre la fiche affichée dans l'onglet Preview si elle a été modifiée.
    // Rien n'est écrit si le fichier a disparu entre-temps (on ne le recrée pas).
    if (previewFilePath.isEmpty() || !ui->mdPreview->isNoteModified())
        return false;
    if (!QFileInfo(previewFilePath).isFile())
        return false;
    ui->mdPreview->saveToMdFile(previewFilePath);
    statusBar()->showMessage(tr("Library note saved : %1").arg(QFileInfo(previewFilePath).fileName()), 3000);
    return true;
}

void MainWindow::onLibraryPreviewCopyToNote(const QString &markdown)
{
    // Ctrl+Alt+C dans l'aperçu de la bibliothèque : la sélection est insérée à la ligne
    // suivant la position du curseur de la note en cours d'édition (paramètres <...> substitués).
    if (currentFileNote.isEmpty()) {
        QMessageBox::information(this, tr("Library"),
            tr("No note is open. Please open a note first, then try again."));
        return;
    }
    // Les images référencées par la sélection sont copiées dans le dossier de la note (cf. plus haut).
    const QString withImages = MarkdownEdit::copyReferencedImages(
        substitutePlaceholders(markdown), ui->mdPreview->imageBaseFolder(), ui->mdEditor->imageBaseFolder());
    if (ui->mdEditor->insertMarkdownOnNextLine(withImages))
        statusBar()->showMessage(tr("Selection inserted into the note."), 3000);
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
    case ItemToolFile:
        node->setIcon(0, QIcon(QStringLiteral(":/icons/file_plain.svg")));
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

        // v2.0.0 : un nom commençant par "_" = exclu des rapports (fausse piste) -> affiché en gris
        if (node && name.startsWith(QLatin1Char('_')))
            node->setForeground(0, QBrush(Qt::gray));

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

    // v2.3.0 : l'arbre vient d'être reconstruit -> on réapplique l'icône et ":port" des dossiers servis
    refreshHttpServerItems();
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

QString MainWindow::readTreeviewMachineFolderPath(QTreeWidgetItem *item)
{
    // ~ l'arborescence Project a la forme <projet> / <machine> / <dossier> / ... :
    // l'item racine (sans parent) est <projet>, ses enfants directs sont les
    // dossiers <machine>. Quel que soit l'élément sélectionné (un <dossier>
    // profondément imbriqué, un fichier, ou <machine> elle-même), on remonte
    // jusqu'à ce niveau <machine> pour que le terminal s'ouvre toujours sur
    // <projet>/<machine>/.
    if (!item)
        return QString();

    QTreeWidgetItem *machineItem = item;
    while (machineItem->parent() && machineItem->parent()->parent())
        machineItem = machineItem->parent();

    // Si l'élément remonté est la racine du projet elle-même (aucun niveau
    // <machine> au-dessus, ex. rien de pertinent sélectionné), il n'y a pas de
    // dossier machine à retourner.
    if (!machineItem->parent())
        return QString();

    return machineItem->data(0, RolePath).toString();
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
