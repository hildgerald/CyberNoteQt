#pragma once

#include <QWidget>
#include <QMenu>
#include <QImage>

QT_BEGIN_NAMESPACE
namespace Ui { class LinuxTermDialog; }
QT_END_NAMESPACE

/**
 * LinuxTermDialog
 *
 * Portage de TFLinuxTerm (ulinuxterm.pas), mais adossé à un vrai pty
 * (TerminalWidget/Pty/TerminalScreen) plutôt qu'à un TProcess + filtrage ANSI
 * maison. Conséquence importante : la grande majorité de la logique métier du
 * Pascal d'origine devient inutile, le shell (bash réel dans un vrai terminal)
 * la gère nativement :
 *   - PrintPrompt / FriendlyPath        -> le PS1 de bash s'affiche tout seul
 *   - NavigateHistory                   -> readline gère l'historique (flèches ↑/↓)
 *   - RewriteSudoCommand / mot de passe -> sudo détecte le pty et demande le
 *                                          mot de passe sans echo, normalement
 *   - RewriteGuiCommand                 -> une appli graphique lancée depuis le
 *                                          terminal se comporte normalement
 *   - ClearScreenBuiltin                -> la commande "clear" fonctionne nativement
 *
 * Ce qui reste à la charge de cette classe : le menu contextuel (Copy/Paste/
 * Copy Selection As Image/Deselect) et le relais vers TerminalWidget.
 */
class LinuxTermDialog : public QWidget
{
    Q_OBJECT

public:
    explicit LinuxTermDialog(QWidget *parent = nullptr, const QString &workingDir = QString());
    ~LinuxTermDialog() override;

    void sendCommand(const QString &command); // relaie vers TerminalWidget::sendCommand
    // v2.6.0 : relaie vers TerminalWidget::sendCommandCapture (sortie renvoyée par commandFinished)
    bool sendCommandCapture(const QString &command, int jobId);

    // Fixe le titre de la fenêtre de façon durable : le shell (bash) envoie sa
    // propre séquence de titre (OSC) dès qu'il démarre, ce qui écraserait sinon
    // silencieusement le nom choisi ici (bug corrigé : le titre saisi dans
    // MainWindow::onNewLinuxTerm n'apparaissait jamais pour cette raison).
    void setFixedTitle(const QString &title);

signals:
    // v2.6.0 : fin d'une commande lancée par sendCommandCapture (relais de TerminalWidget::commandCaptured)
    void commandFinished(int jobId, int exitCode, const QString &output);
    // Ctrl+Alt+S dans le terminal : image de la sélection à insérer dans la note ouverte
    // (relais de TerminalWidget::selectionImageForNote, câblé par MainWindow).
    void imageForNoteReady(const QImage &image);
    // Ctrl+Alt+C dans le terminal : texte sélectionné à insérer dans la note ouverte
    // (relais de TerminalWidget::selectionTextForNote, câblé par MainWindow).
    void textForNoteReady(const QString &text);

private slots:
    void onEditorContextMenuRequested(const QPoint &pos);
    void onShellFinished();
    void onRenameRequested(); // ~ nouveau : renommer un terminal déjà ouvert (clic droit)

private:
    Ui::LinuxTermDialog *ui;
    QMenu *popupMenu = nullptr;
    bool m_titleLocked = false; // true dès qu'un titre a été fixé explicitement (setFixedTitle)
};
