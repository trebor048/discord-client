#pragma once

#include <QObject>

namespace Acheron {
namespace Core {
namespace Appearance {

enum class MemberListMode {
    ResizeHandle,
    SlideOut,
};

/// Layout/scaling preferences for the Appearance settings page: the member
/// list mode (resizable splitter pane vs. slide-out overlay) and three
/// independent scale factors (member cards, guild icons, channel list).
/// Persisted in QSettings under `ui/`; emits configChanged() for live apply.
class AppearanceConfig : public QObject
{
    Q_OBJECT
public:
    static AppearanceConfig &instance();

    MemberListMode memberListMode() const { return memberListMode_; }
    float memberCardScale() const { return memberCardScale_; }
    float guildIconScale() const { return guildIconScale_; }
    float channelScale() const { return channelScale_; }
    /// Numbered unread badges on channels and tabs; off keeps the plain
    /// (non-numbered) unread indicator.
    bool numberedUnread() const { return numberedUnread_; }

    void setMemberListMode(MemberListMode mode);
    void setMemberCardScale(float scale);
    void setGuildIconScale(float scale);
    void setChannelScale(float scale);
    void setNumberedUnread(bool on);

    static constexpr float kMinScale = 0.80f;
    static constexpr float kMaxScale = 1.50f;
    static constexpr float kDefaultScale = 1.0f;
    static constexpr float kStep = 0.05f;

    // The channel list axis gets its own wider range and a smaller default so
    // rows can be scaled much further down (and up) than member/guild icons.
    static constexpr float kChannelMinScale = 0.50f;
    static constexpr float kChannelMaxScale = 2.00f;
    static constexpr float kChannelDefaultScale = 0.85f;

    static float clampScale(float value);
    static float clampChannelScale(float value);
    static float stepScale(float value, int steps);
    static int scaledInt(int base, float scale);
    /// Scale honoring the channel list's wider [kChannelMinScale, kChannelMaxScale]
    /// range. Channel call sites must use this so values above 1.50 (the
    /// member/guild cap) actually take effect.
    static int channelScaledInt(int base, float scale);

    static constexpr const char *kMemberListModeKey = "ui/memberListMode";
    static constexpr const char *kMemberCardScaleKey = "ui/memberCardScale";
    static constexpr const char *kGuildIconScaleKey = "ui/guildIconScale";
    static constexpr const char *kChannelScaleKey = "ui/channelScale";
    static constexpr const char *kNumberedUnreadKey = "ui/numberedUnread";

    // Public so tests can construct independent instances; app code uses instance().
    AppearanceConfig();

signals:
    void configChanged();
    void memberListModeChanged(MemberListMode mode);

private:
    void load();
    void save() const;

    MemberListMode memberListMode_ = MemberListMode::ResizeHandle;
    float memberCardScale_ = kDefaultScale;
    float guildIconScale_ = kDefaultScale;
    float channelScale_ = kDefaultScale;
    bool numberedUnread_ = true;
};

} // namespace Appearance
} // namespace Core
} // namespace Acheron
