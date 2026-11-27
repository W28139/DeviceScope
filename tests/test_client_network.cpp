#include <QtTest>

#include "network/ConnectionState.h"
#include "network/NetworkClient.h"
#include "SimulatorServer.h"

#include "devicescope/protocol/Codec.h"
#include "devicescope/protocol/Message.h"
#include "devicescope/protocol/StreamDecoder.h"

#include <QtCore/QHash>
#include <QtCore/QSet>
#include <QtCore/QVector>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

#include <array>

using namespace devicescope::client;
using namespace devicescope::protocol;
using namespace devicescope::simulator;

namespace {

ConnectionState lastState(const QSignalSpy &spy)
{
    if (spy.isEmpty()) {
        return ConnectionState::Disconnected;
    }
    return spy.last().at(0).value<ConnectionState>();
}

SimulatorConfig makeConfig()
{
    SimulatorConfig config;
    config.address = QHostAddress::LocalHost;
    config.port = 0;
    config.rateHz = 20;
    config.seed = 3;
    return config;
}

QHash<quint32, CommandId> sentCommands(const QSignalSpy &spy)
{
    QHash<quint32, CommandId> result;
    for (const QList<QVariant> &args : spy) {
        result.insert(args.at(0).toUInt(), args.at(1).value<CommandId>());
    }
    return result;
}

bool ackMappingMatches(const QSignalSpy &ackSpy, const QHash<quint32, CommandId> &sent,
                       QSet<quint32> &seen)
{
    for (const QList<QVariant> &args : ackSpy) {
        const quint32 requestId = args.at(0).toUInt();
        if (!sent.contains(requestId)) {
            return false;
        }
        if (args.at(1).value<CommandId>() != sent.value(requestId)) {
            return false;
        }
        if (args.at(2).value<AckResult>() != AckResult::Ok) {
            return false;
        }
        if (args.at(4).toLongLong() < 0) {
            return false;
        }
        seen.insert(requestId);
    }
    return true;
}

} // namespace

// 测试用最小对端：完成 HELLO 握手，按顺序或乱序回 COMMAND_ACK，
// 也能主动发送任意 requestId 的 ACK（用于未知 id 场景）。
class TestStubServer final : public QObject
{
    Q_OBJECT

public:
    explicit TestStubServer(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this,
                &TestStubServer::onNewConnection);
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    quint16 port() const { return m_server.serverPort(); }

    // 收满 count 条命令后逆序回 ACK；0 表示收到即回。
    void collectBeforeReply(int count) { m_collectBeforeReply = count; }

    void sendAck(quint32 requestId, CommandId commandId, AckResult result,
                 const QString &text)
    {
        const auto payload = encodeCommandAck({commandId, result, text});
        if (payload.hasValue()) {
            sendFrame(MessageType::CommandAck, kFlagResponse, requestId, payload.value());
        }
    }

private:
    struct Request {
        quint32 requestId = 0;
        CommandId commandId = CommandId::Start;
    };

    void onNewConnection()
    {
        while (m_server.hasPendingConnections()) {
            m_socket = m_server.nextPendingConnection();
            m_decoder = StreamDecoder();
            m_requests.clear();
            connect(m_socket, &QTcpSocket::readyRead, this, &TestStubServer::onReadyRead);
        }
    }

    void onReadyRead()
    {
        m_decoder.append(m_socket->readAll());

        DecodedMessage message;
        while (m_decoder.next(message) == StreamStatus::Message) {
            if (message.header.type == MessageType::Hello) {
                const auto payload = encodeHelloResponse(
                    HelloResponse{1, QStringLiteral("STUB"), 1, AckResult::Ok});
                if (payload.hasValue()) {
                    sendFrame(MessageType::Hello, kFlagResponse, message.header.requestId,
                              payload.value());
                }
            } else if (message.header.type == MessageType::Command) {
                const auto command = decodeCommand(message.payload);
                if (!command.hasValue()) {
                    continue;
                }
                m_requests.append(Request{message.header.requestId, command.value().commandId});
                if (m_collectBeforeReply <= 0) {
                    sendAck(message.header.requestId, command.value().commandId,
                            AckResult::Ok, QStringLiteral("ok"));
                } else if (m_requests.size() >= m_collectBeforeReply) {
                    for (auto it = m_requests.crbegin(); it != m_requests.crend(); ++it) {
                        sendAck(it->requestId, it->commandId, AckResult::Ok,
                                QStringLiteral("ok"));
                    }
                    m_requests.clear();
                }
            }
        }
    }

    void sendFrame(MessageType type, quint16 flags, quint32 requestId,
                   const QByteArray &payload)
    {
        if (m_socket == nullptr) {
            return;
        }
        const auto frame = encode(type, flags, requestId, payload);
        if (frame.hasValue()) {
            m_socket->write(frame.value());
        }
    }

    QTcpServer m_server;
    QTcpSocket *m_socket = nullptr;
    StreamDecoder m_decoder;
    QVector<Request> m_requests;
    int m_collectBeforeReply = 0;
};

class ClientNetworkTest final : public QObject
{
    Q_OBJECT

private slots:
    void connectAndDisconnectCycles();
    void failedConnectionReportsError();
    void commandBurstMatchesAcks();
    void outOfOrderAcksMatchCommands();
    void unknownAckIsIgnored();
    void commandWhileDisconnectedFails();
};

void ClientNetworkTest::connectAndDisconnectCycles()
{
    SimulatorServer server(makeConfig());
    QVERIFY(server.listen());

    NetworkClient client;
    QSignalSpy stateSpy(&client, &NetworkClient::connectionStateChanged);
    QSignalSpy handshakeSpy(&client, &NetworkClient::handshakeSucceeded);
    QSignalSpy errorSpy(&client, &NetworkClient::connectionError);

    for (int cycle = 0; cycle < 20; ++cycle) {
        client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(lastState(stateSpy)),
                                  static_cast<int>(ConnectionState::Connected), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(handshakeSpy.count(), cycle + 1, 3000);

        client.disconnectFromHost();
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(lastState(stateSpy)),
                                  static_cast<int>(ConnectionState::Disconnected), 3000);
    }

    QCOMPARE(errorSpy.count(), 0);
}

void ClientNetworkTest::failedConnectionReportsError()
{
    // 借一个刚刚释放的端口，确保连接被拒绝
    quint16 deadPort = 0;
    {
        SimulatorServer probe(makeConfig());
        QVERIFY(probe.listen());
        deadPort = probe.port();
    }

    NetworkClient client;
    QSignalSpy stateSpy(&client, &NetworkClient::connectionStateChanged);
    QSignalSpy errorSpy(&client, &NetworkClient::connectionError);

    client.connectToHost(QStringLiteral("127.0.0.1"), deadPort);
    QTRY_VERIFY_WITH_TIMEOUT(!errorSpy.isEmpty(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(lastState(stateSpy)),
                              static_cast<int>(ConnectionState::Error), 3000);

    // 失败后仍能重新连接到正常服务器
    SimulatorServer server(makeConfig());
    QVERIFY(server.listen());
    client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(lastState(stateSpy)),
                              static_cast<int>(ConnectionState::Connected), 3000);
}

void ClientNetworkTest::commandBurstMatchesAcks()
{
    SimulatorServer server(makeConfig());
    QVERIFY(server.listen());

    NetworkClient client;
    QSignalSpy stateSpy(&client, &NetworkClient::connectionStateChanged);
    QSignalSpy sentSpy(&client, &NetworkClient::commandSent);
    QSignalSpy ackSpy(&client, &NetworkClient::commandAcknowledged);
    QSignalSpy failedSpy(&client, &NetworkClient::commandFailed);

    client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(lastState(stateSpy)),
                              static_cast<int>(ConnectionState::Connected), 3000);

    const std::array<CommandId, 3> commands{CommandId::Start, CommandId::Stop,
                                            CommandId::Reset};
    for (int i = 0; i < 50; ++i) {
        client.sendCommand(commands[static_cast<std::size_t>(i) % commands.size()]);
    }

    QTRY_COMPARE_WITH_TIMEOUT(ackSpy.count(), 50, 5000);
    QCOMPARE(sentSpy.count(), 50);
    QCOMPARE(failedSpy.count(), 0);

    const QHash<quint32, CommandId> sent = sentCommands(sentSpy);
    QCOMPARE(sent.size(), 50);

    QSet<quint32> seen;
    QVERIFY(ackMappingMatches(ackSpy, sent, seen));
    QCOMPARE(seen.size(), 50);
}

void ClientNetworkTest::outOfOrderAcksMatchCommands()
{
    TestStubServer server;
    QVERIFY(server.listen());
    server.collectBeforeReply(10);

    NetworkClient client;
    QSignalSpy stateSpy(&client, &NetworkClient::connectionStateChanged);
    QSignalSpy sentSpy(&client, &NetworkClient::commandSent);
    QSignalSpy ackSpy(&client, &NetworkClient::commandAcknowledged);
    QSignalSpy failedSpy(&client, &NetworkClient::commandFailed);

    client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(lastState(stateSpy)),
                              static_cast<int>(ConnectionState::Connected), 3000);

    const std::array<CommandId, 3> commands{CommandId::Start, CommandId::Stop,
                                            CommandId::Reset};
    for (int i = 0; i < 10; ++i) {
        client.sendCommand(commands[static_cast<std::size_t>(i) % commands.size()]);
    }

    QTRY_COMPARE_WITH_TIMEOUT(ackSpy.count(), 10, 3000);
    QCOMPARE(failedSpy.count(), 0);

    const QHash<quint32, CommandId> sent = sentCommands(sentSpy);
    QCOMPARE(sent.size(), 10);

    QSet<quint32> seen;
    QVERIFY(ackMappingMatches(ackSpy, sent, seen));
    QCOMPARE(seen.size(), 10);
}

void ClientNetworkTest::unknownAckIsIgnored()
{
    TestStubServer server;
    QVERIFY(server.listen());

    NetworkClient client;
    QSignalSpy stateSpy(&client, &NetworkClient::connectionStateChanged);
    QSignalSpy ackSpy(&client, &NetworkClient::commandAcknowledged);
    QSignalSpy failedSpy(&client, &NetworkClient::commandFailed);

    client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(lastState(stateSpy)),
                              static_cast<int>(ConnectionState::Connected), 3000);

    // 没有对应待处理请求的 ACK：只记录日志，不产生成功信号，也不断开连接。
    server.sendAck(0xDEADBEEFu, CommandId::Start, AckResult::Ok, QStringLiteral("stale"));
    QTest::qWait(100);
    QCOMPARE(ackSpy.count(), 0);
    QCOMPARE(failedSpy.count(), 0);

    // 连接仍然可用。
    client.sendCommand(CommandId::Stop);
    QTRY_COMPARE_WITH_TIMEOUT(ackSpy.count(), 1, 3000);
    QCOMPARE(static_cast<int>(ackSpy.last().at(1).value<CommandId>()),
             static_cast<int>(CommandId::Stop));
}

void ClientNetworkTest::commandWhileDisconnectedFails()
{
    NetworkClient client;
    QSignalSpy ackSpy(&client, &NetworkClient::commandAcknowledged);
    QSignalSpy failedSpy(&client, &NetworkClient::commandFailed);

    client.sendCommand(CommandId::Start);
    QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 1000);

    QVERIFY(failedSpy.last().at(0).toUInt() != 0);
    QCOMPARE(failedSpy.last().at(1).toString(), QStringLiteral("未连接"));
    QCOMPARE(ackSpy.count(), 0);
}

QTEST_GUILESS_MAIN(ClientNetworkTest)

#include "test_client_network.moc"