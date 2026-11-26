#include <QtTest>

#include "network/ConnectionState.h"
#include "network/NetworkClient.h"
#include "SimulatorServer.h"

using namespace devicescope::client;
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

} // namespace

class ClientNetworkTest final : public QObject
{
    Q_OBJECT

private slots:
    void connectAndDisconnectCycles();
    void failedConnectionReportsError();
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

QTEST_GUILESS_MAIN(ClientNetworkTest)

#include "test_client_network.moc"