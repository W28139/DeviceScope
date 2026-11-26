#include "DeviceModel.h"

#include <cmath>

namespace devicescope::simulator {

namespace {

constexpr double kTwoPi = 6.283185307179586;
constexpr double kAlarmUpper = 40.0; // 超过上限触发报警
constexpr double kAlarmLower = 30.0; // 低于下限清除报警（滞回）

} // namespace

DeviceModel::DeviceModel(quint32 seed)
    : m_rng(seed)
{
}

void DeviceModel::start()
{
    m_state = devicescope::protocol::DeviceState::Running;
}

void DeviceModel::stop()
{
    m_state = devicescope::protocol::DeviceState::Stopped;
}

void DeviceModel::reset()
{
    m_state = devicescope::protocol::DeviceState::Idle;
}

devicescope::protocol::DeviceState DeviceModel::state() const
{
    return m_state;
}

DeviceModel::Sample DeviceModel::sample(quint64 timestampMs)
{
    using namespace devicescope::protocol;  

    const double seconds = static_cast<double>(timestampMs) / 1000.0;  // 毫秒转秒，用于正弦周期
    const double pressure = 101.3 + 2.0 * std::sin(kTwoPi * seconds / 12.0)  // 压力：101.3 kPa 基准 + 12 秒周期波动
                            + m_noise(m_rng) * 0.2;  // 叠加小幅随机噪声

    double temperature = 25.0;  // 非运行态：温度恒为 25℃
    double rpm = 0.0;  // 非运行态：转速为 0
    if (m_state == DeviceState::Running) {  // 运行态才产生动态温度和转速
        temperature = 25.0 + 20.0 * std::sin(kTwoPi * seconds / 20.0) + m_noise(m_rng);  // 20 秒周期、±20℃ 波动 + 噪声
        rpm = 1500.0 + 300.0 * std::sin(kTwoPi * seconds / 7.0) + m_noise(m_rng) * 10.0;  // 7 秒周期、1500±300 转速 + 噪声
    }

    // 合成一次遥测采样的对象
    const Telemetry telemetry{timestampMs,  // 采样时间戳
                              static_cast<float>(temperature),  // 温度
                              static_cast<float>(pressure),  // 压力
                              static_cast<float>(rpm),  // 转速
                              m_state};  // 当前设备状态

    std::optional<Alarm> alarm;  // 本次采样是否产生报警事件
    if (temperature > kAlarmUpper && !m_alarmActive) {  // 温度超上限且此前未报警 → 产生新报警
        m_alarmActive = true;  // 标记报警已激活
        m_activeAlarmId = ++m_alarmCounter;  // 分配新报警 ID
        alarm = Alarm{m_activeAlarmId, 1001, AlarmSeverity::Critical, timestampMs,  // 报警 ID、错误码、级别、时间
                      AlarmState::Raised, QStringLiteral("温度过高")};  // 状态：产生
    } 
    else if (temperature < kAlarmLower && m_alarmActive) {  // 温度低于下限且此前在报警 → 清除报警（滞回）
        m_alarmActive = false;  // 标记报警已解除
        alarm = Alarm{m_activeAlarmId, 1001, AlarmSeverity::Critical, timestampMs,  // 复用同一报警 ID 便于配对
                      AlarmState::Cleared, QStringLiteral("温度恢复正常")};  // 状态：清除
        m_activeAlarmId = 0;  // 清空活动报警 ID
    }

    return Sample{telemetry, alarm};  // 返回本次遥测与可能的报警
}

} // namespace devicescope::simulator