#pragma once

#include "devicescope/protocol/Protocol.h"
#include "devicescope/protocol/Result.h"

#include <QtCore/QByteArray>
#include <QtCore/QByteArrayView>

namespace devicescope::protocol {

// 编码一个完整报文：22 字节报文头 + 负载，自动计算 Header CRC 与 Payload CRC。
// 负载超过 kMaxPayloadSize 时返回 PayloadTooLarge。
Result<QByteArray> encode(MessageType type, quint16 flags, quint32 requestId,
                          QByteArrayView payload);

// 解析报文头（至少 22 字节），依次校验魔数、版本、长度上限和 Header CRC。
// 只读取前 22 字节，忽略调用方传入的多余数据。
Result<Header> decodeHeader(QByteArrayView frame);

// [test] 校验负载长度与 CRC；payload 必须恰好等于报文头声明的长度。
ErrorCode verifyPayload(const Header &header, QByteArrayView payload);

} // namespace devicescope::protocol