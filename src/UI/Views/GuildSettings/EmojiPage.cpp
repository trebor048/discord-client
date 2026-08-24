#include "EmojiPage.hpp"

#include "Core/EmojiCatalog.hpp"
#include "Core/PermissionComputer.hpp"
#include "Discord/Client.hpp"
#include "Discord/Enums.hpp"
#include <QVBoxLayout>

#include "Core/ClientInstance.hpp"
#include "UI/Widgets/GuildSettings/EmojiListWidget.hpp"

namespace Acheron {
namespace UI {
namespace Views {

using Core::Snowflake;

namespace {
QList<Discord::Emoji> emojisForGuild(Core::Snowflake guildId)
{
    QList<Discord::Emoji> emojis;
    const QString guildIdStr = QString::number(quint64(guildId));
    for (const auto &item : Core::EmojiCatalog::customEmojis()) {
        if (item.guildId != guildIdStr)
            continue;

        Discord::Emoji emoji;
        emoji.id = Core::Snowflake(item.customId.toULongLong());
        emoji.name = item.name;
        emoji.animated = item.animated;
        emoji.available = true;
        emoji.managed = false;
        emoji.requireColons = true;
        emojis.append(emoji);
    }
    return emojis;
}
} // namespace

EmojiPage::EmojiPage(Core::ClientInstance *instance, Core::Snowflake guildId, QWidget *parent)
    : GuildSettingsPage(instance, guildId, parent)
{
    setupUi();
    connect(m_instance, &Core::ClientInstance::customEmojisChanged, this, [this]() {
        if (m_emojiWidget)
            m_emojiWidget->setEmojis(emojisForGuild(m_guildId));
    }, Qt::UniqueConnection);
}

void EmojiPage::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_emojiWidget = new EmojiListWidget(this);
    m_emojiWidget->setGuildId(m_guildId);
    m_emojiWidget->setDiscordClient(m_instance->discord());
    m_emojiWidget->setHttpClient(m_instance->discord()->getHttpClient());
    layout->addWidget(m_emojiWidget, 1);
}

void EmojiPage::load()
{
    if (!m_emojiWidget)
        setupUi();

    // Check MANAGE_EXPRESSIONS permission — mirror StickersPage so Upload/Rename/Delete
    // are enabled for authorised users instead of permanently disabled.
    bool canManageExpressions = false;
    {
        const Snowflake myUserId = m_instance->discord()->getMe().id.get();
        auto guild = m_instance->getGuild(m_guildId);
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
    }
    m_emojiWidget->setEnabled(canManageExpressions);

    m_emojiWidget->setEmojis(emojisForGuild(m_guildId));
}

} // namespace Views
} // namespace UI
} // namespace Acheron
