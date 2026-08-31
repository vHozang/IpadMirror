#include "api/LoopbackApiServer.h"

#include "api/HttpParser.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <vector>

namespace padmirror::api {

namespace {

const char* reasonPhrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 415: return "Unsupported Media Type";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        default: return "Error";
    }
}

bool isLoopbackPeer(const QTcpSocket* socket) {
    const QHostAddress peer = socket->peerAddress();
    return peer.isLoopback();
}

} // namespace

LoopbackApiServer::LoopbackApiServer(IApiService& service, QObject* parent)
    : QObject(parent), service_(service), router_(service_) {}

LoopbackApiServer::~LoopbackApiServer() {
    stop();
}

bool LoopbackApiServer::start() {
    if (server_) return true;
    server_ = std::make_unique<QTcpServer>(this);
    connect(server_.get(), &QTcpServer::newConnection, this,
            &LoopbackApiServer::onNewConnection);
    // Bind strictly to IPv4 loopback. Never QHostAddress::Any / AnyIPv6.
    if (!server_->listen(QHostAddress::LocalHost, ApiLimits::kPort)) {
        server_.reset();
        return false;
    }
    return true;
}

void LoopbackApiServer::stop() {
    if (!server_) return;
    // Copy the keys first: closeConnection mutates the map as it goes.
    std::vector<QTcpSocket*> sockets;
    sockets.reserve(connections_.size());
    for (const auto& entry : connections_) sockets.push_back(entry.first);
    for (QTcpSocket* socket : sockets) {
        closeConnection(socket);
    }
    connections_.clear();
    server_->close();
    server_.reset();
}

bool LoopbackApiServer::isListening() const {
    return server_ && server_->isListening();
}

void LoopbackApiServer::onNewConnection() {
    while (server_ && server_->hasPendingConnections()) {
        QTcpSocket* socket = server_->nextPendingConnection();
        if (!socket) break;

        // Defense in depth: refuse any peer that is not loopback, and cap the
        // number of concurrent clients.
        if (!isLoopbackPeer(socket) ||
            connections_.size() >= ApiLimits::kMaxConnections) {
            socket->abort();
            socket->deleteLater();
            continue;
        }

        Connection connection;
        // Parent the timer to the socket so it is destroyed with the socket
        // rather than while its own timeout slot is executing.
        QTimer* timer = new QTimer(socket);
        timer->setSingleShot(true);
        timer->setInterval(ApiLimits::kIdleTimeoutMs);
        connection.idleTimer = timer;
        connect(timer, &QTimer::timeout, this, [this, socket] { closeConnection(socket); });
        connections_.emplace(socket, std::move(connection));
        timer->start();

        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { handleReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { closeConnection(socket); });
    }
}

void LoopbackApiServer::handleReadyRead(QTcpSocket* socket) {
    auto it = connections_.find(socket);
    if (it == connections_.end()) return;
    Connection& connection = it->second;
    if (connection.idleTimer) connection.idleTimer->start(); // reset idle timeout on activity

    connection.buffer.append(socket->readAll());
    // Never let a single connection's buffer exceed the header+body caps.
    const std::size_t hardCap = ApiLimits::kMaxHeaderBytes + ApiLimits::kMaxBodyBytes + 8;
    if (static_cast<std::size_t>(connection.buffer.size()) > hardCap) {
        sendErrorAndClose(socket, 413, "request too large");
        return;
    }

    const std::string buffer(connection.buffer.constData(),
                             static_cast<std::size_t>(connection.buffer.size()));
    HttpRequest request;
    HttpParseOutcome outcome = parseHttpRequest(buffer, request);
    switch (outcome.status) {
        case ParseStatus::NeedMore:
            return;
        case ParseStatus::Error:
            sendErrorAndClose(socket, outcome.errorStatus, outcome.errorMessage);
            return;
        case ParseStatus::Complete:
            break;
    }

    HttpResponse response = router_.handle(request);
    sendResponse(socket, response);
    // One request per connection keeps the surface small; always close after.
    closeConnection(socket);
}

void LoopbackApiServer::sendResponse(QTcpSocket* socket, const HttpResponse& response) {
    if (!socket) return;
    QByteArray out;
    out.append("HTTP/1.1 ");
    out.append(QByteArray::number(response.statusCode));
    out.append(' ');
    out.append(reasonPhrase(response.statusCode));
    out.append("\r\n");
    out.append("Content-Type: ");
    out.append(QByteArray::fromStdString(response.contentType));
    out.append("\r\n");
    out.append("Content-Length: ");
    out.append(QByteArray::number(static_cast<qlonglong>(response.body.size())));
    out.append("\r\n");
    if (!response.allow.empty()) {
        out.append("Allow: ");
        out.append(QByteArray::fromStdString(response.allow));
        out.append("\r\n");
    }
    out.append("Cache-Control: no-store\r\n");
    out.append("Connection: close\r\n");
    out.append("\r\n");
    out.append(QByteArray::fromStdString(response.body));
    socket->write(out);
    socket->flush();
}

void LoopbackApiServer::sendErrorAndClose(QTcpSocket* socket, int status,
                                          const std::string& message) {
    HttpResponse response;
    response.statusCode = status;
    response.contentType = "application/json";
    response.body = std::string("{\"error\":\"") + message + "\"}";
    sendResponse(socket, response);
    closeConnection(socket);
}

void LoopbackApiServer::closeConnection(QTcpSocket* socket) {
    auto it = connections_.find(socket);
    if (it != connections_.end()) {
        if (it->second.idleTimer) it->second.idleTimer->stop();
        connections_.erase(it);
    }
    if (socket) {
        socket->disconnectFromHost();
        socket->deleteLater();
    }
}

} // namespace padmirror::api
