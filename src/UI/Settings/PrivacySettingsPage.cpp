#include "PrivacySettingsPage.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPointer>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "Core/Logging.hpp"
#include "Core/Result.hpp"
#include "Discord/Client.hpp"

namespace Acheron {
namespace UI {

namespace {

// Privacy settings are pushed to the account via PATCH /users/@me/privacy;
// surface failures instead of swallowing them.
void logPrivacyUpdateResult(const char *key, const Core::Result<QJsonObject> &result)
{
    if (!result.success())
        qCWarning(LogUI) << "Failed to update privacy setting" << key << ":" << result.error;
}

} // namespace

PrivacySettingsPage::PrivacySettingsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    // === Privacy section ===
    auto *privacyGroup = new QGroupBox(tr("Privacy"), this);
    auto *privacyLayout = new QFormLayout(privacyGroup);
    privacyLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    privacyLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);

    dmFilterCombo = new QComboBox(this);
    dmFilterCombo->addItem(tr("Everyone"), 0);
    dmFilterCombo->addItem(tr("Friends of Server Members"), 1);
    dmFilterCombo->addItem(tr("Friends Only"), 2);
    dmFilterCombo->setCurrentIndex(QSettings().value("privacy/dm_filter", 0).toInt());
    privacyLayout->addRow(tr("Who can send you direct messages"), dmFilterCombo);

    allowDmFromServerMembers = new QCheckBox(
        tr("Allow DMs from server members by default"), this);
    allowDmFromServerMembers->setChecked(
        QSettings().value("privacy/allow_dm_server_members", true).toBool());
    privacyLayout->addRow(allowDmFromServerMembers);

    friendAddCombo = new QComboBox(this);
    friendAddCombo->addItem(tr("Everyone"), 0);
    friendAddCombo->addItem(tr("Friends of Friends"), 1);
    friendAddCombo->addItem(tr("Server Members"), 2);
    friendAddCombo->addItem(tr("No one"), 3);
    friendAddCombo->setCurrentIndex(QSettings().value("privacy/friend_add_policy", 0).toInt());
    privacyLayout->addRow(tr("Who can add you as a friend"), friendAddCombo);

    layout->addWidget(privacyGroup);

    // === Friend request checkboxes ===
    auto *friendGroup = new QGroupBox(tr("Friend Requests"), this);
    auto *friendLayout = new QVBoxLayout(friendGroup);
    friendLayout->setSpacing(10);

    friendRequestFromEveryone = new QCheckBox(tr("Everyone"), this);
    friendRequestFromEveryone->setChecked(
        QSettings().value("privacy/friend_req_everyone", true).toBool());
    friendLayout->addWidget(friendRequestFromEveryone);

    friendRequestFromFriendsOfFriends = new QCheckBox(tr("Friends of Friends"), this);
    friendRequestFromFriendsOfFriends->setChecked(
        QSettings().value("privacy/friend_req_fof", true).toBool());
    friendLayout->addWidget(friendRequestFromFriendsOfFriends);

    friendRequestFromServerMembers = new QCheckBox(tr("Server Members"), this);
    friendRequestFromServerMembers->setChecked(
        QSettings().value("privacy/friend_req_server", true).toBool());
    friendLayout->addWidget(friendRequestFromServerMembers);

    // The checkboxes are mirrors of the single friend_add_policy combo value;
    // sync them to the stored policy so a fresh profile shows exactly one box
    // checked instead of the all-true defaults above.
    syncFriendRequestControls(QSettings().value("privacy/friend_add_policy", 0).toInt());

    layout->addWidget(friendGroup);

    // === Safety section ===
    auto *safetyGroup = new QGroupBox(tr("Safety"), this);
    auto *safetyLayout = new QFormLayout(safetyGroup);
    safetyLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    safetyLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);

    explicitImageFilterCombo = new QComboBox(this);
    explicitImageFilterCombo->addItem(tr("Keep Me Safe (scan all DMs)"), 0);
    explicitImageFilterCombo->addItem(tr("Moderate (scan DMs from non-friends)"), 1);
    explicitImageFilterCombo->addItem(tr("Off (do not scan)"), 2);
    explicitImageFilterCombo->setCurrentIndex(
        QSettings().value("privacy/explicit_filter", 1).toInt());
    safetyLayout->addRow(tr("Explicit Image Filter"), explicitImageFilterCombo);

    // The filter value is pushed to the account, but this client performs no
    // local image scanning — say so instead of implying local enforcement.
    auto *explicitFilterNote = new QLabel(
            tr("Sent to your account; Acheron does not scan images locally."), safetyGroup);
    explicitFilterNote->setWordWrap(true);
    explicitFilterNote->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 0.8em;"));
    safetyLayout->addRow(explicitFilterNote);

    layout->addWidget(safetyGroup);

    layout->addStretch();

    // Wire up local persistence
    connect(dmFilterCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        QSettings().setValue("privacy/dm_filter", index);
        if (client) {
            QJsonObject payload;
            payload["default_scope_of_dms"] = index;
            client->updatePrivacySettings(payload, [](const Core::Result<QJsonObject> &result) {
                logPrivacyUpdateResult("default_scope_of_dms", result);
            });
        }
        emit privacyChanged();
    });
    connect(allowDmFromServerMembers, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings().setValue("privacy/allow_dm_server_members", checked);
        if (client) {
            QJsonObject payload;
            payload["default_allow_dms_from_server_members"] = checked;
            client->updatePrivacySettings(payload, [](const Core::Result<QJsonObject> &result) {
                logPrivacyUpdateResult("default_allow_dms_from_server_members", result);
            });
        }
        emit privacyChanged();
    });
    connect(friendAddCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        QSettings().setValue("privacy/friend_add_policy", index);
        syncFriendRequestControls(index);
        if (client) {
            QJsonObject payload;
            payload["allow_friend_requests_from"] = index;
            client->updatePrivacySettings(payload, [](const Core::Result<QJsonObject> &result) {
                logPrivacyUpdateResult("allow_friend_requests_from", result);
            });
        }
        emit privacyChanged();
    });
    // The checkboxes edit the same allow_friend_requests_from policy as the
    // combo; syncFriendRequestControls blocks the combo's signal, so the
    // checkboxes must push the resulting policy to the server themselves.
    // The policy is derived from the checkbox state on every toggle: checking
    // a box selects that policy (unchecking the others), unchecking all three
    // means "No one". This keeps the three boxes consistent with the single
    // combo value — an uncheck never silently drops the change.
    auto pushFriendPolicy = [this]() {
        int policy;
        if (friendRequestFromEveryone->isChecked())
            policy = 0;
        else if (friendRequestFromFriendsOfFriends->isChecked())
            policy = 1;
        else if (friendRequestFromServerMembers->isChecked())
            policy = 2;
        else
            policy = 3;

        QSettings().setValue("privacy/friend_add_policy", policy);
        syncFriendRequestControls(policy);
        if (client) {
            QJsonObject payload;
            payload["allow_friend_requests_from"] = policy;
            client->updatePrivacySettings(payload, [](const Core::Result<QJsonObject> &result) {
                logPrivacyUpdateResult("allow_friend_requests_from", result);
            });
        }
    };
    connect(friendRequestFromEveryone, &QCheckBox::toggled, this, [this, pushFriendPolicy](bool checked) {
        QSettings().setValue("privacy/friend_req_everyone", checked);
        pushFriendPolicy();
        emit privacyChanged();
    });
    connect(friendRequestFromFriendsOfFriends, &QCheckBox::toggled, this, [this, pushFriendPolicy](bool checked) {
        QSettings().setValue("privacy/friend_req_fof", checked);
        pushFriendPolicy();
        emit privacyChanged();
    });
    connect(friendRequestFromServerMembers, &QCheckBox::toggled, this, [this, pushFriendPolicy](bool checked) {
        QSettings().setValue("privacy/friend_req_server", checked);
        pushFriendPolicy();
        emit privacyChanged();
    });
    connect(explicitImageFilterCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        QSettings().setValue("privacy/explicit_filter", index);
        if (client) {
            QJsonObject payload;
            payload["explicit_image_filter"] = index;
            client->updatePrivacySettings(payload, [](const Core::Result<QJsonObject> &result) {
                logPrivacyUpdateResult("explicit_image_filter", result);
            });
        }
        emit privacyChanged();
    });

    // If we have an API client, try to fetch the live settings
    fetchSettings();
}

void PrivacySettingsPage::setClient(Discord::Client *c)
{
    client = c;
    fetchSettings();
}

void PrivacySettingsPage::fetchSettings()
{
    if (!client)
        return;

    QPointer<PrivacySettingsPage> guard(this);
    client->fetchPrivacySettings([guard](const Core::Result<QJsonObject> &result) {
        if (!guard)
            return;
        if (!result.success() || !result.value) {
            qCWarning(LogUI) << "Failed to fetch privacy settings:"
                             << (result.success() ? QStringLiteral("empty response") : result.error);
            return;
        }

        const QJsonObject obj = result.value.value();

        if (obj.contains("default_scope_of_dms")) {
            int dmScope = obj.value("default_scope_of_dms").toInt();
            if (dmScope >= 0 && dmScope < guard->dmFilterCombo->count()) {
                QSignalBlocker blocker(guard->dmFilterCombo);
                guard->dmFilterCombo->setCurrentIndex(dmScope);
                QSettings().setValue("privacy/dm_filter", dmScope);
            }
        }
        if (obj.contains("allow_friend_requests_from")) {
            int friendPolicy = obj.value("allow_friend_requests_from").toInt();
            if (friendPolicy >= 0 && friendPolicy < guard->friendAddCombo->count())
                guard->syncFriendRequestControls(friendPolicy);
        }
        if (obj.contains("default_allow_dms_from_server_members")) {
            bool allowServerMembers = obj.value("default_allow_dms_from_server_members").toBool();
            QSignalBlocker blocker(guard->allowDmFromServerMembers);
            guard->allowDmFromServerMembers->setChecked(allowServerMembers);
            QSettings().setValue("privacy/allow_dm_server_members", allowServerMembers);
        }
        if (obj.contains("explicit_image_filter")) {
            int filter = obj.value("explicit_image_filter").toInt();
            if (filter >= 0 && filter < guard->explicitImageFilterCombo->count()) {
                QSignalBlocker blocker(guard->explicitImageFilterCombo);
                guard->explicitImageFilterCombo->setCurrentIndex(filter);
                QSettings().setValue("privacy/explicit_filter", filter);
            }
        }
    });
}

void PrivacySettingsPage::syncFriendRequestControls(int policy)
{
    QSignalBlocker blockCombo(friendAddCombo);
    QSignalBlocker blockEveryone(friendRequestFromEveryone);
    QSignalBlocker blockFof(friendRequestFromFriendsOfFriends);
    QSignalBlocker blockServer(friendRequestFromServerMembers);

    friendAddCombo->setCurrentIndex(policy);
    friendRequestFromEveryone->setChecked(policy == 0);
    friendRequestFromFriendsOfFriends->setChecked(policy == 1);
    friendRequestFromServerMembers->setChecked(policy == 2);

    QSettings().setValue("privacy/friend_add_policy", policy);
    QSettings().setValue("privacy/friend_req_everyone", policy == 0);
    QSettings().setValue("privacy/friend_req_fof", policy == 1);
    QSettings().setValue("privacy/friend_req_server", policy == 2);
}

} // namespace UI
} // namespace Acheron
