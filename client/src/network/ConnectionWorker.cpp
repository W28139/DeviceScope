#include "network/ConnectionWorker.h"

#include "devicescope/protocol/Codec.h"
#include "devicescope/protocol/Message.h"

#include <QtNetwork/QTcpSocket>

namespace devicescope::client {

using namespace devicescope::protocol;

ConnectionWorker::ConnectionWorker(QObject *parent)
    : QObject(parent)
{
}

void ConnectionWorker::start()
{
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

// 握手函数
void ConnectionWorker::sendHello()
{
    const auto payload =
        encodeHelloRequest(HelloRequest{QStringLiteral("DeviceScope Client")});
    if (!payload.hasValue()) {
        fail(QStringLiteral("HELLO 编码失败"));
        return;
    }

    m_helloRequestId = m_nextRequestId++;
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