#include "ContextMenuFactory.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QWidgetAction>
#include <QVBoxLayout>

#include <optional>

#include "MainWindow.hpp"
#include "Core/Session.hpp"
#include "Core/ClientInstance.hpp"
#include "Core/UserManager.hpp"
#include "Core/ImageManager.hpp"
#include "Core/Settings.hpp"
#include "Core/Notification/NotificationManager.hpp"
#include "Input/MessageInput.hpp"
#include "Dialogs/UserProfilePopup.hpp"
#include "Dialogs/ModerateMemberDialog.hpp"
#include "Discord/CdnUrls.hpp"
#include "Discord/Client.hpp"

using namespace Acheron::Core;

namespace Acheron {
namespace UI {

namespace {

QWidget *buildUserMenuHeader(QMenu *parent, Core::Session *session, Snowflake userId,
                             const QString &displayName, const QString &username,
                             const QString &avatarHash)
{
    auto *header = new QWidget(parent);
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(10, 10, 16, 10);
    layout->setSpacing(12);

    constexpr QSize avatarSize(64, 64);
    auto *avatar = new QLabel(header);
    avatar->setFixedSize(avatarSize);
    if (!avatarHash.isEmpty()) {
        QUrl url = Discord::Cdn::userAvatar(userId, avatarHash, 128);
        session->getImageManager()->assign(avatar, url, avatarSize);
    } else {
        avatar->setPixmap(session->getImageManager()->placeholder(avatarSize));
    }
    layout->addWidget(avatar);

    auto *textLayout = new QVBoxLayout;
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    auto *displayLabel = new QLabel(displayName, header);
    QFont displayFont = displayLabel->font();
    displayFont.setBold(true);
    displayFont.setPointSize(displayFont.pointSize() + 2);
    displayLabel->setFont(displayFont);
    textLayout->addWidget(displayLabel);

    if (!username.isEmpty() && username != displayName) {
        auto *usernameLabel = new QLabel(username, header);
        usernameLabel->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
        textLayout->addWidget(usernameLabel);
    }
    textLayout->addStretch();
    layout->addLayout(textLayout, 1);

    return header;
}

QWidget *buildRoleChip(const Discord::Role &role, QWidget *parent)
{
    auto *label = new QLabel(role.name.get(), parent);
    label->setContentsMargins(24, 4, 24, 4);
    if (role.hasColor())
        label->setStyleSheet(QStringLiteral("color: %1;").arg(role.getColor().name()));
    return label;
}

} // namespace

ContextMenuFactory::ContextMenuFactory(MainWindow *window)
    : QObject(window), m_window(window)
{
}

void ContextMenuFactory::showUserContextMenu(Snowflake userId, Snowflake guildId, QPoint globalPos)
{
    QMenu menu(m_window);

    if (m_window->currentInstance) {
        auto user = m_window->currentInstance->users()->getUser(userId);
        QString displayName = m_window->currentInstance->users()->getDisplayName(userId, guildId);
        QString username = (user && user->username.hasValue()) ? user->username.get() : QString();
        QString avatarHash = (user && user->avatar.hasValue()) ? user->avatar.get() : QString();

        auto *header = buildUserMenuHeader(&menu, m_window->session, userId, displayName, username,
                                           avatarHash);
        auto *headerAction = new QWidgetAction(&menu);
        headerAction->setDefaultWidget(header);
        menu.addAction(headerAction);
        menu.addSeparator();
    }

    QAction *profileAction = menu.addAction(tr("Profile"));
    connect(profileAction, &QAction::triggered, m_window, [this, userId, guildId]() {
        (new UserProfilePopup(m_window->session->getImageManager(), m_window->currentInstance, userId,
                              guildId, m_window))
                ->show();
    });

    QAction *mentionAction = menu.addAction(tr("Mention"));
    connect(mentionAction, &QAction::triggered, m_window, [this, userId]() {
        m_window->messageInput->insertText(QStringLiteral("<@%1>").arg(quint64(userId)));
    });

    if (Core::isDeveloperModeEnabled()) {
        QAction *copyIdAction = menu.addAction(tr("Copy ID"));
        connect(copyIdAction, &QAction::triggered, m_window, [userId]() {
            QGuiApplication::clipboard()->setText(QString::number(static_cast<qulonglong>(userId)));
        });
    }

    QAction *openDmAction = menu.addAction(tr("Open DM"));
    std::optional<Snowflake> dmChannelId;
    if (m_window->currentInstance)
        dmChannelId = m_window->currentInstance->findDmChannelWithUser(userId);
    if (dmChannelId.has_value()) {
        connect(openDmAction, &QAction::triggered, m_window,
                [this, channelId = *dmChannelId]() { m_window->selectChannelInTree(channelId); });
    } else {
        openDmAction->setEnabled(false);
    }

    // "Listen to toasts" — add/remove this user from the notification listen list.
    if (m_window->notificationController) {
        if (auto *notif = m_window->notificationController->manager()) {
            const QString userIdStr = QString::number(static_cast<quint64>(userId));
            const bool listened = notif->notifyForList().contains(userIdStr);
            menu.addSeparator();
            QAction *listenAction = menu.addAction(listened ? tr("Stop listening to toasts")
                                                            : tr("Listen to toasts"));
            // The manager is deleteLater'd on instance teardown; guard with
            // QPointer in case the modal menu's nested loop outlives it.
            const QPointer<Core::NotificationManager> notifGuard(notif);
            connect(listenAction, &QAction::triggered, m_window,
                    [notifGuard, userIdStr, listened]() {
                        if (notifGuard.isNull())
                            return;
                        if (listened)
                            notifGuard->removeFromNotifyList(userIdStr);
                        else
                            notifGuard->addToNotifyList(userIdStr);
                    });
        }
    }

    if (guildId.isValid() && m_window->currentInstance) {
        // KICK/BAN/MUTE/DEAFEN are guild-level (base) permissions; evaluate them
        // without the current channel's overwrites which could wrongly strip them.
        const bool canKick = m_window->currentInstance->hasGuildPermission(
                guildId, Discord::Permission::KICK_MEMBERS);
        const bool canBan = m_window->currentInstance->hasGuildPermission(
                guildId, Discord::Permission::BAN_MEMBERS);
        const bool canTimeout = m_window->currentInstance->hasGuildPermission(
                guildId, Discord::Permission::MODERATE_MEMBERS);

        // Role hierarchy / self guard: a non-owner can't moderate themselves,
        // the guild owner, or anyone with a role position equal or higher.
        bool canModerateTarget = true;
        const Snowflake selfId = m_window->currentInstance->accountId();
        if (userId == selfId) {
            canModerateTarget = false;
        } else {
            auto selfGuild = m_window->currentInstance->getGuild(guildId);
            if (selfGuild && selfGuild->ownerId.get() == userId) {
                canModerateTarget = false;
            } else if (selfGuild && selfGuild->ownerId.get() != selfId) {
                const auto selfRoles = m_window->currentInstance->getMemberRolesSorted(guildId, selfId);
                const auto targetRoles = m_window->currentInstance->getMemberRolesSorted(guildId, userId);
                const int selfTop = selfRoles.isEmpty() ? 0 : selfRoles.first().position.get();
                const int targetTop = targetRoles.isEmpty() ? 0 : targetRoles.first().position.get();
                if (targetTop >= selfTop)
                    canModerateTarget = false;
            }
        }

        if (canKick || canBan || canTimeout) {
            // Capture the instance at menu-build time; `currentInstance` can
            // become dangling while the modal dialog's nested event loop runs
            // (e.g. account disconnect), so re-check before every deref.
            auto *instanceAtBuild = m_window->currentInstance;
            auto runModeration = [this, instanceAtBuild, userId, guildId](ModerateMemberDialog::Action initial) {
                if (!m_window->currentInstance || m_window->currentInstance != instanceAtBuild)
                    return;
                const QString name = m_window->currentInstance->users()->getDisplayName(userId, guildId);
                ModerateMemberDialog dialog(name, initial, m_window);
                if (dialog.exec() != QDialog::Accepted)
                    return;
                if (!m_window->currentInstance || m_window->currentInstance != instanceAtBuild)
                    return;
                const auto r = dialog.result();
                auto *client = m_window->currentInstance->discord();
                switch (r.action) {
                case ModerateMemberDialog::Action::Kick:
                    client->kickMember(guildId, userId, r.reason);
                    break;
                case ModerateMemberDialog::Action::Ban:
                    client->banMember(guildId, userId, r.deleteMessageSeconds, r.reason);
                    break;
                case ModerateMemberDialog::Action::TempBan:
                    client->tempBanMember(guildId, userId, r.deleteMessageSeconds, r.reason,
                                          r.durationSeconds);
                    break;
                case ModerateMemberDialog::Action::Timeout:
                    client->setMemberTimeout(guildId, userId, r.durationSeconds, r.reason);
                    break;
                }
            };

            QMenu *moderationMenu = menu.addMenu(tr("Moderation"));
            if (canKick) {
                QAction *kickAction = moderationMenu->addAction(tr("Kick"));
                kickAction->setEnabled(canModerateTarget);
                connect(kickAction, &QAction::triggered, m_window,
                        [runModeration]() { runModeration(ModerateMemberDialog::Action::Kick); });
            }
            if (canBan) {
                QAction *banAction = moderationMenu->addAction(tr("Ban"));
                banAction->setEnabled(canModerateTarget);
                connect(banAction, &QAction::triggered, m_window,
                        [runModeration]() { runModeration(ModerateMemberDialog::Action::Ban); });
            }
            if (canTimeout) {
                QAction *timeoutAction = moderationMenu->addAction(tr("Timeout"));
                timeoutAction->setEnabled(canModerateTarget);
                connect(timeoutAction, &QAction::triggered, m_window,
                        [runModeration]() { runModeration(ModerateMemberDialog::Action::Timeout); });
            }
        }

        const bool canMute = m_window->currentInstance->hasGuildPermission(
                guildId, Discord::Permission::MUTE_MEMBERS);
        const bool canDeafen = m_window->currentInstance->hasGuildPermission(
                guildId, Discord::Permission::DEAFEN_MEMBERS);

        if (canMute || canDeafen) {
            QMenu *voiceMenu = menu.addMenu(tr("Voice"));
            // Resolve the client at trigger time (menu.exec runs a nested event
            // loop during which the account may be torn down); re-check before
            // each call.
            auto *instanceAtBuild = m_window->currentInstance;
            auto callClient = [this, instanceAtBuild](auto action) {
                if (!m_window->currentInstance || m_window->currentInstance != instanceAtBuild)
                    return;
                if (auto *client = m_window->currentInstance->discord())
                    action(client);
            };
            if (canMute) {
                QAction *muteAction = voiceMenu->addAction(tr("Mute"));
                connect(muteAction, &QAction::triggered, m_window,
                        [callClient, guildId, userId]() {
                            callClient([guildId, userId](Discord::Client *c) {
                                c->setMemberMute(guildId, userId, true);
                            });
                        });
                QAction *unmuteAction = voiceMenu->addAction(tr("Unmute"));
                connect(unmuteAction, &QAction::triggered, m_window,
                        [callClient, guildId, userId]() {
                            callClient([guildId, userId](Discord::Client *c) {
                                c->setMemberMute(guildId, userId, false);
                            });
                        });
            }
            if (canDeafen) {
                QAction *deafenAction = voiceMenu->addAction(tr("Deafen"));
                connect(deafenAction, &QAction::triggered, m_window,
                        [callClient, guildId, userId]() {
                            callClient([guildId, userId](Discord::Client *c) {
                                c->setMemberDeaf(guildId, userId, true);
                            });
                        });
                QAction *undeafenAction = voiceMenu->addAction(tr("Undeafen"));
                connect(undeafenAction, &QAction::triggered, m_window,
                        [callClient, guildId, userId]() {
                            callClient([guildId, userId](Discord::Client *c) {
                                c->setMemberDeaf(guildId, userId, false);
                            });
                        });
            }
        }

        QMenu *rolesMenu = menu.addMenu(tr("Roles"));
        const auto memberRoles = m_window->currentInstance->getMemberRolesSorted(guildId, userId);
        if (memberRoles.isEmpty()) {
            rolesMenu->addAction(tr("No roles"))->setEnabled(false);
        } else {
            for (const auto &role : memberRoles) {
                auto *action = new QWidgetAction(rolesMenu);
                action->setDefaultWidget(buildRoleChip(role, rolesMenu));
                rolesMenu->addAction(action);
            }
        }
    }

    menu.exec(globalPos);
}

} // namespace UI
} // namespace Acheron
