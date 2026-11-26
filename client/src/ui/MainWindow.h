#pragma once

#include "network/ConnectionState.h"

#include <QMainWindow>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace devicescope::client {
class NetworkClient;
}

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void setupUi();
    void onConnectClicked();
    void onDisconnectClicked();
    void updateForState(devicescope::client::ConnectionState state);

    devicescope::client::NetworkClient *m_network = nullptr;
    QLineEdit *m_hostEdit = nullptr;
    QSpinBox *m_portSpin = nullptr;
    QPushButton *m_connectButton = nullptr;
    QPushButton *m_disconnectButton = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_deviceLabel = nullptr;
    QLabel *m_statsLabel = nullptr;
};