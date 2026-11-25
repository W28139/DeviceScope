#include <QtTest>

#include "devicescope/protocol/Version.h"

// 协议单元测试类：只做静态常量校验
class ProtocolTest final : public QObject
{
    Q_OBJECT

private slots:
// 各测试用例
    void protocolConstantsAreDefined();
};

void ProtocolTest::protocolConstantsAreDefined()
{
    // QCOMPARE(实际值, 期望值)，比较宏，检查括号里两个参数是否相等
    // 协议魔数：用于收发双方快速识别报文类型（0x4453 对应 ASCII "DS"）
    QCOMPARE(static_cast<int>(devicescope::protocol::kMagic), 0x4453);

    // 当前协议版本号，升级协议格式时需同步递增
    QCOMPARE(static_cast<int>(devicescope::protocol::kProtocolVersion), 1);

    // 固定报文头长度（22 字节），偏离该值说明包头定义被改动
    QCOMPARE(devicescope::protocol::kHeaderSize, 22);

    // 最大负载长度上限 1MB，用于限制单包数据、防止越界或内存爆量
    QCOMPARE(devicescope::protocol::kMaxPayloadSize, 1024u * 1024u);

    // 版本描述字符串应非空，便于日志排查时打出版本信息
    QVERIFY(!devicescope::protocol::versionString().isEmpty());
}

// 无界面测试入口，自动生成 main 并运行全部用例
QTEST_GUILESS_MAIN(ProtocolTest)

// 引入 moc 生成的元对象代码，保证 Q_OBJECT 宏生效
#include "test_placeholder.moc"