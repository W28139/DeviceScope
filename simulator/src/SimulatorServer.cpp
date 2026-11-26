#include "SimulatorServer.h"

#include "Logging.h"
#include "SimulatorSession.h"

#include <QtCore/QLoggingCategory>
#include <QtNetwork/QTcpSocket>

#include <utility>

Q_LOGGING_CATEGORY(lcSimulator, "app.sim")

namespace devicescope::simulator {

SimulatorServer::SimulatorServer(SimulatorConfig config, QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
{
    connect(&m_server, &QTcpServer::newConnection, this, &SimulatorServer::onNewConnection);
}

bool SimulatorServer::listen()
{
    if (!m_server.listen(m_config.address, m_config.port)) {
        qCCritical(lcSimulator) << "failed to listen:" << m_server.errorString();
        return false;
    }
    qCInfo(lcSimulator) << "listening on" << m_config.address.toString()
                        << m_server.serverPort();
    return true;
}

quint16 SimulatorServer::port() const
{
    return m_server.serverPort();
}

void SimulatorServer::stop()
{
    m_server.close();
    // SimulatorSession是自己的子类，可以获取到实例化对象
    const auto sessions = findChildren<SimulatorSession *>();
    for (SimulatorSession *session : sessions) {
        session->close();
        session->deleteLater();
    }
    qCInfo(lcSimulator) << "server stopped";
}

void SimulatorServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        auto *session = new SimulatorSession(socket, m_config, m_nextSessionId++, this);
        qCInfo(lcSimulator) << "client connected:" << socket->peerAddress().toString()
                            << socket->peerPort() << "session" << session->id();
    }
}

} // namespace devicescope::simulator