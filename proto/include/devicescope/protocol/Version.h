#pragma once

#include <QtCore/QString>
#include <QtCore/QtGlobal>

namespace devicescope::protocol {

// 协议魔数：ASCII "DS"，按大端写出的字节序列为 0x44 0x53。
inline constexpr quint16 kMagic = 0x4453;

// 协议版本：首版固定为 1，握手阶段协商；不兼容变更时递增。
inline constexpr quint8 kProtocolVersion = 1;

// 固定报文头长度，共 22 字节，所有多字节字段使用网络字节序（大端）
// Magic(2) + Version(1) + Type(1) + Flags(2) + RequestId(4) + PayloadLength(4) + HeaderCrc(4) + PayloadCrc(4)
// 魔数(2字节) + 版本(1字节) + 类型(1字节) + 标志位(2字节) + 请求ID(4字节) + 负载长度(4字节) + 头部CRC(4字节) + 负载CRC(4字节)
inline constexpr int kHeaderSize = 22;

// 单个报文负载上限 1 MiB。
inline constexpr quint32 kMaxPayloadSize = 1024 * 1024;

QString versionString();

} // namespace devicescope::protocol