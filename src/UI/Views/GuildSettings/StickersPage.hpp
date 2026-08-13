#pragma once

#include "UI/Dialogs/GuildSettingsPage.hpp"

namespace Acheron {
namespace UI {
class StickerListWidget;

namespace Views {

class StickersPage : public GuildSettingsPage
{
    Q_OBJECT
public:
    explicit StickersPage(Core::ClientInstance *instance, Core::Snowflake guildId,
                          QWidget *parent = nullptr)
        : GuildSettingsPage(instance, guildId, parent)
    {}

    void load() override;

private:
    void setupUi();
    StickerListWidget *m_stickerWidget = nullptr;
    bool m_loaded = false;
};

} // namespace Views
} // namespace UI
} // namespace Acheron
