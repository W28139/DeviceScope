#pragma once

#include "DeviceModel.h"
#include "SimulatorServer.h"

#include "devicescope/protocol/StreamDecoder.h"

#include <QtCore/QObject>
#include <QtCore/QTimer>

class QTcpSocket;

namespace devicescope::simulator {

// 单个客户端连接：负责握手、心跳、命令、遥测与报警的协议交互。
class SimulatorSession final : public QObject
{
    Q_OBJECT

public:
    SimulatorSession(QTcpSocket *socket, const SimulatorConfig &config, quint32 id,
                     QObject *parent = nullptr);

    quint32 id() const;
    void close();

private:
    void onReadyRead();     // socket 可读回调：把新数据喂给解码器并处理消息
    void onTelemetryTick();  // 遥测定时器回调：周期性采集并发送 TELEMETRY
    void handleMessage(const devicescope::protocol::DecodedMessage &message);   // 按消息类型分发处理已解码的报文

    void sendFrame(devicescope::protocol::MessageType type, quint16 flags,  // 发送一帧
                   quint32 requestId, const QByteArray &payload);   
    void sendError(quint32 requestId, quint16 code, const QString &text);   // 发送 ERROR 消息

    QTcpSocket *m_socket = nullptr;                 // TCP 连接，收发字节流
    devicescope::protocol::StreamDecoder m_decoder; // 流式解码器，负责粘包/半包/重同步
    DeviceModel m_device;       // 设备状态模型，保存温度、压力、转速等当前值
    QTimer m_telemetryTimer;    // 遥测发送定时器
    int m_rateHz = 10;          // 遥测发送频率，单位 Hz
    quint32 m_id = 0;           // 请求/消息 ID 计数器，用于关联请求与响应
    bool m_handshaken = false;  // 是否已完成 HELLO 握手，未握手时可能拒绝业务消息
    bool m_closing = false;     // 是否正在关闭连接，避免关闭过程中继续处理
};

} // namespace devicescope::simulator