#include "network/NetworkClient.h"

#include "network/ConnectionWorker.h"

#include <QtCore/QMetaObject>

namespace devicescope::client {

NetworkClient::NetworkClient(QObject *parent)
    : QObject(parent)
    , m_worker(new ConnectionWorker(&m_telemetry))
{
    qRegisterMetaType<ConnectionState>("devicescope::client::ConnectionState");
    qRegisterMetaType<devicescope::protocol::CommandId>("devicescope::protocol::CommandId");
    qRegisterMetaType<devicescope::protocol::AckResult>("devicescope::protocol::AckResult");

    m_worker->moveToThread(&m_thread);

    connect(&m_thread, &QThread::started, m_worker, &ConnectionWorker::start);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    // 绑定本线程信号与worker线程中的函数
    connect(this, &NetworkClient::connectRequested, m_worker,
            &ConnectionWorker::connectToHost);
    connect(this, &NetworkClient::disconnectRequested, m_worker,
            &ConnectionWorker::disconnectFromHost);
    connect(this, &NetworkClient::commandRequested, m_worker,
            &ConnectionWorker::sendCommand);

    // 信号-信号连接（signal-to-signal），作用是转发信号 (解耦 / 封装) ———— worker线程的信号投递到这里，然后转到UI主线程的，槽函数在主线程中执行
    connect(m_worker, &ConnectionWorker::connectionStateChanged, this,
            &NetworkClient::connectionStateChanged);
    connect(m_worker, &ConnectionWorker::handshakeSucceeded, this,
            &NetworkClient::handshakeSucceeded);
    connect(m_worker, &ConnectionWorker::connectionError, this,
            &NetworkClient::connectionError);
    connect(m_worker, &ConnectionWorker::statsChanged, this,
            &NetworkClient::statsChanged);
    connect(m_worker, &ConnectionWorker::commandSent, this, &NetworkClient::commandSent);
    connect(m_worker, &ConnectionWorker::commandAcknowledged, this,
            &NetworkClient::commandAcknowledged);
    connect(m_worker, &ConnectionWorker::commandFailed, this, &NetworkClient::commandFailed);
    connect(m_worker, &ConnectionWorker::commandTimedOut, this,
            &NetworkClient::commandTimedOut);
    connect(m_worker, &ConnectionWorker::reconnectScheduled, this,
            &NetworkClient::reconnectScheduled);

    m_thread.setObjectName(QStringLiteral("NetworkThread"));
    m_thread.start();
}

NetworkClient::~NetworkClient()
{
    if (m_worker != nullptr && m_thread.isRunning()) {
        // 让 m_worker 所在线程执行 shutdown(),调用方阻塞等待它执行完
        QMetaObject::invokeMethod(m_worker, &ConnectionWorker::shutdown,
                                  Qt::BlockingQueuedConnection);
        m_thread.quit();
        m_thread.wait(3000);
    }
    m_worker = nullptr;
}

void NetworkClient::connectToHost(const QString &host, quint16 port)
{
    emit connectRequested(host, port);
}

void NetworkClient::disconnectFromHost()
{
    emit disconnectRequested();
}

void NetworkClient::sendCommand(devicescope::protocol::CommandId commandId)
{
    emit commandRequested(commandId);
}

} // namespace devicescope::client