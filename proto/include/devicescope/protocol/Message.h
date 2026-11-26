#pragma once

#include "devicescope/protocol/Protocol.h"
#include "devicescope/protocol/Result.h"

#include <QtCore/QByteArray>
#include <QtCore/QByteArrayView>
#include <QtCore/QString>

namespace devicescope::protocol {

// 报警级别。
enum class AlarmSeverity : quint8 {
    Info = 0,
    Warning = 1,
    Critical = 2,
};

// 报警状态：产生或清除。
enum class AlarmState : quint8 {
    Raised = 0,
    Cleared = 1,
};


// 下面是不同 MssageType 的具体内容
// HELLO 请求：客户端声明自己的名称，版本协商使用报文头 Version。
struct HelloRequest {
    QString clientName;
};

// HELLO 响应：设备身份、能力位与握手结果。
struct HelloResponse {
    quint16 deviceId = 0;
    QString deviceName;
    quint32 capabilities = 0;
    AckResult result = AckResult::Ok;
};

// HEARTBEAT：UTC 毫秒时间戳。
struct Heartbeat {
    quint64 timestampMs = 0;
};

// TELEMETRY：一次遥测采样。
struct Telemetry {
    quint64 timestampMs = 0;
    float temperatureC = 0.0f;
    float pressureKPa = 0.0f;
    float rpm = 0.0f;
    DeviceState deviceState = DeviceState::Idle;
};

// COMMAND：控制命令；parameter 第一版填 0，预留后续扩展。
struct Command {
    CommandId commandId = CommandId::Start;
    quint32 parameter = 0;
};

// COMMAND_ACK：命令执行结果。
struct CommandAck {
    CommandId commandId = CommandId::Start;
    AckResult result = AckResult::Ok;
    QString message;
};

// ALARM：报警事件。
struct Alarm {
    quint32 alarmId = 0;
    quint16 code = 0;
    AlarmSeverity severity = AlarmSeverity::Info;
    quint64 timestampMs = 0;
    AlarmState state = AlarmState::Raised;
    QString message;
};

// ERROR：协议或设备错误。
struct Error {
    quint16 code = 0;
    QString message;
};

// 完整报文 = 固定 22 字节报文头 + 消息体 payload。消息体单独编解码。
Result<QByteArray> encodeHelloRequest(const HelloRequest &message);
Result<HelloRequest> decodeHelloRequest(QByteArrayView payload);

Result<QByteArray> encodeHelloResponse(const HelloResponse &message);
Result<HelloResponse> decodeHelloResponse(QByteArrayView payload);

Result<QByteArray> encodeHeartbeat(const Heartbeat &message);
Result<Heartbeat> decodeHeartbeat(QByteArrayView payload);

Result<QByteArray> encodeTelemetry(const Telemetry &message);
Result<Telemetry> decodeTelemetry(QByteArrayView payload);

Result<QByteArray> encodeCommand(const Command &message);
Result<Command> decodeCommand(QByteArrayView payload);

Result<QByteArray> encodeCommandAck(const CommandAck &message);
Result<CommandAck> decodeCommandAck(QByteArrayView payload);

Result<QByteArray> encodeAlarm(const Alarm &message);
Result<Alarm> decodeAlarm(QByteArrayView payload);

Result<QByteArray> encodeError(const Error &message);
Result<Error> decodeError(QByteArrayView payload);

} // namespace devicescope::protocol