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
    connect(friendList, &QListWidget::itemClicked, this, &FriendsPage::onItemClicked);
    connect(friendList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        Snowflake userId = item->data(Qt::UserRole).toULongLong();
        if (userId.isValid())
            openConversation(userId);
    });

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

void FriendsPage::onRelationshipChanged(Core::Snowflake)
{
    rebuildList();
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
        switch (tab) {
        case Tab::Online:
            // Simple heuristic: friends with valid user data
            if (rel.type.get() != Discord::RelationshipType::FRIEND)
                continue;
            break;
        case Tab::All:
            if (rel.type.get() != Discord::RelationshipType::FRIEND)
                continue;
            break;
        case Tab::Pending:
            if (rel.type.get() != Discord::RelationshipType::INCOMING_REQUEST &&
                rel.type.get() != Discord::RelationshipType::OUTGOING_REQUEST)
                continue;
            break;
        case Tab::Blocked:
            if (rel.type.get() != Discord::RelationshipType::BLOCKED)
                continue;
            break;
        }

        QString label;
        if (rel.user.hasValue()) {
            label = rel.user->getDisplayName();
        } else {
            label = QString::number(rel.id.get());
        }

        QString status = statusText(rel);
        if (!status.isEmpty())
            label += QStringLiteral(" — %1").arg(status);

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
