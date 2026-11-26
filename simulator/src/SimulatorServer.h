#pragma once

#include <QtCore/QObject>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpServer>

namespace devicescope::simulator {

struct SimulatorConfig {
    QHostAddress address = QHostAddress::LocalHost;
    quint16 port = 9000;
    int rateHz = 10;
    quint32 seed = 1;
};

// 监听 TCP 端口，为每个连接创建一个 SimulatorSession。
class SimulatorServer final : public QObject
{
    Q_OBJECT

public:
    explicit SimulatorServer(SimulatorConfig config, QObject *parent = nullptr);

    bool listen();
    quint16 port() const;
    void stop();

private:
    void onNewConnection();

    SimulatorConfig m_config;
    QTcpServer m_server;
    quint32 m_nextSessionId = 1;
};

} // namespace devicescope::simulator