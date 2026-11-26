#include "devicescope/protocol/Crc32.h"

#include <array>
#include <cstddef>

namespace devicescope::protocol {

namespace {

// 编译期生成查表：多项式 0xEDB88320 的 256 项查找表。
constexpr std::array<quint32, 256> makeCrcTable()
{
    std::array<quint32, 256> table{};
    for (std::size_t index = 0; index < table.size(); ++index) {
        quint32 value = static_cast<quint32>(index);
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
        }
        table[index] = value;
    }
    return table;
}

constexpr std::array<quint32, 256> kCrcTable = makeCrcTable();

} // namespace

quint32 crc32(QByteArrayView data)
{
    quint32 state = 0xFFFFFFFFu;
    const auto *bytes = reinterpret_cast<const quint8 *>(data.data());
    for (qsizetype index = 0; index < data.size(); ++index) {
        state = kCrcTable[(state ^ bytes[index]) & 0xFFu] ^ (state >> 8);
    }
    return state ^ 0xFFFFFFFFu;
}

} // namespace devicescope::protocol