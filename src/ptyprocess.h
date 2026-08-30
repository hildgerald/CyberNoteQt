#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QStringList>

class QSocketNotifier;

/**
 * Pty
 *
 * Wrapper autour d'un vrai pseudo-terminal POSIX (forkpty). Contrairement à
 * QProcess (simples pipes), le programme lancé voit un vrai terminal
 * (isatty() vrai) : couleurs, prompt interactif, job control (Ctrl+C/Ctrl+Z),
 * saisie de mot de passe sudo sans echo, et applications plein écran
 * (vim, htop, nano, less...) fonctionnent normalement.
 */
class Pty : public QObject
{
    Q_OBJECT

public:
    explicit Pty(QObject *parent = nullptr);
    ~Pty() override;

    // Démarre `program` (ex: "/bin/bash") avec `args` sur un pty de taille rows x cols.
    // workingDir (optionnel) : dossier de démarrage du processus enfant (vide = hérite
    // du dossier courant du processus CyberNoteQt, comportement précédent).
    bool start(const QString &program, const QStringList &args, int rows, int cols,
               const QString &workingDir = QString());

    void writeData(const QByteArray &data);
    void resize(int rows, int cols);

    bool isRunning() const { return masterFd >= 0; }
    qint64 pid() const { return childPid; }

    void terminate(); // envoie SIGHUP/SIGKILL au groupe de processus et ferme le pty

signals:
    void readyRead(const QByteArray &data);
    void finished();

private slots:
    void onMasterReadyRead();

private:
    int masterFd = -1;
    pid_t childPid = -1;
    QSocketNotifier *notifier = nullptr;
};
