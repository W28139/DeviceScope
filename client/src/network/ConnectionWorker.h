#pragma once

#include "network/ConnectionState.h"

#include "devicescope/protocol/Protocol.h"
#include "devicescope/protocol/StreamDecoder.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QHash>
#include <QtCore/QObject>

class QTcpSocket;
class QTimer;

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
    void commandTimedOut(quint32 requestId);
    void reconnectScheduled(int attempt, int delayMs); // 已安排下一次自动重连

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
    void sendHeartbeat();  // 发送一次心跳
    void onConnected();    // socket 连接成功回调
    void onDisconnected(); // socket 断开回调
    void onReadyRead();    // socket 可读回调，交给 StreamDecoder 解码
    void onSocketError();  // socket 错误回调
    quint32 nextRequestId(); // 生成下一个请求 ID
    void handleCommandAck(const devicescope::protocol::DecodedMessage &message); // 处理命令 ACK
    void scanPending(); // 扫描待处理命令，处理超时
    void resetSession(); // 结束会话：未完成命令标失败、清空解包器和定时器
    void startConnect(bool resetStats); // 实际发起连接；重连时不清零统计
    void scheduleReconnect(); // 按退避策略安排下一次自动重连
    void cancelReconnect();   // 取消待执行的自动重连
    void onReconnectTimeout(); // 重连定时器到点
    void onSessionLost();      // 会话不可用后决定是否进入自动重连

    QTcpSocket *m_socket = nullptr; // 网络 socket，生命周期由 worker 管理
    devicescope::protocol::StreamDecoder m_decoder; // 流式协议解码器
    ConnectionState m_state = ConnectionState::Disconnected; // 当前连接状态
    quint32 m_nextRequestId = 1; // 下一个请求 ID
    quint32 m_helloRequestId = 0; // 握手请求 ID
    quint64 m_receivedFrames = 0; // 已接收帧计数
    QElapsedTimer m_clock; // 启动后单调递增，用于命令延迟和后续超时判断
    QHash<quint32, PendingRequest> m_pending;   // requestId -> 待确认命令
    QTimer *m_timeoutTimer = nullptr; // 100 ms 扫描定时器，仅在 Connected 时运行
    QTimer *m_heartbeatTimer = nullptr; // 2 s 心跳定时器，仅在 Connected 时运行
    QElapsedTimer m_lastRx; // 最后一次成功解出报文的时间，用于假死检测
    QTimer *m_reconnectTimer = nullptr; // 单次自动重连定时器
    QString m_host; // 最近一次连接目标
    quint16 m_port = 0;
    int m_reconnectAttempt = 0; // 连续重连次数，用于指数退避
    bool m_handshakeDone = false; // 上一次会话是否握手成功过
    bool m_reconnectActive = false; // 自动重连循环是否进行中
    bool m_manualDisconnect = false; // 用户主动断开，不自动重连
    QElapsedTimer m_connectedAt; // 握手成功时刻，稳定 10 秒后重置退避
};

} // namespace devicescope::client