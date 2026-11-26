#include "devicescope/protocol/Version.h"

#include "devicescope/protocol/Protocol.h"

namespace devicescope::protocol {

QString versionString()
{
    return QStringLiteral("DeviceScope protocol v%1").arg(kProtocolVersion);
}

} // namespace devicescope::protocol