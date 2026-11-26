#include "network/NetworkClient.h"

#include "network/ConnectionWorker.h"

#include <QtCore/QMetaObject>

namespace devicescope::client {

NetworkClient::NetworkClient(QObject *parent)
    : QObject(parent)
    , m_worker(new ConnectionWorker)
{
    qRegisterMetaType<ConnectionState>("devicescope::client::ConnectionState");

    m_worker->moveToThread(&m_thread);

    connect(&m_thread, &QThread::started, m_worker, &ConnectionWorker::start);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    // 绑定本线程信号与worker线程中的函数
    connect(this, &NetworkClient::connectRequested, m_worker,
            &ConnectionWorker::connectToHost);
    connect(this, &NetworkClient::disconnectRequested, m_worker,
            &ConnectionWorker::disconnectFromHost);

    // 绑定worker线程中的信号，与UI线程中的函数
    connect(m_worker, &ConnectionWorker::connectionStateChanged, this,
            &NetworkClient::connectionStateChanged);
    connect(m_worker, &ConnectionWorker::handshakeSucceeded, this,
            &NetworkClient::handshakeSucceeded);
    connect(m_worker, &ConnectionWorker::connectionError, this,
            &NetworkClient::connectionError);
    connect(m_worker, &ConnectionWorker::statsChanged, this,
            &NetworkClient::statsChanged);

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

} // namespace devicescope::client