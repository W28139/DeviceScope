#pragma once

#include "network/ConnectionState.h"

#include "devicescope/protocol/Protocol.h"
#include "devicescope/protocol/StreamDecoder.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QHash>
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
    void sendCommand(devicescope::protocol::CommandId commandId);

signals:
    void connectionStateChanged(devicescope::client::ConnectionState state);
    void handshakeSucceeded(quint16 deviceId, const QString &deviceName);
    void connectionError(const QString &message);
    void statsChanged(quint64 receivedFrames);
    void commandSent(quint32 requestId, devicescope::protocol::CommandId commandId);
    void commandAcknowledged(quint32 requestId, devicescope::protocol::CommandId commandId,
                             devicescope::protocol::AckResult result, const QString &message,
                             qint64 latencyMs);
    void commandFailed(quint32 requestId, const QString &reason);

private:
    // 已发送、等待 COMMAND_ACK 的命令。
    struct PendingRequest {
        devicescope::protocol::CommandId commandId = devicescope::protocol::CommandId::Start;
        qint64 sentAtMs = 0;  // 发送时间，相对于 m_clock 的单调时间
    };

    void ensureSocket();   // 确保 socket 已创建
    void setState(ConnectionState state); // 更新连接状态并发出信号
    void fail(const QString &message);    // 统一错误处理
    void sendHello();      // 发送握手 HELLO
    void onConnected();    // socket 连接成功回调
    void onDisconnected(); // socket 断开回调
    void onReadyRead();    // socket 可读回调，交给 StreamDecoder 解码
    void onSocketError();  // socket 错误回调
    quint32 nextRequestId(); // 生成下一个请求 ID
    void handleCommandAck(const devicescope::protocol::DecodedMessage &message); // 处理命令 ACK

    QTcpSocket *m_socket = nullptr; // 网络 socket，生命周期由 worker 管理
    devicescope::protocol::StreamDecoder m_decoder; // 流式协议解码器
    ConnectionState m_state = ConnectionState::Disconnected; // 当前连接状态
    quint32 m_nextRequestId = 1; // 下一个请求 ID
    quint32 m_helloRequestId = 0; // 握手请求 ID
    quint64 m_receivedFrames = 0; // 已接收帧计数
    QElapsedTimer m_clock; // 启动后单调递增，用于命令延迟和后续超时判断
    QHash<quint32, PendingRequest> m_pending;   // requestId -> 待确认命令
};

} // namespace devicescope::client