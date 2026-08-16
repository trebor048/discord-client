#pragma once

#include <QObject>
#include <QHash>
#include <QSettings>

#include <optional>

#include "Core/Snowflake.hpp"
#include "Discord/Enums.hpp"

namespace Acheron {
namespace Core {

// Per-channel / per-server notification override, e.g. "notify only mentions
// for this channel" or "mute this server until <time>".
struct ChannelNotificationOverride
{
    bool muted = false;
    qint64 muteUntilMs = 0;
    Discord::MessageNotificationLevel level = Discord::MessageNotificationLevel::ALL_MESSAGES;
};

struct ServerNotificationOverride
{
    bool muted = false;
    qint64 muteUntilMs = 0;
    Discord::MessageNotificationLevel level = Discord::MessageNotificationLevel::ALL_MESSAGES;
};

// Central settings service (Stage 0.1). Wraps QSettings with typed accessors,
// a changed() signal, and per-channel/server notification overrides. Use the
// singleton via Settings::instance().
class Settings : public QObject
{
    Q_OBJECT
public:
    static Settings &instance();

    void load();
    void save();

    [[nodiscard]] bool developerMode() const { return m_developerMode; }
    void setDeveloperMode(bool enabled);

    [[nodiscard]] std::optional<ChannelNotificationOverride> channelOverride(Snowflake id) const;
    void setChannelOverride(Snowflake id, const ChannelNotificationOverride &override);
    void clearChannelOverride(Snowflake id);

    [[nodiscard]] std::optional<ServerNotificationOverride> serverOverride(Snowflake id) const;
    void setServerOverride(Snowflake id, const ServerNotificationOverride &override);
    void clearServerOverride(Snowflake id);

    [[nodiscard]] bool isChannelMuted(Snowflake id) const;
    void setChannelMuted(Snowflake id, bool muted);
    [[nodiscard]] bool isServerMuted(Snowflake id) const;
    void setServerMuted(Snowflake id, bool muted);

signals:
    void changed();

private:
    Settings();
    ~Settings() override = default;

    bool m_developerMode = false;
    QHash<Snowflake, ChannelNotificationOverride> m_channelOverrides;
    QHash<Snowflake, ServerNotificationOverride> m_serverOverrides;
};

// Convenience used by context menus.
inline bool isDeveloperModeEnabled()
{
    return Settings::instance().developerMode();
}

} // namespace Core
} // namespace Acheron
