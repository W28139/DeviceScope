#include <QtTest>

#include "core/TelemetryQueue.h"

#include <thread>
#include <vector>

using devicescope::client::TelemetryQueue;
using devicescope::protocol::Telemetry;

namespace {

Telemetry makeSample(quint64 timestampMs)
{
    Telemetry sample;
    sample.timestampMs = timestampMs;
    sample.temperatureC = static_cast<float>(timestampMs);
    sample.pressureKPa = 100.0f;
    sample.rpm = 1000.0f;
    sample.deviceState = devicescope::protocol::DeviceState::Running;
    return sample;
}

} // namespace

// 遥测有界队列：FIFO、满时丢最旧、批量取出与并发写入。
class TelemetryQueueTest final : public QObject
{
    Q_OBJECT

private slots:
    void pushAndDrainInOrder();
    void dropOldestWhenFull();
    void drainRespectsLimit();
    void clearKeepsDroppedCount();
    void concurrentPushIsSafe();
};

void TelemetryQueueTest::pushAndDrainInOrder()
{
    TelemetryQueue queue(10);
    for (quint64 i = 0; i < 5; ++i) {
        queue.push(makeSample(i));
    }
    QCOMPARE(queue.size(), 5);
    QCOMPARE(queue.droppedCount(), quint64(0));

    const auto first = queue.drain(3);
    QCOMPARE(first.size(), 3);
    for (int i = 0; i < first.size(); ++i) {
        QCOMPARE(first.at(i).timestampMs, quint64(i));
    }
    QCOMPARE(queue.size(), 2);

    const auto rest = queue.drain(10);
    QCOMPARE(rest.size(), 2);
    QCOMPARE(rest.at(0).timestampMs, quint64(3));
    QCOMPARE(rest.at(1).timestampMs, quint64(4));
    QCOMPARE(queue.size(), 0);
}

void TelemetryQueueTest::dropOldestWhenFull()
{
    TelemetryQueue queue(3);
    for (quint64 i = 0; i < 5; ++i) {
        queue.push(makeSample(i));
    }

    QCOMPARE(queue.size(), 3);
    QCOMPARE(queue.droppedCount(), quint64(2));

    const auto samples = queue.drain(10);
    QCOMPARE(samples.size(), 3);
    QCOMPARE(samples.at(0).timestampMs, quint64(2));
    QCOMPARE(samples.at(1).timestampMs, quint64(3));
    QCOMPARE(samples.at(2).timestampMs, quint64(4));
}

void TelemetryQueueTest::drainRespectsLimit()
{
    TelemetryQueue queue(10);
    for (quint64 i = 0; i < 5; ++i) {
        queue.push(makeSample(i));
    }

    QVERIFY(queue.drain(0).isEmpty());
    QVERIFY(queue.drain(-1).isEmpty());
    QCOMPARE(queue.size(), 5);

    const auto partial = queue.drain(2);
    QCOMPARE(partial.size(), 2);
    QCOMPARE(partial.at(0).timestampMs, quint64(0));
    QCOMPARE(partial.at(1).timestampMs, quint64(1));
    QCOMPARE(queue.size(), 3);
}

void TelemetryQueueTest::clearKeepsDroppedCount()
{
    TelemetryQueue queue(2);
    for (quint64 i = 0; i < 3; ++i) {
        queue.push(makeSample(i));
    }
    QCOMPARE(queue.droppedCount(), quint64(1));

    queue.clear();
    QCOMPARE(queue.size(), 0);
    QCOMPARE(queue.droppedCount(), quint64(1));
    QVERIFY(queue.drain(10).isEmpty());
}

void TelemetryQueueTest::concurrentPushIsSafe()
{
    TelemetryQueue queue(4096);
    constexpr int kThreads = 4;
    constexpr quint64 kPerThread = 500;

    std::vector<std::thread> producers;
    producers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        producers.emplace_back([&queue, t]() {
            for (quint64 i = 0; i < kPerThread; ++i) {
                queue.push(makeSample(static_cast<quint64>(t) * kPerThread + i));
            }
        });
    }
    for (std::thread &producer : producers) {
        producer.join();
    }

    QCOMPARE(queue.size(), static_cast<int>(kThreads * kPerThread));
    QCOMPARE(queue.droppedCount(), quint64(0));

    const auto samples = queue.drain(static_cast<int>(kThreads * kPerThread));
    QCOMPARE(samples.size(), static_cast<int>(kThreads * kPerThread));
    QCOMPARE(queue.size(), 0);
}

QTEST_GUILESS_MAIN(TelemetryQueueTest)

#include "test_telemetry_queue.moc"