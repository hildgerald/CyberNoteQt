#include "httpfileserver.h"

#include <QDateTime>
#include <QHostAddress>
#include <QLocale>
#include <QMetaObject>
#include <QMimeDatabase>
#include <QMimeType>
#include <QUrl>
#include <QStringList>

namespace {

constexpr int    kMaxHeaderSize   = 16 * 1024;   // taille maximale de l'en-tête de requête
constexpr int    kHeaderTimeoutMs = 15 * 1000;   // délai pour recevoir un en-tête complet
constexpr int    kMaxConnections  = 256;         // connexions simultanées par serveur
constexpr qint64 kChunkSize       = 64 * 1024;   // taille d'un bloc de fichier
constexpr qint64 kHighWater       = 256 * 1024;  // file d'écriture au-delà de laquelle on attend

const char *statusText(int status)
{
    switch (status) {
    case 200: return "OK";
    case 206: return "Partial Content";
    case 301: return "Moved Permanently";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 416: return "Range Not Satisfiable";
    case 431: return "Request Header Fields Too Large";
    default:  return "Internal Server Error";
    }
}

QByteArray httpDate(const QDateTime &when)
{
    return QLocale::c().toString(when.toUTC(), QStringLiteral("ddd, dd MMM yyyy HH:mm:ss")).toLatin1()
           + QByteArrayLiteral(" GMT");
}

// Analyse d'un en-tête "Range: bytes=a-b" (une seule plage).
// Retour : 0 = ignorer l'en-tête (envoyer tout le fichier), 1 = plage valide, -1 = non satisfaisable (416).
int parseRange(const QByteArray &header, qint64 size, qint64 *start, qint64 *end)
{
    QByteArray v = header.trimmed();
    if (v.isEmpty() || !v.toLower().startsWith("bytes="))
        return 0;
    v = v.mid(6).trimmed();
    if (v.contains(','))
        return 0; // plusieurs plages : non gérées, on envoie tout le fichier
    const int dash = v.indexOf('-');
    if (dash < 0)
        return 0;
    const QByteArray a = v.left(dash).trimmed();
    const QByteArray b = v.mid(dash + 1).trimmed();

    bool ok = false;
    if (a.isEmpty()) {                 // "-N" : les N derniers octets
        const qint64 n = b.toLongLong(&ok);
        if (!ok || n < 0)
            return 0;
        if (n == 0 || size == 0)
            return -1;
        *start = qMax<qint64>(0, size - n);
        *end = size - 1;
        return 1;
    }
    const qint64 s = a.toLongLong(&ok);
    if (!ok || s < 0)
        return 0;
    qint64 e = size - 1;
    if (!b.isEmpty()) {
        e = b.toLongLong(&ok);
        if (!ok || e < s)
            return 0;
    }
    if (s >= size)
        return -1;
    *start = s;
    *end = qMin<qint64>(e, size - 1);
    return 1;
}

} // namespace

// ===========================================================================
// HttpConnection
// ===========================================================================

HttpConnection::HttpConnection(QTcpSocket *socket, HttpFileServer *server)
    : QObject(server), m_socket(socket), m_server(server)
{
    m_socket->setParent(this);
    connect(m_socket, &QTcpSocket::readyRead,      this, &HttpConnection::onReadyRead);
    connect(m_socket, &QTcpSocket::bytesWritten,   this, &HttpConnection::onBytesWritten);
    connect(m_socket, &QTcpSocket::disconnected,   this, &HttpConnection::onSocketClosed);
    connect(m_socket, &QTcpSocket::errorOccurred,  this, &HttpConnection::onSocketClosed);

    m_headerTimer.setSingleShot(true);
    m_headerTimer.setInterval(kHeaderTimeoutMs);
    connect(&m_headerTimer, &QTimer::timeout, this, &HttpConnection::onHeaderTimeout);
    m_headerTimer.start();

    // des octets ont pu arriver avant la connexion du signal readyRead
    if (m_socket->bytesAvailable() > 0)
        QMetaObject::invokeMethod(this, &HttpConnection::onReadyRead, Qt::QueuedConnection);
}

HttpConnection::~HttpConnection()
{
    abort();
}

void HttpConnection::abort()
{
    m_headerTimer.stop();
    if (m_socket) {
        m_socket->disconnect(this); // plus aucun slot ne doit être appelé pendant/après la fermeture
        m_socket->abort();
    }
}

void HttpConnection::onSocketClosed()
{
    m_headerTimer.stop();
    deleteLater();
}

void HttpConnection::onHeaderTimeout()
{
    abort();
    deleteLater();
}

void HttpConnection::onReadyRead()
{
    if (m_handled) {         // requête déjà traitée : on ignore la suite (corps, requêtes en pipeline)
        m_socket->readAll();
        return;
    }
    m_buffer += m_socket->readAll();

    int end = -1;
    const int crlf = m_buffer.indexOf("\r\n\r\n");
    const int lf   = m_buffer.indexOf("\n\n"); // tolère un en-tête tapé à la main (netcat...)
    if (crlf >= 0 && (lf < 0 || crlf < lf))
        end = crlf;
    else if (lf >= 0)
        end = lf;

    if (end < 0 || end > kMaxHeaderSize) {
        if (m_buffer.size() > kMaxHeaderSize || end > kMaxHeaderSize) {
            m_handled = true;
            m_headerTimer.stop();
            sendError(431, QStringLiteral("Request header too large."));
        }
        return;
    }
    processRequest(m_buffer.left(end));
}

void HttpConnection::onBytesWritten()
{
    pump();
}

void HttpConnection::processRequest(const QByteArray &head)
{
    m_handled = true;
    m_headerTimer.stop();

    const QList<QByteArray> lines = head.split('\n');
    const QByteArray requestLine = lines.value(0).trimmed();
    QList<QByteArray> parts;
    const QList<QByteArray> rawParts = requestLine.split(' ');
    for (const QByteArray &p : rawParts)
        if (!p.isEmpty())
            parts << p;
    if (parts.size() < 2 || parts.size() > 3
        || (parts.size() == 3 && !parts.at(2).startsWith("HTTP/"))) {
        sendError(400, QStringLiteral("Malformed request."));
        return;
    }

    m_method = QString::fromLatin1(parts.at(0)).toUpper();
    QByteArray target = parts.at(1);

    QByteArray rangeHeader;
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const int colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).trimmed().toLower() == "range")
            rangeHeader = line.mid(colon + 1).trimmed();
    }

    m_headOnly = (m_method == QLatin1String("HEAD"));
    if (m_method != QLatin1String("GET") && !m_headOnly) {
        sendError(405, QStringLiteral("Only GET and HEAD are supported."),
                  { Header("Allow", "GET, HEAD") });
        return;
    }

    // forme absolue "http://hote/chemin" -> "/chemin"
    if (target.startsWith("http://") || target.startsWith("https://")) {
        const int slash = target.indexOf('/', target.indexOf("://") + 3);
        target = (slash < 0) ? QByteArray("/") : target.mid(slash);
    }
    if (!target.startsWith('/')) {
        sendError(400, QStringLiteral("Malformed request target."));
        return;
    }
    for (char c : std::as_const(target)) {
        if (static_cast<uchar>(c) < 0x20 || static_cast<uchar>(c) == 0x7f) {
            sendError(400, QStringLiteral("Malformed request target."));
            return;
        }
    }

    // on retire la requête (?...) et le fragment (#...)
    int cut = target.size();
    const int q = target.indexOf('?');
    const int f = target.indexOf('#');
    if (q >= 0) cut = qMin(cut, q);
    if (f >= 0) cut = qMin(cut, f);
    const QByteArray rawPath = target.left(cut);

    const QByteArray decoded = QByteArray::fromPercentEncoding(rawPath);
    if (decoded.contains('\0')) {
        sendError(400, QStringLiteral("Malformed request target."));
        return;
    }
    const QString path = QString::fromUtf8(decoded);
    m_path = path;

    // segments : ".." refusé, éléments cachés (.xxx) invisibles
    QStringList segments;
    const QStringList rawSegments = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &s : rawSegments) {
        if (s == QLatin1String("."))
            continue;
        if (s == QLatin1String("..")) {
            sendError(400, QStringLiteral("Malformed request target."));
            return;
        }
        if (s.startsWith(QLatin1Char('.'))) {
            sendError(404, QStringLiteral("Not found."));
            return;
        }
        segments << s;
    }

    // résolution dans le dossier partagé (chemins canoniques : les liens symboliques qui
    // sortent du dossier sont refusés)
    const QString rootCanon = QDir(m_server->rootDir()).canonicalPath();
    if (rootCanon.isEmpty()) {
        sendError(404, QStringLiteral("The shared folder no longer exists."));
        return;
    }
    const QString rootPrefix = rootCanon.endsWith(QLatin1Char('/')) ? rootCanon : rootCanon + QLatin1Char('/');
    const QFileInfo info(rootPrefix + segments.join(QLatin1Char('/')));
    if (!info.exists()) {
        sendError(404, QStringLiteral("Not found."));
        return;
    }
    const QString canon = info.canonicalFilePath();
    if (canon != rootCanon && !canon.startsWith(rootPrefix)) {
        sendError(403, QStringLiteral("Forbidden."));
        return;
    }

    if (info.isDir()) {
        if (!path.endsWith(QLatin1Char('/'))) { // "/dossier" -> "/dossier/"
            const QByteArray location = rawPath + '/';
            sendError(301, QStringLiteral("Moved permanently."), { Header("Location", location) });
            return;
        }
        QString urlPath = QStringLiteral("/");
        if (!segments.isEmpty())
            urlPath += segments.join(QLatin1Char('/')) + QLatin1Char('/');
        sendDirectoryListing(QDir(canon), urlPath, rootPrefix);
    } else if (info.isFile()) {
        sendFile(QFileInfo(canon), rangeHeader);
    } else {
        sendError(403, QStringLiteral("Forbidden."));
    }
}

void HttpConnection::sendResponse(int status, const QByteArray &contentType, const QByteArray &body,
                                  const QList<Header> &extraHeaders)
{
    QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + ' ' + statusText(status) + "\r\n";
    out += "Server: CyberNote\r\n";
    out += "Date: " + httpDate(QDateTime::currentDateTimeUtc()) + "\r\n";
    out += "Connection: close\r\n";
    out += "Content-Type: " + contentType + "\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    for (const Header &h : extraHeaders)
        out += h.first + ": " + h.second + "\r\n";
    out += "\r\n";
    if (!m_headOnly)
        out += body;

    m_socket->write(out);
    logRequest(status);
    finishAndClose();
}

void HttpConnection::sendError(int status, const QString &message, const QList<Header> &extraHeaders)
{
    const QString title = QString::number(status) + QLatin1Char(' ') + QString::fromLatin1(statusText(status));
    const QString html = QStringLiteral("<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>%1</title></head>"
                                        "<body><h1>%1</h1><p>%2</p></body></html>\n")
                             .arg(title.toHtmlEscaped(), message.toHtmlEscaped());
    sendResponse(status, "text/html; charset=utf-8", html.toUtf8(), extraHeaders);
}

void HttpConnection::sendDirectoryListing(const QDir &dir, const QString &urlPath, const QString &rootPrefix)
{
    const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot,
                                                    QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    const QString title = QStringLiteral("Index of ") + urlPath;

    QString html;
    html += QStringLiteral("<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>")
            + title.toHtmlEscaped() + QStringLiteral("</title>\n"
            "<style>body{font-family:monospace;margin:2em}td{padding:2px 24px 2px 0}"
            "th{text-align:left;padding-right:24px}a{text-decoration:none}</style></head><body>\n<h1>")
            + title.toHtmlEscaped() + QStringLiteral("</h1>\n<table>\n"
            "<tr><th>Name</th><th>Size</th><th>Last modified</th></tr>\n");
    if (urlPath != QLatin1String("/"))
        html += QStringLiteral("<tr><td><a href=\"../\">../</a></td><td></td><td></td></tr>\n");

    for (const QFileInfo &e : entries) {
        const QString canon = e.canonicalFilePath();
        if (canon.isEmpty() || !canon.startsWith(rootPrefix))
            continue; // lien symbolique sortant du dossier partagé
        const bool isDir = e.isDir();
        if (!isDir && !e.isFile())
            continue;

        const QString name = e.fileName() + (isDir ? QStringLiteral("/") : QString());
        const QString href = QString::fromLatin1(QUrl::toPercentEncoding(e.fileName()))
                             + (isDir ? QStringLiteral("/") : QString());
        const QString size = isDir ? QStringLiteral("-") : QLocale::c().formattedDataSize(e.size());
        html += QStringLiteral("<tr><td><a href=\"%1\">%2</a></td><td>%3</td><td>%4</td></tr>\n")
                    .arg(href, name.toHtmlEscaped(), size.toHtmlEscaped(),
                         e.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    }
    html += QStringLiteral("</table>\n</body></html>\n");

    sendResponse(200, "text/html; charset=utf-8", html.toUtf8());
}

void HttpConnection::sendFile(const QFileInfo &info, const QByteArray &rangeHeader)
{
    auto *file = new QFile(info.absoluteFilePath(), this);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        sendError(403, QStringLiteral("Forbidden."));
        return;
    }

    const qint64 size = file->size();
    qint64 start = 0;
    qint64 end = size - 1;
    const int range = parseRange(rangeHeader, size, &start, &end);
    if (range < 0) {
        delete file;
        sendError(416, QStringLiteral("Requested range not satisfiable."),
                  { Header("Content-Range", "bytes */" + QByteArray::number(size)) });
        return;
    }
    const bool partial = (range > 0);
    const qint64 length = (size == 0) ? 0 : (end - start + 1);
    const int status = partial ? 206 : 200;

    QByteArray mime = QMimeDatabase().mimeTypeForFile(info, QMimeDatabase::MatchExtension).name().toLatin1();
    if (mime.isEmpty())
        mime = "application/octet-stream";

    QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + ' ' + statusText(status) + "\r\n";
    out += "Server: CyberNote\r\n";
    out += "Date: " + httpDate(QDateTime::currentDateTimeUtc()) + "\r\n";
    out += "Connection: close\r\n";
    out += "Content-Type: " + mime + "\r\n";
    out += "Content-Length: " + QByteArray::number(length) + "\r\n";
    out += "Accept-Ranges: bytes\r\n";
    out += "Last-Modified: " + httpDate(info.lastModified()) + "\r\n";
    if (partial)
        out += "Content-Range: bytes " + QByteArray::number(start) + '-' + QByteArray::number(end)
               + '/' + QByteArray::number(size) + "\r\n";
    out += "\r\n";
    m_socket->write(out);
    logRequest(status);

    if (m_headOnly || length == 0 || !file->seek(start)) {
        delete file;
        finishAndClose();
        return;
    }
    m_file = file;
    m_remaining = length;
    pump();
}

void HttpConnection::pump()
{
    if (!m_file || m_socket->state() != QAbstractSocket::ConnectedState)
        return;

    while (m_remaining > 0 && m_socket->bytesToWrite() < kHighWater) {
        const QByteArray block = m_file->read(qMin(kChunkSize, m_remaining));
        if (block.isEmpty()) {       // fichier tronqué / erreur de lecture : on coupe net
            abort();
            deleteLater();
            return;
        }
        m_socket->write(block);
        m_remaining -= block.size();
    }

    if (m_remaining == 0) {
        delete m_file;
        m_file = nullptr;
        finishAndClose();
    }
}

void HttpConnection::finishAndClose()
{
    m_socket->disconnectFromHost(); // la fermeture a lieu une fois les données envoyées
}

void HttpConnection::logRequest(int status)
{
    bool ok = false;
    const quint32 v4 = m_socket->peerAddress().toIPv4Address(&ok);
    const QString peer = ok ? QHostAddress(v4).toString() : m_socket->peerAddress().toString();
    emit m_server->requestServed(peer, m_method, m_path, status);
}

// ===========================================================================
// HttpFileServer
// ===========================================================================

HttpFileServer::HttpFileServer(const QString &rootDir, QObject *parent)
    : QObject(parent), m_root(QDir::cleanPath(rootDir))
{
    connect(&m_tcp, &QTcpServer::newConnection, this, &HttpFileServer::onNewConnection);
}

HttpFileServer::~HttpFileServer()
{
    stop();
}

bool HttpFileServer::start(quint16 port)
{
    if (m_tcp.isListening())
        return true;
    return m_tcp.listen(QHostAddress::Any, port);
}

void HttpFileServer::stop()
{
    m_tcp.close();
    const QList<HttpConnection *> connections =
        findChildren<HttpConnection *>(QString(), Qt::FindDirectChildrenOnly);
    for (HttpConnection *c : connections)
        delete c; // le destructeur coupe la connexion
}

void HttpFileServer::onNewConnection()
{
    while (QTcpSocket *socket = m_tcp.nextPendingConnection()) {
        if (findChildren<HttpConnection *>(QString(), Qt::FindDirectChildrenOnly).size() >= kMaxConnections) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        new HttpConnection(socket, this); // détruit par lui-même (deleteLater) ou par stop()
    }
}
