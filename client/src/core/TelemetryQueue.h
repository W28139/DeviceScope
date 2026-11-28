#pragma once

#include "devicescope/protocol/Message.h"

#include <QtCore/QMutex>
#include <QtCore/QQueue>
#include <QtCore/QVector>
// 自己写的一个数据结构，底层是队列
namespace devicescope::client {

// 线程安全的遥测有界队列：网络 worker 线程写入，UI 线程批量取出。
// 队列满时丢弃最旧样本并累计 dropped 计数，保证内存有界。
class TelemetryQueue
{
public:
    explicit TelemetryQueue(int capacity = 4096);

    // 入队一条遥测；队列已满时先丢弃最旧样本，再放入新样本。
    void push(const devicescope::protocol::Telemetry &sample);

    // 按先进先出取出最多 maxSamples 条；maxSamples <= 0 时返回空。
    QVector<devicescope::protocol::Telemetry> drain(int maxSamples);

    // 清空队列；不清零 dropped 计数（切换会话时调用）。
    void clear();

    int size() const;
    int capacity() const;
    quint64 droppedCount() const;

private:
    mutable QMutex m_mutex;
    QQueue<devicescope::protocol::Telemetry> m_samples;
    const int m_capacity;
    quint64 m_dropped = 0;
};

} // namespace devicescope::client