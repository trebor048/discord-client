#include "StickersPage.hpp"

#include "Core/ClientInstance.hpp"
#include "Core/PermissionComputer.hpp"
#include "Discord/Client.hpp"
#include "Discord/Enums.hpp"
#include "UI/Widgets/GuildSettings/StickerListWidget.hpp"

#include <QVBoxLayout>

namespace Acheron {
namespace UI {
namespace Views {

using Core::Snowflake;

void StickersPage::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_stickerWidget = new StickerListWidget(this);
    m_stickerWidget->setGuildId(m_guildId);
    m_stickerWidget->setDiscordClient(m_instance->discord());
    m_stickerWidget->setHttpClient(m_instance->discord()->getHttpClient());

    layout->addWidget(m_stickerWidget);
}

void StickersPage::load()
{
    if (!m_stickerWidget)
        setupUi();

    if (m_loaded)
        return;

    m_loaded = true;

    // Check MANAGE_EXPRESSIONS permission
    const Snowflake myUserId = m_instance->discord()->getMe().id.get();
    auto guild = m_instance->getGuild(m_guildId);

    bool canManageExpressions = false;
    if (guild) {
        QList<Snowflake> memberRoleIds;
        auto roles = m_instance->users()->getMemberRoles(m_guildId, myUserId);
        if (roles)
            memberRoleIds = *roles;
        const auto guildRoles = m_instance->getRolesForGuild(m_guildId);
        Discord::Permissions perms =
            Core::PermissionComputer::computeBasePermissions(guild->ownerId.get(), myUserId,
                                                              m_guildId, memberRoleIds,
                                                              guildRoles);
        canManageExpressions = perms.testFlag(Discord::Permission::MANAGE_EXPRESSIONS) ||
                               perms.testFlag(Discord::Permission::ADMINISTRATOR);
    }

    m_stickerWidget->setEnabled(canManageExpressions);

    // Load current stickers
    const auto &allStickers = m_instance->guildStickers();
    if (allStickers.contains(m_guildId))
        m_stickerWidget->setStickers(allStickers[m_guildId]);
    else
        m_stickerWidget->setStickers({});

    // Listen for sticker store changes
    connect(m_instance, &Core::ClientInstance::stickerStoreChanged, this,
            [this](Core::Snowflake gid) {
                if (gid != m_guildId)
                    return;
                const auto &stickers = m_instance->guildStickers();
                if (stickers.contains(m_guildId))
                    m_stickerWidget->setStickers(stickers[m_guildId]);
                else
                    m_stickerWidget->setStickers({});
            });

    // Handle modifications (refresh from gateway)
    connect(m_stickerWidget, &StickerListWidget::stickerModified, this, [this]() {
        const auto &stickers = m_instance->guildStickers();
        if (stickers.contains(m_guildId))
            m_stickerWidget->setStickers(stickers[m_guildId]);
        else
            m_stickerWidget->setStickers({});
    });
}

} // namespace Views
} // namespace UI
} // namespace Acheron
