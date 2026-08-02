#include "ptyprocess.h"

#include <QSocketNotifier>
#include <QDebug>

#include <pty.h>       // forkpty (glibc)
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <cstdlib>
#include <vector>

Pty::Pty(QObject *parent)
    : QObject(parent)
{
}

Pty::~Pty()
{
    terminate();
}

bool Pty::start(const QString &program, const QStringList &args, int rows, int cols,
                 const QString &workingDir)
{
    struct winsize ws;
    ws.ws_row = static_cast<unsigned short>(rows);
    ws.ws_col = static_cast<unsigned short>(cols);
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    int master = -1;
    const pid_t pid = forkpty(&master, nullptr, nullptr, &ws);

    if (pid < 0) {
        qWarning() << "Pty::start: forkpty failed";
        return false;
    }

    if (pid == 0) {
        // --- Processus enfant : devient le shell interactif ---
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);

        if (!workingDir.isEmpty())
            ::chdir(workingDir.toUtf8().constData()); // si échec (dossier invalide), reste sur le cwd hérité

        std::vector<std::string> argStorage;
        argStorage.push_back(program.toStdString());
        for (const QString &a : args)
            argStorage.push_back(a.toStdString());

        std::vector<char *> argv;
        for (auto &s : argStorage)
            argv.push_back(s.data());
        argv.push_back(nullptr);

        execvp(program.toUtf8().constData(), argv.data());
        // Si execvp échoue (binaire introuvable) :
        _exit(127);
    }

    // --- Processus parent ---
    childPid = pid;
    masterFd = master;

    const int flags = fcntl(masterFd, F_GETFL, 0);
    fcntl(masterFd, F_SETFL, flags | O_NONBLOCK);

    notifier = new QSocketNotifier(masterFd, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, &Pty::onMasterReadyRead);

    return true;
}

void Pty::onMasterReadyRead()
{
    char buf[8192];
    QByteArray all;
    while (true) {
        const ssize_t n = ::read(masterFd, buf, sizeof(buf));
        if (n > 0) {
            all.append(buf, static_cast<int>(n));
            if (n < static_cast<ssize_t>(sizeof(buf)))
                break; // probablement plus rien à lire pour l'instant
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break; // rien de plus disponible
        // n == 0 ou erreur définitive (EIO le plus souvent quand le shell se termine)
        if (!all.isEmpty())
            emit readyRead(all);
        if (notifier) {
            notifier->setEnabled(false);
        }
        emit finished();
        return;
    }
    if (!all.isEmpty())
        emit readyRead(all);
}

void Pty::writeData(const QByteArray &data)
{
    if (masterFd < 0)
        return;
    qint64 written = 0;
    while (written < data.size()) {
        const ssize_t n = ::write(masterFd, data.constData() + written, data.size() - written);
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            break;
        }
        written += n;
    }
}

void Pty::resize(int rows, int cols)
{
    if (masterFd < 0)
        return;
    struct winsize ws;
    ws.ws_row = static_cast<unsigned short>(rows);
    ws.ws_col = static_cast<unsigned short>(cols);
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    ioctl(masterFd, TIOCSWINSZ, &ws); // le noyau envoie SIGWINCH au groupe de processus premier plan
}

void Pty::terminate()
{
    if (childPid > 0) {
        kill(-childPid, SIGHUP); // -pid = groupe de processus (setsid côté enfant via forkpty)
        int status = 0;
        waitpid(childPid, &status, WNOHANG);
        childPid = -1;
    }
    if (notifier) {
        notifier->setEnabled(false);
        notifier->deleteLater();
        notifier = nullptr;
    }
    if (masterFd >= 0) {
        ::close(masterFd);
        masterFd = -1;
    }
}
