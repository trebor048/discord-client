#pragma once

#include "UI/Dialogs/GuildSettingsPage.hpp"

namespace Acheron {
namespace UI {
class EmojiListWidget;

namespace Views {

class EmojiPage : public GuildSettingsPage
{
    Q_OBJECT
public:
    explicit EmojiPage(Core::ClientInstance *instance, Core::Snowflake guildId,
                       QWidget *parent = nullptr);

    void load() override;

private:
    void setupUi();
    EmojiListWidget *m_emojiWidget = nullptr;
};

} // namespace Views
} // namespace UI
} // namespace Acheron
