#include "network/ConnectionWorker.h"

#include "devicescope/protocol/Codec.h"
#include "devicescope/protocol/Message.h"

#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QRandomGenerator>
#include <QtCore/QTimer>
#include <QtNetwork/QTcpSocket>

namespace devicescope::client {

using namespace devicescope::protocol;

namespace {
constexpr int kScanIntervalMs = 100;        // 超时扫描频率
constexpr qint64 kCommandTimeoutMs = 3000;  // 命令默认超时
constexpr int kHeartbeatIntervalMs = 2000;  // 心跳发送间隔
constexpr qint64 kLinkTimeoutMs = 6000;     // 超过该时间没有收到任何报文视为假死
constexpr qint64 kStableConnectionMs = 10000; // 连接稳定该时间后重置重连退避
constexpr int kReconnectBackoffMs[] = {1000, 2000, 4000, 8000, 16000, 30000};
} // namespace

ConnectionWorker::ConnectionWorker(QObject *parent)
    : QObject(parent)
{
}

void ConnectionWorker::start()
{
    m_clock.start();

    m_timeoutTimer = new QTimer(this);  // 必须在 worker 线程中创建
    m_timeoutTimer->setInterval(kScanIntervalMs);
    connect(m_timeoutTimer, &QTimer::timeout, this, &ConnectionWorker::scanPending);

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(kHeartbeatIntervalMs);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &ConnectionWorker::sendHeartbeat);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &ConnectionWorker::onReconnectTimeout);

    ensureSocket();
    emit connectionStateChanged(m_state);
}

// 由UI线程中的信号触发而执行；用户主动发起连接
void ConnectionWorker::connectToHost(const QString &host, quint16 port)
{
    ensureSocket();
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        return;
    }

    // 用户点击连接时，取消重连
    cancelReconnect();  // 用户手动连接优先于待执行的重连

    // 重置状态，开始新的连接
    m_host = host;
    m_port = port;
    m_manualDisconnect = false;
    m_reconnectActive = false;
    m_handshakeDone = false;

    startConnect(true);
}

// 实际发起连接的函数
// resetStats = false 重连调用
// resetStats = true 初次连接
void ConnectionWorker::startConnect(bool resetStats)
{
    ensureSocket();
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        return;
    }

    qInfo() << (resetStats ? "connecting to" : "reconnecting to") << m_host << m_port;

    resetSession();  // 清理上一次会话残留；正常情况下已清空，此处保证幂等
    m_helloRequestId = 0;
    if (resetStats) {
        m_receivedFrames = 0;   // 新连接，重置接收的帧数
        emit statsChanged(0);
    }

    // 设置状态为正在连接（第一次连接，不存在连接失败重试逻辑）
    setState(ConnectionState::Connecting);
    m_socket->connectToHost(m_host, m_port);
}

// 由UI线程中的信号触发而执行
void ConnectionWorker::disconnectFromHost()
{
    qInfo() << "manual disconnect requested";

    // 标记为“手动断开”，阻止自动重连逻辑被误触发
    m_manualDisconnect = true;
    m_reconnectActive = false;   // 关闭重连流程
    cancelReconnect();           // 取消已排定的重连定时器

    // 情况1：处于错误状态 —— socket 可能已不可用，直接 abort 并复位
    if (m_state == ConnectionState::Error) {
        if (m_socket != nullptr) {
            m_socket->abort();   // 立即丢弃 socket，不等待底层协议
        }
        resetSession();          // 清理会话相关资源
        setState(ConnectionState::Disconnected);
        return;
    }

    // 情况2：没有 socket 或已处于未连接状态 —— 无需真正断开
    if (m_socket == nullptr || m_socket->state() == QAbstractSocket::UnconnectedState) {
        resetSession();
        setState(ConnectionState::Disconnected);
        return;
    }

    // 情况3：正在连接中 —— abort 可立即取消进行中的连接/重连尝试
    if (m_socket->state() == QAbstractSocket::ConnectingState) {
        m_socket->abort();       // 取消进行中的连接/重连尝试
        resetSession();
        setState(ConnectionState::Disconnected);
        return;
    }

    // 情况4：已连接 —— 进入“断开中”状态，优雅关闭
    // 实际断开完成信号（disconnected）会异步回调，届时再复位状态
    setState(ConnectionState::Disconnecting);
    m_socket->disconnectFromHost();
}

void ConnectionWorker::shutdown()
{
    // 程序退出等同于手动结束会话：不再安排自动重连
    m_manualDisconnect = true;
    m_reconnectActive = false;
    cancelReconnect();

    if (m_socket != nullptr) {
        m_socket->abort();
    }
}

// 发送一条控制命令，由 UI 线程中的信号触发，跨线程投递到 worker 线程执行
void ConnectionWorker::sendCommand(CommandId commandId)
{
    // 分配请求 ID
    const quint32 requestId = nextRequestId();

    // 前置条件校验：必须已连接且有 socket，否则直接失败
    if (m_state != ConnectionState::Connected || m_socket == nullptr) {
        emit commandFailed(requestId, QStringLiteral("未连接"));
        return;
    }

    // 编码命令体
    const auto payload = encodeCommand(Command{commandId, 0});
    if (!payload.hasValue()) {
        emit commandFailed(requestId, QStringLiteral("COMMAND 编码失败"));
        return;
    }

    // 封装成完整帧
    const auto frame = encode(MessageType::Command, kFlagNone, requestId, payload.value());
    if (!frame.hasValue()) {
        emit commandFailed(requestId, QStringLiteral("COMMAND 报文编码失败"));
        return;
    }

    // 写入 socket；write 返回 -1 表示立即失败
    if (m_socket->write(frame.value()) < 0) {
        emit commandFailed(requestId,
                           QStringLiteral("发送失败: %1").arg(m_socket->errorString()));
        return;
    }

    // 记录"待 ACK"请求：以 requestId 为 key，保存 commandId 和发送时刻
    // 发送时刻用单调时钟，后续收到 ACK 时算 latencyMs
    m_pending.insert(requestId, PendingRequest{commandId, m_clock.elapsed()});
    // 通知外界"命令已发出"
    emit commandSent(requestId, commandId);
}

void ConnectionWorker::ensureSocket()
{
    if (m_socket != nullptr) {
        return;
    }
    
    // 新建立的连接，先绑定信号与槽函数，客户端一个socket对应一个连接
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &ConnectionWorker::onConnected);        // 建立连接时，先握手
    connect(m_socket, &QTcpSocket::disconnected, this, &ConnectionWorker::onDisconnected);  // 关闭连接信号，触发关闭
    connect(m_socket, &QTcpSocket::readyRead, this, &ConnectionWorker::onReadyRead);        // 读事件触发
    connect(m_socket, &QTcpSocket::errorOccurred, this, &ConnectionWorker::onSocketError);  // 错误事件触发
}

void ConnectionWorker::setState(ConnectionState state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;

    // 扫描定时器只在 Connected 时运行，断开后不再访问请求表。
    if (m_timeoutTimer != nullptr) {
        if (state == ConnectionState::Connected) {
            m_timeoutTimer->start();
        } 
        else {
            m_timeoutTimer->stop();
        }
    }

    // 心跳与假死检测也只在 Connected 期间工作。
    if (m_heartbeatTimer != nullptr) {
        if (state == ConnectionState::Connected) {
            m_lastRx.start();
            m_heartbeatTimer->start();
        } 
        else {
            m_heartbeatTimer->stop();
        }
    }

    emit connectionStateChanged(state);
}

void ConnectionWorker::fail(const QString &message)
{
    qWarning() << "connection failed:" << message;
    emit connectionError(message);
    setState(ConnectionState::Error);
    resetSession();
    if (m_socket != nullptr && m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
}

// 结束一次会话：未完成命令统一通知一次失败，清空解包器和定时器状态。
// 可以重复调用：请求表清空后不会重复发失败信号。
void ConnectionWorker::resetSession()
{
    for (auto it = m_pending.constBegin(); it != m_pending.constEnd(); ++it) {
        emit commandFailed(it.key(), QStringLiteral("连接断开"));
    }
    m_pending.clear();

    m_decoder = StreamDecoder();
    m_lastRx.invalidate();

    if (m_timeoutTimer != nullptr) {
        m_timeoutTimer->stop();
    }
    if (m_heartbeatTimer != nullptr) {
        m_heartbeatTimer->stop();
    }
}

// 会话不可用后决定是否进入自动重连：
// - 用户手动断开：不重连；
// - 初次连接从未握手成功：不自动重试，交给用户；
// - 已握手过的会话异常断开（含重连尝试失败）：按退避继续重连。
void ConnectionWorker::onSessionLost()
{
    // 用户主动断开 —— 不自动重连，同时清除重连标志位 
    if (m_manualDisconnect) {
        m_reconnectActive = false;
        return;
    }
    // 握手尚未完成
    if (!m_handshakeDone) {
        return;
    }

    m_reconnectActive = true;
    scheduleReconnect();
}

void ConnectionWorker::scheduleReconnect()
{
    // 重连定时器已在运行 —— 说明已经安排了下一次重连，无需重复调度
    if (m_reconnectTimer == nullptr || m_reconnectTimer->isActive()) {
        return;
    }

    // 没有目标端口 —— 无法重连，直接放弃并清标志
    if (m_port == 0) {  
        m_reconnectActive = false;
        return;
    }

    // 计算退避延迟：取当前尝试次数对应的基准值（超过 5 次封顶于最后一档）
    const int index = qMin(m_reconnectAttempt, 5);
    const int baseMs = kReconnectBackoffMs[index];
    //    加入 ±20% 随机抖动，避免多客户端同时掉线后「重连风暴」同步冲击服务端
    const int jitterMs = baseMs / 5;  // ±20% 抖动
    const int delayMs = baseMs + QRandomGenerator::global()->bounded(-jitterMs, jitterMs + 1);

    // 先自增尝试计数，再进入等待：
    //    - 计数用于下一次退避计算，也随信号上报给 UI 展示「第 N 次重连」
    ++m_reconnectAttempt;
    //    等待期间状态复用 Connecting：这样断开按钮仍走 disconnectFromHost 的
    //    "ConnectingState" 分支，可 abort 并取消这次等待中的重连
    setState(ConnectionState::Connecting);  // 等待期间复用 Connecting，断开按钮可取消
    qInfo() << "reconnect attempt" << m_reconnectAttempt << "scheduled in" << delayMs << "ms";
    emit reconnectScheduled(m_reconnectAttempt, delayMs);
    // 启动定时器，到点后触发实际的重连（connectToHost + 握手）
    m_reconnectTimer->start(delayMs);
}

void ConnectionWorker::cancelReconnect()
{
    if (m_reconnectTimer != nullptr) {
        m_reconnectTimer->stop();
    }
    m_reconnectAttempt = 0;
}

void ConnectionWorker::onReconnectTimeout()
{
    startConnect(false);
}

quint32 ConnectionWorker::nextRequestId()
{
    const quint32 id = m_nextRequestId++;
    if (m_nextRequestId == 0) {  // 回绕到 0 时跳到 1，0 保留给未分配/无效请求
        m_nextRequestId = 1;
    }
    return id;
}

// 处理服务端返回的 COMMAND_ACK，与 sendCommand() 中登记的 pending 记录闭环
void ConnectionWorker::handleCommandAck(const DecodedMessage &message)
{
    // 1. 解码 ACK 负载（commandId / result / message 等）
    const auto ack = decodeCommandAck(message.payload);
    if (!ack.hasValue()) {  // 负载已通过 CRC，格式错误只记录并忽略
        qWarning() << "COMMAND_ACK 解析失败";
        return;
    }

    // 2. 用帧头里的 requestId 去 pending 表里找对应请求
    const quint32 requestId = message.header.requestId;
    const auto it = m_pending.find(requestId);
    if (it == m_pending.end()) {  
        // 找不到：可能是已超时清除、或服务端重复发 ACK，忽略即可
        qWarning() << "收到未知 requestId 的 COMMAND_ACK:" << requestId;
        return;
    }
    // 3. 校验 ACK 里的 commandId 是否和当初发出的命令一致
    //    不一致说明协议/服务端有异常，但这里只告警、仍继续处理（宽松策略）
    if (it->commandId != ack.value().commandId) {
        qWarning() << "COMMAND_ACK 命令不匹配: 期望"
                   << static_cast<int>(it->commandId) << "实际"
                   << static_cast<int>(ack.value().commandId);
    }

    // 4. 用单调时钟计算延迟，然后从 pending 中移除，避免重复匹配
    const qint64 latencyMs = m_clock.elapsed() - it->sentAtMs;
    m_pending.erase(it);

    // 5. 对外发出 ACK 信号，UI 据此更新命令状态、展示延迟/结果
    emit commandAcknowledged(requestId, ack.value().commandId, ack.value().result,
                             ack.value().message, latencyMs);
}

// 100 ms 扫描一次：超过默认超时的命令移出请求表并上报。
void ConnectionWorker::scanPending()
{
    // 假死检测：超过阈值没有解出任何报文，视为链路失效。
    if (m_lastRx.isValid() && m_lastRx.elapsed() >= kLinkTimeoutMs) {
        fail(QStringLiteral("心跳超时"));
        return;
    }

    // 稳定连接超过阈值后，下一次断线重新从 1 秒退避开始。
    if (m_connectedAt.isValid() && m_connectedAt.elapsed() >= kStableConnectionMs) {
        m_reconnectAttempt = 0;
    }

    const qint64 now = m_clock.elapsed();
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (now - it->sentAtMs >= kCommandTimeoutMs) {
            const quint32 requestId = it.key();
            it = m_pending.erase(it);
            qWarning() << "command timed out, requestId" << requestId;
            emit commandTimedOut(requestId);
        } else {
            ++it;
        }
    }
}

// 握手函数
void ConnectionWorker::sendHello()
{
    const auto payload =
        encodeHelloRequest(HelloRequest{QStringLiteral("DeviceScope Client")});
    if (!payload.hasValue()) {
        fail(QStringLiteral("HELLO 编码失败"));
        return;
    }

    m_helloRequestId = nextRequestId();
    const auto frame =
        encode(MessageType::Hello, kFlagNone, m_helloRequestId, payload.value());
    if (!frame.hasValue()) {
        fail(QStringLiteral("HELLO 编码失败"));
        return;
    }
    m_socket->write(frame.value());
}

void ConnectionWorker::onConnected()
{
    setState(ConnectionState::Handshaking);
    sendHello();
}

// 心跳只用于链路存活，不参与命令超时判定。
void ConnectionWorker::sendHeartbeat()
{
    if (m_socket == nullptr || m_state != ConnectionState::Connected) {
        return;
    }

    const auto payload =
        encodeHeartbeat(Heartbeat{static_cast<quint64>(QDateTime::currentMSecsSinceEpoch())});
    if (!payload.hasValue()) {
        return;
    }
    const auto frame = encode(MessageType::Heartbeat, kFlagNone, 0, payload.value());
    if (frame.hasValue()) {
        m_socket->write(frame.value());
    }
}

// socket 的 QTcpSocket::disconnected 信号回调：连接从已连接变为未连接，如对端关闭
// 覆盖“已经连上过、然后断开”的主路径
void ConnectionWorker::onDisconnected()
{
    qInfo() << "socket disconnected";

    // 清理本次会话资源
    resetSession();

    // 已进入重连等待时保持 Connecting，避免状态在 Disconnected/Connecting 间闪动,其它状态统一置为 Disconnected
    if (!m_reconnectActive) {
        // Error 状态保留，便于 UI 显示失败原因；用户重新连接或点击断开后清除。
        if (m_state != ConnectionState::Error) {
            setState(ConnectionState::Disconnected);
        }
    }

    // 交给掉线处理逻辑决定是否调度重连
    onSessionLost();
}

// 任意阶段发生 socket 错误
// 覆盖“重连尝试本身失败”的路径
void ConnectionWorker::onSocketError()
{
    if (m_socket == nullptr) {  // socket 已不存在，没有错误可处理
        return;
    }

    switch (m_state) {
    // 正在建立连接时出错（连接被拒、主机不可达、超时等）
    case ConnectionState::Connecting:
        emit connectionError(QStringLiteral("连接失败: %1").arg(m_socket->errorString()));    // 带“连接失败”前缀提示
        setState(ConnectionState::Error);
        break;
    // 已连上但握手阶段出错（对端断开、协议错误等）
    case ConnectionState::Handshaking:
    // 已握手后运行中出错（连接被重置、读写出错等） 
    case ConnectionState::Connected:
        emit connectionError(m_socket->errorString());    // 直接上报底层错误文本
        setState(ConnectionState::Error);
        break;
    // 已断开/已错误等状态，忽略重复错误
    default:
        break;
    }

    // socket 已不可用；必要时安排自动重连（连接尝试失败时不会触发 disconnected）
    onSessionLost();
}

void ConnectionWorker::onReadyRead()
{
    m_decoder.append(m_socket->readAll());  

    DecodedMessage message;
    while (true) {  // 尽可能多解帧，直到半包、出错或需要等待
        const StreamStatus status = m_decoder.next(message);  // 尝试从缓冲区解出一帧
        if (status == StreamStatus::Message) {  // 成功解出一帧
            ++m_receivedFrames;  // 累计已接收帧数
            m_lastRx.restart();  // 任何完整报文都算链路存活
            emit statsChanged(m_receivedFrames);  // 通知上层统计变化

            if (m_state == ConnectionState::Handshaking) {  // 仅在握手阶段处理握手相关消息
                if (message.header.type == MessageType::Error) {  // 对端回了 ERROR，握手失败
                    const auto error = decodeError(message.payload);  // 解码错误消息体
                    fail(error.hasValue() ? error.value().message  // 有消息体就用其文本
                                          : QStringLiteral("握手失败"));  // 解码失败则用默认文本
                    return;  // 握手已失败，停止继续解析
                }

                if (message.header.type == MessageType::Hello) {  // 收到 HELLO 响应
                    const auto response = decodeHelloResponse(message.payload);  // 解码响应体
                    if (!response.hasValue()) {  // 响应体格式不对
                        fail(QStringLiteral("HELLO 响应格式错误"));
                        return;
                    }
                    if (response.value().result != AckResult::Ok) {  // 对端明确拒绝握手
                        fail(QStringLiteral("握手被拒绝"));
                        return;
                    }
                    m_handshakeDone = true;
                    m_reconnectActive = false;
                    m_connectedAt.start();
                    qInfo() << "handshake ok: device" << response.value().deviceId
                             << response.value().deviceName;
                    setState(ConnectionState::Connected);  // 握手成功，进入已连接状态
                    emit handshakeSucceeded(response.value().deviceId,  // 上报设备 ID
                                            response.value().deviceName);  // 上报设备名
                }
            } else if (message.header.type == MessageType::CommandAck) {
                handleCommandAck(message);  // 按 requestId 匹配命令响应
            }
            continue;  // 继续解下一帧（可能还有粘包）
        }

        if (status == StreamStatus::BadPacket) {  // 坏包已被解码器丢弃/重同步
            continue;  // 继续尝试解下一帧
        }
        if (status == StreamStatus::NeedMoreData) {  // 半包，缓冲区数据不够
            break;  // 等下一次 readyRead
        }
        if (status == StreamStatus::UnsupportedVersion) {  // 协议版本不支持
            fail(QStringLiteral("协议版本不支持"));  // 握手/连接失败
            return;
        }
        fail(QStringLiteral("协议流错误，连接已断开"));  // 其他致命流错误
        return;
    }
}

} // namespace devicescope::client