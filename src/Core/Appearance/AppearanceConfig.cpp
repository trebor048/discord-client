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
    channelScale_ = clampScale(settings.value(kChannelScaleKey, kDefaultScale).toFloat());
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
    settings.sync();
}

float AppearanceConfig::clampScale(float value)
{
    return std::clamp(value, kMinScale, kMaxScale);
}

float AppearanceConfig::stepScale(float value, int steps)
{
    return clampScale(value + static_cast<float>(steps) * kStep);
}

int AppearanceConfig::scaledInt(int base, float scale)
{
    return static_cast<int>(
            std::lround(static_cast<float>(base) * std::clamp(scale, kMinScale, kMaxScale)));
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
    scale = clampScale(scale);
    if (qFuzzyCompare(scale, channelScale_))
        return;
    channelScale_ = scale;
    save();
    emit configChanged();
}

} // namespace Appearance
} // namespace Core
} // namespace Acheron
