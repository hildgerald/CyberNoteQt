#include "netcattermdialog.h"
#include "./ui_netcattermdialog.h"
#include "terminalwidget.h"

#include <QCloseEvent>
#include <QInputDialog>
#include <QLineEdit>
#include <QIcon>

NetcatTermDialog::NetcatTermDialog(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::NetcatTermDialog)
{
    // Qt::Window : force une vraie fenêtre séparée, cf. commentaire équivalent dans
    // LinuxTermDialog::LinuxTermDialog.
    setWindowFlags(Qt::Window);

    ui->setupUi(this);

    popupMenu = new QMenu(this);
    popupMenu->addAction(ui->actionCopy);
    popupMenu->addAction(ui->actionPaste);
    popupMenu->addAction(ui->actionCopyImage);
    popupMenu->addSeparator();
    popupMenu->addAction(ui->actionDeselect);
    popupMenu->addSeparator();
    popupMenu->addAction(ui->actionRenameTerminal);

    ui->terminal->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->terminal, &QWidget::customContextMenuRequested,
            this, &NetcatTermDialog::onEditorContextMenuRequested);

    connect(ui->actionCopy,           &QAction::triggered, ui->terminal, &TerminalWidget::copySelection);
    connect(ui->actionPaste,          &QAction::triggered, ui->terminal, &TerminalWidget::pasteFromClipboard);
    connect(ui->actionCopyImage,      &QAction::triggered, ui->terminal, &TerminalWidget::copySelectionAsImage);
    connect(ui->actionDeselect,       &QAction::triggered, ui->terminal, &TerminalWidget::clearSelection);
    connect(ui->actionRenameTerminal, &QAction::triggered, this, &NetcatTermDialog::onRenameRequested);

    connect(ui->terminal, &TerminalWidget::shellFinished, this, &NetcatTermDialog::onShellFinished);

    connect(ui->buttonConnect, &QPushButton::clicked, this, &NetcatTermDialog::onConnectButtonClicked);

    // État initial : déconnecté, port par défaut 443 (déjà fixé dans le .ui), champ éditable.
    setConnectedState(false);
}

NetcatTermDialog::~NetcatTermDialog()
{
    // Coupe proprement netcat s'il tourne encore avant destruction du widget.
    ui->terminal->stopProgram();
    delete ui;
}

void NetcatTermDialog::onConnectButtonClicked()
{
    if (!m_connected) {
        // --- Déconnecté -> Connecter : démarre netcat en écoute sur le port choisi ---
        const int port = ui->spinBoxPort->value();

        ui->terminal->resetTerminal(); // repart d'un écran propre à chaque nouvelle écoute
        // -l : écoute ; -v : verbeux (affiche la connexion entrante) ; -n : pas de
        // résolution DNS ; -p : port d'écoute (syntaxe netcat-traditional/GNU, présent
        // par défaut sur les distributions Linux type Kali/Debian visées par CyberNoteQt).
        ui->terminal->startProgram(QStringLiteral("nc"),
                                    { QStringLiteral("-lvnp"), QString::number(port) });
        setConnectedState(true);
        ui->terminal->setFocus();
    } else {
        // --- Connecté -> Déconnecter : arrête netcat, redonne la main sur le port ---
        ui->terminal->stopProgram();
        setConnectedState(false);
    }
}

void NetcatTermDialog::onShellFinished()
{
    // ~ netcat s'est terminé de lui-même (connexion entrante fermée par le correspondant,
    // "nc" introuvable, erreur de bind sur le port...) : on repasse en état déconnecté au
    // lieu de fermer la fenêtre (contrairement à LinuxTermDialog), pour permettre de
    // relancer l'écoute directement.
    if (m_connected)
        setConnectedState(false);
}

void NetcatTermDialog::setConnectedState(bool connected)
{
    m_connected = connected;

    ui->spinBoxPort->setEnabled(!connected);          // champ grisé/non modifiable une fois connecté
    ui->buttonConnect->setText(connected ? tr("Disconnect") : tr("Connect"));
    updateStatusIcon();
}

void NetcatTermDialog::updateStatusIcon()
{
    const QString iconPath = m_connected ? QStringLiteral(":/icons/status_connected.svg")
                                          : QStringLiteral(":/icons/status_disconnected.svg");
    ui->labelStatusIcon->setPixmap(QIcon(iconPath).pixmap(20, 20));
    ui->labelStatusIcon->setToolTip(m_connected ? tr("Connected") : tr("Disconnected"));
}

void NetcatTermDialog::onEditorContextMenuRequested(const QPoint &pos)
{
    ui->actionCopy->setEnabled(ui->terminal->hasSelection());
    ui->actionCopyImage->setEnabled(ui->terminal->hasSelection());
    popupMenu->exec(ui->terminal->mapToGlobal(pos));
}

void NetcatTermDialog::onRenameRequested()
{
    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Rename Terminal"),
                                                    tr("Name :"), QLineEdit::Normal,
                                                    windowTitle(), &ok);
    if (ok && !newName.trimmed().isEmpty())
        setFixedTitle(newName);
}

void NetcatTermDialog::setFixedTitle(const QString &title)
{
    setWindowTitle(title);
    m_titleLocked = true;
}
