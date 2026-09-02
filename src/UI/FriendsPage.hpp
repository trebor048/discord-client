#pragma once

#include <QDialog>
#include <QHash>
#include <QSet>
#include <QTimer>

#include "Core/Snowflake.hpp"
#include "Discord/Entities.hpp"

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTabWidget;
class QVBoxLayout;

namespace Acheron {
namespace Core {
class ClientInstance;
class RelationshipManager;
class UserManager;
} // namespace Core
namespace Discord {
struct User;
} // namespace Discord
namespace UI {

class FriendsPage : public QDialog
{
    Q_OBJECT
public:
    explicit FriendsPage(Core::ClientInstance *instance, QWidget *parent = nullptr);

    void refresh();

signals:
    void openDmRequested(Core::Snowflake userId);
    void openUserProfileRequested(Core::Snowflake userId);

private slots:
    void onTabChanged(int index);
    void onRelationshipChanged(Core::Snowflake userId);
    void onAddFriendClicked();
    void onItemClicked(QListWidgetItem *item);

private:
    enum class Tab {
        Online = 0,
        All = 1,
        Pending = 2,
        Blocked = 3,
    };

    void rebuildList();
    void scheduleRebuild();
    void applyPendingRelationshipChanges();
    bool updateRowInPlace(QListWidget *list, Tab tab, Core::Snowflake userId);
    bool relationshipMatchesTab(Tab tab, const Discord::Relationship &rel) const;
    QString labelFor(const Discord::Relationship &rel) const;
    QString statusText(const Discord::Relationship &rel) const;
    void addFriendRow(QVBoxLayout *layout, const Discord::Relationship &rel);
    void openConversation(Core::Snowflake userId);

    Core::ClientInstance *instance;
    Core::RelationshipManager *relationships;
    Core::UserManager *users;

    QTabWidget *tabs;
    QListWidget *friendList;
    QPushButton *addFriendButton;
    QLineEdit *usernameEdit;
    QLineEdit *discriminatorEdit;

    // Coalesces bursts of relationship changes into at most one rebuild per
    // event-loop pass; the pending user ids drive cheap in-place row updates
    // when the change is visible in the current tab.
    QTimer *rebuildTimer = nullptr;
    QSet<Core::Snowflake> changedUserIds;
};

} // namespace UI
} // namespace Acheron
