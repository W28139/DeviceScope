#include "devicescope/protocol/Version.h"

namespace devicescope::protocol {

QString versionString()
{
    return QStringLiteral("DeviceScope protocol v%1").arg(kProtocolVersion);
}

} // namespace devicescope::protocol