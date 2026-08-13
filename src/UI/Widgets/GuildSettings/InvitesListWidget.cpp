#include "InvitesListWidget.hpp"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVariantMap>
#include <QVBoxLayout>

#include "Core/ClientInstance.hpp"
#include "Discord/Client.hpp"

namespace Acheron {
namespace UI {
namespace Widgets {

using Core::Snowflake;

InvitesListWidget::InvitesListWidget(Core::ClientInstance *instance, Core::Snowflake guildId,
                                     QWidget *parent)
    : GuildSettingsPage(instance, guildId, parent)
{
    setupUi();

    auto *client = m_instance->discord();
    connect(client, &Discord::Client::inviteCreated, this, &InvitesListWidget::onInviteCreated);
    connect(client, &Discord::Client::inviteDeleted, this, &InvitesListWidget::onInviteDeleted);
    connect(client, &Discord::Client::guildInvitesFetched, this, &InvitesListWidget::onInvitesFetched);
}

void InvitesListWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("Invites"), this);
    title->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold;"));
    layout->addWidget(title);

    m_inviteList = new QListWidget(this);
    m_inviteList->setAlternatingRowColors(true);
    m_inviteList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_inviteList, 1);

    auto *btnLayout = new QHBoxLayout();
    m_revokeButton = new QPushButton(QStringLiteral("Revoke"), this);
    m_revokeButton->setEnabled(false);
    m_refreshButton = new QPushButton(QStringLiteral("Refresh"), this);
    btnLayout->addStretch();
    btnLayout->addWidget(m_revokeButton);
    btnLayout->addWidget(m_refreshButton);
    layout->addLayout(btnLayout);

    connect(m_inviteList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *current) {
                m_revokeButton->setEnabled(current != nullptr);
            });
    connect(m_revokeButton, &QPushButton::clicked, this, &InvitesListWidget::onRevokeClicked);
    connect(m_refreshButton, &QPushButton::clicked, this, &InvitesListWidget::load);
}

void InvitesListWidget::load()
{
    m_inviteList->clear();
    auto *item = new QListWidgetItem(m_inviteList);
    item->setText(QStringLiteral("Loading invites..."));
    item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);

    m_instance->discord()->fetchGuildInvites(m_guildId, [](const auto &) {});
}

void InvitesListWidget::onInvitesFetched(Core::Snowflake guildId,
                                         const QList<Discord::InviteData> &invites)
{
    if (guildId != m_guildId)
        return;
    m_invites = invites;
    populateList(invites);
}

void InvitesListWidget::populateList(const QList<Discord::InviteData> &invites)
{
    m_inviteList->clear();

    for (const auto &invite : invites) {
        const QString code = invite.code.getOr(QString());
        const QString inviterName = invite.inviter.hasValue() ? invite.inviter.get().getDisplayName()
                                                              : QStringLiteral("Unknown");
        const int uses = invite.uses.getOr(0);
        const int maxUses = invite.maxUses.getOr(0);
        const QString usesStr = maxUses > 0 ? QStringLiteral("%1/%2").arg(uses).arg(maxUses)
                                            : QStringLiteral("%1").arg(uses);

        QString expiry = QStringLiteral("Never");
        if (invite.createdAt.hasValue() && invite.maxAge.getOr(0) > 0) {
            QDateTime expiryTime = invite.createdAt.get().addSecs(invite.maxAge.get());
            if (expiryTime < QDateTime::currentDateTime())
                expiry = QStringLiteral("Expired");
            else
                expiry = QStringLiteral("Expires ") + QLocale::system().toString(expiryTime, QLocale::ShortFormat);
        }

        auto *item = new QListWidgetItem(m_inviteList);
        item->setText(QStringLiteral("%1 — Inviter: %2 — Uses: %3 — %4")
                          .arg(code, inviterName, usesStr, expiry));

        // Store code + channelId for revocation
        QVariantMap data;
        data["code"] = code;
        data["channelId"] = static_cast<quint64>(invite.channelId.getOr(Snowflake::Invalid));
        item->setData(Qt::UserRole, data);
    }

    if (m_inviteList->count() == 0) {
        auto *item = new QListWidgetItem(m_inviteList);
        item->setText(QStringLiteral("No active invites."));
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
    }
}

void InvitesListWidget::onRevokeClicked()
{
    auto *current = m_inviteList->currentItem();
    if (!current)
        return;

    const QVariantMap data = current->data(Qt::UserRole).toMap();
    const QString code = data["code"].toString();
    const Snowflake channelId(data["channelId"].toULongLong());

    m_instance->discord()->revokeInvite(channelId, code);
    emit statusMessage(QStringLiteral("Revoking invite..."));
}

void InvitesListWidget::onInviteCreated(const Discord::InviteCreate &event)
{
    if (event.guildId.getOr(Snowflake::Invalid) != m_guildId)
        return;
    // Just re-fetch for simplicity
    load();
}

void InvitesListWidget::onInviteDeleted(const Discord::InviteDelete &event)
{
    if (event.guildId.getOr(Snowflake::Invalid) != m_guildId)
        return;

    const QString code = event.code.get();
    for (int i = 0; i < m_inviteList->count(); ++i) {
        auto *item = m_inviteList->item(i);
        const QVariantMap data = item->data(Qt::UserRole).toMap();
        if (data["code"].toString() == code) {
            delete m_inviteList->takeItem(i);
            break;
        }
    }

    if (m_inviteList->count() == 0) {
        auto *item = new QListWidgetItem(m_inviteList);
        item->setText(QStringLiteral("No active invites."));
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
    }
}

} // namespace Widgets
} // namespace UI
} // namespace Acheron
