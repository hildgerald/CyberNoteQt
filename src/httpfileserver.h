#pragma once

// ~ nouveau (v2.3.0) : petit serveur HTTP de distribution de fichiers, codé en interne (aucune
// bibliothèque externe : uniquement QTcpServer/QTcpSocket de Qt6::Network, intégrés à la boucle
// d'événements -> pas de thread).
//
// Ce que fait le serveur :
//   - GET et HEAD (toute autre méthode -> 405) ; une requête par connexion (Connection: close) ;
//   - dossier -> page d'index HTML (dossiers d'abord, tailles, dates) ; fichier -> envoi en flux
//     (par blocs, sans charger le fichier en mémoire) avec Content-Type, Last-Modified et
//     reprise de téléchargement (en-tête Range, une plage : wget -c, curl -C -, navigateurs...) ;
//   - sécurité : jamais en dehors du dossier racine (segments ".." refusés, liens symboliques
//     pointant hors du dossier refusés) ; les fichiers et dossiers cachés (nom commençant par
//     ".", ex. le fichier .target d'une machine qui contient des informations sensibles) ne
//     sont ni listés ni servis ; délai maximal pour recevoir l'en-tête de requête ; nombre de
//     connexions simultanées plafonné.
//
// Le serveur écoute sur toutes les interfaces (c'est son but : servir les machines cibles).

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QByteArray>
#include <QList>
#include <QPair>

class HttpFileServer;

// Une connexion cliente : lit la requête, répond, puis ferme. Créée et détruite par HttpFileServer.
class HttpConnection : public QObject
{
    Q_OBJECT
public:
    HttpConnection(QTcpSocket *socket, HttpFileServer *server);
    ~HttpConnection() override;

    void abort(); // coupe immédiatement la connexion

private slots:
    void onReadyRead();
    void onBytesWritten();
    void onSocketClosed();
    void onHeaderTimeout();

private:
    using Header = QPair<QByteArray, QByteArray>;

    void processRequest(const QByteArray &head);
    void sendResponse(int status, const QByteArray &contentType, const QByteArray &body,
                      const QList<Header> &extraHeaders = {});
    void sendError(int status, const QString &message, const QList<Header> &extraHeaders = {});
    void sendDirectoryListing(const QDir &dir, const QString &urlPath, const QString &rootPrefix);
    void sendFile(const QFileInfo &info, const QByteArray &rangeHeader);
    void finishAndClose();
    void pump(); // envoie le prochain bloc du fichier tant que la file d'écriture n'est pas pleine
    void logRequest(int status);

    QTcpSocket *m_socket = nullptr;
    HttpFileServer *m_server = nullptr;
    QTimer m_headerTimer;
    QByteArray m_buffer;
    bool m_handled = false;   // la requête a déjà été traitée (les données suivantes sont ignorées)
    bool m_headOnly = false;
    QString m_method;
    QString m_path;           // chemin demandé (décodé), pour le journal
    QFile *m_file = nullptr;  // fichier en cours d'envoi
    qint64 m_remaining = 0;   // octets restant à envoyer
};

class HttpFileServer : public QObject
{
    Q_OBJECT
public:
    explicit HttpFileServer(const QString &rootDir, QObject *parent = nullptr);
    ~HttpFileServer() override;

    // Démarre l'écoute sur `port` (toutes les interfaces). Retourne false si l'écoute est
    // impossible (port déjà utilisé, droits insuffisants...) : voir errorString()/serverError().
    bool start(quint16 port);
    void stop(); // ferme l'écoute ET toutes les connexions en cours

    bool isListening() const { return m_tcp.isListening(); }
    quint16 port() const { return m_tcp.serverPort(); }
    QString rootDir() const { return m_root; }
    QString errorString() const { return m_tcp.errorString(); }
    QAbstractSocket::SocketError serverError() const { return m_tcp.serverError(); }

signals:
    // émis à chaque requête traitée (pour le journal / la barre d'état)
    void requestServed(const QString &peer, const QString &method, const QString &path, int status);

private slots:
    void onNewConnection();

private:
    QTcpServer m_tcp;
    QString m_root;
};
