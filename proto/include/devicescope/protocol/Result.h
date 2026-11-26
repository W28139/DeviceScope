#pragma once

#include <QtCore/QtGlobal>

#include <optional>
#include <utility>

namespace devicescope::protocol {

// 协议库统一的错误码：所有编解码入口通过 Result 返回错误，
// 不跨模块抛异常。
enum class ErrorCode : quint8 {
    Ok = 0,
    InvalidMagic,          // 魔数不匹配
    UnsupportedVersion,    // 协议版本不支持
    TruncatedHeader,       // 报文头不足 22 字节
    HeaderCrcMismatch,     // Header CRC 校验失败
    PayloadTooLarge,       // 长度字段超过 1 MiB
    TruncatedPayload,      // 实际负载小于声明长度
    PayloadLengthMismatch, // 实际负载大于声明长度
    PayloadCrcMismatch,    // Payload CRC 校验失败
    MalformedPayload,      // 负载字段截断、多余字节或非法 UTF-8
    StringTooLong,         // 字符串 UTF-8 字节数超过 65535
    InvalidDeviceState,    // DeviceState 取值越界
    InvalidCommandId,      // CommandId 取值越界
    InvalidAckResult,      // AckResult 取值越界
    InvalidAlarmSeverity,  // AlarmSeverity 取值越界
    InvalidAlarmState,     // AlarmState 取值越界
};

// 轻量 Result：要么持有值，要么持有错误码。
// 调用方必须先检查 hasValue() / operator bool，再访问 value()。
template <typename T>
class Result
{
public:
    static Result success(T value) { return Result(std::move(value)); }
    static Result failure(ErrorCode code) { return Result(code); }

    bool hasValue() const noexcept { return m_value.has_value(); }
    explicit operator bool() const noexcept { return hasValue(); }

    ErrorCode error() const noexcept
    {
        return hasValue() ? ErrorCode::Ok : m_error;
    }

    const T &value() const & { return m_value.value(); }

private:
    explicit Result(T value)
        : m_value(std::move(value))
    {
    }

    explicit Result(ErrorCode code)
        : m_error(code)
    {
    }

    std::optional<T> m_value;
    ErrorCode m_error = ErrorCode::Ok;
};

} // namespace devicescope::protocol