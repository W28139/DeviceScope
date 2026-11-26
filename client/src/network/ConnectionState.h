#pragma once

#include <QtCore/QMetaType>
#include <QtCore/QtGlobal>

namespace devicescope::client {

// 连接状态机：UI 只根据该状态更新控件。
enum class ConnectionState : quint8 {
    Disconnected,
    Connecting,
    Handshaking,
    Connected,
    Disconnecting,
    Error,
};

} // namespace devicescope::client

Q_DECLARE_METATYPE(devicescope::client::ConnectionState)