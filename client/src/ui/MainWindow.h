#pragma once

#include "network/ConnectionState.h"

#include "devicescope/protocol/Message.h"

#include <QtCore/QVector>
#include <QtWidgets/QMainWindow>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

namespace devicescope::client {
class NetworkClient;
class TelemetryQueue;
}

// 主窗口：负责 UI 布局与交互，展示连接状态和遥测数据
// 自身不处理底层连接逻辑，通过 NetworkClient / TelemetryQueue 与工作线程交互
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    // ---------- 初始化 ----------
    void setupUi();                     // 搭建界面骨架（调用下面的 createXxxTab 等）
    QWidget *createRealtimeTab();       // 创建“实时数据”页，返回页面对应 QWidget

    // ---------- 交互槽函数 ----------
    void onConnectClicked();            // “连接”按钮：读取 host/port 并请求连接
    void onDisconnectClicked();         // “断开”按钮：请求断开
    void updateForState(devicescope::client::ConnectionState state);  // 根据连接状态刷新按钮可用性、状态标签等

    // ---------- 数据刷新 ----------
    void onTelemetryTick();             // 定时器回调：从 TelemetryQueue 批量取数据并刷新视图
    void appendSamples(const QVector<devicescope::protocol::Telemetry> &samples); // 追加一批样本到表格
    void updateSnapshot(const devicescope::protocol::Telemetry &sample);          // 更新“最新值”面板（温度/压力/RPM 等）
    void clearRealtimeView();           // 清空实时视图（断开或重连时调用）

    // ---------- 业务对象（非拥有/外部注入） ----------
    devicescope::client::NetworkClient *m_network = nullptr;   // 网络客户端：连接/断开入口
    devicescope::client::TelemetryQueue *m_telemetry = nullptr; // 遥测队列：工作线程写入、UI 线程消费

    // ---------- 连接参数输入 ----------
    QLineEdit *m_hostEdit = nullptr;    // 主机地址输入框
    QSpinBox *m_portSpin = nullptr;     // 端口输入框

    // ---------- 连接控制 ----------
    QPushButton *m_connectButton = nullptr;    // “连接”按钮
    QPushButton *m_disconnectButton = nullptr; // “断开”按钮

    // ---------- 状态显示 ----------
    QLabel *m_stateLabel = nullptr;     // 当前连接状态（Disconnected/Connecting/Connected...）
    QLabel *m_deviceLabel = nullptr;    // 设备信息（型号/ID 等）
    QLabel *m_statsLabel = nullptr;     // 收发统计（累计样本数、速率等）
    QLabel *m_droppedLabel = nullptr;   // 丢弃计数（与 m_shownDropped 配合，只显示增量/当前值）

    // ---------- 实时数据页控件 ----------
    QTableWidget *m_telemetryTable = nullptr; // 历史/滚动样本表格
    QLabel *m_temperatureLabel = nullptr;     // 最新温度值
    QLabel *m_pressureLabel = nullptr;        // 最新压力值
    QLabel *m_rpmLabel = nullptr;             // 最新转速值
    QLabel *m_stateValueLabel = nullptr;      // 最新设备状态值
    QLabel *m_lastUpdateLabel = nullptr;      // 最近一次更新时间戳

    // ---------- 刷新节流 ----------
    QTimer *m_refreshTimer = nullptr;   // 周期性刷新 UI（避免每条数据都更新界面）
    quint64 m_shownDropped = 0;         // 上次已展示的丢弃数，用于只在变化时刷新或计算增量
};