#include "core/TelemetryQueue.h"

#include <QtCore/QMutexLocker>
#include <QtCore/QtGlobal>

namespace devicescope::client {

TelemetryQueue::TelemetryQueue(int capacity)
    : m_capacity(qMax(1, capacity))
{
}

void TelemetryQueue::push(const devicescope::protocol::Telemetry &sample)
{
    QMutexLocker<QMutex> locker(&m_mutex);

    // 队列已满：丢最旧的，保证有界并记录丢弃量。
    if (m_samples.size() >= m_capacity) {
        m_samples.dequeue();
        ++m_dropped;
    }
    m_samples.enqueue(sample);
}

QVector<devicescope::protocol::Telemetry> TelemetryQueue::drain(int maxSamples)
{
    QVector<devicescope::protocol::Telemetry> result;
    if (maxSamples <= 0) {
        return result;
    }

    QMutexLocker<QMutex> locker(&m_mutex);
    const int count = qMin(maxSamples, m_samples.size());
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        result.append(m_samples.dequeue());
    }
    return result;
}

void TelemetryQueue::clear()
{
    QMutexLocker<QMutex> locker(&m_mutex);
    m_samples.clear();
}

int TelemetryQueue::size() const
{
    QMutexLocker<QMutex> locker(&m_mutex);
    return m_samples.size();
}

int TelemetryQueue::capacity() const
{
    return m_capacity;
}

quint64 TelemetryQueue::droppedCount() const
{
    QMutexLocker<QMutex> locker(&m_mutex);
    return m_dropped;
}

} // namespace devicescope::client