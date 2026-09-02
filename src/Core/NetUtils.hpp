#pragma once

#include <QHostAddress>
#include <QString>

namespace Acheron {
namespace Core {
namespace NetUtils {

/// Returns true when `host` resolves to a literal loopback/link-local/
/// multicast/private address and should not be fetched from untrusted
/// content (SSRF guard). This is a literal-IP check; callers following HTTP
/// redirects must re-check the redirect target and may additionally want DNS
/// rebinding protection at a higher layer.
inline bool isPrivateHost(const QString &host)
{
    if (host.isEmpty())
        return true;
    if (host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1")
        || host == QLatin1String("::1"))
        return true;

    QHostAddress addr(host);
    if (!addr.isNull()
        && (addr.isLoopback() || addr.isLinkLocal() || addr.isMulticast()))
        return true;

    if (!addr.isNull() && addr.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 ip = addr.toIPv4Address();
        if ((ip & 0xFF000000) == 0x0A000000) return true; // 10.0.0.0/8
        if ((ip & 0xFFF00000) == 0xAC100000) return true; // 172.16.0.0/12
        if ((ip & 0xFFFF0000) == 0xC0A80000) return true; // 192.168.0.0/16
        if ((ip & 0xFFFF0000) == 0xA9FE0000) return true; // 169.254.0.0/16
    } else if (!addr.isNull() && addr.protocol() == QAbstractSocket::IPv6Protocol) {
        const auto ip6 = addr.toIPv6Address();
        if ((ip6.c[0] & 0xFE) == 0xFC) return true; // fc00::/7 ULA
    }
    return false;
}

} // namespace NetUtils
} // namespace Core
} // namespace Acheron
