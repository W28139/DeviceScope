#pragma once

#include <QtCore/QByteArrayView>
#include <QtCore/QtGlobal>

namespace devicescope::protocol {

// 计算 CRC32：反射多项式 0xEDB88320，初值 0xFFFFFFFF，结果异或 0xFFFFFFFF。
quint32 crc32(QByteArrayView data);

} // namespace devicescope::protocol