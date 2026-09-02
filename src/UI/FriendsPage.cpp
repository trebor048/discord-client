#include "FriendsPage.hpp"

#include "Core/AnimationUtils.hpp"
#include "Core/ClientInstance.hpp"
#include "Core/RelationshipManager.hpp"
#include "Core/UserManager.hpp"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

using Core::Snowflake;

FriendsPage::FriendsPage(Core::ClientInstance *instance, QWidget *parent)
    : QDialog(parent), instance(instance)
{
    relationships = instance->relationships();
    users = instance->users();

    setWindowTitle(tr("Friends"));
    setMinimumSize(480, 520);
    resize(520, 580);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(8);

    // Add friend row
    auto *addGroup = new QGroupBox(tr("Add Friend"), this);
    auto *addLayout = new QHBoxLayout(addGroup);

    usernameEdit = new QLineEdit(this);
    usernameEdit->setPlaceholderText(tr("Username"));
    addLayout->addWidget(usernameEdit, 1);

    discriminatorEdit = new QLineEdit(this);
    discriminatorEdit->setPlaceholderText(tr("Tag"));
    discriminatorEdit->setFixedWidth(60);
    addLayout->addWidget(discriminatorEdit);

    addFriendButton = new QPushButton(tr("Send Request"), this);
    addLayout->addWidget(addFriendButton);

    outer->addWidget(addGroup);

    connect(addFriendButton, &QPushButton::clicked, this, &FriendsPage::onAddFriendClicked);

    // Tabs
    tabs = new QTabWidget(this);
    outer->addWidget(tabs);

    // Friend list
    friendList = new QListWidget(this);
    tabs->addTab(friendList, tr("Online"));

    auto *allTab = new QListWidget(this);
    tabs->addTab(allTab, tr("All"));

    auto *pendingTab = new QListWidget(this);
    tabs->addTab(pendingTab, tr("Pending"));

    auto *blockedTab = new QListWidget(this);
    tabs->addTab(blockedTab, tr("Blocked"));

    connect(tabs, &QTabWidget::currentChanged, this, &FriendsPage::onTabChanged);

    // Every tab needs the row interactions — previously only the Online tab
    // had them, leaving All/Pending/Blocked rows unclickable (no profile, no
    // DM).
    for (QListWidget *list : { friendList, allTab, pendingTab, blockedTab }) {
        connect(list, &QListWidget::itemClicked, this, &FriendsPage::onItemClicked);
        connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
            Snowflake userId = item->data(Qt::UserRole).toULongLong();
            if (userId.isValid())
                openConversation(userId);
        });
    }

    connect(relationships, &Core::RelationshipManager::relationshipChanged,
            this, &FriendsPage::onRelationshipChanged);

    refresh();
}

void FriendsPage::refresh()
{
    rebuildList();
}

void FriendsPage::onTabChanged(int)
{
    rebuildList();
    if (auto *list = qobject_cast<QWidget *>(tabs->currentWidget()))
        Acheron::Core::AnimationUtils::fadeIn(list, 150);
}

void FriendsPage::onRelationshipChanged(Core::Snowflake userId)
{
    // Coalesce bursts of relationship changes (e.g. a batch arriving in one
    // event-loop pass) into a single deferred rebuild: the previous handler
    // destroyed and recreated every widget on each individual change.
    changedUserIds.insert(userId);
    scheduleRebuild();
}

void FriendsPage::scheduleRebuild()
{
    if (!rebuildTimer) {
        rebuildTimer = new QTimer(this);
        rebuildTimer->setSingleShot(true);
        rebuildTimer->setInterval(0);
        connect(rebuildTimer, &QTimer::timeout,
                this, &FriendsPage::applyPendingRelationshipChanges);
    }
    rebuildTimer->start();
}

void FriendsPage::applyPendingRelationshipChanges()
{
    const QSet<Core::Snowflake> pending = changedUserIds;
    changedUserIds.clear();

    QListWidget *list = qobject_cast<QListWidget *>(tabs->currentWidget());
    if (!list) {
        rebuildList();
        return;
    }

    const Tab tab = static_cast<Tab>(tabs->currentIndex());
    bool needsFullRebuild = false;
    for (const Core::Snowflake &userId : pending) {
        if (!updateRowInPlace(list, tab, userId)) {
            needsFullRebuild = true;
            break;
        }
    }

    if (needsFullRebuild)
        rebuildList();
}

bool FriendsPage::updateRowInPlace(QListWidget *list, Tab tab, Core::Snowflake userId)
{
    QListWidgetItem *row = nullptr;
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem *candidate = list->item(i);
        if (candidate && Core::Snowflake(candidate->data(Qt::UserRole).toULongLong()) == userId) {
            row = candidate;
            break;
        }
    }

    const auto relOpt = relationships->getRelationship(userId);
    if (!relOpt)
        return false; // relationship removed -> the row (if present) must go away

    const Discord::Relationship &rel = *relOpt;
    if (!relationshipMatchesTab(tab, rel))
        return !row; // the row must leave this tab (or was never in it): rebuild only if visible here

    if (!row)
        return false; // belongs in this tab but is not present (fresh add) -> rebuild

    // Same tab, same type: only the display text can have changed. Updating it
    // in place keeps the widget (and any scroll/selection state) intact.
    const QString label = labelFor(rel);
    row->setText(label);
    row->setToolTip(label);
    return true;
}

bool FriendsPage::relationshipMatchesTab(Tab tab, const Discord::Relationship &rel) const
{
    switch (tab) {
    case Tab::Online:
        // "Online" means friends with a non-offline presence; friends with no
        // cached presence (or an explicit offline status) belong to the All
        // tab instead. Previously every friend appeared here regardless of
        // presence.
        if (rel.type.get() != Discord::RelationshipType::FRIEND)
            return false;
        if (!instance)
            return true;
        {
            const auto presence = instance->presence(rel.id.get());
            return presence.has_value()
                    && presence->status != QStringLiteral("offline");
        }
    case Tab::All:
        return rel.type.get() == Discord::RelationshipType::FRIEND;
    case Tab::Pending:
        return rel.type.get() == Discord::RelationshipType::INCOMING_REQUEST ||
               rel.type.get() == Discord::RelationshipType::OUTGOING_REQUEST;
    case Tab::Blocked:
        return rel.type.get() == Discord::RelationshipType::BLOCKED;
    }
    return false;
}

QString FriendsPage::labelFor(const Discord::Relationship &rel) const
{
    QString label;
    if (rel.user.hasValue()) {
        label = rel.user->getDisplayName();
    } else {
        label = QString::number(rel.id.get());
    }

    const QString status = statusText(rel);
    if (!status.isEmpty())
        label += QStringLiteral(" — %1").arg(status);
    return label;
}

void FriendsPage::onAddFriendClicked()
{
    QString username = usernameEdit->text().trimmed();
    QString tag = discriminatorEdit->text().trimmed();
    if (username.isEmpty())
        return;

    instance->discord()->sendFriendRequest(username, tag);
    usernameEdit->clear();
    discriminatorEdit->clear();
}

void FriendsPage::onItemClicked(QListWidgetItem *item)
{
    if (!item)
        return;

    Snowflake userId(item->data(Qt::UserRole).toULongLong());
    if (userId.isValid())
        emit openUserProfileRequested(userId);
}

QString FriendsPage::statusText(const Discord::Relationship &rel) const
{
    switch (rel.type.get()) {
    case Discord::RelationshipType::FRIEND:
        return tr("Friend");
    case Discord::RelationshipType::INCOMING_REQUEST:
        return tr("Incoming Friend Request");
    case Discord::RelationshipType::OUTGOING_REQUEST:
        return tr("Outgoing Friend Request");
    case Discord::RelationshipType::BLOCKED:
        return tr("Blocked");
    default:
        return {};
    }
}

void FriendsPage::rebuildList()
{
    Tab tab = static_cast<Tab>(tabs->currentIndex());
    QListWidget *list = qobject_cast<QListWidget *>(tabs->currentWidget());
    if (!list)
        return;

    list->clear();

    auto allRelationships = relationships->allRelationships();
    for (const auto &rel : allRelationships) {
        if (!relationshipMatchesTab(tab, rel))
            continue;

        const QString label = labelFor(rel);

        auto *item = new QListWidgetItem(label, list);
        item->setData(Qt::UserRole, static_cast<qulonglong>(rel.id.get()));
        item->setToolTip(label);
    }

    if (list->count() == 0) {
        auto *item = new QListWidgetItem(tr("(none)"), list);
        item->setFlags(Qt::NoItemFlags);
    }

    Core::AnimationUtils::fadeIn(list, 150);
}

void FriendsPage::openConversation(Core::Snowflake userId)
{
    emit openDmRequested(userId);
}

} // namespace UI
} // namespace Acheron
