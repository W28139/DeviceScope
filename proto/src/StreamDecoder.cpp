#include "devicescope/protocol/StreamDecoder.h"

#include "devicescope/protocol/Codec.h"

#include <QtCore/QtEndian>

#include <algorithm>
#include <cstring>

namespace devicescope::protocol {

namespace {

// 内部缓冲上限：两个最大报文。
constexpr qsizetype kMaxBufferedBytes =
    2 * (kHeaderSize + static_cast<qsizetype>(kMaxPayloadSize));

quint16 readU16(QByteArrayView data)
{
    quint16 value = 0;
    std::memcpy(&value, data.data(), sizeof value);
    return qFromBigEndian(value);
}

quint32 readU32(QByteArrayView data, qsizetype offset)
{
    quint32 value = 0;
    std::memcpy(&value, data.data() + offset, sizeof value);
    return qFromBigEndian(value);
}

} // namespace

void StreamDecoder::append(const QByteArray &data)
{
    if (m_fatal || data.isEmpty()) {
        return;
    }
    if (m_buffer.size() + data.size() > kMaxBufferedBytes) {
        m_fatal = true;
        return;
    }
    m_buffer.append(data);
    m_maxBufferSize = std::max(m_maxBufferSize, m_buffer.size());
}

StreamStatus StreamDecoder::next(DecodedMessage &message)
{
    if (m_fatal) {
        return StreamStatus::FatalError;
    }

    // 魔数重同步：只看已到达字节。
    // 第一个字节不对，说明当前起点不是帧头，丢 1 字节滑动窗口
    if (!m_buffer.isEmpty()
        && static_cast<quint8>(m_buffer.at(0)) != static_cast<quint8>(kMagic >> 8)) {
        dropBytes(1);
        return StreamStatus::BadPacket;
    }
    // 前两字节拼起来不是魔数，同样丢 1 字节继续找。
    if (m_buffer.size() >= 2 && readU16(m_buffer) != kMagic) {
        dropBytes(1);
        return StreamStatus::BadPacket;
    }

    // 半包：报文头不完整，等待更多数据。
    if (m_buffer.size() < kHeaderSize) {
        return StreamStatus::NeedMoreData;
    }

    const Result<Header> header = decodeHeader(m_buffer);
    if (!header.hasValue()) {
        switch (header.error()) {
        case ErrorCode::InvalidMagic:
            // 魔数错：帧头不可信，长度字段也不可信，只能逐字节重同步。
            dropBytes(1);
            return StreamStatus::BadPacket;
        case ErrorCode::UnsupportedVersion: {
            // 版本不支持，但魔数已对，长度字段位置可信：
            // 长度合法就按长度丢整包，避免逐字节扫描。
            const quint32 length = readU32(m_buffer, 10);
            if (length > kMaxPayloadSize) {
                m_fatal = true;
                return StreamStatus::FatalError;
            }
            const qsizetype total = kHeaderSize + static_cast<qsizetype>(length);
            if (m_buffer.size() < total) {
                return StreamStatus::NeedMoreData;
            }
            dropBytes(total);
            return StreamStatus::BadPacket;
        }
        case ErrorCode::PayloadTooLarge:
            // 长度字段本身不可信或超出协议上限，流已无法安全恢复。
            m_fatal = true;
            return StreamStatus::FatalError;
        case ErrorCode::TruncatedHeader:
            return StreamStatus::NeedMoreData;
        default:
            // Header CRC 错误等：头部字段不可信，长度不可信，
            // 不能按长度跳，只能丢 1 字节重新找魔数。
            dropBytes(1);
            return StreamStatus::BadPacket;
        }
    }

    const Header &headerValue = header.value();
    const qsizetype total = kHeaderSize + static_cast<qsizetype>(headerValue.payloadLength);
    if (m_buffer.size() < total) {
        return StreamStatus::NeedMoreData;
    }

    const QByteArray payload = m_buffer.mid(kHeaderSize, total - kHeaderSize);
    // 头可信、长度可信，只是 payload 校验失败：
    // 可以安全地丢整包。
    if (verifyPayload(headerValue, payload) != ErrorCode::Ok) {
        dropBytes(total);
        return StreamStatus::BadPacket;
    }

    // 已获取完整的帧，放入 message 返回
    message.header = headerValue;
    message.payload = payload;
    m_buffer.remove(0, total);
    ++m_messageCount;
    return StreamStatus::Message;
}

StreamStats StreamDecoder::stats() const
{
    StreamStats result;
    result.messageCount = m_messageCount;
    result.badPacketCount = m_badPacketCount;
    result.droppedBytes = m_droppedBytes;
    result.bufferSize = m_buffer.size();
    result.maxBufferSize = m_maxBufferSize;
    return result;
}

void StreamDecoder::dropBytes(qsizetype count)
{
    if (count <= 0) {
        return;
    }
    const qsizetype actual = std::min(count, m_buffer.size());
    m_buffer.remove(0, actual);
    m_droppedBytes += static_cast<quint64>(actual);
    ++m_badPacketCount;
}

} // namespace devicescope::protocol