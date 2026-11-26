#include "devicescope/protocol/Codec.h"

#include "devicescope/protocol/Crc32.h"

#include <QtCore/QtEndian>

#include <cstring>
#include <utility>

namespace devicescope::protocol {

namespace {

void appendU16(QByteArray &buffer, quint16 value)
{
    const quint16 be = qToBigEndian(value);
    buffer.append(reinterpret_cast<const char *>(&be), sizeof be);
}

void appendU32(QByteArray &buffer, quint32 value)
{
    const quint32 be = qToBigEndian(value);
    buffer.append(reinterpret_cast<const char *>(&be), sizeof be);
}

quint16 readU16(QByteArrayView data, qsizetype offset)
{
    quint16 value = 0;
    std::memcpy(&value, data.data() + offset, sizeof value);
    return qFromBigEndian(value);
}

quint32 readU32(QByteArrayView data, qsizetype offset)
{
    quint32 value = 0;
    std::memcpy(&value, data.data() + offset, sizeof value);
    return qFromBigEndian(value);
}

} // namespace

Result<QByteArray> encode(MessageType type, quint16 flags, quint32 requestId,
                          QByteArrayView payload)
{
    if (static_cast<quint64>(payload.size()) > kMaxPayloadSize) {
        return Result<QByteArray>::failure(ErrorCode::PayloadTooLarge);
    }

    QByteArray frame;
    frame.reserve(kHeaderSize + payload.size());

    // 前 14 字节：Magic ~ Payload Length，Header CRC 的覆盖范围。
    appendU16(frame, kMagic);
    frame.append(static_cast<char>(kProtocolVersion));
    frame.append(static_cast<char>(static_cast<quint8>(type)));
    appendU16(frame, flags);
    appendU32(frame, requestId);
    appendU32(frame, static_cast<quint32>(payload.size()));

    appendU32(frame, crc32(
                         QByteArrayView(frame.constData(), kHeaderCrcCoveredSize)));
    appendU32(frame, crc32(payload));

    if (!payload.isEmpty()) {
        frame.append(payload.data(), payload.size());
    }

    return Result<QByteArray>::success(std::move(frame));
}

Result<Header> decodeHeader(QByteArrayView frame)
{
    if (frame.size() < kHeaderSize) {
        return Result<Header>::failure(ErrorCode::TruncatedHeader);
    }

    if (readU16(frame, 0) != kMagic) {
        return Result<Header>::failure(ErrorCode::InvalidMagic);
    }

    const quint8 version = static_cast<quint8>(frame[2]);
    if (version != kProtocolVersion) {
        return Result<Header>::failure(ErrorCode::UnsupportedVersion);
    }

    Header header;
    header.type = static_cast<MessageType>(static_cast<quint8>(frame[3]));
    header.flags = readU16(frame, 4);
    header.requestId = readU32(frame, 6);
    header.payloadLength = readU32(frame, 10);
    header.payloadCrc = readU32(frame, 18);

    if (header.payloadLength > kMaxPayloadSize) {
        return Result<Header>::failure(ErrorCode::PayloadTooLarge);
    }

    const quint32 headerCrc = readU32(frame, 14);
    const quint32 expected = crc32(frame.first(kHeaderCrcCoveredSize));
    if (headerCrc != expected) {
        return Result<Header>::failure(ErrorCode::HeaderCrcMismatch);
    }

    return Result<Header>::success(header);
}

ErrorCode verifyPayload(const Header &header, QByteArrayView payload)
{
    if (static_cast<quint64>(payload.size()) < header.payloadLength) {
        return ErrorCode::TruncatedPayload;
    }
    if (static_cast<quint64>(payload.size()) > header.payloadLength) {
        return ErrorCode::PayloadLengthMismatch;
    }
    if (crc32(payload) != header.payloadCrc) {
        return ErrorCode::PayloadCrcMismatch;
    }
    return ErrorCode::Ok;
}

} // namespace devicescope::protocol