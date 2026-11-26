#include <QtTest>

#include "devicescope/protocol/Codec.h"
#include "devicescope/protocol/Message.h"
#include "devicescope/protocol/StreamDecoder.h"

#include <QtCore/QtEndian>

#include <algorithm>
#include <cstring>
#include <random>

using namespace devicescope::protocol;

namespace {

int status(StreamStatus value)
{
    return static_cast<int>(value);
}

} // namespace

class StreamTest final : public QObject
{
    Q_OBJECT

private slots:
    void goldenVector();
    void byteByByteAndMultiple();
    void badPacketRecovery();
    void fatalLimits();
    void unknownMessageType();
    void randomFragmentation();
};

void StreamTest::goldenVector()
{
    // 手工十六进制黄金向量（CRC 由独立实现计算，不经过被测代码）：
    // HEARTBEAT，RequestId=0，负载 01 02 03 04 05 06 07 08
    const QByteArray frame = QByteArray::fromHex(
        "4453010200000000000000000008f0fc270e3fca88c50102030405060708");

    StreamDecoder decoder;
    decoder.append(frame);

    DecodedMessage message;
    QCOMPARE(status(decoder.next(message)), status(StreamStatus::Message));
    QCOMPARE(static_cast<int>(message.header.type), static_cast<int>(MessageType::Heartbeat));
    QCOMPARE(message.header.requestId, 0u);
    QCOMPARE(message.header.payloadLength, 8u);
    QCOMPARE(message.payload, QByteArray::fromHex("0102030405060708"));

    const auto heartbeat = decodeHeartbeat(message.payload);
    QVERIFY(heartbeat.hasValue());
    QCOMPARE(heartbeat.value().timestampMs, 0x0102030405060708ULL);

    QCOMPARE(decoder.stats().messageCount, quint64(1));
    QCOMPARE(decoder.stats().bufferSize, qsizetype(0));
}

void StreamTest::byteByByteAndMultiple()
{
    const auto first = encode(MessageType::Telemetry, kFlagNone, 1u, QByteArray::fromHex("0102"));
    QVERIFY(first.hasValue());
    const auto empty = encode(MessageType::Heartbeat, kFlagNone, 2u, QByteArray());
    QVERIFY(empty.hasValue());

    // 一字节一字节喂入：除最后一字节外都应返回 NeedMoreData
    StreamDecoder decoder;
    DecodedMessage message;

    for (qsizetype index = 0; index < first.value().size(); ++index) {
        decoder.append(first.value().mid(index, 1));
        const StreamStatus result = decoder.next(message);
        if (index + 1 < first.value().size()) {
            QCOMPARE(status(result), status(StreamStatus::NeedMoreData));
        } else {
            QCOMPARE(status(result), status(StreamStatus::Message));
            QCOMPARE(message.payload, QByteArray::fromHex("0102"));
        }
    }

    // 粘包 + 空负载：一次 append 两帧
    decoder.append(empty.value() + empty.value());
    QCOMPARE(status(decoder.next(message)), status(StreamStatus::Message));
    QVERIFY(message.payload.isEmpty());
    QCOMPARE(status(decoder.next(message)), status(StreamStatus::Message));
    QVERIFY(message.payload.isEmpty());
    QCOMPARE(status(decoder.next(message)), status(StreamStatus::NeedMoreData));
    QCOMPARE(decoder.stats().messageCount, quint64(3));
}

void StreamTest::badPacketRecovery()
{
    const auto good = encode(MessageType::Error, kFlagNone, 9u, QByteArrayLiteral("ok"));
    QVERIFY(good.hasValue());

    QByteArray badCrc = good.value();
    badCrc[badCrc.size() - 1] = static_cast<char>(badCrc.at(badCrc.size() - 1) ^ 0x7F);

    QByteArray badVersion = good.value();
    badVersion[2] = 0x02;

    QByteArray stream = QByteArray::fromHex("00ff");
    stream += good.value();
    stream += badCrc;
    stream += badVersion;
    stream += good.value();

    StreamDecoder decoder;
    decoder.append(stream);

    int messages = 0;
    int badPackets = 0;
    int unsupportedVersions = 0;
    DecodedMessage message;

    while (true) {
        const StreamStatus result = decoder.next(message);
        if (result == StreamStatus::Message) {
            ++messages;
            QCOMPARE(message.payload, QByteArrayLiteral("ok"));
            continue;
        }
        if (result == StreamStatus::BadPacket) {
            ++badPackets;
            continue;
        }
        if (result == StreamStatus::UnsupportedVersion) {
            ++unsupportedVersions;
            continue;
        }
        QCOMPARE(status(result), status(StreamStatus::NeedMoreData));
        break;
    }

    QCOMPARE(messages, 2);
    QCOMPARE(badPackets, 3);           // 2 个垃圾字节 + 1 个坏 CRC
    QCOMPARE(unsupportedVersions, 1);  // 1 个坏版本
    QCOMPARE(decoder.stats().badPacketCount, quint64(4));
    QCOMPARE(decoder.stats().droppedBytes,
             quint64(2 + badCrc.size() + badVersion.size()));
}

void StreamTest::fatalLimits()
{
    // 最大合法负载可以解出
    {
        const QByteArray payload(static_cast<qsizetype>(kMaxPayloadSize), 'z');
        const auto frame = encode(MessageType::Telemetry, kFlagNone, 1u, payload);
        QVERIFY(frame.hasValue());

        StreamDecoder decoder;
        decoder.append(frame.value());
        DecodedMessage message;
        QCOMPARE(status(decoder.next(message)), status(StreamStatus::Message));
        QCOMPARE(message.payload.size(), payload.size());
        QCOMPARE(status(decoder.next(message)), status(StreamStatus::NeedMoreData));
    }

    // 长度字段超过 1 MiB：致命错误
    {
        const auto frame = encode(MessageType::Heartbeat, kFlagNone, 1u, QByteArray::fromHex("00"));
        QVERIFY(frame.hasValue());
        QByteArray oversize = frame.value();
        const quint32 length = qToBigEndian(kMaxPayloadSize + 1u);
        std::memcpy(oversize.data() + 10, &length, sizeof length);

        StreamDecoder decoder;
        decoder.append(oversize);
        DecodedMessage message;
        QCOMPARE(status(decoder.next(message)), status(StreamStatus::FatalError));
        QCOMPARE(status(decoder.next(message)), status(StreamStatus::FatalError));
    }

    // 缓冲超过两个最大报文：致命错误
    {
        const qsizetype limit = 2 * (kHeaderSize + static_cast<qsizetype>(kMaxPayloadSize));
        StreamDecoder decoder;
        decoder.append(QByteArray(limit + 1, 'x'));
        DecodedMessage message;
        QCOMPARE(status(decoder.next(message)), status(StreamStatus::FatalError));
    }
}

void StreamTest::unknownMessageType()
{
    const auto frame =
        encode(static_cast<MessageType>(0x7F), kFlagNone, 5u, QByteArray::fromHex("deadbeef"));
    QVERIFY(frame.hasValue());

    StreamDecoder decoder;
    decoder.append(frame.value());

    DecodedMessage message;
    QCOMPARE(status(decoder.next(message)), status(StreamStatus::Message));
    QCOMPARE(static_cast<int>(message.header.type), 0x7F);
    QCOMPARE(message.payload, QByteArray::fromHex("deadbeef"));
}

void StreamTest::randomFragmentation()
{
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> chunkDistribution(1, 4);

    // 固定种子执行 1000 次随机切分
    for (int trial = 0; trial < 1000; ++trial) {
        QByteArray stream;
        QList<QByteArray> expected;
        for (int index = 0; index < 5; ++index) {
            const QByteArray payload =
                QByteArray::number(index)
                + QByteArray(3 + index, static_cast<char>('a' + index));
            expected.append(payload);

            const auto frame = encode(MessageType::Telemetry, kFlagNone,
                                      static_cast<quint32>(index + 1), payload);
            QVERIFY(frame.hasValue());
            stream.append(frame.value());
        }

        StreamDecoder decoder;
        qsizetype offset = 0;
        int received = 0;
        bool failed = false;
        DecodedMessage message;

        while (offset < stream.size() && !failed) {
            const int size = std::min(chunkDistribution(rng),
                                      static_cast<int>(stream.size() - offset));
            decoder.append(stream.mid(offset, size));
            offset += size;

            while (true) {
                const StreamStatus result = decoder.next(message);
                if (result == StreamStatus::Message) {
                    if (received >= expected.size()
                        || message.payload != expected.at(received)) {
                        failed = true;
                        break;
                    }
                    ++received;
                    continue;
                }
                if (result == StreamStatus::NeedMoreData) {
                    break;
                }
                failed = true;
                break;
            }
        }

        QVERIFY(!failed);
        QCOMPARE(received, static_cast<int>(expected.size()));
    }
}

QTEST_GUILESS_MAIN(StreamTest)

#include "test_stream.moc"