#include <QtTest>

#include "devicescope/protocol/Codec.h"
#include "devicescope/protocol/Crc32.h"
#include "devicescope/protocol/Message.h"

#include <limits>

using namespace devicescope::protocol;

namespace {

int code(ErrorCode value)
{
    return static_cast<int>(value);
}

} // namespace

class CodecTest final : public QObject
{
    Q_OBJECT

private slots:
    void crc32KnownVector();
    void headerEncodeDecodeAndValidation();
    void payloadSizeLimits();
    void messageRoundTrips();
    void malformedPayloadRejected();
};

void CodecTest::crc32KnownVector()
{
    // CRC-32/ISO-HDLC 参考向量："123456789" → 0xCBF43926
    QCOMPARE(crc32(QByteArrayView("123456789", 9)), 0xCBF43926u);
}

void CodecTest::headerEncodeDecodeAndValidation()
{
    const QByteArray payload = QByteArray::fromHex("0102");
    const auto encoded = encode(MessageType::Heartbeat, kFlagResponse, 0x11223344u, payload);
    QVERIFY(encoded.hasValue());

    const QByteArray frame = encoded.value();
    QCOMPARE(frame.size(), static_cast<qsizetype>(kHeaderSize) + payload.size());

    const auto decoded = decodeHeader(frame);
    QVERIFY(decoded.hasValue());
    QCOMPARE(static_cast<int>(decoded.value().type),
             static_cast<int>(MessageType::Heartbeat));
    QCOMPARE(decoded.value().flags, kFlagResponse);
    QCOMPARE(decoded.value().requestId, 0x11223344u);
    QCOMPARE(decoded.value().payloadLength, static_cast<quint32>(payload.size()));
    QCOMPARE(code(verifyPayload(decoded.value(), payload)), code(ErrorCode::Ok));

    // 头部校验：截断、魔数、版本、Header CRC
    QCOMPARE(code(decodeHeader(frame.first(kHeaderSize - 1)).error()),
             code(ErrorCode::TruncatedHeader));

    QByteArray broken = frame;
    broken[0] = 0x00;
    QCOMPARE(code(decodeHeader(broken).error()), code(ErrorCode::InvalidMagic));

    broken = frame;
    broken[2] = 0x02;
    QCOMPARE(code(decodeHeader(broken).error()), code(ErrorCode::UnsupportedVersion));

    broken = frame;
    broken[6] = static_cast<char>(broken[6] ^ 0x7F);
    QCOMPARE(code(decodeHeader(broken).error()), code(ErrorCode::HeaderCrcMismatch));
}

void CodecTest::payloadSizeLimits()
{
    // 恰好 1 MiB 允许编码
    const QByteArray maxPayload(static_cast<qsizetype>(kMaxPayloadSize), 'x');
    const auto maxFrame = encode(MessageType::Telemetry, kFlagNone, 1u, maxPayload);
    QVERIFY(maxFrame.hasValue());
    QCOMPARE(maxFrame.value().size(),
             static_cast<qsizetype>(kHeaderSize) + maxPayload.size());

    // 1 MiB + 1 必须被拒绝
    QByteArray tooLarge = maxPayload;
    tooLarge.append('x');
    const auto rejected = encode(MessageType::Telemetry, kFlagNone, 1u, tooLarge);
    QVERIFY(!rejected.hasValue());
    QCOMPARE(code(rejected.error()), code(ErrorCode::PayloadTooLarge));
}

void CodecTest::messageRoundTrips()
{
    // HELLO 请求：中文与空字符串
    const HelloRequest helloRequest{QString::fromUtf8("客户端-Δ")};
    const auto helloPayload = encodeHelloRequest(helloRequest);
    QVERIFY(helloPayload.hasValue());
    const auto hello = decodeHelloRequest(helloPayload.value());
    QVERIFY(hello.hasValue());
    QCOMPARE(hello.value().clientName, helloRequest.clientName);

    const HelloRequest emptyHello;
    const auto emptyHelloPayload = encodeHelloRequest(emptyHello);
    QVERIFY(emptyHelloPayload.hasValue());
    const auto emptyHelloDecoded = decodeHelloRequest(emptyHelloPayload.value());
    QVERIFY(emptyHelloDecoded.hasValue());
    QCOMPARE(emptyHelloDecoded.value().clientName, QString());

    // HELLO 响应：u16/u32 边界值
    const HelloResponse helloResponse{65535, QStringLiteral("DS-SIM"), 0xFFFFFFFFu,
                                      AckResult::Rejected};
    const auto responsePayload = encodeHelloResponse(helloResponse);
    QVERIFY(responsePayload.hasValue());
    const auto response = decodeHelloResponse(responsePayload.value());
    QVERIFY(response.hasValue());
    QCOMPARE(response.value().deviceId, quint16(65535));
    QCOMPARE(response.value().deviceName, helloResponse.deviceName);
    QCOMPARE(response.value().capabilities, 0xFFFFFFFFu);
    QCOMPARE(static_cast<int>(response.value().result),
             static_cast<int>(AckResult::Rejected));

    // HEARTBEAT：u64 边界值
    const Heartbeat heartbeat{std::numeric_limits<quint64>::max()};
    const auto heartbeatPayload = encodeHeartbeat(heartbeat);
    QVERIFY(heartbeatPayload.hasValue());
    const auto heartbeatDecoded = decodeHeartbeat(heartbeatPayload.value());
    QVERIFY(heartbeatDecoded.hasValue());
    QCOMPARE(heartbeatDecoded.value().timestampMs, heartbeat.timestampMs);

    // TELEMETRY：负数
    const Telemetry telemetry{1731234567890ULL, -40.5f, 101.325f, -123.25f,
                              DeviceState::Fault};
    const auto telemetryPayload = encodeTelemetry(telemetry);
    QVERIFY(telemetryPayload.hasValue());
    const auto telemetryDecoded = decodeTelemetry(telemetryPayload.value());
    QVERIFY(telemetryDecoded.hasValue());
    QCOMPARE(telemetryDecoded.value().timestampMs, telemetry.timestampMs);
    QCOMPARE(telemetryDecoded.value().temperatureC, telemetry.temperatureC);
    QCOMPARE(telemetryDecoded.value().pressureKPa, telemetry.pressureKPa);
    QCOMPARE(telemetryDecoded.value().rpm, telemetry.rpm);
    QCOMPARE(static_cast<int>(telemetryDecoded.value().deviceState),
             static_cast<int>(DeviceState::Fault));

    // COMMAND
    const Command command{CommandId::Reset, 0xFFFFFFFFu};
    const auto commandPayload = encodeCommand(command);
    QVERIFY(commandPayload.hasValue());
    const auto commandDecoded = decodeCommand(commandPayload.value());
    QVERIFY(commandDecoded.hasValue());
    QCOMPARE(static_cast<int>(commandDecoded.value().commandId),
             static_cast<int>(CommandId::Reset));
    QCOMPARE(commandDecoded.value().parameter, 0xFFFFFFFFu);

    // COMMAND_ACK
    const CommandAck commandAck{CommandId::Stop, AckResult::Failed,
                                QString::fromUtf8("设备忙")};
    const auto ackPayload = encodeCommandAck(commandAck);
    QVERIFY(ackPayload.hasValue());
    const auto ackDecoded = decodeCommandAck(ackPayload.value());
    QVERIFY(ackDecoded.hasValue());
    QCOMPARE(static_cast<int>(ackDecoded.value().commandId),
             static_cast<int>(CommandId::Stop));
    QCOMPARE(static_cast<int>(ackDecoded.value().result),
             static_cast<int>(AckResult::Failed));
    QCOMPARE(ackDecoded.value().message, commandAck.message);

    // ALARM：空消息
    const Alarm alarm{42u, 1001u, AlarmSeverity::Critical, 1731234567890ULL,
                      AlarmState::Cleared, QString()};
    const auto alarmPayload = encodeAlarm(alarm);
    QVERIFY(alarmPayload.hasValue());
    const auto alarmDecoded = decodeAlarm(alarmPayload.value());
    QVERIFY(alarmDecoded.hasValue());
    QCOMPARE(alarmDecoded.value().alarmId, 42u);
    QCOMPARE(alarmDecoded.value().code, quint16(1001));
    QCOMPARE(static_cast<int>(alarmDecoded.value().severity),
             static_cast<int>(AlarmSeverity::Critical));
    QCOMPARE(alarmDecoded.value().timestampMs, alarm.timestampMs);
    QCOMPARE(static_cast<int>(alarmDecoded.value().state),
             static_cast<int>(AlarmState::Cleared));
    QCOMPARE(alarmDecoded.value().message, QString());

    // ERROR：u16 边界值与空消息
    const Error error{65535u, QString()};
    const auto errorPayload = encodeError(error);
    QVERIFY(errorPayload.hasValue());
    const auto errorDecoded = decodeError(errorPayload.value());
    QVERIFY(errorDecoded.hasValue());
    QCOMPARE(errorDecoded.value().code, quint16(65535));
    QCOMPARE(errorDecoded.value().message, QString());
}

void CodecTest::malformedPayloadRejected()
{
    // 字段截断与尾部多余字节
    QCOMPARE(code(decodeHeartbeat(QByteArray(7, '\0')).error()),
             code(ErrorCode::MalformedPayload));
    QCOMPARE(code(decodeHeartbeat(QByteArray(9, '\0')).error()),
             code(ErrorCode::MalformedPayload));
}

QTEST_GUILESS_MAIN(CodecTest)

#include "test_codec.moc"