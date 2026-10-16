#pragma once

#include <QWidget>
#include <QMenu>

QT_BEGIN_NAMESPACE
namespace Ui { class NetcatTermDialog; }
QT_END_NAMESPACE

/**
 * NetcatTermDialog
 *
 * Fenêtre "Start Netcat Term" (menu Tools). Contient :
 *  - une barre supérieure avec un bouton Connecter/Déconnecter, une icône d'état
 *    de connexion, et un champ de saisie du port d'écoute (1-65535, 443 par défaut) ;
 *  - un terminal PTY (TerminalWidget, le même composant que LinuxTermDialog) qui,
 *    une fois connecté, exécute netcat en mode écoute ("nc -lvnp <port>") sur ce pty :
 *    la sortie de netcat (bannière, données reçues de la connexion entrante...)
 *    s'affiche donc nativement dans le terminal, et tout ce qui est tapé au clavier
 *    est envoyé à netcat (donc au correspondant une fois la connexion établie).
 *
 * États :
 *  - Déconnecté (par défaut) : champ port éditable, bouton "Connect", icône grise.
 *  - Connecté : champ port grisé (non modifiable), bouton "Disconnect", icône verte,
 *    netcat écoute sur le port choisi.
 *
 * Si netcat se termine de lui-même (ex: connexion entrante refusée puis nc quitte,
 * ou "nc" introuvable), la fenêtre repasse automatiquement en état déconnecté plutôt
 * que de se fermer (contrairement à LinuxTermDialog dont la fin du shell ferme la
 * fenêtre) : l'utilisateur peut relancer l'écoute sans avoir à rouvrir la fenêtre.
 */
class NetcatTermDialog : public QWidget
{
    Q_OBJECT

public:
    explicit NetcatTermDialog(QWidget *parent = nullptr);
    ~NetcatTermDialog() override;

    // Fixe le titre de la fenêtre de façon durable (~ LinuxTermDialog::setFixedTitle).
    void setFixedTitle(const QString &title);

private slots:
    void onConnectButtonClicked();
    void onShellFinished();       // netcat s'est terminé (connexion fermée, erreur...)
    void onEditorContextMenuRequested(const QPoint &pos);
    void onRenameRequested();

private:
    Ui::NetcatTermDialog *ui;
    QMenu *popupMenu = nullptr;
    bool m_titleLocked = false;
    bool m_connected = false; // état de connexion courant (~ affiché par le bouton + l'icône)

    void setConnectedState(bool connected);
    void updateStatusIcon();
};
