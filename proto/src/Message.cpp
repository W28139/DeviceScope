#include "devicescope/protocol/Message.h"

#include <QtCore/QStringConverter>
#include <QtCore/QtEndian>

#include <bit>
#include <cstring>
#include <utility>

namespace devicescope::protocol {

namespace {

// 负载写入器：只负责按大端布局追加字段。
// 与下面的读取逻辑分开实现，避免编码/解码共用同一函数而掩盖偏移错误。
class PayloadWriter
{
public:
    void appendU8(quint8 value)
    {
        m_data.append(static_cast<char>(value));
    }

    void appendU16(quint16 value)
    {
        const quint16 be = qToBigEndian(value);
        m_data.append(reinterpret_cast<const char *>(&be), sizeof be);
    }

    void appendU32(quint32 value)
    {
        const quint32 be = qToBigEndian(value);
        m_data.append(reinterpret_cast<const char *>(&be), sizeof be);
    }

    void appendU64(quint64 value)
    {
        const quint64 be = qToBigEndian(value);
        m_data.append(reinterpret_cast<const char *>(&be), sizeof be);
    }

    void appendF32(float value)
    {
        appendU32(std::bit_cast<quint32>(value));
    }

    // 字符串编码：u16 字节长度 + UTF-8 字节。超过 65535 字节返回 false。
    bool appendString(const QString &value)
    {
        const QByteArray utf8 = value.toUtf8();
        if (utf8.size() > static_cast<qsizetype>(kMaxStringBytes)) {
            return false;
        }
        appendU16(static_cast<quint16>(utf8.size()));
        m_data.append(utf8);
        return true;
    }

    QByteArray take() { return std::move(m_data); }

private:
    QByteArray m_data;
};

// 负载读取器：每次读取都做边界检查，越界返回 false。
class PayloadReader
{
public:
    explicit PayloadReader(QByteArrayView data)
        : m_data(data)
    {
    }

    bool readU8(quint8 &value)
    {
        if (remaining() < 1) {
            return false;
        }
        value = static_cast<quint8>(m_data[m_offset]);
        ++m_offset;
        return true;
    }

    bool readU16(quint16 &value)
    {
        if (remaining() < static_cast<qsizetype>(sizeof(quint16))) {
            return false;
        }
        quint16 raw = 0;
        std::memcpy(&raw, m_data.data() + m_offset, sizeof raw);
        value = qFromBigEndian(raw);
        m_offset += static_cast<qsizetype>(sizeof raw);
        return true;
    }

    bool readU32(quint32 &value)
    {
        if (remaining() < static_cast<qsizetype>(sizeof(quint32))) {
            return false;
        }
        quint32 raw = 0;
        std::memcpy(&raw, m_data.data() + m_offset, sizeof raw);
        value = qFromBigEndian(raw);
        m_offset += static_cast<qsizetype>(sizeof raw);
        return true;
    }

    bool readU64(quint64 &value)
    {
        if (remaining() < static_cast<qsizetype>(sizeof(quint64))) {
            return false;
        }
        quint64 raw = 0;
        std::memcpy(&raw, m_data.data() + m_offset, sizeof raw);
        value = qFromBigEndian(raw);
        m_offset += static_cast<qsizetype>(sizeof raw);
        return true;
    }

    bool readF32(float &value)
    {
        quint32 raw = 0;
        if (!readU32(raw)) {
            return false;
        }
        value = std::bit_cast<float>(raw);
        return true;
    }

    // 字符串读取：u16 长度必须落在剩余范围内，且内容必须是合法 UTF-8。
    bool readString(QString &value)
    {
        quint16 size = 0;
        if (!readU16(size)) {
            return false;
        }
        if (remaining() < static_cast<qsizetype>(size)) {
            return false;
        }

        const QByteArrayView bytes = m_data.sliced(m_offset, size);
        QStringDecoder decoder(QStringDecoder::Utf8);
        value = decoder(bytes);
        if (decoder.hasError()) {
            return false;
        }

        m_offset += static_cast<qsizetype>(size);
        return true;
    }

    bool atEnd() const { return m_offset == m_data.size(); }

private:
    qsizetype remaining() const { return m_data.size() - m_offset; }

    QByteArrayView m_data;
    qsizetype m_offset = 0;
};

template <typename Enum>
bool enumInRange(quint8 raw, Enum first, Enum last)
{
    return raw >= static_cast<quint8>(first) && raw <= static_cast<quint8>(last);
}

} // namespace

Result<QByteArray> encodeHelloRequest(const HelloRequest &message)
{
    PayloadWriter writer;
    if (!writer.appendString(message.clientName)) {
        return Result<QByteArray>::failure(ErrorCode::StringTooLong);
    }
    return Result<QByteArray>::success(writer.take());
}

Result<HelloRequest> decodeHelloRequest(QByteArrayView payload)
{
    PayloadReader reader(payload);
    HelloRequest message;
    if (!reader.readString(message.clientName) || !reader.atEnd()) {
        return Result<HelloRequest>::failure(ErrorCode::MalformedPayload);
    }
    return Result<HelloRequest>::success(message);
}

Result<QByteArray> encodeHelloResponse(const HelloResponse &message)
{
    PayloadWriter writer;
    writer.appendU16(message.deviceId);
    if (!writer.appendString(message.deviceName)) {
        return Result<QByteArray>::failure(ErrorCode::StringTooLong);
    }
    writer.appendU32(message.capabilities);
    writer.appendU8(static_cast<quint8>(message.result));
    return Result<QByteArray>::success(writer.take());
}

Result<HelloResponse> decodeHelloResponse(QByteArrayView payload)
{
    PayloadReader reader(payload);
    HelloResponse message;
    quint8 result = 0;
    if (!reader.readU16(message.deviceId) || !reader.readString(message.deviceName)
        || !reader.readU32(message.capabilities) || !reader.readU8(result)) {
        return Result<HelloResponse>::failure(ErrorCode::MalformedPayload);
    }
    if (!enumInRange(result, AckResult::Ok, AckResult::Failed)) {
        return Result<HelloResponse>::failure(ErrorCode::InvalidAckResult);
    }
    if (!reader.atEnd()) {
        return Result<HelloResponse>::failure(ErrorCode::MalformedPayload);
    }
    message.result = static_cast<AckResult>(result);
    return Result<HelloResponse>::success(message);
}

Result<QByteArray> encodeHeartbeat(const Heartbeat &message)
{
    PayloadWriter writer;
    writer.appendU64(message.timestampMs);
    return Result<QByteArray>::success(writer.take());
}

Result<Heartbeat> decodeHeartbeat(QByteArrayView payload)
{
    PayloadReader reader(payload);
    Heartbeat message;
    if (!reader.readU64(message.timestampMs) || !reader.atEnd()) {
        return Result<Heartbeat>::failure(ErrorCode::MalformedPayload);
    }
    return Result<Heartbeat>::success(message);
}

Result<QByteArray> encodeTelemetry(const Telemetry &message)
{
    PayloadWriter writer;
    writer.appendU64(message.timestampMs);
    writer.appendF32(message.temperatureC);
    writer.appendF32(message.pressureKPa);
    writer.appendF32(message.rpm);
    writer.appendU8(static_cast<quint8>(message.deviceState));
    return Result<QByteArray>::success(writer.take());
}

Result<Telemetry> decodeTelemetry(QByteArrayView payload)
{
    PayloadReader reader(payload);
    Telemetry message;
    quint8 deviceState = 0;
    if (!reader.readU64(message.timestampMs) || !reader.readF32(message.temperatureC)
        || !reader.readF32(message.pressureKPa) || !reader.readF32(message.rpm)
        || !reader.readU8(deviceState)) {
        return Result<Telemetry>::failure(ErrorCode::MalformedPayload);
    }
    if (!enumInRange(deviceState, DeviceState::Idle, DeviceState::Fault)) {
        return Result<Telemetry>::failure(ErrorCode::InvalidDeviceState);
    }
    if (!reader.atEnd()) {
        return Result<Telemetry>::failure(ErrorCode::MalformedPayload);
    }
    message.deviceState = static_cast<DeviceState>(deviceState);
    return Result<Telemetry>::success(message);
}

Result<QByteArray> encodeCommand(const Command &message)
{
    PayloadWriter writer;
    writer.appendU8(static_cast<quint8>(message.commandId));
    writer.appendU32(message.parameter);
    return Result<QByteArray>::success(writer.take());
}

Result<Command> decodeCommand(QByteArrayView payload)
{
    PayloadReader reader(payload);
    Command message;
    quint8 commandId = 0;
    if (!reader.readU8(commandId) || !reader.readU32(message.parameter)) {
        return Result<Command>::failure(ErrorCode::MalformedPayload);
    }
    if (!enumInRange(commandId, CommandId::Start, CommandId::Reset)) {
        return Result<Command>::failure(ErrorCode::InvalidCommandId);
    }
    if (!reader.atEnd()) {
        return Result<Command>::failure(ErrorCode::MalformedPayload);
    }
    message.commandId = static_cast<CommandId>(commandId);
    return Result<Command>::success(message);
}

Result<QByteArray> encodeCommandAck(const CommandAck &message)
{
    PayloadWriter writer;
    writer.appendU8(static_cast<quint8>(message.commandId));
    writer.appendU8(static_cast<quint8>(message.result));
    if (!writer.appendString(message.message)) {
        return Result<QByteArray>::failure(ErrorCode::StringTooLong);
    }
    return Result<QByteArray>::success(writer.take());
}

Result<CommandAck> decodeCommandAck(QByteArrayView payload)
{
    PayloadReader reader(payload);
    CommandAck message;
    quint8 commandId = 0;
    quint8 result = 0;
    if (!reader.readU8(commandId) || !reader.readU8(result)
        || !reader.readString(message.message)) {
        return Result<CommandAck>::failure(ErrorCode::MalformedPayload);
    }
    if (!enumInRange(commandId, CommandId::Start, CommandId::Reset)) {
        return Result<CommandAck>::failure(ErrorCode::InvalidCommandId);
    }
    if (!enumInRange(result, AckResult::Ok, AckResult::Failed)) {
        return Result<CommandAck>::failure(ErrorCode::InvalidAckResult);
    }
    if (!reader.atEnd()) {
        return Result<CommandAck>::failure(ErrorCode::MalformedPayload);
    }
    message.commandId = static_cast<CommandId>(commandId);
    message.result = static_cast<AckResult>(result);
    return Result<CommandAck>::success(message);
}

Result<QByteArray> encodeAlarm(const Alarm &message)
{
    PayloadWriter writer;
    writer.appendU32(message.alarmId);
    writer.appendU16(message.code);
    writer.appendU8(static_cast<quint8>(message.severity));
    writer.appendU64(message.timestampMs);
    writer.appendU8(static_cast<quint8>(message.state));
    if (!writer.appendString(message.message)) {
        return Result<QByteArray>::failure(ErrorCode::StringTooLong);
    }
    return Result<QByteArray>::success(writer.take());
}

Result<Alarm> decodeAlarm(QByteArrayView payload)
{
    PayloadReader reader(payload);
    Alarm message;
    quint8 severity = 0;
    quint8 state = 0;
    if (!reader.readU32(message.alarmId) || !reader.readU16(message.code)
        || !reader.readU8(severity) || !reader.readU64(message.timestampMs)
        || !reader.readU8(state) || !reader.readString(message.message)) {
        return Result<Alarm>::failure(ErrorCode::MalformedPayload);
    }
    if (!enumInRange(severity, AlarmSeverity::Info, AlarmSeverity::Critical)) {
        return Result<Alarm>::failure(ErrorCode::InvalidAlarmSeverity);
    }
    if (!enumInRange(state, AlarmState::Raised, AlarmState::Cleared)) {
        return Result<Alarm>::failure(ErrorCode::InvalidAlarmState);
    }
    if (!reader.atEnd()) {
        return Result<Alarm>::failure(ErrorCode::MalformedPayload);
    }
    message.severity = static_cast<AlarmSeverity>(severity);
    message.state = static_cast<AlarmState>(state);
    return Result<Alarm>::success(message);
}

Result<QByteArray> encodeError(const Error &message)
{
    PayloadWriter writer;
    writer.appendU16(message.code);
    if (!writer.appendString(message.message)) {
        return Result<QByteArray>::failure(ErrorCode::StringTooLong);
    }
    return Result<QByteArray>::success(writer.take());
}

Result<Error> decodeError(QByteArrayView payload)
{
    PayloadReader reader(payload);
    Error message;
    if (!reader.readU16(message.code) || !reader.readString(message.message)
        || !reader.atEnd()) {
        return Result<Error>::failure(ErrorCode::MalformedPayload);
    }
    return Result<Error>::success(message);
}

} // namespace devicescope::protocol