#include <QtTest>

#include "DeviceModel.h"
#include "SimulatorServer.h"

#include "devicescope/protocol/Codec.h"
#include "devicescope/protocol/Message.h"
#include "devicescope/protocol/StreamDecoder.h"

#include <QtCore/QElapsedTimer>
#include <QtNetwork/QTcpSocket>

using namespace devicescope::protocol;
using namespace devicescope::simulator;

namespace {

SimulatorConfig makeConfig(int rateHz)
{
    SimulatorConfig config;
    config.address = QHostAddress::LocalHost;
    config.port = 0; // 由系统分配
    config.rateHz = rateHz;
    config.seed = 7;
    return config;
}

bool sendFrame(QTcpSocket &socket, MessageType type, quint32 requestId,
               const QByteArray &payload)
{
    const auto frame = encode(type, kFlagNone, requestId, payload);
    return frame.hasValue() && socket.write(frame.value()) == frame.value().size();
}

bool waitForMessage(QTcpSocket &socket, StreamDecoder &decoder, MessageType expected,
                    DecodedMessage &out, int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (socket.bytesAvailable() > 0) {
            decoder.append(socket.readAll());
        }
        while (true) {
            const StreamStatus status = decoder.next(out);
            if (status == StreamStatus::Message) {
                if (out.header.type == expected) {
                    return true;
                }
                continue;
            }
            if (status == StreamStatus::NeedMoreData) {
                break;
            }
            return false;
        }
        QTest::qWait(5);
    }
    return false;
}

bool waitForTelemetryState(QTcpSocket &socket, StreamDecoder &decoder, DeviceState expected,
                           Telemetry &out, int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (socket.bytesAvailable() > 0) {
            decoder.append(socket.readAll());
        }
        while (true) {
            DecodedMessage message;
            const StreamStatus status = decoder.next(message);
            if (status == StreamStatus::Message) {
                if (message.header.type != MessageType::Telemetry) {
                    continue;
                }
                const auto telemetry = decodeTelemetry(message.payload);
                if (telemetry.hasValue() && telemetry.value().deviceState == expected) {
                    out = telemetry.value();
                    return true;
                }
                continue;
            }
            if (status == StreamStatus::NeedMoreData) {
                break;
            }
            return false;
        }
        QTest::qWait(5);
    }
    return false;
}

bool connectAndHandshake(SimulatorServer &server, QTcpSocket &socket, StreamDecoder &decoder)
{
    socket.connectToHost(QHostAddress::LocalHost, server.port());
    if (!socket.waitForConnected(2000)) {
        return false;
    }

    const auto helloPayload =
        encodeHelloRequest(HelloRequest{QStringLiteral("test-client")});
    if (!helloPayload.hasValue()) {
        return false;
    }
    const auto helloFrame =
        encode(MessageType::Hello, kFlagNone, 1u, helloPayload.value());
    if (!helloFrame.hasValue()
        || socket.write(helloFrame.value()) != helloFrame.value().size()) {
        return false;
    }
    socket.flush();

    DecodedMessage message;
    if (!waitForMessage(socket, decoder, MessageType::Hello, message)) {
        return false;
    }
    const auto response = decodeHelloResponse(message.payload);
    return response.hasValue() && response.value().result == AckResult::Ok;
}

} // namespace

class SimulatorTest final : public QObject
{
    Q_OBJECT

private slots:
    void handshakeAndTelemetry();
    void versionMismatchRejected();
    void heartbeatEcho();
    void commandsChangeState();
    void telemetryRate();
    void unknownTypeIgnored();
    void alarmHysteresis();
};

void SimulatorTest::handshakeAndTelemetry()
{
    SimulatorServer server(makeConfig(20));
    QVERIFY(server.listen());

    QTcpSocket socket;
    StreamDecoder decoder;
    QVERIFY(connectAndHandshake(server, socket, decoder));

    DecodedMessage message;
    QVERIFY(waitForMessage(socket, decoder, MessageType::Telemetry, message));
    const auto telemetry = decodeTelemetry(message.payload);
    QVERIFY(telemetry.hasValue());
    QCOMPARE(static_cast<int>(telemetry.value().deviceState),
             static_cast<int>(DeviceState::Idle));
}

void SimulatorTest::versionMismatchRejected()
{
    SimulatorServer server(makeConfig(10));
    QVERIFY(server.listen());

    QTcpSocket socket;
    StreamDecoder decoder;
    socket.connectToHost(QHostAddress::LocalHost, server.port());
    QVERIFY(socket.waitForConnected(2000));

    const auto helloPayload =
        encodeHelloRequest(HelloRequest{QStringLiteral("old-client")});
    QVERIFY(helloPayload.hasValue());
    const auto helloFrame =
        encode(MessageType::Hello, kFlagNone, 1u, helloPayload.value());
    QVERIFY(helloFrame.hasValue());
    QByteArray frame = helloFrame.value();
    frame[2] = 0x02; // 把 Version 改成 2
    QCOMPARE(socket.write(frame), static_cast<qint64>(frame.size()));
    socket.flush();

    DecodedMessage message;
    QVERIFY(waitForMessage(socket, decoder, MessageType::Error, message));
    const auto error = decodeError(message.payload);
    QVERIFY(error.hasValue());
    QCOMPARE(error.value().code, quint16(1));

    QTRY_COMPARE(static_cast<int>(socket.state()),
                 static_cast<int>(QAbstractSocket::UnconnectedState));
}

void SimulatorTest::heartbeatEcho()
{
    SimulatorServer server(makeConfig(10));
    QVERIFY(server.listen());

    QTcpSocket socket;
    StreamDecoder decoder;
    QVERIFY(connectAndHandshake(server, socket, decoder));

    const auto payload = encodeHeartbeat(Heartbeat{424242ULL});
    QVERIFY(payload.hasValue());
    QVERIFY(sendFrame(socket, MessageType::Heartbeat, 21, payload.value()));
    socket.flush();

    DecodedMessage message;
    QVERIFY(waitForMessage(socket, decoder, MessageType::Heartbeat, message));
    const auto heartbeat = decodeHeartbeat(message.payload);
    QVERIFY(heartbeat.hasValue());
    QCOMPARE(heartbeat.value().timestampMs, quint64(424242));
}

void SimulatorTest::commandsChangeState()
{
    SimulatorServer server(makeConfig(20));
    QVERIFY(server.listen());

    QTcpSocket socket;
    StreamDecoder decoder;
    QVERIFY(connectAndHandshake(server, socket, decoder));

    const auto sendCommand = [&](CommandId commandId) {
        const auto payload = encodeCommand(Command{commandId, 0});
        return payload.hasValue()
               && sendFrame(socket, MessageType::Command, 31, payload.value());
    };

    const auto waitForAck = [&](CommandId expected) {
        DecodedMessage message;
        if (!waitForMessage(socket, decoder, MessageType::CommandAck, message)) {
            return false;
        }
        const auto ack = decodeCommandAck(message.payload);
        return ack.hasValue() && ack.value().commandId == expected
               && ack.value().result == AckResult::Ok;
    };

    QVERIFY(sendCommand(CommandId::Start));
    QVERIFY(waitForAck(CommandId::Start));
    Telemetry telemetry;
    QVERIFY(waitForTelemetryState(socket, decoder, DeviceState::Running, telemetry));

    QVERIFY(sendCommand(CommandId::Stop));
    QVERIFY(waitForAck(CommandId::Stop));
    QVERIFY(waitForTelemetryState(socket, decoder, DeviceState::Stopped, telemetry));

    QVERIFY(sendCommand(CommandId::Reset));
    QVERIFY(waitForAck(CommandId::Reset));
    QVERIFY(waitForTelemetryState(socket, decoder, DeviceState::Idle, telemetry));
}

void SimulatorTest::telemetryRate()
{
    SimulatorServer server(makeConfig(50));
    QVERIFY(server.listen());

    QTcpSocket socket;
    StreamDecoder decoder;
    QVERIFY(connectAndHandshake(server, socket, decoder));

    QElapsedTimer timer;
    timer.start();
    int count = 0;
    DecodedMessage message;

    while (timer.elapsed() < 400) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (socket.bytesAvailable() > 0) {
            decoder.append(socket.readAll());
        }
        while (true) {
            const StreamStatus status = decoder.next(message);
            if (status == StreamStatus::Message) {
                if (message.header.type == MessageType::Telemetry) {
                    ++count;
                }
                continue;
            }
            if (status == StreamStatus::NeedMoreData) {
                break;
            }
            QFAIL("unexpected stream status");
        }
        QTest::qWait(2);
    }

    // 50 Hz × 0.4 s ≈ 20 包，留出调度误差
    QVERIFY2(count >= 8, qPrintable(QString::number(count)));
    QVERIFY2(count <= 30, qPrintable(QString::number(count)));
}

void SimulatorTest::unknownTypeIgnored()
{
    SimulatorServer server(makeConfig(10));
    QVERIFY(server.listen());

    QTcpSocket socket;
    StreamDecoder decoder;
    QVERIFY(connectAndHandshake(server, socket, decoder));

    QVERIFY(sendFrame(socket, static_cast<MessageType>(0x7F), 41,
                      QByteArray::fromHex("deadbeef")));
    const auto heartbeat = encodeHeartbeat(Heartbeat{777ULL});
    QVERIFY(heartbeat.hasValue());
    QVERIFY(sendFrame(socket, MessageType::Heartbeat, 42, heartbeat.value()));
    socket.flush();

    DecodedMessage message;
    QVERIFY(waitForMessage(socket, decoder, MessageType::Heartbeat, message));
    QCOMPARE(static_cast<int>(socket.state()),
             static_cast<int>(QAbstractSocket::ConnectedState));
}

void SimulatorTest::alarmHysteresis()
{
    DeviceModel device(7);

    // 空闲状态温度 25，不报警
    DeviceModel::Sample sample = device.sample(1000);
    QVERIFY(!sample.alarm.has_value());

    device.start();
    sample = device.sample(5000); // sin(π/2)=1 → 约 45℃
    QVERIFY(sample.alarm.has_value());
    QCOMPARE(static_cast<int>(sample.alarm->state), static_cast<int>(AlarmState::Raised));
    const quint32 alarmId = sample.alarm->alarmId;

    sample = device.sample(15000); // sin(3π/2)=-1 → 约 5℃
    QVERIFY(sample.alarm.has_value());
    QCOMPARE(static_cast<int>(sample.alarm->state), static_cast<int>(AlarmState::Cleared));
    QCOMPARE(sample.alarm->alarmId, alarmId);

    // 已清除后不重复产生事件
    sample = device.sample(15000);
    QVERIFY(!sample.alarm.has_value());
}

QTEST_GUILESS_MAIN(SimulatorTest)

#include "test_simulator.moc"