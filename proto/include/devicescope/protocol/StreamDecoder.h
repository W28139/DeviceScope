#pragma once

#include "devicescope/protocol/Protocol.h"

#include <QtCore/QByteArray>
#include <QtCore/QtGlobal>

namespace devicescope::protocol {

// 一次成功解包的结果：报文头 + 原始负载（消息体由调用方按类型解码）。
struct DecodedMessage {
    Header header;
    QByteArray payload;
};

// next() 的返回值。
enum class StreamStatus : quint8 {
    Message,      // 成功取出一包，message 有效
    NeedMoreData, // 数据不足，等待下一次 append
    BadPacket,    // 丢弃了一个坏包/坏字节，调用方可以继续 next()
    FatalError,   // 长度或缓冲超限，调用方必须断开连接
};

// 解包统计。
struct StreamStats {
    quint64 messageCount = 0;   // 成功解出的报文数
    quint64 badPacketCount = 0; // 丢弃的坏包/坏字节次数
    quint64 droppedBytes = 0;   // 累计丢弃字节数
    qsizetype bufferSize = 0;   // 当前缓冲字节数
    qsizetype maxBufferSize = 0; // 历史最大缓冲字节数
};

// 把 TCP 字节流拆成一个个完整报文。
// 处理半包保留、粘包逐包取出、坏魔数逐字节重同步、
// 版本/CRC 错误丢弃整包，以及长度和缓冲上限。
class StreamDecoder
{
public:
    void append(const QByteArray &data);

    // 取出下一包；返回 Message 时 message 有效，其余情况不改动 message。
    StreamStatus next(DecodedMessage &message);

    StreamStats stats() const;

private:
    void dropBytes(qsizetype count);

    QByteArray m_buffer;
    bool m_fatal = false;
    quint64 m_messageCount = 0;
    quint64 m_badPacketCount = 0;
    quint64 m_droppedBytes = 0;
    qsizetype m_maxBufferSize = 0;
};

} // namespace devicescope::protocol