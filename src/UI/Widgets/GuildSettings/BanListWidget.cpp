#include "BanListWidget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

#include "Core/ClientInstance.hpp"
#include "Discord/Client.hpp"

namespace Acheron {
namespace UI {
namespace Widgets {

using Core::Snowflake;

BanListWidget::BanListWidget(Core::ClientInstance *instance, Core::Snowflake guildId,
                             QWidget *parent)
    : GuildSettingsPage(instance, guildId, parent)
{
    setupUi();

    auto *client = m_instance->discord();
    connect(client, &Discord::Client::guildBanAdded, this, &BanListWidget::onBanAdded);
    connect(client, &Discord::Client::guildBanRemoved, this, &BanListWidget::onBanRemoved);
    connect(client, &Discord::Client::guildBansFetched, this, &BanListWidget::onBansFetched);
}

void BanListWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("Bans"), this);
    title->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold;"));
    layout->addWidget(title);

    m_banList = new QListWidget(this);
    m_banList->setAlternatingRowColors(true);
    m_banList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_banList, 1);

    auto *btnLayout = new QHBoxLayout();
    m_unbanButton = new QPushButton(QStringLiteral("Revoke Ban"), this);
    m_unbanButton->setEnabled(false);
    m_refreshButton = new QPushButton(QStringLiteral("Refresh"), this);
    btnLayout->addStretch();
    btnLayout->addWidget(m_unbanButton);
    btnLayout->addWidget(m_refreshButton);
    layout->addLayout(btnLayout);

    connect(m_banList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *current) {
                m_unbanButton->setEnabled(current != nullptr);
            });
    connect(m_unbanButton, &QPushButton::clicked, this, &BanListWidget::onUnbanClicked);
    connect(m_refreshButton, &QPushButton::clicked, this, &BanListWidget::load);
}

void BanListWidget::load()
{
    m_banList->clear();

    auto *item = new QListWidgetItem(m_banList);
    item->setText(QStringLiteral("Loading bans..."));
    item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);

    m_instance->discord()->fetchGuildBans(m_guildId, [](const auto &) {});
}

void BanListWidget::onBansFetched(Core::Snowflake guildId, const QList<Discord::BanEntry> &bans)
{
    if (guildId != m_guildId)
        return;

    m_bans = bans;
    populateList(bans);
}

void BanListWidget::populateList(const QList<Discord::BanEntry> &bans)
{
    m_banList->clear();

    for (const auto &ban : bans) {
        const auto &user = ban.user.get();
        const QString displayName = user.getDisplayName();
        const QString reason = ban.reason.getOr(QStringLiteral("No reason provided"));
        const QString userIdStr = QString::number(user.id.get());

        auto *item = new QListWidgetItem(m_banList);
        item->setText(QStringLiteral("%1 (%2)\n   Reason: %3")
                          .arg(displayName, userIdStr, reason));
        item->setToolTip(QStringLiteral("User ID: %1\nReason: %2").arg(userIdStr, reason));
        item->setData(Qt::UserRole, static_cast<quint64>(user.id.get()));
    }

    if (m_banList->count() == 0) {
        auto *item = new QListWidgetItem(m_banList);
        item->setText(QStringLiteral("No banned members."));
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
    }
}

void BanListWidget::onUnbanClicked()
{
    auto *current = m_banList->currentItem();
    if (!current)
        return;

    const quint64 userId = current->data(Qt::UserRole).toULongLong();
    m_instance->discord()->unbanMember(m_guildId, Snowflake(userId));
    emit statusMessage(QStringLiteral("Unbanning user..."));
}

void BanListWidget::onBanAdded(const Discord::GuildBan &event)
{
    if (event.guildId.get() != m_guildId)
        return;

    // Re-fetch the ban list to get the reason
    load();
}

void BanListWidget::onBanRemoved(const Discord::GuildBan &event)
{
    if (event.guildId.get() != m_guildId)
        return;

    const quint64 userId = static_cast<quint64>(event.user.get().id.get());

    // Remove from local list
    for (int i = 0; i < m_banList->count(); ++i) {
        auto *item = m_banList->item(i);
        if (item->data(Qt::UserRole).toULongLong() == userId) {
            delete m_banList->takeItem(i);
            break;
        }
    }

    // Also update m_bans
    m_bans.removeIf(
        [userId](const Discord::BanEntry &b) { return static_cast<quint64>(b.user.get().id.get()) == userId; });

    if (m_banList->count() == 0) {
        auto *item = new QListWidgetItem(m_banList);
        item->setText(QStringLiteral("No banned members."));
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
    }
}

} // namespace Widgets
} // namespace UI
} // namespace Acheron
