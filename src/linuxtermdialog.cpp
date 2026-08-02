#include "linuxtermdialog.h"
#include "./ui_linuxtermdialog.h"
#include "terminalwidget.h"

#include <QCloseEvent>
#include <QInputDialog>
#include <QLineEdit>

LinuxTermDialog::LinuxTermDialog(QWidget *parent, const QString &workingDir)
    : QWidget(parent)
    , ui(new Ui::LinuxTermDialog)
{
    // Qt::Window : force une vraie fenêtre séparée (barre de titre, icônes
    // réduire/agrandir/fermer, entrée dans le gestionnaire de fenêtres) même si
    // un widget parent est fourni. Sans ce flag, un QWidget construit avec un
    // parent (new LinuxTermDialog(this)) est traité comme un widget ENFANT
    // intégré dans la fenêtre du parent au lieu d'une fenêtre à part -- c'est
    // exactement le bug observé. Garder le parent (plutôt que nullptr) reste
    // utile : la fenêtre du terminal se ferme automatiquement si MainWindow est
    // détruite, et peut s'empiler correctement au-dessus d'elle.
    setWindowFlags(Qt::Window);

    ui->setupUi(this);

    popupMenu = new QMenu(this);
    popupMenu->addAction(ui->actionCopy);
    popupMenu->addAction(ui->actionPaste);
    popupMenu->addAction(ui->actionCopyImage);
    popupMenu->addSeparator();
    popupMenu->addAction(ui->actionDeselect);
    popupMenu->addSeparator();
    popupMenu->addAction(ui->actionRenameTerminal); // ~ nouveau : renommer un terminal déjà ouvert

    ui->terminal->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->terminal, &QWidget::customContextMenuRequested,
            this, &LinuxTermDialog::onEditorContextMenuRequested);

    connect(ui->actionCopy,          &QAction::triggered, ui->terminal, &TerminalWidget::copySelection);
    connect(ui->actionPaste,         &QAction::triggered, ui->terminal, &TerminalWidget::pasteFromClipboard);
    connect(ui->actionCopyImage,     &QAction::triggered, ui->terminal, &TerminalWidget::copySelectionAsImage);
    connect(ui->actionDeselect,      &QAction::triggered, ui->terminal, &TerminalWidget::clearSelection);
    connect(ui->actionRenameTerminal,&QAction::triggered, this, &LinuxTermDialog::onRenameRequested);

    connect(ui->terminal, &TerminalWidget::titleChanged, this, [this](const QString &title) {
        if (!m_titleLocked && !title.isEmpty())
            setWindowTitle(title);
    });
    connect(ui->terminal, &TerminalWidget::shellFinished, this, &LinuxTermDialog::onShellFinished);

    ui->terminal->startShell(workingDir); // ~ TFLinuxTerm.FormCreate -> CreateShell, mais sur un vrai pty
    ui->terminal->setFocus();
}

LinuxTermDialog::~LinuxTermDialog()
{
    delete ui;
}

void LinuxTermDialog::onEditorContextMenuRequested(const QPoint &pos)
{
    ui->actionCopy->setEnabled(ui->terminal->hasSelection());
    ui->actionCopyImage->setEnabled(ui->terminal->hasSelection());
    popupMenu->exec(ui->terminal->mapToGlobal(pos));
}

void LinuxTermDialog::onShellFinished()
{
    // ~ TFLinuxTerm.CommandFinished : le shell interactif s'est terminé (exit/Ctrl+D) -> on ferme la fenêtre
    close();
}

void LinuxTermDialog::onRenameRequested()
{
    // ~ nouveau : renommer un terminal déjà ouvert (demandé en complément du nom
    // saisi à la création dans MainWindow::onNewLinuxTerm).
    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Rename Terminal"),
                                                    tr("Name :"), QLineEdit::Normal,
                                                    windowTitle(), &ok);
    if (ok && !newName.trimmed().isEmpty())
        setFixedTitle(newName);
}

void LinuxTermDialog::sendCommand(const QString &command)
{
    ui->terminal->sendCommand(command);
    ui->terminal->setFocus();
    raise();
    activateWindow();
}

void LinuxTermDialog::setFixedTitle(const QString &title)
{
    setWindowTitle(title);
    m_titleLocked = true;
}
