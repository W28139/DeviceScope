#pragma once

#include "network/ConnectionState.h"

#include "devicescope/protocol/StreamDecoder.h"

#include <QtCore/QObject>

class QTcpSocket;

namespace devicescope::client {

// 运行在独立线程中的网络对象：socket 的创建、收发和销毁都发生在这里。
class ConnectionWorker final : public QObject
{
    Q_OBJECT

public:
    explicit ConnectionWorker(QObject *parent = nullptr);

public slots:
    void start(); // 线程启动后创建 socket
    void connectToHost(const QString &host, quint16 port);
    void disconnectFromHost();
    void shutdown();

signals:
    void connectionStateChanged(devicescope::client::ConnectionState state);
    void handshakeSucceeded(quint16 deviceId, const QString &deviceName);
    void connectionError(const QString &message);
    void statsChanged(quint64 receivedFrames);

private:
    void ensureSocket();
    void setState(ConnectionState state);
    void fail(const QString &message);
    void sendHello();
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError();

    QTcpSocket *m_socket = nullptr;
    devicescope::protocol::StreamDecoder m_decoder;
    ConnectionState m_state = ConnectionState::Disconnected;
    quint32 m_nextRequestId = 1;
    quint32 m_helloRequestId = 0;
    quint64 m_receivedFrames = 0;
};

} // namespace devicescope::client