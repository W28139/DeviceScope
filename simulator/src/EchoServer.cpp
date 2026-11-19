#include "EchoServer.h"

#include <QHostAddress>
#include <QLoggingCategory>
#include <QTcpSocket>

Q_LOGGING_CATEGORY(lcSimulator, "app.sim")

EchoServer::EchoServer(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket *socket = m_server.nextPendingConnection()) {
            qCInfo(lcSimulator) << "client connected:"
                                << socket->peerAddress().toString()
                                << socket->peerPort();

            connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                const QByteArray data = socket->readAll();
                qCInfo(lcSimulator) << "received" << data.size() << "bytes, echoing";
                socket->write(data);
            });

            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
}

bool EchoServer::listen(const QHostAddress &address, quint16 port)
{
    if (!m_server.listen(address, port)) {
        qCCritical(lcSimulator) << "failed to listen:" << m_server.errorString();
        return false;
    }

    qCInfo(lcSimulator) << "listening on"
                        << address.toString()
                        << m_server.serverPort();
    return true;
}

quint16 EchoServer::port() const
{
    return m_server.serverPort();
}