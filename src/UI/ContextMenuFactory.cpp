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
#include "Input/MessageInput.hpp"
#include "Dialogs/UserProfilePopup.hpp"
#include "Discord/CdnUrls.hpp"

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

    QAction *copyIdAction = menu.addAction(tr("Copy ID"));
    connect(copyIdAction, &QAction::triggered, m_window, [userId]() {
        QGuiApplication::clipboard()->setText(QString::number(static_cast<qulonglong>(userId)));
    });

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

    if (guildId.isValid() && m_window->currentInstance) {
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
