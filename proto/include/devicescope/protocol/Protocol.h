#pragma once

#include <QtCore/QtGlobal>

namespace devicescope::protocol {

// 协议魔数：ASCII "DS"，按大端写出的字节序列为 0x44 0x53。
inline constexpr quint16 kMagic = 0x4453;

// 协议版本：首版固定为 1，握手阶段协商；不兼容变更时递增。
inline constexpr quint8 kProtocolVersion = 1;

// 固定报文头长度，共 22 字节，所有多字节字段使用网络字节序（大端）：
// Magic(2) + Version(1) + Type(1) + Flags(2) + RequestId(4)  + PayloadLength(4) + HeaderCrc(4) + PayloadCrc(4)
inline constexpr int kHeaderSize = 22;

// Header CRC 覆盖的字节数：从 Magic 到 Payload Length，共 14 字节。
inline constexpr int kHeaderCrcCoveredSize = 14;

// 单个报文负载上限 1 MiB，超过上限按致命错误处理。
inline constexpr quint32 kMaxPayloadSize = 1024u * 1024u;

// 字符串长度字段是 u16，因此 UTF-8 编码后的字节数最多 65535。
inline constexpr quint16 kMaxStringBytes = 0xFFFF;

// Flags 位定义（位于报文头偏移 4，共 2 字节）。
inline constexpr quint16 kFlagNone = 0x0000;
inline constexpr quint16 kFlagResponse = 0x0001; // bit0：该报文是一条响应

// 消息类型：位于报文头偏移 3。
enum class MessageType : quint8 {
    Hello = 0x01,
    Heartbeat = 0x02,
    Telemetry = 0x03,
    Command = 0x04,
    CommandAck = 0x05,
    Alarm = 0x06,
    Error = 0x07,
};

// 设备运行状态：用于 TELEMETRY 与界面展示。
enum class DeviceState : quint8 {
    Idle = 0,
    Running = 1,
    Stopped = 2,
    Fault = 3,
};

// 控制命令编号：用于 COMMAND 与 COMMAND_ACK。
enum class CommandId : quint8 {
    Start = 1,
    Stop = 2,
    Reset = 3,
};

// 命令/握手结果：用于 HELLO 响应与 COMMAND_ACK。
enum class AckResult : quint8 {
    Ok = 0,
    Rejected = 1,
    Failed = 2,
};

// 解码后的报文头（不含负载）。
struct Header {
    MessageType type = MessageType::Error;
    quint16 flags = kFlagNone;
    quint32 requestId = 0;
    quint32 payloadLength = 0;
    quint32 payloadCrc = 0;
};

} // namespace devicescope::protocol