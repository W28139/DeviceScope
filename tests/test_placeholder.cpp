#include <QtTest>

#include "devicescope/protocol/Version.h"

class ProtocolTest final : public QObject
{
    Q_OBJECT

private slots:
    void protocolConstantsAreDefined();
};

void ProtocolTest::protocolConstantsAreDefined()
{
    QCOMPARE(static_cast<int>(devicescope::protocol::kMagic), 0x4453);
    QCOMPARE(static_cast<int>(devicescope::protocol::kProtocolVersion), 1);
    QCOMPARE(devicescope::protocol::kHeaderSize, 22);
    QCOMPARE(devicescope::protocol::kMaxPayloadSize, 1024u * 1024u);
    QVERIFY(!devicescope::protocol::versionString().isEmpty());
}

QTEST_GUILESS_MAIN(ProtocolTest)

#include "test_placeholder.moc"