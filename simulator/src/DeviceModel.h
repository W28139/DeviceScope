#pragma once

#include "devicescope/protocol/Message.h"

#include <QtCore/QtGlobal>

#include <optional>
#include <random>

namespace devicescope::simulator {

// 设备行为模型：按时间戳生成遥测，并维护带滞回的报警状态。
class DeviceModel
{
public:
    struct Sample {
        devicescope::protocol::Telemetry telemetry;         // 本次采样的遥测数据
        std::optional<devicescope::protocol::Alarm> alarm;  // 本采样触发的报警事件；无报警则为空
    };

    // 构造函数：seed 决定随机噪声序列，便于复现
    explicit DeviceModel(quint32 seed = 1);

    void start();   // 切换设备到运行状态
    void stop();    // 切换设备到停止状态
    void reset();   // 复位设备状态与报警状态

    // 查询当前设备状态
    devicescope::protocol::DeviceState state() const;

    // 生成一次采样；timestampMs 由调用方提供（UTC 毫秒）。
    Sample sample(quint64 timestampMs);     // 按当前状态生成一次遥测，并判断是否触发/清除报警

private:
    std::mt19937 m_rng;     // 随机数引擎，给遥测加噪声
    std::uniform_real_distribution<double> m_noise{-0.5, 0.5};      // 噪声分布：[-0.5, 0.5)
    devicescope::protocol::DeviceState m_state = devicescope::protocol::DeviceState::Idle;  // 当前设备状态，默认空闲
    bool m_alarmActive = false;     // 当前是否处于报警中，用于滞回判断
    quint32 m_alarmCounter = 0;     // 报警 ID 计数器，每次新报警递增
    quint32 m_activeAlarmId = 0;    // 当前活动报警的 ID，清除时用它关联
};

} // namespace devicescope::simulator