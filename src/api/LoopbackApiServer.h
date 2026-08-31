#pragma once

// Loopback-only TCP server for the automation API (milestone M8).
//
// Binds strictly to 127.0.0.1:17890 (QHostAddress::LocalHost — never a wildcard
// or LAN/IPv6-any address). Every accepted connection is re-verified to be a
// loopback peer before any byte is parsed; non-loopback peers are dropped. The
// server enforces the fixed ApiLimits bounds (max concurrent connections, idle
// timeout, header/body sizes via HttpParser) and delegates all request handling
// to the pure ApiRouter. It owns no policy of its own beyond transport safety.

#include "api/ApiContracts.h"
#include "api/ApiRouter.h"

#include <QByteArray>
#include <QObject>

#include <memory>
#include <unordered_map>

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace padmirror::api {

class LoopbackApiServer final : public QObject {
    Q_OBJECT
public:
    explicit LoopbackApiServer(IApiService& service, QObject* parent = nullptr);
    ~LoopbackApiServer() override;

    // Binds to 127.0.0.1:kPort. Returns false if the port is unavailable.
    bool start();
    void stop();
    [[nodiscard]] bool isListening() const;

private slots:
    void onNewConnection();

private:
    struct Connection {
        QByteArray buffer;
        QTimer* idleTimer = nullptr; // parented to the socket; deleted with it
    };

    void handleReadyRead(QTcpSocket* socket);
    void closeConnection(QTcpSocket* socket);
    void sendResponse(QTcpSocket* socket, const HttpResponse& response);
    void sendErrorAndClose(QTcpSocket* socket, int status, const std::string& message);

    IApiService& service_;
    ApiRouter router_;
    std::unique_ptr<QTcpServer> server_;
    std::unordered_map<QTcpSocket*, Connection> connections_;
};

} // namespace padmirror::api
