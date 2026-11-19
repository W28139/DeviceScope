#pragma once

#include <QObject>
#include <QTcpServer>

class QHostAddress;

class EchoServer final : public QObject
{
    Q_OBJECT

public:
    explicit EchoServer(QObject *parent = nullptr);

    bool listen(const QHostAddress &address, quint16 port);
    quint16 port() const;

private:
    QTcpServer m_server;
};