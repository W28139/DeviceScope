#include "network/ConnectionWorker.h"

#include "devicescope/protocol/Codec.h"
#include "devicescope/protocol/Message.h"

#include <QtCore/QDebug>
#include <QtNetwork/QTcpSocket>

namespace devicescope::client {

using namespace devicescope::protocol;

ConnectionWorker::ConnectionWorker(QObject *parent)
    : QObject(parent)
{
}

void ConnectionWorker::start()
{
    m_clock.start();
    ensureSocket();
    emit connectionStateChanged(m_state);
}

// 由UI线程中的信号触发而执行
void ConnectionWorker::connectToHost(const QString &host, quint16 port)
{
    ensureSocket();
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        return;
    }

    m_decoder = StreamDecoder();
    m_receivedFrames = 0;
    m_helloRequestId = 0;
    emit statsChanged(0);

    setState(ConnectionState::Connecting);
    m_socket->connectToHost(host, port);
}

// 由UI线程中的信号触发而执行
void ConnectionWorker::disconnectFromHost()
{
    if (m_state == ConnectionState::Error) {
        setState(ConnectionState::Disconnected);
        return;
    }
    if (m_socket == nullptr || m_socket->state() == QAbstractSocket::UnconnectedState) {
        setState(ConnectionState::Disconnected);
        return;
    }

    setState(ConnectionState::Disconnecting);
    m_socket->disconnectFromHost();
}

void ConnectionWorker::shutdown()
{
    if (m_socket != nullptr) {
        m_socket->abort();
    }
}

// 发送一条命令，由 UI 线程中的信号触发，跨线程投递到 worker 线程执行
void ConnectionWorker::sendCommand(CommandId commandId)
{
    // 分配请求 ID
    const quint32 requestId = nextRequestId();

    // 前置条件校验：必须已连接且有 socket，否则直接失败
    if (m_state != ConnectionState::Connected || m_socket == nullptr) {
        emit commandFailed(requestId, QStringLiteral("未连接"));
        return;
    }

    // 编码命令体
    const auto payload = encodeCommand(Command{commandId, 0});
    if (!payload.hasValue()) {
        emit commandFailed(requestId, QStringLiteral("COMMAND 编码失败"));
        return;
    }

    // 封装成完整帧
    const auto frame = encode(MessageType::Command, kFlagNone, requestId, payload.value());
    if (!frame.hasValue()) {
        emit commandFailed(requestId, QStringLiteral("COMMAND 报文编码失败"));
        return;
    }

    // 写入 socket；write 返回 -1 表示立即失败
    if (m_socket->write(frame.value()) < 0) {
        emit commandFailed(requestId,
                           QStringLiteral("发送失败: %1").arg(m_socket->errorString()));
        return;
    }

    // 记录"待 ACK"请求：以 requestId 为 key，保存 commandId 和发送时刻
    // 发送时刻用单调时钟，后续收到 ACK 时算 latencyMs
    m_pending.insert(requestId, PendingRequest{commandId, m_clock.elapsed()});
    // 通知外界"命令已发出"
    emit commandSent(requestId, commandId);
}

void ConnectionWorker::ensureSocket()
{
    if (m_socket != nullptr) {
        return;
    }
    
    // 新建立的连接，先绑定信号与槽函数，客户端一个socket对应一个连接
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &ConnectionWorker::onConnected);        // 建立连接时，先握手
    connect(m_socket, &QTcpSocket::disconnected, this, &ConnectionWorker::onDisconnected);  // 关闭连接信号，触发关闭
    connect(m_socket, &QTcpSocket::readyRead, this, &ConnectionWorker::onReadyRead);        // 读事件触发
    connect(m_socket, &QTcpSocket::errorOccurred, this, &ConnectionWorker::onSocketError);  // 错误事件触发
}

void ConnectionWorker::setState(ConnectionState state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit connectionStateChanged(state);
}

void ConnectionWorker::fail(const QString &message)
{
    emit connectionError(message);
    setState(ConnectionState::Error);
    if (m_socket != nullptr && m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
}

quint32 ConnectionWorker::nextRequestId()
{
    const quint32 id = m_nextRequestId++;
    if (m_nextRequestId == 0) {  // 回绕到 0 时跳到 1，0 保留给未分配/无效请求
        m_nextRequestId = 1;
    }
    return id;
}

// 处理服务端返回的 COMMAND_ACK，与 sendCommand() 中登记的 pending 记录闭环
void ConnectionWorker::handleCommandAck(const DecodedMessage &message)
{
    // 1. 解码 ACK 负载（commandId / result / message 等）
    const auto ack = decodeCommandAck(message.payload);
    if (!ack.hasValue()) {  // 负载已通过 CRC，格式错误只记录并忽略
        qWarning() << "COMMAND_ACK 解析失败";
        return;
    }

    // 2. 用帧头里的 requestId 去 pending 表里找对应请求
    const quint32 requestId = message.header.requestId;
    const auto it = m_pending.find(requestId);
    if (it == m_pending.end()) {  
        // 找不到：可能是已超时清除、或服务端重复发 ACK，忽略即可
        qWarning() << "收到未知 requestId 的 COMMAND_ACK:" << requestId;
        return;
    }
    // 3. 校验 ACK 里的 commandId 是否和当初发出的命令一致
    //    不一致说明协议/服务端有异常，但这里只告警、仍继续处理（宽松策略）
    if (it->commandId != ack.value().commandId) {
        qWarning() << "COMMAND_ACK 命令不匹配: 期望"
                   << static_cast<int>(it->commandId) << "实际"
                   << static_cast<int>(ack.value().commandId);
    }

    // 4. 用单调时钟计算延迟，然后从 pending 中移除，避免重复匹配
    const qint64 latencyMs = m_clock.elapsed() - it->sentAtMs;
    m_pending.erase(it);

    // 5. 对外发出 ACK 信号，UI 据此更新命令状态、展示延迟/结果
    emit commandAcknowledged(requestId, ack.value().commandId, ack.value().result,
                             ack.value().message, latencyMs);
}

// 握手函数
void ConnectionWorker::sendHello()
{
    const auto payload =
        encodeHelloRequest(HelloRequest{QStringLiteral("DeviceScope Client")});
    if (!payload.hasValue()) {
        fail(QStringLiteral("HELLO 编码失败"));
        return;
    }

    m_helloRequestId = nextRequestId();
    const auto frame =
        encode(MessageType::Hello, kFlagNone, m_helloRequestId, payload.value());
    if (!frame.hasValue()) {
        fail(QStringLiteral("HELLO 编码失败"));
        return;
    }
    m_socket->write(frame.value());
}

void ConnectionWorker::onConnected()
{
    setState(ConnectionState::Handshaking);
    sendHello();
}

void ConnectionWorker::onDisconnected()
{
    // Error 状态保留，便于 UI 显示失败原因；用户重新连接或点击断开后清除。
    if (m_state != ConnectionState::Error) {
        setState(ConnectionState::Disconnected);
    }
}

void ConnectionWorker::onSocketError()
{
    if (m_socket == nullptr) {  // socket 已不存在，没有错误可处理
        return;
    }

    switch (m_state) {
    // 正在建立连接时出错（连接被拒、主机不可达、超时等）
    case ConnectionState::Connecting:
        emit connectionError(QStringLiteral("连接失败: %1").arg(m_socket->errorString()));    // 带“连接失败”前缀提示
        setState(ConnectionState::Error);
        break;
    // 已连上但握手阶段出错（对端断开、协议错误等）
    case ConnectionState::Handshaking:
    // 已握手后运行中出错（连接被重置、读写出错等） 
    case ConnectionState::Connected:
        emit connectionError(m_socket->errorString());    // 直接上报底层错误文本
        setState(ConnectionState::Error);
        break;
    // 已断开/已错误等状态，忽略重复错误
    default:
        break;
    }
}

void ConnectionWorker::onReadyRead()
{
    m_decoder.append(m_socket->readAll());  

    DecodedMessage message;
    while (true) {  // 尽可能多解帧，直到半包、出错或需要等待
        const StreamStatus status = m_decoder.next(message);  // 尝试从缓冲区解出一帧
        if (status == StreamStatus::Message) {  // 成功解出一帧
            ++m_receivedFrames;  // 累计已接收帧数
            emit statsChanged(m_receivedFrames);  // 通知上层统计变化

            if (m_state == ConnectionState::Handshaking) {  // 仅在握手阶段处理握手相关消息
                if (message.header.type == MessageType::Error) {  // 对端回了 ERROR，握手失败
                    const auto error = decodeError(message.payload);  // 解码错误消息体
                    fail(error.hasValue() ? error.value().message  // 有消息体就用其文本
                                          : QStringLiteral("握手失败"));  // 解码失败则用默认文本
                    return;  // 握手已失败，停止继续解析
                }

                if (message.header.type == MessageType::Hello) {  // 收到 HELLO 响应
                    const auto response = decodeHelloResponse(message.payload);  // 解码响应体
                    if (!response.hasValue()) {  // 响应体格式不对
                        fail(QStringLiteral("HELLO 响应格式错误"));
                        return;
                    }
                    if (response.value().result != AckResult::Ok) {  // 对端明确拒绝握手
                        fail(QStringLiteral("握手被拒绝"));
                        return;
                    }
                    setState(ConnectionState::Connected);  // 握手成功，进入已连接状态
                    emit handshakeSucceeded(response.value().deviceId,  // 上报设备 ID
                                            response.value().deviceName);  // 上报设备名
                }
            } else if (message.header.type == MessageType::CommandAck) {
                handleCommandAck(message);  // 按 requestId 匹配命令响应
            }
            continue;  // 继续解下一帧（可能还有粘包）
        }

        if (status == StreamStatus::BadPacket) {  // 坏包已被解码器丢弃/重同步
            continue;  // 继续尝试解下一帧
        }
        if (status == StreamStatus::NeedMoreData) {  // 半包，缓冲区数据不够
            break;  // 等下一次 readyRead
        }
        if (status == StreamStatus::UnsupportedVersion) {  // 协议版本不支持
            fail(QStringLiteral("协议版本不支持"));  // 握手/连接失败
            return;
        }
        fail(QStringLiteral("协议流错误，连接已断开"));  // 其他致命流错误
        return;
    }
}

} // namespace devicescope::client