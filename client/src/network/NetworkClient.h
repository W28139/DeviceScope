#pragma once

#include "network/ConnectionState.h"

#include "devicescope/protocol/Protocol.h"

#include <QtCore/QObject>
#include <QtCore/QThread>

namespace devicescope::client {

class ConnectionWorker;

// mainwindow类 管理 NetworkClient类，NetworkClient类管理 ConnectionWorker类
class NetworkClient final : public QObject
{
    Q_OBJECT

public:
    explicit NetworkClient(QObject *parent = nullptr);  // 构造函数：创建 worker 并移入 m_thread
    ~NetworkClient() override;                          // 析构：停止线程、回收 worker，保证安全退出

    void connectToHost(const QString &host, quint16 port);  // 发起连接（UI 线程调用，转成跨线程请求）
    void disconnectFromHost();                              // 主动断开连接（UI 线程调用）
    void sendCommand(devicescope::protocol::CommandId commandId);  // 发送控制命令

signals:
    void connectionStateChanged(devicescope::client::ConnectionState state);  // 连接状态变化：Disconnected/Connecting/Connected 等
    void handshakeSucceeded(quint16 deviceId, const QString &deviceName);   // HELLO 握手成功，带回设备身份
    void connectionError(const QString &message);       // 网络/协议错误，供 UI 提示
    void statsChanged(quint64 receivedFrames);          // 统计更新：已接收帧数
    void commandSent(quint32 requestId, devicescope::protocol::CommandId commandId);  // 命令已发出
    void commandAcknowledged(quint32 requestId, devicescope::protocol::CommandId commandId,
                             devicescope::protocol::AckResult result, const QString &message,
                             qint64 latencyMs);  // 命令收到 ACK
    void commandFailed(quint32 requestId, const QString &reason);  // 命令未发出
    void commandTimedOut(quint32 requestId);  // 命令在超时时间内没有收到 ACK

    // 跨线程请求（由 UI 线程发出，worker 线程执行）。
    void connectRequested(const QString &host, quint16 port);   // 跨线程信号：请求 worker 连接
    void disconnectRequested();                                 // 跨线程信号：请求 worker 断开
    void commandRequested(devicescope::protocol::CommandId commandId);  // 跨线程信号：请求 worker 发命令

private:
    QThread m_thread;       // 工作线程，承载 ConnectionWorker 的事件循环
    ConnectionWorker *m_worker = nullptr;   // 实际干活的 worker，运行在 m_thread 中
};

} // namespace devicescope::client