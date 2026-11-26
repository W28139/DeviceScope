#include "SimulatorSession.h"  

#include "Logging.h"  

#include "devicescope/protocol/Codec.h"  
#include "devicescope/protocol/Message.h"  

#include <QtCore/QDateTime>  
#include <QtCore/QLoggingCategory>  
#include <QtNetwork/QTcpSocket>  

namespace devicescope::simulator {

using namespace devicescope::protocol;  

SimulatorSession::SimulatorSession(QTcpSocket *socket, const SimulatorConfig &config,
                                   quint32 id, QObject *parent)
    : QObject(parent)  // 以父对象管理生命周期
    , m_socket(socket)  // 保存连接
    , m_device(config.seed)  // 用配置种子初始化设备模型
    , m_rateHz(config.rateHz)  // 保存遥测频率
    , m_id(id)  // 保存会话 ID
{
    m_socket->setParent(this);  // 让 socket 随会话一起析构

    connect(m_socket, &QTcpSocket::readyRead, this, &SimulatorSession::onReadyRead);  // 可读就解析
    connect(m_socket, &QTcpSocket::disconnected, this, [this] {  // 断开时清理
        m_telemetryTimer.stop();  // 停止遥测定时器
        qCInfo(lcSimulator) << "session" << m_id << "disconnected";  // 记录断开日志
        deleteLater();  // 延后删除自身
    });

    m_telemetryTimer.setInterval(1000 / m_rateHz);  // 按频率设置周期，10Hz -> 100ms
    connect(&m_telemetryTimer, &QTimer::timeout, this, &SimulatorSession::onTelemetryTick);  // 到期发遥测
}

quint32 SimulatorSession::id() const
{
    return m_id;  // 返回会话 ID
}

void SimulatorSession::close()
{
    m_telemetryTimer.stop();  // 先停定时器，避免关闭后还发
    m_closing = true;  // 标记正在关闭，onReadyRead 会丢弃数据
    if (m_socket != nullptr) {
        m_socket->disconnectFromHost();  // 优雅断开
    }
}

void SimulatorSession::onReadyRead()
{
    if (m_closing) {  // 关闭中不再处理
        m_socket->readAll();  // 把数据读掉丢弃
        return;
    }

    const QByteArray data = m_socket->readAll();  // 取出本次到达的所有字节
    m_decoder.append(data);  // 追加到解码器缓冲区

    DecodedMessage message;
    while (true) {  // 尽可能多解析，直到半包/出错/关闭
        const StreamStatus status = m_decoder.next(message);  // 尝试解出一帧
        if (status == StreamStatus::Message) {  // 成功解出一帧
            handleMessage(message);  // 分发处理
            if (m_closing) {  // 处理中可能触发关闭
                return;
            }
            continue;  // 继续解下一帧
        }
        if (status == StreamStatus::BadPacket) {  // 坏包已由解码器丢弃/重同步
            qCWarning(lcSimulator) << "session" << m_id << "dropped bad packet";  // 记录后继续
            continue;
        }
        if (status == StreamStatus::NeedMoreData) {  // 半包，等更多数据
            break;
        }
        if (status == StreamStatus::UnsupportedVersion) {  // 版本不支持
            qCWarning(lcSimulator) << "session" << m_id << "unsupported protocol version";
            sendError(0, 1, QStringLiteral("unsupported protocol version"));  // 回错误
            close();  // 关闭连接
            return;
        }
        qCWarning(lcSimulator) << "session" << m_id << "fatal stream error";  // 致命错误
        close();  // 关闭连接
        return;
    }
}

void SimulatorSession::handleMessage(const DecodedMessage &message)
{
    const MessageType type = message.header.type;  // 报文头里的消息类型

    if (!m_handshaken) {  // 未握手阶段
        if (type != MessageType::Hello) {  // 第一条必须是 HELLO
            sendError(message.header.requestId, 2, QStringLiteral("expected HELLO"));
            close();
            return;
        }

        const auto request = decodeHelloRequest(message.payload);  // 解码 HELLO 请求
        if (!request.hasValue()) {  // 解码失败
            sendError(message.header.requestId, 3, QStringLiteral("malformed HELLO"));
            close();
            return;
        }

        const HelloResponse response{1, QStringLiteral("DS-SIM-01"), 0x1u, AckResult::Ok};  // 构造响应
        const auto payload = encodeHelloResponse(response);  // 编码响应体
        if (!payload.hasValue()) {  // 编码失败
            close();
            return;
        }
        sendFrame(MessageType::Hello, kFlagResponse, message.header.requestId, payload.value());  // 发响应
        m_handshaken = true;  // 标记握手完成
        qCInfo(lcSimulator) << "session" << m_id << "handshake from"
                            << request.value().clientName;  // 记录客户端名
        m_telemetryTimer.start();  // 握手后开始周期发遥测
        return;
    }

    switch (type) {  // 已握手，按类型分发
    case MessageType::Heartbeat:  // 心跳
        sendFrame(MessageType::Heartbeat, kFlagResponse, message.header.requestId,
                  message.payload);  // 原样回显 payload
        break;

    case MessageType::Command: {  // 控制命令
        const auto command = decodeCommand(message.payload);  // 解码命令
        if (!command.hasValue()) {  // 解码失败
            sendError(message.header.requestId, 4, QStringLiteral("malformed COMMAND"));
            return;
        }

        QString text;
        switch (command.value().commandId) {  // 根据命令 ID 操作设备模型
        case CommandId::Start:
            m_device.start();
            text = QStringLiteral("已启动");
            break;
        case CommandId::Stop:
            m_device.stop();
            text = QStringLiteral("已停止");
            break;
        case CommandId::Reset:
            m_device.reset();
            text = QStringLiteral("已复位");
            break;
        }

        const CommandAck ack{command.value().commandId, AckResult::Ok, text};  // 构造 ACK
        const auto payload = encodeCommandAck(ack);  // 编码 ACK
        if (payload.hasValue()) {
            sendFrame(MessageType::CommandAck, kFlagResponse, message.header.requestId,
                      payload.value());  // 发 ACK
        }
        qCInfo(lcSimulator) << "session" << m_id << "command" << text;  // 记录命令结果
        break;
    }

    case MessageType::Hello:  // 重复 HELLO
        qCWarning(lcSimulator) << "session" << m_id << "duplicate HELLO ignored";  // 忽略
        break;

    default:  // 未知类型
        qCWarning(lcSimulator) << "session" << m_id << "unknown message type"
                               << static_cast<int>(type);  // 记录
        break;
    }
}

void SimulatorSession::onTelemetryTick()
{
    if (!m_handshaken || m_closing) {  // 未握手或正在关闭则不发
        return;
    }

    const quint64 timestampMs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());  // 当前 UTC 毫秒
    const DeviceModel::Sample sample = m_device.sample(timestampMs);  // 采样一次

    const auto telemetryPayload = encodeTelemetry(sample.telemetry);  // 编码遥测
    if (telemetryPayload.hasValue()) {
        sendFrame(MessageType::Telemetry, kFlagNone, 0, telemetryPayload.value());  // 发遥测
    }

    if (sample.alarm.has_value()) {  // 采样同时产生报警
        const auto alarmPayload = encodeAlarm(*sample.alarm);  // 编码报警
        if (alarmPayload.hasValue()) {
            sendFrame(MessageType::Alarm, kFlagNone, 0, alarmPayload.value());  // 发报警
        }
    }
}

void SimulatorSession::sendFrame(MessageType type, quint16 flags, quint32 requestId,
                                 const QByteArray &payload)
{
    const auto frame = encode(type, flags, requestId, payload);  // 编出完整报文（头+体）
    if (!frame.hasValue()) {  // 编码失败
        qCWarning(lcSimulator) << "session" << m_id << "encode failed";
        return;
    }
    m_socket->write(frame.value());  // 写入 socket 发送
}

void SimulatorSession::sendError(quint32 requestId, quint16 code, const QString &text)
{
    // 把错误单独encode编码
    const Result<QByteArray> payload = encodeError(Error{code, text});  // 编码 ERROR 消息体
    if (payload.hasValue()) {  // 编码成功才发
        sendFrame(MessageType::Error, kFlagResponse, requestId, payload.value());  // 以响应标志发错误
    }
}

} // namespace devicescope::simulator