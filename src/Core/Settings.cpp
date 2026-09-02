#include "Settings.hpp"

namespace Acheron {
namespace Core {

Settings &Settings::instance()
{
    static Settings s;
    return s;
}

Settings::Settings()
{
    load();
}

void Settings::load()
{
    QSettings s;

    m_developerMode = s.value("dev/developerMode", false).toBool();

    auto loadGroup = [&s](const QString &prefix) -> QStringList {
        s.beginGroup(prefix);
        const QStringList keys = s.childGroups();
        s.endGroup();
        return keys;
    };

    m_channelOverrides.clear();
    for (const QString &key : loadGroup("overrides/channels")) {
        bool ok = false;
        const quint64 raw = key.toULongLong(&ok);
        if (!ok || raw == 0)
            continue;
        const Snowflake id(raw);

        s.beginGroup(QStringLiteral("overrides/channels/%1").arg(key));
        ChannelNotificationOverride o;
        o.muted = s.value("muted", false).toBool();
        o.muteUntilMs = s.value("mute_until", 0).toLongLong();
        const int rawLevel = s.value("level", 0).toInt();
        o.level = (rawLevel >= 0 && rawLevel <= 3)
                ? static_cast<Discord::MessageNotificationLevel>(rawLevel)
                : Discord::MessageNotificationLevel::ALL_MESSAGES;
        s.endGroup();
        m_channelOverrides.insert(id, o);
    }

    m_serverOverrides.clear();
    for (const QString &key : loadGroup("overrides/servers")) {
        bool ok = false;
        const quint64 raw = key.toULongLong(&ok);
        if (!ok || raw == 0)
            continue;
        const Snowflake id(raw);

        s.beginGroup(QStringLiteral("overrides/servers/%1").arg(key));
        ServerNotificationOverride o;
        o.muted = s.value("muted", false).toBool();
        o.muteUntilMs = s.value("mute_until", 0).toLongLong();
        const int rawLevel = s.value("level", 0).toInt();
        o.level = (rawLevel >= 0 && rawLevel <= 3)
                ? static_cast<Discord::MessageNotificationLevel>(rawLevel)
                : Discord::MessageNotificationLevel::ALL_MESSAGES;
        s.endGroup();
        m_serverOverrides.insert(id, o);
    }
}

void Settings::save()
{
    QSettings s;

    s.setValue("dev/developerMode", m_developerMode);

    s.remove("overrides/channels");
    for (auto it = m_channelOverrides.cbegin(); it != m_channelOverrides.cend(); ++it) {
        s.beginGroup(QStringLiteral("overrides/channels/%1").arg(it.key().toString()));
        s.setValue("muted", it.value().muted);
        s.setValue("mute_until", it.value().muteUntilMs);
        s.setValue("level", static_cast<int>(it.value().level));
        s.endGroup();
    }

    s.remove("overrides/servers");
    for (auto it = m_serverOverrides.cbegin(); it != m_serverOverrides.cend(); ++it) {
        s.beginGroup(QStringLiteral("overrides/servers/%1").arg(it.key().toString()));
        s.setValue("muted", it.value().muted);
        s.setValue("mute_until", it.value().muteUntilMs);
        s.setValue("level", static_cast<int>(it.value().level));
        s.endGroup();
    }

    s.sync();
}

void Settings::setDeveloperMode(bool enabled)
{
    if (m_developerMode == enabled)
        return;
    m_developerMode = enabled;
    save();
    emit changed();
}

std::optional<ChannelNotificationOverride> Settings::channelOverride(Snowflake id) const
{
    auto it = m_channelOverrides.constFind(id);
    if (it == m_channelOverrides.constEnd())
        return std::nullopt;
    return it.value();
}

void Settings::setChannelOverride(Snowflake id, const ChannelNotificationOverride &override)
{
    m_channelOverrides.insert(id, override);
    save();
    emit changed();
}

void Settings::clearChannelOverride(Snowflake id)
{
    if (!m_channelOverrides.remove(id))
        return;
    save();
    emit changed();
}

std::optional<ServerNotificationOverride> Settings::serverOverride(Snowflake id) const
{
    auto it = m_serverOverrides.constFind(id);
    if (it == m_serverOverrides.constEnd())
        return std::nullopt;
    return it.value();
}

void Settings::setServerOverride(Snowflake id, const ServerNotificationOverride &override)
{
    m_serverOverrides.insert(id, override);
    save();
    emit changed();
}

void Settings::clearServerOverride(Snowflake id)
{
    if (!m_serverOverrides.remove(id))
        return;
    save();
    emit changed();
}

bool Settings::isChannelMuted(Snowflake id) const
{
    auto ov = channelOverride(id);
    return ov && ov->muted;
}

void Settings::setChannelMuted(Snowflake id, bool muted)
{
    if (muted) {
        auto ov = channelOverride(id).value_or(ChannelNotificationOverride{});
        ov.muted = true;
        ov.muteUntilMs = 0;
        setChannelOverride(id, ov);
        return;
    }

    auto ov = channelOverride(id);
    if (!ov || !ov->muted)
        return;
    ov->muted = false;
    ov->muteUntilMs = 0;
    if (ov->level == Discord::MessageNotificationLevel::ALL_MESSAGES)
        clearChannelOverride(id);
    else
        setChannelOverride(id, *ov);
}

bool Settings::isServerMuted(Snowflake id) const
{
    auto ov = serverOverride(id);
    return ov && ov->muted;
}

void Settings::setServerMuted(Snowflake id, bool muted)
{
    if (muted) {
        auto ov = serverOverride(id).value_or(ServerNotificationOverride{});
        ov.muted = true;
        ov.muteUntilMs = 0;
        setServerOverride(id, ov);
        return;
    }

    auto ov = serverOverride(id);
    if (!ov || !ov->muted)
        return;
    ov->muted = false;
    ov->muteUntilMs = 0;
    if (ov->level == Discord::MessageNotificationLevel::ALL_MESSAGES)
        clearServerOverride(id);
    else
        setServerOverride(id, *ov);
}

} // namespace Core
} // namespace Acheron
