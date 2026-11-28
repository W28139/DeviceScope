#include "ui/MainWindow.h"

#include "core/TelemetryQueue.h"
#include "network/NetworkClient.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QTimer>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTableWidgetItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

using devicescope::client::ConnectionState;
using devicescope::client::NetworkClient;
using devicescope::protocol::Telemetry;

namespace {

constexpr int kRefreshIntervalMs = 50;   // UI 刷新上限约 20 FPS
constexpr int kMaxDrainPerTick = 200;    // 单次最多取出的样本数，避免卡顿恢复时爆发
constexpr int kMaxRows = 200;            // 表格保留的最近遥测条数

// 把状态字段转为文字，方便显示
QString deviceStateText(devicescope::protocol::DeviceState state)
{
    switch (state) {
    case devicescope::protocol::DeviceState::Idle:
        return QObject::tr("空闲");
    case devicescope::protocol::DeviceState::Running:
        return QObject::tr("运行中");
    case devicescope::protocol::DeviceState::Stopped:
        return QObject::tr("已停止");
    case devicescope::protocol::DeviceState::Fault:
        return QObject::tr("故障");
    }
    return QObject::tr("未知");
}

// 格式化时间
QString formatTimestamp(quint64 timestampMs)
{
    return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(timestampMs))
        .toString(QStringLiteral("HH:mm:ss.zzz"));
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_network(new NetworkClient(this))
    , m_telemetry(m_network->telemetryQueue())
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
                // 保留错误文本，直到下一次连接或断开
                statusBar()->showMessage(message);
            });
    connect(m_network, &NetworkClient::reconnectScheduled, this,
            [this](int attempt, int delayMs) {
                statusBar()->showMessage(
                    tr("连接断开，第 %1 次重连将在 %2 秒后开始")
                        .arg(attempt)
                        .arg(delayMs / 1000.0, 0, 'f', 1));
            });
    connect(m_network, &NetworkClient::commandFailed, this,
            [this](quint32 requestId, const QString &reason) {
                // 断线批量失败由重连提示统一展示，避免覆盖倒计时
                if (reason == QStringLiteral("连接断开")) {
                    return;
                }
                statusBar()->showMessage(
                    tr("命令失败（#%1）：%2").arg(requestId).arg(reason), 5000);
            });
    connect(m_network, &NetworkClient::commandTimedOut, this,
            [this](quint32 requestId) {
                statusBar()->showMessage(tr("命令超时（#%1）").arg(requestId), 5000);
            });
    connect(m_network, &NetworkClient::statsChanged, this,
            [this](quint64 receivedFrames) {
                m_statsLabel->setText(tr("收包: %1").arg(receivedFrames));
            });

    // 定时批量取遥测：网络线程只负责入队，UI 线程每 50 ms 刷新一次。
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(kRefreshIntervalMs);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::onTelemetryTick);
    m_refreshTimer->start();

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

    // 中部标签页：实时监控已实现，其余步骤 8–12 填充
    auto *tabs = new QTabWidget(central);
    tabs->addTab(createRealtimeTab(), tr("实时监控"));

    const auto addPlaceholder = [tabs](const QString &title, const QString &text) {
        auto *label = new QLabel(text, tabs);
        label->setAlignment(Qt::AlignCenter);
        tabs->addTab(label, title);
    };
    addPlaceholder(tr("报警"), tr("步骤 9 实现"));
    addPlaceholder(tr("历史"), tr("步骤 11 实现"));
    addPlaceholder(tr("日志"), tr("步骤 12 实现"));
    layout->addWidget(tabs, 1);

    setCentralWidget(central);

    // 底部状态栏
    m_deviceLabel = new QLabel(tr("设备: -"), this);
    m_statsLabel = new QLabel(tr("收包: 0"), this);
    m_droppedLabel = new QLabel(tr("丢弃样本: 0"), this);
    statusBar()->addPermanentWidget(m_deviceLabel);
    statusBar()->addPermanentWidget(m_statsLabel);
    statusBar()->addPermanentWidget(m_droppedLabel);

    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_disconnectButton, &QPushButton::clicked, this,
            &MainWindow::onDisconnectClicked);
}

QWidget *MainWindow::createRealtimeTab()
{
    auto *tab = new QWidget;   // “实时数据”页的根容器

    // ---------- 当前值快照区 ----------
    // 只展示最新一条样本，避免逐样本刷新整个表格；每 tick 只更新这几个 label
    auto *snapshotBox = new QGroupBox(tr("当前值"), tab);
    auto *form = new QFormLayout(snapshotBox);           // 表单布局：左标签右值
    m_temperatureLabel = new QLabel(QStringLiteral("-"), snapshotBox);  // 初始占位符 "-"
    m_pressureLabel = new QLabel(QStringLiteral("-"), snapshotBox);
    m_rpmLabel = new QLabel(QStringLiteral("-"), snapshotBox);
    m_stateValueLabel = new QLabel(QStringLiteral("-"), snapshotBox);
    m_lastUpdateLabel = new QLabel(QStringLiteral("-"), snapshotBox);
    form->addRow(tr("温度"), m_temperatureLabel);
    form->addRow(tr("压力"), m_pressureLabel);
    form->addRow(tr("转速"), m_rpmLabel);
    form->addRow(tr("设备状态"), m_stateValueLabel);
    form->addRow(tr("最后更新"), m_lastUpdateLabel);

    // ---------- 最近遥测表格 ----------
    // 0 行 5 列；行数由 kMaxRows 在 appendSamples 中限制（滚动窗口）
    m_telemetryTable = new QTableWidget(0, 5, tab);
    m_telemetryTable->setHorizontalHeaderLabels(
        {tr("时间"), tr("温度 (°C)"), tr("压力 (kPa)"), tr("转速 (rpm)"), tr("状态")});
    m_telemetryTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);  // 列宽平均撑满
    m_telemetryTable->horizontalHeader()->setHighlightSections(false);  // 点击表头不高亮
    m_telemetryTable->verticalHeader()->setVisible(false);              // 隐藏行号列
    m_telemetryTable->setEditTriggers(QAbstractItemView::NoEditTriggers); // 只读，禁止编辑
    m_telemetryTable->setSelectionBehavior(QAbstractItemView::SelectRows); // 选中整行
    m_telemetryTable->setSelectionMode(QAbstractItemView::SingleSelection); // 单选
    m_telemetryTable->setAlternatingRowColors(true);   // 隔行变色，易读
    m_telemetryTable->setShowGrid(false);              // 隐藏网格线，观感更干净

    // ---------- 页面整体布局：快照在上，表格占据剩余空间 ----------
    auto *layout = new QVBoxLayout(tab);
    layout->addWidget(snapshotBox);
    layout->addWidget(m_telemetryTable, 1);   // 拉伸因子 1，让表格填满剩余高度
    return tab;
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

void MainWindow::onTelemetryTick()
{
    if (m_telemetry == nullptr) {
        return;
    }

    const auto samples = m_telemetry->drain(kMaxDrainPerTick);
    if (!samples.isEmpty()) {
        appendSamples(samples);
        updateSnapshot(samples.last());
    }

    // 获取程序丢弃样本总数，只在变化时刷新，避免每帧无意义重绘
    const quint64 dropped = m_telemetry->droppedCount();
    if (m_droppedLabel != nullptr && dropped != m_shownDropped) {
        m_shownDropped = dropped;
        m_droppedLabel->setText(tr("丢弃样本: %1").arg(dropped));
    }
}

void MainWindow::appendSamples(const QVector<Telemetry> &samples)
{
    // 空指针或空批次直接返回，避免无意义操作
    if (m_telemetryTable == nullptr || samples.isEmpty()) {
        return;
    }

    // 批量插入期间关闭重绘，避免每加一行都触发一次界面刷新（性能关键）
    m_telemetryTable->setUpdatesEnabled(false);
    for (const Telemetry &sample : samples) {
        // 追加到表格末尾
        const int row = m_telemetryTable->rowCount();
        m_telemetryTable->insertRow(row);

        // 小工具函数：在第 row 行的 column 列放置文本
        const auto setItem = [this, row](int column, const QString &text) {
            m_telemetryTable->setItem(row, column, new QTableWidgetItem(text));
        };
        // 各列格式化：时间戳 / 温度(1位小数) / 压力(1位小数) / 转速(取整) / 设备状态文本
        setItem(0, formatTimestamp(sample.timestampMs));
        setItem(1, QString::number(sample.temperatureC, 'f', 1));
        setItem(2, QString::number(sample.pressureKPa, 'f', 1));
        setItem(3, QString::number(sample.rpm, 'f', 0));
        setItem(4, deviceStateText(sample.deviceState));
    }

    // 超出最大行数时从顶部逐行删除，保持表格只保留最近 kMaxRows 条（滚动窗口）
    while (m_telemetryTable->rowCount() > kMaxRows) {
        m_telemetryTable->removeRow(0);
    }

    // 恢复重绘并滚到最新一行，让用户始终看到最新数据
    m_telemetryTable->setUpdatesEnabled(true);
    m_telemetryTable->scrollToBottom();
}

void MainWindow::updateSnapshot(const Telemetry &sample)
{
    if (m_temperatureLabel == nullptr) {
        return;
    }

    m_temperatureLabel->setText(QStringLiteral("%1 °C").arg(sample.temperatureC, 0, 'f', 1));
    m_pressureLabel->setText(QStringLiteral("%1 kPa").arg(sample.pressureKPa, 0, 'f', 1));
    m_rpmLabel->setText(QStringLiteral("%1 rpm").arg(sample.rpm, 0, 'f', 0));
    m_stateValueLabel->setText(deviceStateText(sample.deviceState));
    m_lastUpdateLabel->setText(formatTimestamp(sample.timestampMs));
}

void MainWindow::clearRealtimeView()
{
    if (m_telemetryTable != nullptr) {
        m_telemetryTable->setRowCount(0);
    }
    if (m_temperatureLabel != nullptr) {
        m_temperatureLabel->setText(QStringLiteral("-"));
        m_pressureLabel->setText(QStringLiteral("-"));
        m_rpmLabel->setText(QStringLiteral("-"));
        m_stateValueLabel->setText(QStringLiteral("-"));
        m_lastUpdateLabel->setText(QStringLiteral("-"));
    }
}

void MainWindow::updateForState(devicescope::client::ConnectionState state)
{
    switch (state) {
    case ConnectionState::Disconnected:
        statusBar()->clearMessage();
        m_stateLabel->setText(tr("未连接"));
        m_connectButton->setEnabled(true);
        m_disconnectButton->setEnabled(false);
        m_hostEdit->setEnabled(true);
        m_portSpin->setEnabled(true);
        m_deviceLabel->setText(tr("设备: -"));
        clearRealtimeView();
        break;

    case ConnectionState::Connecting:
        statusBar()->clearMessage();
        m_stateLabel->setText(tr("连接中…"));
        m_connectButton->setEnabled(false);
        m_disconnectButton->setEnabled(true);
        m_hostEdit->setEnabled(false);
        m_portSpin->setEnabled(false);
        clearRealtimeView();  // 重连时清掉旧会话的展示数据
        break;

    case ConnectionState::Handshaking:
        m_stateLabel->setText(tr("握手中…"));
        m_connectButton->setEnabled(false);
        m_disconnectButton->setEnabled(true);
        break;

    case ConnectionState::Connected:
        statusBar()->clearMessage();
        m_stateLabel->setText(tr("已连接"));
        m_connectButton->setEnabled(false);
        m_disconnectButton->setEnabled(true);
        break;

    case ConnectionState::Disconnecting:
        statusBar()->clearMessage();
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
        clearRealtimeView();
        break;
    }
}