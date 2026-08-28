#include "Core/Appearance/AppearanceConfig.hpp"

#include <QSettings>

#include <algorithm>
#include <cmath>

namespace Acheron {
namespace Core {
namespace Appearance {

AppearanceConfig &AppearanceConfig::instance()
{
    static AppearanceConfig config;
    return config;
}

AppearanceConfig::AppearanceConfig()
{
    load();
}

void AppearanceConfig::load()
{
    QSettings settings;
    memberListMode_ = settings.value(kMemberListModeKey, QStringLiteral("resize")).toString()
                              == QLatin1String("slide")
                              ? MemberListMode::SlideOut
                              : MemberListMode::ResizeHandle;
    memberCardScale_ = clampScale(settings.value(kMemberCardScaleKey, kDefaultScale).toFloat());
    guildIconScale_ = clampScale(settings.value(kGuildIconScaleKey, kDefaultScale).toFloat());
    channelScale_ = clampChannelScale(
            settings.value(kChannelScaleKey, kChannelDefaultScale).toFloat());
    numberedUnread_ = settings.value(kNumberedUnreadKey, true).toBool();
#if defined(Q_OS_WIN)
    customTitleBar_ = settings.value(kCustomTitleBarKey, true).toBool();
#else
    customTitleBar_ = settings.value(kCustomTitleBarKey, false).toBool();
#endif
}

void AppearanceConfig::save() const
{
    QSettings settings;
    settings.setValue(kMemberListModeKey,
                      memberListMode_ == MemberListMode::SlideOut ? QStringLiteral("slide")
                                                                  : QStringLiteral("resize"));
    settings.setValue(kMemberCardScaleKey, memberCardScale_);
    settings.setValue(kGuildIconScaleKey, guildIconScale_);
    settings.setValue(kChannelScaleKey, channelScale_);
    settings.setValue(kNumberedUnreadKey, numberedUnread_);
    settings.setValue(kCustomTitleBarKey, customTitleBar_);
    settings.sync();
}

float AppearanceConfig::clampScale(float value)
{
    // std::clamp passes NaN through (both comparisons are false) and lround of
    // NaN is UB, so reject non-finite inputs explicitly. Finite values clamp
    // to the configured bounds (2.0 -> max, 0.5 -> min).
    if (!std::isfinite(value))
        return kDefaultScale;
    return std::clamp(value, kMinScale, kMaxScale);
}

float AppearanceConfig::clampChannelScale(float value)
{
    // Same NaN guard as clampScale, against the channel list's wider range.
    if (!std::isfinite(value))
        return kChannelDefaultScale;
    return std::clamp(value, kChannelMinScale, kChannelMaxScale);
}

float AppearanceConfig::stepScale(float value, int steps)
{
    return clampScale(value + static_cast<float>(steps) * kStep);
}

int AppearanceConfig::scaledInt(int base, float scale)
{
    return static_cast<int>(std::lround(static_cast<float>(base) * clampScale(scale)));
}

int AppearanceConfig::channelScaledInt(int base, float scale)
{
    return static_cast<int>(std::lround(static_cast<float>(base) * clampChannelScale(scale)));
}

void AppearanceConfig::setMemberListMode(MemberListMode mode)
{
    if (mode == memberListMode_)
        return;
    memberListMode_ = mode;
    save();
    emit memberListModeChanged(memberListMode_);
    emit configChanged();
}

void AppearanceConfig::setMemberCardScale(float scale)
{
    scale = clampScale(scale);
    if (qFuzzyCompare(scale, memberCardScale_))
        return;
    memberCardScale_ = scale;
    save();
    emit configChanged();
}

void AppearanceConfig::setGuildIconScale(float scale)
{
    scale = clampScale(scale);
    if (qFuzzyCompare(scale, guildIconScale_))
        return;
    guildIconScale_ = scale;
    save();
    emit configChanged();
}

void AppearanceConfig::setChannelScale(float scale)
{
    scale = clampChannelScale(scale);
    if (qFuzzyCompare(scale, channelScale_))
        return;
    channelScale_ = scale;
    save();
    emit configChanged();
}

void AppearanceConfig::setNumberedUnread(bool on)
{
    if (on == numberedUnread_)
        return;
    numberedUnread_ = on;
    save();
    emit configChanged();
}

void AppearanceConfig::setCustomTitleBar(bool on)
{
    if (on == customTitleBar_)
        return;
    customTitleBar_ = on;
    save();
    emit configChanged();
}

} // namespace Appearance
} // namespace Core
} // namespace Acheron
