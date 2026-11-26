#include "ui/MainWindow.h"

#include "network/NetworkClient.h"

#include <QtCore/QCoreApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

using devicescope::client::ConnectionState;
using devicescope::client::NetworkClient;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_network(new NetworkClient(this))
{
    setWindowTitle(QStringLiteral("DeviceScope %1")
                       .arg(QCoreApplication::applicationVersion()));
    setMinimumSize(800, 500);
    resize(960, 640);

    setupUi();

    connect(m_network, &NetworkClient::connectionStateChanged, this, &MainWindow::updateForState);
    connect(m_network, &NetworkClient::handshakeSucceeded, this,
            [this](quint16 deviceId, const QString &deviceName) {
                m_deviceLabel->setText(
                    tr("设备: %1 (#%2)").arg(deviceName).arg(deviceId));
            });
    connect(m_network, &NetworkClient::connectionError, this,
            [this](const QString &message) {
                statusBar()->showMessage(message, 5000);    // 在窗口底部的状态栏上临时显示一段文字 message
            });
    connect(m_network, &NetworkClient::statsChanged, this,
            [this](quint64 receivedFrames) {
                m_statsLabel->setText(tr("收包: %1").arg(receivedFrames));
            });

    updateForState(ConnectionState::Disconnected);
}

void MainWindow::setupUi()
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);

    // 顶部连接栏
    auto *connectionBar = new QWidget(central);
    auto *barLayout = new QHBoxLayout(connectionBar);
    barLayout->setContentsMargins(0, 0, 0, 0);

    barLayout->addWidget(new QLabel(tr("地址"), connectionBar));
    m_hostEdit = new QLineEdit(QStringLiteral("127.0.0.1"), connectionBar);
    m_hostEdit->setMaximumWidth(160);
    barLayout->addWidget(m_hostEdit);

    barLayout->addWidget(new QLabel(tr("端口"), connectionBar));
    m_portSpin = new QSpinBox(connectionBar);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(9000);
    barLayout->addWidget(m_portSpin);

    m_connectButton = new QPushButton(tr("连接"), connectionBar);
    m_disconnectButton = new QPushButton(tr("断开"), connectionBar);
    m_disconnectButton->setEnabled(false);
    barLayout->addWidget(m_connectButton);
    barLayout->addWidget(m_disconnectButton);
    barLayout->addStretch(1);

    m_stateLabel = new QLabel(connectionBar);
    barLayout->addWidget(m_stateLabel);
    layout->addWidget(connectionBar);

    // 中部标签页（步骤 7–12 填充内容）
    auto *tabs = new QTabWidget(central);
    const auto addPlaceholder = [tabs](const QString &title, const QString &text) {
        auto *label = new QLabel(text, tabs);
        label->setAlignment(Qt::AlignCenter);
        tabs->addTab(label, title);
    };
    addPlaceholder(tr("实时监控"), tr("步骤 7 实现"));
    addPlaceholder(tr("报警"), tr("步骤 9 实现"));
    addPlaceholder(tr("历史"), tr("步骤 11 实现"));
    addPlaceholder(tr("日志"), tr("步骤 12 实现"));
    layout->addWidget(tabs, 1);

    setCentralWidget(central);

    // 底部状态栏
    m_deviceLabel = new QLabel(tr("设备: -"), this);
    m_statsLabel = new QLabel(tr("收包: 0"), this);
    statusBar()->addPermanentWidget(m_deviceLabel);
    statusBar()->addPermanentWidget(m_statsLabel);

    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_disconnectButton, &QPushButton::clicked, this,
            &MainWindow::onDisconnectClicked);
}

void MainWindow::onConnectClicked()
{
    const QString host = m_hostEdit->text().trimmed();
    if (host.isEmpty()) {
        statusBar()->showMessage(tr("请输入地址"), 3000);
        return;
    }
    m_network->connectToHost(host, static_cast<quint16>(m_portSpin->value()));
}

void MainWindow::onDisconnectClicked()
{
    m_network->disconnectFromHost();
}

void MainWindow::updateForState(devicescope::client::ConnectionState state)
{
    switch (state) {
    case ConnectionState::Disconnected:
        m_stateLabel->setText(tr("未连接"));
        m_connectButton->setEnabled(true);
        m_disconnectButton->setEnabled(false);
        m_hostEdit->setEnabled(true);
        m_portSpin->setEnabled(true);
        m_deviceLabel->setText(tr("设备: -"));
        break;

    case ConnectionState::Connecting:
        m_stateLabel->setText(tr("连接中…"));
        m_connectButton->setEnabled(false);
        m_disconnectButton->setEnabled(true);
        m_hostEdit->setEnabled(false);
        m_portSpin->setEnabled(false);
        break;

    case ConnectionState::Handshaking:
        m_stateLabel->setText(tr("握手中…"));
        m_connectButton->setEnabled(false);
        m_disconnectButton->setEnabled(true);
        break;

    case ConnectionState::Connected:
        m_stateLabel->setText(tr("已连接"));
        m_connectButton->setEnabled(false);
        m_disconnectButton->setEnabled(true);
        break;

    case ConnectionState::Disconnecting:
        m_stateLabel->setText(tr("断开中…"));
        m_connectButton->setEnabled(false);
        m_disconnectButton->setEnabled(false);
        break;

    case ConnectionState::Error:
        m_stateLabel->setText(tr("连接失败"));
        m_connectButton->setEnabled(true);
        m_disconnectButton->setEnabled(false);
        m_hostEdit->setEnabled(true);
        m_portSpin->setEnabled(true);
        m_deviceLabel->setText(tr("设备: -"));
        break;
    }
}