#include "AuditLogWidget.hpp"

#include <QDateTime>
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

AuditLogWidget::AuditLogWidget(Core::ClientInstance *instance, Core::Snowflake guildId,
                               QWidget *parent)
    : GuildSettingsPage(instance, guildId, parent)
{
    setupUi();

    connect(m_instance->discord(), &Discord::Client::auditLogFetched, this,
            &AuditLogWidget::onLogFetched);
}

void AuditLogWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("Audit Log"), this);
    title->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold;"));
    layout->addWidget(title);

    // Filter bar
    auto *filterLayout = new QHBoxLayout();

    m_userFilterCombo = new QComboBox(this);
    m_userFilterCombo->addItem(QStringLiteral("All Users"), static_cast<qulonglong>(Snowflake::Invalid));
    m_userFilterCombo->setMinimumWidth(180);
    filterLayout->addWidget(new QLabel(QStringLiteral("User:"), this));
    filterLayout->addWidget(m_userFilterCombo);

    m_actionFilterCombo = new QComboBox(this);
    m_actionFilterCombo->addItem(QStringLiteral("All Actions"), static_cast<qulonglong>(Snowflake::Invalid));
    // Common action types
    const QList<QPair<int, QString>> actionTypes = {
        { 1, QStringLiteral("Guild Update") },
        { 10, QStringLiteral("Channel Create") },
        { 11, QStringLiteral("Channel Update") },
        { 12, QStringLiteral("Channel Delete") },
        { 20, QStringLiteral("Kick") },
        { 22, QStringLiteral("Ban") },
        { 23, QStringLiteral("Unban") },
        { 24, QStringLiteral("Member Update") },
        { 25, QStringLiteral("Member Role Update") },
        { 30, QStringLiteral("Role Create") },
        { 31, QStringLiteral("Role Update") },
        { 32, QStringLiteral("Role Delete") },
        { 40, QStringLiteral("Invite Create") },
        { 42, QStringLiteral("Invite Delete") },
        { 50, QStringLiteral("Emoji Create") },
        { 52, QStringLiteral("Emoji Delete") },
        { 60, QStringLiteral("Message Delete") },
        { 72, QStringLiteral("Message Bulk Delete") },
        { 80, QStringLiteral("Webhook Create") },
        { 81, QStringLiteral("Webhook Update") },
        { 82, QStringLiteral("Webhook Delete") },
    };
    for (const auto &at : actionTypes)
        m_actionFilterCombo->addItem(at.second, at.first);
    m_actionFilterCombo->setMinimumWidth(180);
    filterLayout->addWidget(new QLabel(QStringLiteral("Action:"), this));
    filterLayout->addWidget(m_actionFilterCombo);
    filterLayout->addStretch();

    layout->addLayout(filterLayout);

    m_logList = new QListWidget(this);
    m_logList->setAlternatingRowColors(true);
    m_logList->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_logList, 1);

    m_loadMoreButton = new QPushButton(QStringLiteral("Load More"), this);
    layout->addWidget(m_loadMoreButton);

    connect(m_loadMoreButton, &QPushButton::clicked, this, &AuditLogWidget::onLoadMore);
    connect(m_userFilterCombo, &QComboBox::currentIndexChanged, this,
            &AuditLogWidget::onFilterChanged);
    connect(m_actionFilterCombo, &QComboBox::currentIndexChanged, this,
            &AuditLogWidget::onFilterChanged);
}

void AuditLogWidget::load()
{
    m_logList->clear();
    m_lastEntryId = Snowflake::Invalid;
    m_userMap.clear();

    m_loading = true;
    m_loadMoreButton->setEnabled(false);

    Snowflake userId(m_userFilterCombo->currentData().toULongLong());
    if (!userId.isValid())
        userId = Snowflake::Invalid;

    Snowflake actionType(m_actionFilterCombo->currentData().toULongLong());
    if (!actionType.isValid())
        actionType = Snowflake::Invalid;

    m_instance->discord()->fetchGuildAuditLog(m_guildId, userId, actionType,
                                                Snowflake::Invalid, 50, [](const auto &) {});
}

void AuditLogWidget::onLogFetched(Core::Snowflake guildId, const Discord::AuditLogData &log)
{
    if (guildId != m_guildId)
        return;

    m_loading = false;

    // Build user map
    if (log.users.hasValue()) {
        for (const auto &user : log.users.get())
            m_userMap[user.id.get()] = user;
    }

    const auto &entries = log.auditLogEntries.getOr(QList<Discord::AuditLogEntry>{});
    appendEntries(entries, m_userMap);

    m_loadMoreButton->setEnabled(!entries.isEmpty());
}

void AuditLogWidget::appendEntries(const QList<Discord::AuditLogEntry> &entries,
                                   const QHash<Core::Snowflake, Discord::User> &userMap)
{
    if (entries.isEmpty()) {
        if (m_logList->count() == 0) {
            auto *item = new QListWidgetItem(m_logList);
            item->setText(QStringLiteral("No audit log entries."));
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        }
        return;
    }

    for (const auto &entry : entries) {
        const QString text = formatEntry(entry, userMap);
        auto *item = new QListWidgetItem(m_logList);
        item->setText(text);
        item->setToolTip(entry.reason.getOr(QString()));

        const Snowflake entryId = entry.id.get();
        if (entryId.isValid())
            m_lastEntryId = entryId;
    }
}

void AuditLogWidget::onLoadMore()
{
    if (m_loading)
        return;

    m_loading = true;
    m_loadMoreButton->setEnabled(false);

    Snowflake userId(m_userFilterCombo->currentData().toULongLong());
    if (!userId.isValid())
        userId = Snowflake::Invalid;

    Snowflake actionType(m_actionFilterCombo->currentData().toULongLong());
    if (!actionType.isValid())
        actionType = Snowflake::Invalid;

    m_instance->discord()->fetchGuildAuditLog(m_guildId, userId, actionType,
                                                m_lastEntryId, 50, [](const auto &) {});
}

void AuditLogWidget::onFilterChanged()
{
    load();
}

QString AuditLogWidget::actionTypeToString(int actionType)
{
    switch (actionType) {
        case 1:  return QStringLiteral("Guild Update");
        case 10: return QStringLiteral("Channel Create");
        case 11: return QStringLiteral("Channel Update");
        case 12: return QStringLiteral("Channel Delete");
        case 13: return QStringLiteral("Channel Permission Update");
        case 20: return QStringLiteral("Kick");
        case 21: return QStringLiteral("Prune");
        case 22: return QStringLiteral("Ban");
        case 23: return QStringLiteral("Unban");
        case 24: return QStringLiteral("Member Update");
        case 25: return QStringLiteral("Member Role Update");
        case 30: return QStringLiteral("Role Create");
        case 31: return QStringLiteral("Role Update");
        case 32: return QStringLiteral("Role Delete");
        case 40: return QStringLiteral("Invite Create");
        case 41: return QStringLiteral("Invite Update");
        case 42: return QStringLiteral("Invite Delete");
        case 50: return QStringLiteral("Emoji Create");
        case 51: return QStringLiteral("Emoji Update");
        case 52: return QStringLiteral("Emoji Delete");
        case 60: return QStringLiteral("Sticker Create");
        case 61: return QStringLiteral("Sticker Update");
        case 62: return QStringLiteral("Sticker Delete");
        case 72: return QStringLiteral("Message Bulk Delete");
        case 73: return QStringLiteral("Message Pin");
        case 74: return QStringLiteral("Message Unpin");
        case 80: return QStringLiteral("Webhook Create");
        case 81: return QStringLiteral("Webhook Update");
        case 82: return QStringLiteral("Webhook Delete");
        case 83: return QStringLiteral("Emoji Update");
        case 84: return QStringLiteral("Integration Create");
        case 85: return QStringLiteral("Integration Update");
        case 86: return QStringLiteral("Integration Delete");
        case 90: return QStringLiteral("Stage Create");
        case 91: return QStringLiteral("Stage Update");
        case 92: return QStringLiteral("Stage Delete");
        case 100: return QStringLiteral("Sticker Create");
        case 110: return QStringLiteral("Event Create");
        case 111: return QStringLiteral("Event Update");
        case 112: return QStringLiteral("Event Delete");
        case 140: return QStringLiteral("Thread Create");
        case 141: return QStringLiteral("Thread Update");
        case 142: return QStringLiteral("Thread Delete");
        case 150: return QStringLiteral("Application Command Permission Update");
        default: return QStringLiteral("Unknown (%1)").arg(actionType);
    }
}

QString AuditLogWidget::formatEntry(const Discord::AuditLogEntry &entry,
                                    const QHash<Core::Snowflake, Discord::User> &userMap)
{
    const Snowflake userId = entry.userId.getOr(Snowflake::Invalid);
    const QString userName = (userId.isValid() && userMap.contains(userId))
                                 ? userMap.value(userId).getDisplayName()
                                 : QStringLiteral("Unknown");

    const int actionType = entry.actionType.getOr(0);
    const QString actionStr = actionTypeToString(actionType);

    const Snowflake targetId = entry.targetId.getOr(Snowflake::Invalid);
    const QString targetStr = targetId.isValid() ? QString::number(targetId) : QStringLiteral("-");

    const Snowflake entryId = entry.id.get();
    const QString timeStr = entryId.isValid()
                                ? QLocale::system().toString(entryId.toDateTime(), QLocale::ShortFormat)
                                : QString();

    const QString reason = entry.reason.getOr(QString());

    return QStringLiteral("[%1] %2 performed %3 on target %4%5")
        .arg(timeStr, userName, actionStr, targetStr,
             reason.isEmpty() ? QString() : QStringLiteral(" — Reason: %1").arg(reason));
}

} // namespace Widgets
} // namespace UI
} // namespace Acheron
