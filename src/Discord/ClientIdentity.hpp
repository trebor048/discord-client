#pragma once

#include <QString>
#include <QMutex>

#include <optional>

#include "Objects.hpp"

namespace Acheron {
namespace Discord {

struct ClientPropertiesBuildParams
{
    bool includeClientHeartbeatSessionId;
    QString clientAppState;
    std::optional<bool> isFastConnect;
    std::optional<QString> gatewayConnectReasons;
};

class ClientIdentity
{
public:
    ClientIdentity();

    // QString getLaunchId() const;
    // QString getLaunchSignature() const;

    void regenerateClientHeartbeatSessionId();

    ClientProperties buildClientProperties(const ClientPropertiesBuildParams &params) const;

private:
    static QString generateLaunchSignature();

    mutable QMutex mutex;
    QString launchId;
    QString launchSignature;
    QString clientHeartbeatSessionId;
};

} // namespace Discord
} // namespace Acheron
