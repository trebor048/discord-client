#include "MainWindow.hpp"

#include <QMessageBox>
#include <QPointer>
#include <QPropertyAnimation>
#include <QSettings>
#include <QGraphicsOpacityEffect>
#include <QTimer>

#include "Chat/ChatModel.hpp"
#include "Chat/ChatDelegate.hpp"
#include "Chat/ChatView.hpp"
#include "Forum/ForumBrowser.hpp"
#include "Forum/ForumPostModel.hpp"
#include "Forum/NewPostDialog.hpp"
#include "ThreadBrowser/ThreadBrowserPopup.hpp"
#include "Core/ForumManager.hpp"
#include "ChannelList/ChannelTreeModel.hpp"
#include "ChannelList/ChannelFilterProxyModel.hpp"
#include "ChannelList/ChannelDelegate.hpp"
#include "ChannelList/ChannelTreeView.hpp"
#include "ChannelList/ServerRailView.hpp"
#include "ChannelList/ServerRailModel.hpp"
#include "ChannelList/ServerRailDelegate.hpp"
#include "TabBar/TabBar.hpp"
#include "Accounts/AccountsWindow.hpp"
#include "FriendsPage.hpp"
#include "Settings/SettingsWindow.hpp"
#include "Settings/NotificationsPage.hpp"
#include "Settings/VoicePage.hpp"
#include "Accounts/AccountsModel.hpp"
#include "Core/ClientInstance.hpp"
#include "Core/AccountInfo.hpp"
#include "Core/UserManager.hpp"
#include "Core/TypingTracker.hpp"
#include "Core/Logging.hpp"
#include "Core/ReadStateManager.hpp"
#include "Core/Notification/NotificationManager.hpp"
#include "Core/Theme/Icons.hpp"
#include "Discord/Events.hpp"
#include "Splitter.hpp"
#include "TypingIndicator.hpp"
#include "SlowModeIndicator.hpp"
#include "ConnectionBanner.hpp"
#include "BrowserCaptchaResolver.hpp"
#include "Dialogs/ChannelQuickSwitch.hpp"
#include "Dialogs/ConfirmPopup.hpp"
#include "Dialogs/EditProfileDialog.hpp"
#include "Dialogs/PinnedMessagesPanel.hpp"
#include "Dialogs/ShortcutSheet.hpp"
#include "Dialogs/UserProfilePopup.hpp"
#include "Dialogs/GuildSettingsDialog.hpp"
#include "Discord/CdnUrls.hpp"
#include "Core/ImageManager.hpp"
#include "Core/MemberListManager.hpp"
#include "Core/Session.hpp"
#include "Core/Theme/Manager.hpp"
#include "Core/AnimationUtils.hpp"
#ifndef ACHERON_NO_VOICE
#include "Core/AV/VoiceManager.hpp"
#include "Core/AV/PushToTalkListener.hpp"
#include "VoiceStatusBar.hpp"
#endif

#include <QScreen>

using namespace Acheron::Core;

namespace Acheron {
namespace UI {

MainWindow::MainWindow(Session *session, QWidget *parent) : QMainWindow(parent), session(session)
{
    WindowManager::trackWindow(this);

    auto *captchaResolver = new BrowserCaptchaResolver(this, this);
    session->setCaptchaResolver(captchaResolver);

    chatModel = new ChatModel(session->getImageManager(), this);
    notificationSoundsEnabled = QSettings().value("notifications/sounds", true).toBool();
    channelTreeModel = new ChannelTreeModel(session, this);
    channelFilterProxy = new ChannelFilterProxyModel(session, this);
    channelFilterProxy->setSourceModel(channelTreeModel);
    channelFilterProxy->setDynamicSortFilter(true);
    channelFilterProxy->sort(0);
    accountsModel = new AccountsModel(session, this);
    serverRailModel = new ServerRailModel(session, channelTreeModel, this);

    chatModel->setAvatarUrlResolver([](const Discord::User &user) -> QUrl {
        return Discord::Cdn::userAvatar(user.id.get(), user.avatar.get(), 64);
    });

    chatModel->setDisplayNameResolver([this](Snowflake userId, Snowflake guildId) -> QString {
        if (!currentInstance)
            return QString();
        return currentInstance->users()->getDisplayName(userId, guildId);
    });

    chatModel->setRoleColorResolver(
            [this](Snowflake userId, Snowflake guildId) { return resolveRoleColor(userId, guildId); });

    typingTracker = new TypingTracker(this);
    memberListModel = new MemberListModel(session->getImageManager(), this);

    channelListMode = (QSettings().value("ui/channelListMode").toString() == "classic")
                              ? ChannelListMode::Classic
                              : ChannelListMode::Tree;

    // Extract the five former monolith concerns. These are constructed before
    // setupUi() so every forwarded call below resolves against a live controller.
    channelController = new ChannelSelectionController(this);
    voiceController = new VoiceStateController(this);
    windowManager = new WindowManager(this);
    contextMenuFactory = new ContextMenuFactory(this);
    notificationController = new NotificationController(this);

    setupUi();
    setupMenu();

    qApp->installEventFilter(this);

    voiceController->createPushToTalkListener();

    typingIndicator->setRoleColorResolver(
            [this](Snowflake userId, Snowflake guildId) { return resolveRoleColor(userId, guildId); });

    connect(typingTracker, &TypingTracker::typersChanged, this,
            [this]() { typingIndicator->setTypers(typingTracker->getActiveTypers()); });

    connect(session, &Session::ready, this, [this](const Discord::Ready &ready) {
        channelTreeModel->populateFromReady(ready);
        if (channelListMode == ChannelListMode::Tree)
            channelTree->performDefaultExpansion();
        applyTreeState();
        maybeActivatePendingChannel(ready.user->id);
        refreshTabReadStates();
        if (channelListMode == ChannelListMode::Classic)
            applyPendingRailSelection(ready.user->id);
    });

    connect(accountsModel, &AccountsModel::dataChanged, this,
            [this, session](const QModelIndex &topLeft, const QModelIndex &bottomRight,
                            const QVector<int> &roles) {
                for (int row = topLeft.row(); row <= bottomRight.row(); ++row) {
                    QModelIndex idx = accountsModel->index(row, 0);
                    Snowflake accId(idx.data(AccountsModel::AccountObjectRole).value<quint64>());
                    const auto *accPtr = accountsModel->getAccountById(accId);
                    if (!accPtr)
                        continue;

                    Acheron::Core::AccountInfo acc = *accPtr;

                    if (acc.state == Acheron::Core::ConnectionState::Connected) {
                        channelTreeModel->addAccount(acc);

                        ClientInstance *instance = session->client(acc.id);
                        if (instance)
                            setupPermanentConnections(instance);

                        for (int i = 0; i < channelFilterProxy->rowCount(QModelIndex()); ++i) {
                            QModelIndex proxyIdx = channelFilterProxy->index(i, 0, QModelIndex());
                            if (proxyIdx.data(ChannelTreeModel::IdRole).toULongLong() == static_cast<quint64>(acc.id)) {
                                channelTree->expand(proxyIdx);
                                break;
                            }
                        }
                    } else if (acc.state == Acheron::Core::ConnectionState::Disconnected) {
                        bool wasShowing = channelListMode == ChannelListMode::Classic && channelController->railSelectedAccountId == acc.id;
                        channelTreeModel->removeAccount(acc.id);
                        if (wasShowing) {
                            channelController->railHasSelection = false;
                            channelTree->setRootIndex({});
                            selectInitialRailItem();
                        }
                    }
                }
            });

    for (const auto &instance : session->getClients()) {
        if (instance)
            setupPermanentConnections(instance);
    }

    restoreWindowState();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (windowManager->skipNextWindowStateSave)
        windowManager->skipNextWindowStateSave = false;
    else
        saveWindowState();

    hide();

    event->accept();
}

bool MainWindow::eventFilter(QObject *obj, QEvent *ev)
{
    if (ev->type() == QEvent::MouseButtonPress && isActiveWindow()) {
        auto *me = static_cast<QMouseEvent *>(ev);
        if (me->button() == Qt::BackButton) {
            tabBar->navigateBack();
            return true;
        }
        if (me->button() == Qt::ForwardButton) {
            tabBar->navigateForward();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::onChannelSelectionChanged(const QModelIndex &current, const QModelIndex &previous)
{
    channelController->onChannelSelectionChanged(current, previous);
}

void MainWindow::applyChannelChrome(Core::ClientInstance *instance, Core::Snowflake channelId,
                                    const QString &name, bool isDm, Core::Snowflake guildId)
{
    channelController->applyChannelChrome(instance, channelId, name, isDm, guildId);
}

void MainWindow::switchChatChannel(Core::Snowflake channelId, Core::Snowflake guildId)
{
    channelController->switchChatChannel(channelId, guildId);
}

void MainWindow::switchActiveInstance(Core::ClientInstance *newInstance)
{
    if (currentInstance) {
        currentInstance->forums()->setCurrentForum({});

        auto *msgs = currentInstance->messages();
        disconnect(msgs, nullptr, chatModel, nullptr);
        disconnect(msgs, nullptr, this, nullptr);
        disconnect(currentInstance->discord(), &Discord::Client::typingStart, this, nullptr);
        disconnect(currentInstance->permissions(), nullptr, this, nullptr);
        disconnect(currentInstance, &Core::ClientInstance::membersUpdated, this, nullptr);
        disconnect(memberListView, nullptr, currentInstance->memberList(), nullptr);
        disconnect(currentInstance->forums(), nullptr, this, nullptr);

        // Clean up old notification manager
        notificationController->teardown();
    }

    if (windowManager->friendsWindow) {
        windowManager->friendsWindow->close();
        windowManager->friendsWindow = nullptr;
    }

    currentInstance = newInstance;
    auto *msgs = currentInstance->messages();

    memberListModel->setManager(currentInstance->memberList());

    forumModel->setManager(currentInstance->forums());
    connect(currentInstance->forums(), &Core::ForumManager::loadingChanged, this,
            [this](Core::Snowflake forumId, bool loading) {
                if (forumId == channelController->currentForumId)
                    forumBrowser->setLoading(loading);
            });
    connect(memberListView, &MemberListView::visibleRangeChanged,
            currentInstance->memberList(), &Core::MemberListManager::updateSubscriptionRange);

    chatView->setCurrentUserId(currentInstance->accountId());

    typingTracker->clear();
    typingTracker->setUserManager(currentInstance->users());
    typingTracker->setCurrentUserId(currentInstance->accountId());

    // Initialize notification manager for this instance
    notificationController->setupForInstance(currentInstance);

    // Connect settings window to notification manager
    if (windowManager->settingsWindow) {
        windowManager->settingsWindow->setNotificationManager(notificationController->manager());
        windowManager->settingsWindow->setVoiceManager(currentInstance ? currentInstance->voice() : nullptr);
    }

    connect(msgs, &MessageManager::messagesReceived, chatModel, &ChatModel::handleIncomingMessages);
    connect(msgs, &MessageManager::messageErrored, chatModel, &ChatModel::handleMessageErrored);
    connect(msgs, &MessageManager::messageDeleted, chatModel, &ChatModel::handleMessageDeleted);
    connect(msgs, &MessageManager::attachmentUploadProgress, chatModel, &ChatModel::handleUploadProgress);
    connect(msgs, &MessageManager::messageErrored, this, [this]() {
        if (messageInput)
            messageInput->setSendBlocked(false);
    });
    connect(msgs, &MessageManager::messagesReceived, this,
            [this](const MessageRequestResult &result) {
                if (result.type == Discord::Client::MessageLoadType::History &&
                    result.channelId == chatModel->getActiveChannelId()) {
                    if (result.success)
                        chatView->onHistoryRequestFinished();
                    else
                        chatView->onHistoryRequestFailed();
                }
            });

    connect(currentInstance->discord(), &Discord::Client::typingStart, this,
            &MainWindow::onTypingStart);
    // Note: messageCreated is now handled by NotificationManager
    // We still need to remove typer
    connect(currentInstance->discord(), &Discord::Client::messageCreated, this,
            [this](const Discord::Message &msg) {
                typingTracker->removeTyper(msg.channelId, msg.author->id);
            });

    connect(currentInstance->permissions(), &Core::PermissionManager::channelPermissionsChanged,
            this, &MainWindow::onChannelPermissionsChanged);

    connect(currentInstance, &Core::ClientInstance::membersUpdated, this,
            [this](Snowflake guildId, const QList<Snowflake> &userIds) {
                for (const auto &userId : userIds)
                    channelController->userColorCache.remove(userId);

                chatModel->refreshUsersInView(userIds);
                forumModel->refreshAuthors();
            });

#ifndef ACHERON_NO_VOICE
    updateVoiceStatusLabel();
#endif
}

void MainWindow::setViewMode(ChannelSelectionController::ViewMode mode)
{
    channelController->setViewMode(mode);
}

void MainWindow::setMemberListVisible(bool visible)
{
    channelController->setMemberListVisible(visible);
}

void MainWindow::updateMemberListVisibility()
{
    channelController->updateMemberListVisibility();
}

TabEntry MainWindow::makeTabEntry(ChannelNode *node, ChannelNode *accountNode) const
{
    return channelController->makeTabEntry(node, accountNode);
}

void MainWindow::openForumChannel(Core::ClientInstance *instance, Core::Snowflake forumId,
                                  Core::Snowflake guildId)
{
    channelController->openForumChannel(instance, forumId, guildId);
}

void MainWindow::openForumPost(Core::Snowflake threadId, Core::Snowflake guildId)
{
    channelController->openForumPost(threadId, guildId);
}

void MainWindow::onNewPostRequested()
{
    channelController->onNewPostRequested();
}

void MainWindow::confirmAndLeaveGuild(Snowflake accountId, Snowflake guildId)
{
    ClientInstance *instance = session->client(accountId);
    if (!instance)
        return;

    auto guild = instance->getGuild(guildId);
    QString guildName = guild.has_value() ? guild->name.get() : QString::number(guildId);

    ConfirmPopup dialog(tr("Leave Server"),
                        tr("Are you sure you want to leave <b>%1</b>?").arg(guildName.toHtmlEscaped()),
                        tr("Leave"), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    instance->discord()->leaveGuild(guildId);
}

void MainWindow::openGuildSettings(Core::Snowflake accountId, Core::Snowflake guildId)
{
    ClientInstance *instance = session->client(accountId);
    if (!instance)
        return;

    auto *dlg = new GuildSettingsDialog(instance, guildId, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::setupPermanentConnections(Core::ClientInstance *instance)
{
    if (!instance)
        return;

    if (instancesSignalsConnected.contains(instance->accountId()))
        return;
    instancesSignalsConnected.insert(instance->accountId());

    connect(instance, &QObject::destroyed, this,
            [this, accountId = instance->accountId()]() {
                instancesSignalsConnected.remove(accountId);
            });

    connect(instance, &Core::ClientInstance::guildCreated, this,
            [this, instance](const Discord::GatewayGuild &guild) {
                channelTreeModel->addGuild(guild, instance->accountId());
                if (channelListMode == ChannelListMode::Tree)
                    channelTree->performDefaultExpansion();
            });

    connect(instance, &Core::ClientInstance::guildRemoved, this,
            [this, instance](Snowflake guildId) {
                channelTreeModel->removeGuild(instance->accountId(), guildId);
            });

    connect(instance->discord(), &Discord::Client::guildLeaveFailed, this,
            [this, instance](Snowflake guildId, const QString &error) {
                auto guild = instance->getGuild(guildId);
                QString guildName = guild.has_value() ? guild->name.get() : QString::number(guildId);
                QMessageBox::warning(this,
                                     tr("Failed to leave server"),
                                     tr("Could not leave <b>%1</b>.<br>Error: %2")
                                             .arg(guildName.toHtmlEscaped(), error));
            });

    connect(instance, &Core::ClientInstance::channelCreated, this,
            [this, instance](const Discord::ChannelCreate &event) {
                channelTreeModel->addChannel(event, instance->accountId());
            });

    connect(instance, &Core::ClientInstance::channelUpdated, this,
            [this, instance](const Discord::ChannelUpdate &update) {
                channelTreeModel->updateChannel(update, instance->accountId());

                if (!update.channel.hasValue())
                    return;
                const auto &ch = update.channel.get();
                if (!ch.id.hasValue() || ch.id.get() != chatModel->getActiveChannelId())
                    return;
                if (instance != currentInstance)
                    return;

                int rateLimit = ch.rateLimitPerUser.hasValue() ? ch.rateLimitPerUser.get() : 0;
                Snowflake userId = instance->accountId();
                bool canBypass = instance->permissions()->hasChannelPermission(
                        userId, ch.id.get(), Discord::Permission::BYPASS_SLOWMODE);
                slowModeIndicator->setSlowMode(ch.id.get(), rateLimit, canBypass);
            });

    connect(instance, &Core::ClientInstance::channelDeleted, this,
            [this, instance](const Discord::ChannelDelete &event) {
                channelTreeModel->deleteChannel(event, instance->accountId());
            });

    connect(instance, &Core::ClientInstance::threadCreated, this,
            [this, instance](const Discord::Channel &thread) {
                channelTreeModel->addThread(thread, instance->accountId());
            });
    connect(instance, &Core::ClientInstance::threadUpdated, this,
            [this, instance](const Discord::Channel &thread) {
                channelTreeModel->updateThread(thread, instance->accountId());
            });
    connect(instance, &Core::ClientInstance::threadDeleted, this,
            [this, instance](Core::Snowflake threadId, Core::Snowflake, Core::Snowflake) {
                channelTreeModel->removeThread(threadId, instance->accountId());
            });
    connect(instance, &Core::ClientInstance::threadListSynced, this,
            [this, instance](Core::Snowflake guildId, const QList<Core::Snowflake> &parentIds,
                             const QList<Discord::Channel> &threads) {
                channelTreeModel->syncThreads(guildId, parentIds, threads, instance->accountId());
            });
    connect(instance, &Core::ClientInstance::threadMembershipChanged, this,
            [this, instance](Core::Snowflake threadId) {
                Core::Snowflake acc = instance->accountId();
                if (instance->isThreadJoined(threadId)) {
                    if (channelTreeModel->findChannelTreeNode(threadId, acc))
                        channelTreeModel->promoteTemporaryThread(threadId);
                    else if (auto ch = instance->getChannel(threadId))
                        channelTreeModel->addThread(*ch, acc);
                    channelTreeModel->updateReadState(threadId, acc);
                } else if (chatModel->getActiveChannelId() == threadId) {
                    if (auto ch = instance->getChannel(threadId))
                        channelTreeModel->showTemporaryThread(*ch, acc);
                } else {
                    channelTreeModel->removeThread(threadId, acc);
                }
            });

    // todo: i dont really like the refresh users logic rn
    connect(instance, &Core::ClientInstance::guildRoleCreated, this,
            [this](const Discord::GuildRoleCreate &event) {
                if (event.guildId.hasValue())
                    refreshGuildRoleData(event.guildId.get());
            });

    connect(instance, &Core::ClientInstance::guildRoleUpdated, this,
            [this](const Discord::GuildRoleUpdate &event) {
                if (event.guildId.hasValue())
                    refreshGuildRoleData(event.guildId.get());
            });

    connect(instance, &Core::ClientInstance::guildRoleDeleted, this,
            [this](const Discord::GuildRoleDelete &event) {
                if (event.guildId.hasValue())
                    refreshGuildRoleData(event.guildId.get());
            });

    connect(instance, &Core::ClientInstance::readStateChanged, this,
            [this, instance](Core::Snowflake channelId) {
                channelTreeModel->updateReadState(channelId, instance->accountId());
                refreshTabReadStates();
                forumModel->refreshPost(channelId);
            });

    connect(instance, &Core::ClientInstance::forumBadgeChanged, this,
            [this, instance](Core::Snowflake forumId) {
                channelTreeModel->updateForumBadge(forumId, instance->accountId());
            });

    connect(instance, &Core::ClientInstance::forumJoinedPostsChanged, this,
            [this, instance](Core::Snowflake forumId) {
                channelTreeModel->updateForumThreads(forumId, instance->accountId());
            });

    connect(instance, &Core::ClientInstance::channelLastMessageUpdated, this,
            [this, instance](Core::Snowflake channelId, Core::Snowflake messageId) {
                channelTreeModel->updateChannelLastMessageId(channelId, messageId,
                                                             instance->accountId());
                refreshTabReadStates();
            });

    connect(instance, &Core::ClientInstance::customEmojisChanged, this,
            [this]() {
                messageInput->refreshEmojiCompleter();
            });

    connect(instance, &Core::ClientInstance::stickerStoreChanged, this,
            [this, instance](Core::Snowflake guildId) {
                if (guildId != channelController->cachedGuildId)
                    return;

                const auto &stickerList = instance->guildStickers();
                if (stickerList.contains(guildId))
                    messageInput->setAvailableStickers({ { guildId, stickerList[guildId] } });
            });

    connect(instance, &Core::ClientInstance::guildSettingsChanged, this,
            [this, instance](Core::Snowflake guildId) {
                channelTreeModel->updateGuildSettings(guildId, instance->accountId());
            });

    connect(instance, &Core::ClientInstance::reconnecting, this,
            [this](int attempt, int maxAttempts) {
                connectionBanner->showReconnecting(attempt, maxAttempts);
            });

    connect(instance, &Core::ClientInstance::stateChanged, this,
            [this](Core::ConnectionState state) {
                if (state == Core::ConnectionState::Connected)
                    connectionBanner->hide();
            });

    connect(instance, &Core::ClientInstance::authenticationFailed, this,
            [this](const Core::AccountInfo &info) {
                connectionBanner->hide();
                const QString accountLabel = info.displayName.isEmpty() ? info.username
                                                                        : info.displayName;
                QWidget *parent = (windowManager->accountsWindow && windowManager->accountsWindow->isVisible())
                                          ? static_cast<QWidget *>(windowManager->accountsWindow)
                                          : static_cast<QWidget *>(this);
                auto *box = new QMessageBox(parent);
                box->setAttribute(Qt::WA_DeleteOnClose);
                box->setIcon(QMessageBox::Critical);
                box->setWindowTitle(tr("Authentication Failed"));
                box->setText(tr("Discord rejected the token for account \"%1\".").arg(accountLabel));
                box->setInformativeText(tr("Discord's gateway rejected your token. The stored token is invalid. Check or update your token and try again."));
                box->setStandardButtons(QMessageBox::Ok);
                box->setWindowModality(Qt::WindowModal);
                box->show();
            });

    connect(instance, &Core::ClientInstance::voiceStateChanged, this,
            [this, instance](Core::Snowflake channelId, Core::Snowflake) {
                channelTree->setAccountVoiceChannel(instance->accountId(), channelId);
#ifndef ACHERON_NO_VOICE
                updateVoiceStatusLabel();
#endif
            });

#ifndef ACHERON_NO_VOICE
    voiceController->connectInstanceVoice(instance);
#endif
}

void MainWindow::setupUi()
{
    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);

    channelTree = new ChannelTreeView(this);

#ifndef ACHERON_NO_VOICE
    voiceStatusBar = new VoiceStatusBar(this);
    voiceStatusBar->setImageManager(session->getImageManager());
    connect(voiceStatusBar, &VoiceStatusBar::disconnectRequested, this, [this]() {
        voiceController->disconnectActiveVoice();
    });
#endif

    serverRail = new ServerRailView(this);
    serverRail->setModel(serverRailModel);
    serverRail->setItemDelegate(new ServerRailDelegate(serverRail));
    serverRail->setFixedWidth(ServerRailDelegate::RailWidth);
    serverRail->hide();
    connect(serverRail, &ServerRailView::accountHomeClicked, this, &MainWindow::onRailAccountHomeClicked);
    connect(serverRail, &ServerRailView::guildClicked, this, &MainWindow::onRailGuildClicked);
    connect(serverRail, &ServerRailView::folderToggleClicked, this,
            [this](Snowflake accountId, Snowflake folderId) {
                serverRailModel->toggleFolder(accountId, folderId);
            });
    connect(serverRail, &ServerRailView::markAsReadRequested, this,
            [this](Snowflake accountId, Snowflake id, bool isFolder) {
                markIndexAsRead(accountId, isFolder ? channelTreeModel->folderIndex(accountId, id)
                                                    : channelTreeModel->serverIndex(accountId, id));
            });

    connect(serverRail, &ServerRailView::leaveGuildRequested, this, &MainWindow::confirmAndLeaveGuild);
    connect(serverRail, &ServerRailView::serverSettingsRequested, this, &MainWindow::openGuildSettings);

    guildHeaderLabel = new QLabel(this);
    guildHeaderLabel->setContentsMargins(12, 8, 12, 8);
    {
        QFont headerFont = guildHeaderLabel->font();
        headerFont.setBold(true);
        guildHeaderLabel->setFont(headerFont);
    }
    guildHeaderLabel->hide();

    customStatusLabel = new QLabel(this);
    customStatusLabel->setContentsMargins(12, 6, 12, 6);
    customStatusLabel->setWordWrap(true);
    customStatusLabel->setStyleSheet(QStringLiteral("QLabel { color: palette(mid); font-size: 11px; }"));
    applyCustomStatus(QSettings().value("general/custom_status").toString());

    auto *rightSideWidget = new QWidget(central);
    auto *rightLayout = new QVBoxLayout(rightSideWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    connectionBanner = new ConnectionBanner(rightSideWidget);
    tabBar = new TabBar(session->getImageManager(), rightSideWidget);

    threadBrowserButton = new QToolButton(rightSideWidget);
    threadBrowserButton->setText(tr("Threads"));
    threadBrowserButton->setIcon(Core::Theme::Icons::icon(Core::Theme::Icons::Name::Spool, Core::Theme::Token::PrimaryText));
    threadBrowserButton->setIconSize(QSize(16, 16));
    threadBrowserButton->setToolTip(tr("Browse threads"));
    threadBrowserButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    threadBrowserButton->setAutoRaise(true);
    threadBrowserButton->setCursor(Qt::PointingHandCursor);
    connect(threadBrowserButton, &QToolButton::clicked, this, &MainWindow::openThreadBrowser);

    pinnedMessagesButton = new QToolButton(rightSideWidget);
    pinnedMessagesButton->setText(tr("Pinned"));
    pinnedMessagesButton->setIcon(Core::Theme::Icons::icon(Core::Theme::Icons::Name::MessageCircle, Core::Theme::Token::PrimaryText));
    pinnedMessagesButton->setIconSize(QSize(16, 16));
    pinnedMessagesButton->setToolTip(tr("View pinned messages"));
    pinnedMessagesButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    pinnedMessagesButton->setAutoRaise(true);
    pinnedMessagesButton->setCursor(Qt::PointingHandCursor);
    connect(pinnedMessagesButton, &QToolButton::clicked, this, &MainWindow::openPinnedMessages);

    channelToolbar = new QWidget(rightSideWidget);
    channelToolbar->setObjectName("channelToolbar");
    channelToolbar->setAttribute(Qt::WA_StyledBackground, true);
    auto *channelToolbarLayout = new QHBoxLayout(channelToolbar);
    channelToolbarLayout->setContentsMargins(8, 3, 8, 3);
    channelToolbarLayout->setSpacing(4);
    channelToolbarLayout->addStretch(1);
    channelToolbarLayout->addWidget(pinnedMessagesButton, 0);
    channelToolbarLayout->addWidget(threadBrowserButton, 0);
    channelToolbar->setStyleSheet(
            "#channelToolbar { border-bottom: 1px solid rgba(128, 128, 128, 0.25); }");
    channelToolbar->hide();

    chatView = new ChatView(rightSideWidget);
    chatView->setFont(Core::Theme::Manager::instance().font(Core::Theme::FontRole::Message));
    messageInput = new MessageInput(rightSideWidget);
    messageInput->setCompact(QSettings().value("ui/compactInput", false).toBool());
    typingIndicator = new TypingIndicator(rightSideWidget);
    slowModeIndicator = new SlowModeIndicator(rightSideWidget);

    auto *statusRow = new QWidget(rightSideWidget);
    auto *statusRowLayout = new QHBoxLayout(statusRow);
    statusRowLayout->setContentsMargins(0, 0, 0, 0);
    statusRowLayout->setSpacing(0);
    statusRowLayout->addWidget(typingIndicator, 1);
    statusRowLayout->addWidget(slowModeIndicator, 0);
    statusRow->setFixedHeight(typingIndicator->minimumHeight());

    connect(slowModeIndicator, &SlowModeIndicator::cooldownChanged, this,
            [this](bool onCooldown) {
                if (!currentInstance)
                    return;
                Snowflake channelId = chatModel->getActiveChannelId();
                if (!channelId.isValid())
                    return;
                messageInput->setSendBlocked(onCooldown);
                if (!onCooldown) {
                    QModelIndex current = channelTree->currentIndex();
                    if (current.isValid()) {
                        auto *node = channelTreeModel->nodeFromIndex(
                                channelFilterProxy->mapToSource(current));
                        if (node)
                            messageInput->setPlaceholder("Message #" + node->name);
                    }
                }
            });

    threadHeader = new QWidget(rightSideWidget);
    auto *threadHeaderLayout = new QHBoxLayout(threadHeader);
    threadHeaderLayout->setContentsMargins(8, 4, 8, 4);
    popOutButton = new QPushButton(tr("Open as channel"), threadHeader);
    closeThreadButton = new QPushButton(tr("Close"), threadHeader);
    threadHeaderLayout->addStretch(1);
    threadHeaderLayout->addWidget(popOutButton, 0);
    threadHeaderLayout->addWidget(closeThreadButton, 0);
    threadHeader->setVisible(false);

    connect(popOutButton, &QPushButton::clicked, this, [this]() {
        setViewMode(channelController->viewMode == ChannelSelectionController::ViewMode::ThreadPopout
                            ? ChannelSelectionController::ViewMode::ForumSplit
                            : ChannelSelectionController::ViewMode::ThreadPopout);
    });
    connect(closeThreadButton, &QPushButton::clicked, this,
            [this]() { setViewMode(ChannelSelectionController::ViewMode::ForumBrowse); });

    threadPane = new QWidget(rightSideWidget);
    auto *threadPaneLayout = new QVBoxLayout(threadPane);
    threadPaneLayout->setContentsMargins(0, 0, 0, 0);
    threadPaneLayout->setSpacing(0);
    threadPaneLayout->addWidget(threadHeader, 0);
    threadPaneLayout->addWidget(chatView, 1);
    threadPaneLayout->addWidget(statusRow, 0);
    threadPaneLayout->addWidget(messageInput, 0);

    forumModel = new ForumPostModel(session->getImageManager(), this);
    forumModel->setDisplayNameResolver([this](Snowflake userId, Snowflake guildId) -> QString {
        if (!currentInstance || !guildId.isValid())
            return QString();
        auto member = currentInstance->users()->getMember(guildId, userId);
        if (member && member->nick.hasValue())
            return member->nick.get();
        return QString();
    });
    forumModel->setRoleColorResolver([this](Snowflake userId, Snowflake guildId) { return resolveRoleColor(userId, guildId); });
    forumBrowser = new ForumBrowser(rightSideWidget);
    forumBrowser->setModel(forumModel);
    forumBrowser->setVisible(false);

    connect(forumBrowser, &ForumBrowser::postActivated, this, &MainWindow::openForumPost);
    connect(forumBrowser, &ForumBrowser::newPostRequested, this, &MainWindow::onNewPostRequested);
    connect(forumBrowser, &ForumBrowser::sortModeChanged, this, [this](int mode) {
        if (currentInstance && channelController->currentForumId.isValid())
            currentInstance->forums()->setSortMode(channelController->currentForumId, static_cast<Core::ForumSortMode>(mode));
    });

    centerSplitter = new QSplitter(Qt::Horizontal, rightSideWidget);
    centerSplitter->addWidget(forumBrowser);
    centerSplitter->addWidget(threadPane);
    centerSplitter->setStretchFactor(0, 1);
    centerSplitter->setStretchFactor(1, 1);
    centerSplitter->setSizes({ 350, 550 });

    rightLayout->addWidget(connectionBanner, 0);
    rightLayout->addWidget(tabBar, 0);
    rightLayout->addWidget(channelToolbar, 0);
    rightLayout->addWidget(centerSplitter, 1);

    memberListView = new MemberListView(central);
    memberListView->setModel(memberListModel);
    memberListView->setItemDelegate(new MemberListDelegate(memberListView));

    connect(&Core::Theme::Manager::instance(), &Core::Theme::Manager::themeChanged, this, [this]() {
        chatModel->invalidateDocCache();
        chatView->viewport()->update();
        channelTree->viewport()->update();
        memberListView->viewport()->update();
    });

    connect(&Core::Theme::Manager::instance(), &Core::Theme::Manager::metricsChanged, this, [this]() {
        chatView->setFont(Core::Theme::Manager::instance().font(Core::Theme::FontRole::Message));
        chatModel->invalidateLayout();
        chatView->doItemsLayout();
        channelTree->viewport()->update();
        memberListView->viewport()->update();
    });

    connect(memberListView, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                QModelIndex idx = memberListView->indexAt(pos);
                if (!idx.isValid())
                    return;
                if (idx.data(MemberListModel::ItemTypeRole).toInt() != static_cast<int>(Core::MemberListItem::Type::Member))
                    return;
                Snowflake userId = idx.data(MemberListModel::UserIdRole).toULongLong();
                Snowflake guildId = currentInstance
                                            ? currentInstance->memberList()->currentGuildId()
                                            : Snowflake::Invalid;
                showUserContextMenu(userId, guildId,
                                    memberListView->viewport()->mapToGlobal(pos));
            });

    leftSideWidget = buildLeftSide();

    mainSplitter = new Splitter(this);
    mainSplitter->addWidget(leftSideWidget);
    mainSplitter->addWidget(rightSideWidget);
    mainSplitter->addWidget(memberListView);

    mainSplitter->setCollapsible(0, false);
    mainSplitter->setCollapsible(2, false);
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setStretchFactor(2, 0);
    memberListView->setMinimumWidth(140);
    memberListView->setMaximumWidth(400);

    memberListView->hide();

    channelTree->setModel(channelFilterProxy);
    channelTree->setHeaderHidden(true);
    channelTree->setIndentation(0);
    channelTree->setItemDelegate(new ChannelDelegate(channelFilterProxy, channelTree));
    channelTree->setIconSize(QSize(24, 24));
    channelTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    channelTree->setFrameShape(QFrame::NoFrame);
    channelTree->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    channelTree->setExpandsOnDoubleClick(false);

    connect(channelTree, &ChannelTreeView::markAsReadRequested, this,
            [this](const QModelIndex &proxyIndex) {
                QModelIndex sourceIndex = channelFilterProxy->mapToSource(proxyIndex);
                auto *node = channelTreeModel->nodeFromIndex(sourceIndex);
                if (!node)
                    return;

                ChannelNode *accountNode = channelTreeModel->getAccountNodeFor(node);
                if (!accountNode)
                    return;

                markIndexAsRead(accountNode->id, sourceIndex);
            });

    chatView->setModel(chatModel);
    chatView->setItemDelegate(new ChatDelegate(session->getImageManager(), chatView));
    chatView->setIconSize(QSize(24, 24));
    chatView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chatView->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    chatView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    chatView->setUniformItemSizes(false);
    chatView->setWordWrap(true);
    chatView->setResizeMode(QListView::Adjust);
    chatView->setCompactMode(QSettings().value("ui/compactMessages", false).toBool());
    chatView->setShowTimestamps(QSettings().value("ui/showTimestamps", false).toBool());

    if (QSettings().value("ui/alwaysShowScrollbars", false).toBool()) {
        chatView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        channelTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    }

    connect(messageInput, &MessageInput::stickerPicked, this, [this](Snowflake stickerId) {
        if (!currentInstance) {
            qCWarning(LogCore) << "Cannot send sticker: no active instance";
            return;
        }
        Snowflake channelId = chatModel->getActiveChannelId();
        if (!channelId.isValid())
            return;
        currentInstance->discord()->sendSticker(channelId, stickerId);
        QTimer::singleShot(0, messageInput, &MessageInput::clear);
    });

    connect(messageInput, &MessageInput::sendMessage, this, [this](const QString &text, const QList<Core::PendingAttachment> &attachments) {
        if (!currentInstance) {
            qCWarning(LogCore) << "Cannot send message: no active instance";
            return;
        }

        Snowflake channelId = chatModel->getActiveChannelId();
        if (!channelId.isValid()) {
            qCWarning(LogCore) << "Cannot send message: no active channel";
            return;
        }

        if (messageInput->isSendBlocked()) {
            qCDebug(LogCore) << "Cannot send message: slowmode cooldown active";
            return;
        }

        Snowflake replyTo = messageInput->replyTargetMessageId();
        currentInstance->messages()->sendMessage(channelId, text, replyTo, attachments);

        int rateLimit = currentInstance->getChannelRateLimit(channelId);
        Snowflake userId = currentInstance->accountId();
        bool canBypass = currentInstance->permissions()->hasChannelPermission(
                userId, channelId, Discord::Permission::BYPASS_SLOWMODE);
        if (rateLimit > 0 && !canBypass) {
            slowModeIndicator->startCooldown(channelId, rateLimit);
            messageInput->setSendBlocked(true);
            messageInput->setPlaceholder("Slowmode is active");
        }

        QTimer::singleShot(0, messageInput, &MessageInput::clear);
    });

    connect(chatView, &ChatView::historyRequested, this, [this]() {
        Snowflake oldestId = chatModel->getOldestMessageId();

        if (currentInstance && oldestId.isValid())
            currentInstance->messages()->requestLoadHistory(chatModel->getActiveChannelId(),
                                                            oldestId);
    });

    connect(chatView, &ChatView::filesDropped, this, [this](const QList<QUrl> &urls) {
        messageInput->queueAttachments(urls);
    });

    connect(chatView, &ChatView::cancelUploadRequested, this,
            [this](Snowflake channelId, Snowflake messageId) {
                if (currentInstance)
                    currentInstance->messages()->cancelSend(channelId, QString::number(messageId));
            });

    connect(chatView, &ChatView::deleteMessageRequested, this,
            [this](Snowflake channelId, Snowflake messageId) {
                if (!currentInstance)
                    return;

                if (QApplication::keyboardModifiers() & Qt::ShiftModifier) {
                    currentInstance->discord()->deleteMessage(channelId, messageId);
                    return;
                }

                ConfirmPopup dialog(tr("Delete Message"),
                                    tr("Are you sure you want to delete this message?"),
                                    tr("Delete"), this);
                if (dialog.exec() == QDialog::Accepted)
                    currentInstance->discord()->deleteMessage(channelId, messageId);
            });

    connect(chatView, &ChatView::pinMessageRequested, this,
            [this](Snowflake channelId, Snowflake messageId) {
                if (currentInstance)
                    currentInstance->discord()->pinMessage(channelId, messageId);
            });

    connect(chatView, &ChatView::pinnedMessagesRequested, this,
            [this](Snowflake channelId) {
                Q_UNUSED(channelId);
                openPinnedMessages();
            });

    connect(chatView, &ChatView::editMessageRequested, this,
            [this](Snowflake channelId, Snowflake messageId, const QString &content) {
                if (currentInstance)
                    currentInstance->discord()->editMessage(channelId, messageId, content);
            });

    connect(chatView, &ChatView::replyToMessageRequested, this,
            [this](Snowflake channelId, Snowflake messageId) {
                // Find the message in the model to get author name and content
                for (int row = 0; row < chatModel->rowCount(); ++row) {
                    QModelIndex idx = chatModel->index(row, 0);
                    Snowflake msgId = idx.data(ChatModel::MessageIdRole).toULongLong();
                    if (msgId == messageId) {
                        QString authorName = idx.data(ChatModel::UsernameRole).toString();
                        QString content = idx.data(ChatModel::ContentRole).toString();
                        messageInput->setReplyTarget(messageId, authorName, content);
                        return;
                    }
                }
                // Fallback if message not found in model
                messageInput->setReplyTarget(messageId, tr("Unknown"), QString());
            });

    connect(chatView, &ChatView::addReactionRequested, this,
            [this](Snowflake channelId, Snowflake messageId, const QString &emoji) {
                if (currentInstance)
                    currentInstance->discord()->addReaction(channelId, messageId, emoji);
            });

    connect(chatView, &ChatView::toggleReactionClicked, this,
            [this](Snowflake channelId, Snowflake messageId, const QString &emoji, bool currentlyReacted, bool isBurst) {
                if (!currentInstance)
                    return;
                if (currentlyReacted)
                    currentInstance->discord()->removeReaction(channelId, messageId, emoji, isBurst);
                else
                    currentInstance->discord()->addReaction(channelId, messageId, emoji, isBurst);
            });

    connect(chatView, &ChatView::userContextMenuRequested, this,
            [this](Snowflake userId, QPoint globalPos) {
                showUserContextMenu(userId, channelController->cachedGuildId, globalPos);
            });

    connect(chatView, &ChatView::channelMentionClicked, this, &MainWindow::navigateToChannel);

    connect(channelTree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &MainWindow::onChannelSelectionChanged);

    connect(tabBar, &TabBar::tabChanged, this, &MainWindow::switchToTabEntry);

    connect(channelTree, &ChannelTreeView::openInNewTabRequested, this,
            [this](const QModelIndex &proxyIndex) {
                QModelIndex sourceIndex = channelFilterProxy->mapToSource(proxyIndex);
                auto *node = channelTreeModel->nodeFromIndex(sourceIndex);
                if (!node)
                    return;

                ChannelNode *accountNode = channelTreeModel->getAccountNodeFor(node);
                if (!accountNode)
                    return;

                ChannelNode *guildNode = ChannelTreeModel::findGuildNode(node);
                Snowflake guildId = guildNode ? guildNode->id : Snowflake::Invalid;

                TabEntry entry;
                entry.channelId = node->id;
                entry.guildId = guildId;
                entry.accountId = accountNode->id;
                entry.name = node->name;
                entry.isDm = (node->type == ChannelNode::Type::DMChannel);
                if (guildNode && !guildNode->iconHash.isEmpty())
                    entry.iconUrl = Discord::Cdn::guildIcon(guildNode->id, guildNode->iconHash,
                                                            64);
                tabBar->openNewTab(entry);
            });

    connect(channelTree, &ChannelTreeView::openInNewWindowRequested, this,
            [this](const QModelIndex &proxyIndex) {
                QModelIndex sourceIndex = channelFilterProxy->mapToSource(proxyIndex);
                auto *node = channelTreeModel->nodeFromIndex(sourceIndex);
                if (!node)
                    return;

                ChannelNode *accountNode = channelTreeModel->getAccountNodeFor(node);
                if (!accountNode)
                    return;

                ChannelNode *guildNode = ChannelTreeModel::findGuildNode(node);
                Snowflake guildId = guildNode ? guildNode->id : Snowflake::Invalid;

                TabEntry entry;
                entry.channelId = node->id;
                entry.guildId = guildId;
                entry.accountId = accountNode->id;
                entry.name = node->name;
                entry.isDm = (node->type == ChannelNode::Type::DMChannel);
                if (guildNode && !guildNode->iconHash.isEmpty())
                    entry.iconUrl = Discord::Cdn::guildIcon(guildNode->id, guildNode->iconHash,
                                                            64);
                openChannelInNewWindow(entry, false);
            });

    auto threadMembership = [this](const QModelIndex &proxyIndex, bool join) {
        QModelIndex sourceIndex = channelFilterProxy->mapToSource(proxyIndex);
        auto *node = channelTreeModel->nodeFromIndex(sourceIndex);
        if (!node || node->type != ChannelNode::Type::Thread)
            return;
        ChannelNode *accountNode = channelTreeModel->getAccountNodeFor(node);
        if (!accountNode)
            return;
        auto *instance = session->client(accountNode->id);
        if (!instance)
            return;
        if (join)
            instance->discord()->joinThread(node->id);
        else
            instance->discord()->leaveThread(node->id);
    };
    connect(channelTree, &ChannelTreeView::joinThreadRequested, this, [threadMembership](const QModelIndex &proxyIndex) { threadMembership(proxyIndex, true); });
    connect(channelTree, &ChannelTreeView::leaveThreadRequested, this, [threadMembership](const QModelIndex &proxyIndex) { threadMembership(proxyIndex, false); });

    connect(channelTree, &ChannelTreeView::leaveGuildRequested, this, &MainWindow::confirmAndLeaveGuild);
    connect(channelTree, &ChannelTreeView::serverSettingsRequested, this, &MainWindow::openGuildSettings);

#ifndef ACHERON_NO_VOICE
    connect(channelTree, &ChannelTreeView::joinVoiceChannelRequested, this,
            [this](const QModelIndex &proxyIndex) {
                voiceController->joinVoiceChannel(proxyIndex);
            });

    connect(channelTree, &ChannelTreeView::disconnectVoiceRequested, this,
            [this](const QModelIndex &proxyIndex) {
                voiceController->disconnectVoiceChannel(proxyIndex);
            });
#endif

    layout->addWidget(mainSplitter);
    layout->setContentsMargins(0, 0, 4, 0);
    setCentralWidget(central);
}

QWidget *MainWindow::buildLeftSide()
{
    auto *container = new QWidget;

    if (channelListMode == ChannelListMode::Classic) {
        auto *h = new QHBoxLayout(container);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(0);

        serverRail->show();
        h->addWidget(serverRail, 0);

        auto *secondary = new QWidget(container);
        auto *v = new QVBoxLayout(secondary);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        v->addWidget(guildHeaderLabel, 0);
        guildHeaderLabel->setVisible(!guildHeaderLabel->text().isEmpty());
        channelTree->show();
        v->addWidget(channelTree, 1);
#ifndef ACHERON_NO_VOICE
        if (voiceStatusBar)
            v->addWidget(voiceStatusBar, 0);
#endif
        v->addWidget(customStatusLabel, 0);
        h->addWidget(secondary, 1);
    } else {
        auto *v = new QVBoxLayout(container);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        channelTree->show();
        v->addWidget(channelTree, 1);
#ifndef ACHERON_NO_VOICE
        if (voiceStatusBar)
            v->addWidget(voiceStatusBar, 0);
#endif
        v->addWidget(customStatusLabel, 0);
    }

    container->setMinimumWidth(200);
    return container;
}

void MainWindow::setChannelListMode(ChannelListMode mode)
{
    if (channelListMode == mode)
        return;
    channelListMode = mode;
    QSettings().setValue("ui/channelListMode", mode == ChannelListMode::Classic ? "classic" : "tree");

    QList<int> splitterSizes = mainSplitter ? mainSplitter->sizes() : QList<int>();
#ifndef ACHERON_NO_VOICE
    bool voiceVisible = voiceStatusBar && voiceStatusBar->isVisible();
#endif

    channelTree->setParent(this);
    channelTree->hide();
    serverRail->setParent(this);
    serverRail->hide();
    guildHeaderLabel->setParent(this);
    guildHeaderLabel->hide();
    customStatusLabel->setParent(this);
    customStatusLabel->hide();
#ifndef ACHERON_NO_VOICE
    if (voiceStatusBar) {
        voiceStatusBar->setParent(this);
        voiceStatusBar->hide();
    }
#endif

    QWidget *newLeft = buildLeftSide();
    QWidget *old = mainSplitter->replaceWidget(0, newLeft);
    leftSideWidget = newLeft;
    if (old)
        old->deleteLater();

    mainSplitter->setCollapsible(0, false);
    mainSplitter->setStretchFactor(0, 0);
    if (splitterSizes.size() == mainSplitter->count()) {
        const int delta = (mode == ChannelListMode::Classic) ? ServerRailDelegate::RailWidth : -ServerRailDelegate::RailWidth;
        splitterSizes[0] = qMax(0, splitterSizes[0] + delta);
        if (splitterSizes.size() > 1)
            splitterSizes[1] = qMax(0, splitterSizes[1] - delta);
        mainSplitter->setSizes(splitterSizes);
    }

#ifndef ACHERON_NO_VOICE
    if (voiceStatusBar)
        voiceStatusBar->setVisible(voiceVisible);
#endif

    if (mode == ChannelListMode::Tree) {
        channelTree->setRootIndex({});
        channelTree->performDefaultExpansion();
        applyTreeState();
    } else {
        serverRailModel->rebuild();
        channelController->railHasSelection = false;
        selectInitialRailItem();
    }
}

void MainWindow::onRailGuildSelected(Snowflake accountId, Snowflake guildId)
{
    channelController->onRailGuildSelected(accountId, guildId);
}

void MainWindow::onRailAccountHomeSelected(Snowflake accountId)
{
    channelController->onRailAccountHomeSelected(accountId);
}

void MainWindow::onRailAccountHomeClicked(Snowflake accountId)
{
    channelController->onRailAccountHomeClicked(accountId);
}

void MainWindow::selectInitialRailItem()
{
    channelController->selectInitialRailItem();
}

void MainWindow::applyPendingRailSelection(Snowflake accountId)
{
    channelController->applyPendingRailSelection(accountId);
}

void MainWindow::onRailGuildClicked(Snowflake accountId, Snowflake guildId)
{
    channelController->onRailGuildClicked(accountId, guildId);
}

Core::Snowflake MainWindow::resolveRailChannel(Snowflake accountId, Snowflake guildId)
{
    return channelController->resolveRailChannel(accountId, guildId);
}

bool MainWindow::channelReadable(Snowflake accountId, Snowflake guildId, Snowflake channelId)
{
    return channelController->channelReadable(accountId, guildId, channelId);
}

Core::Snowflake MainWindow::firstReadableChannel(Snowflake accountId, Snowflake guildId)
{
    return channelController->firstReadableChannel(accountId, guildId);
}

void MainWindow::markIndexAsRead(Snowflake accountId, const QModelIndex &sourceIndex)
{
    channelController->markIndexAsRead(accountId, sourceIndex);
}

void MainWindow::recordLastViewedChannel(Snowflake accountId, Snowflake guildId, Snowflake channelId)
{
    channelController->recordLastViewedChannel(accountId, guildId, channelId);
}

void MainWindow::recordRecentChannel(const TabEntry &entry)
{
    channelController->recordRecentChannel(entry);
}

#ifndef ACHERON_NO_VOICE
void MainWindow::updateVoiceStatusLabel()
{
    voiceController->updateVoiceStatusLabel();
}
#endif

void MainWindow::switchToTabEntry(const TabEntry &entry)
{
    channelController->switchToTabEntry(entry);
}

void MainWindow::activateChannel(const TabEntry &entry)
{
    channelController->activateChannel(entry);
}

void MainWindow::refreshTabReadStates()
{
    channelController->refreshTabReadStates();
}

namespace {

bool isNativeExpandable(ChannelNode::Type type)
{
    return type == ChannelNode::Type::Account ||
           type == ChannelNode::Type::DMHeader ||
           type == ChannelNode::Type::Folder ||
           type == ChannelNode::Type::Server;
}

// <accountId>:<type>:<nodeId>
QStringList mergeTreeKeys(const QStringList &previous, const QStringList &captured,
                          const QSet<QString> &presentAccounts)
{
    QStringList result;
    for (const QString &key : previous) {
        if (!presentAccounts.contains(key.section(':', 0, 0)))
            result.append(key);
    }
    result.append(captured);
    return result;
}

} // namespace

void MainWindow::forEachSourceNode(const std::function<void(const QModelIndex &, ChannelNode *)> &fn) const
{
    std::function<void(const QModelIndex &)> walk = [&](const QModelIndex &parent) {
        int rows = channelTreeModel->rowCount(parent);
        for (int i = 0; i < rows; ++i) {
            QModelIndex idx = channelTreeModel->index(i, 0, parent);
            if (ChannelNode *node = channelTreeModel->nodeFromIndex(idx))
                fn(idx, node);
            walk(idx);
        }
    };
    walk({});
}

QString MainWindow::treeNodeKey(const ChannelNode *node) const
{
    const ChannelNode *acct = node;
    while (acct && acct->type != ChannelNode::Type::Account)
        acct = acct->parent;

    quint64 acctId = acct ? static_cast<quint64>(acct->id) : 0;
    return QStringLiteral("%1:%2:%3")
            .arg(acctId)
            .arg(static_cast<int>(node->type))
            .arg(static_cast<quint64>(node->id));
}

void MainWindow::captureTreeState(QStringList &expanded,
                                  QStringList &collapsed,
                                  QStringList &collapsedCategories,
                                  QSet<QString> &presentAccounts) const
{
    forEachSourceNode([&](const QModelIndex &sourceIndex, ChannelNode *node) {
        if (node->type == ChannelNode::Type::Account)
            presentAccounts.insert(QString::number(static_cast<quint64>(node->id)));

        if (node->type == ChannelNode::Type::Category) {
            if (node->collapsed)
                collapsedCategories.append(treeNodeKey(node));
            return;
        }

        if (!isNativeExpandable(node->type))
            return;

        QModelIndex proxyIndex = channelFilterProxy->mapFromSource(sourceIndex);
        if (!proxyIndex.isValid())
            return;

        if (channelTree->isExpanded(proxyIndex))
            expanded.append(treeNodeKey(node));
        else
            collapsed.append(treeNodeKey(node));
    });
}

void MainWindow::applyTreeState()
{
    if (!hasSavedTreeState)
        return;

    // first restore the pseudo-collapsed stuff for categories
    forEachSourceNode([this](const QModelIndex &sourceIndex, ChannelNode *node) {
        if (node->type == ChannelNode::Type::Category)
            channelTreeModel->setCollapsed(sourceIndex, savedCollapsedCategories.contains(treeNodeKey(node)));
    });
    channelFilterProxy->invalidateFilter();

    // the rest
    forEachSourceNode([this](const QModelIndex &sourceIndex, ChannelNode *node) {
        if (!isNativeExpandable(node->type))
            return;

        QString key = treeNodeKey(node);
        QModelIndex proxyIndex = channelFilterProxy->mapFromSource(sourceIndex);
        if (!proxyIndex.isValid())
            return;

        if (savedExpandedNodes.contains(key))
            channelTree->expand(proxyIndex);
        else if (savedCollapsedNodes.contains(key))
            channelTree->collapse(proxyIndex);
    });
}

void MainWindow::maybeActivatePendingChannel(Core::Snowflake accountId)
{
    channelController->maybeActivatePendingChannel(accountId);
}

void MainWindow::saveWindowState()
{
    QSettings settings;

    settings.setValue("layout/geometry", saveGeometry());
    if (mainSplitter)
        settings.setValue("layout/splitter", mainSplitter->saveState());

    const QList<TabEntry> all = tabBar->tabEntries();
    int activeIndex = tabBar->activeTabIndex();
    QList<TabEntry> valid;
    int newActive = 0;
    for (int i = 0; i < all.size(); ++i) {
        if (!all[i].channelId.isValid())
            continue;
        if (i == activeIndex)
            newActive = valid.size();
        valid.append(all[i]);
    }

    settings.remove("layout/tabs");
    settings.beginWriteArray("layout/tabs");
    for (int i = 0; i < valid.size(); ++i) {
        settings.setArrayIndex(i);
        const TabEntry &entry = valid[i];
        settings.setValue("channelId", static_cast<quint64>(entry.channelId));
        settings.setValue("guildId", static_cast<quint64>(entry.guildId));
        settings.setValue("accountId", static_cast<quint64>(entry.accountId));
        settings.setValue("name", entry.name);
        settings.setValue("iconUrl", entry.iconUrl);
        settings.setValue("isDm", entry.isDm);
        settings.setValue("pinned", entry.pinned);
        settings.setValue("isForum", entry.isForum);
    }
    settings.endArray();
    settings.setValue("layout/activeTab", newActive);

    QStringList expanded, collapsed, collapsedCategories;
    QSet<QString> presentAccounts;
    captureTreeState(expanded, collapsed, collapsedCategories, presentAccounts);

    if (channelListMode == ChannelListMode::Tree) {
        settings.setValue("layout/tree/expanded",
                          mergeTreeKeys(settings.value("layout/tree/expanded").toStringList(),
                                        expanded,
                                        presentAccounts));
        settings.setValue("layout/tree/collapsed",
                          mergeTreeKeys(settings.value("layout/tree/collapsed").toStringList(),
                                        collapsed,
                                        presentAccounts));
    }
    settings.setValue("layout/tree/collapsedCategories",
                      mergeTreeKeys(settings.value("layout/tree/collapsedCategories").toStringList(),
                                    collapsedCategories,
                                    presentAccounts));

    settings.setValue("ui/channelListMode", channelListMode == ChannelListMode::Classic ? "classic" : "tree");
    if (serverRailModel)
        settings.setValue("ui/rail/expandedFolders", serverRailModel->expandedFolderKeys());

    settings.remove("ui/rail/lastChannels");
    settings.beginWriteArray("ui/rail/lastChannels");
    int lastChannelIdx = 0;
    for (auto it = channelController->lastViewedChannel.cbegin(); it != channelController->lastViewedChannel.cend(); ++it) {
        settings.setArrayIndex(lastChannelIdx++);
        settings.setValue("key", it.key());
        settings.setValue("channel", static_cast<qulonglong>(it.value()));
    }
    settings.endArray();

    settings.remove("ui/recentChannels");
    settings.beginWriteArray("ui/recentChannels");
    for (int i = 0; i < channelController->recentChannels.size(); ++i) {
        settings.setArrayIndex(i);
        const TabEntry &entry = channelController->recentChannels[i];
        settings.setValue("channelId", static_cast<qulonglong>(entry.channelId));
        settings.setValue("guildId", static_cast<qulonglong>(entry.guildId));
        settings.setValue("accountId", static_cast<qulonglong>(entry.accountId));
        settings.setValue("name", entry.name);
        settings.setValue("iconUrl", entry.iconUrl);
        settings.setValue("isDm", entry.isDm);
    }
    settings.endArray();
}

void MainWindow::restoreWindowState()
{
    QSettings settings;

    if (settings.contains("layout/geometry"))
        restoreGeometry(settings.value("layout/geometry").toByteArray());
    if (mainSplitter && settings.contains("layout/splitter"))
        mainSplitter->restoreState(settings.value("layout/splitter").toByteArray());

    const auto toSet = [](const QStringList &list) {
        return QSet<QString>(list.cbegin(), list.cend());
    };
    savedExpandedNodes = toSet(settings.value("layout/tree/expanded").toStringList());
    savedCollapsedNodes = toSet(settings.value("layout/tree/collapsed").toStringList());
    savedCollapsedCategories = toSet(settings.value("layout/tree/collapsedCategories").toStringList());
    hasSavedTreeState = !savedExpandedNodes.isEmpty() ||
                        !savedCollapsedNodes.isEmpty() ||
                        !savedCollapsedCategories.isEmpty();

    savedRailAccountId = Core::Snowflake(settings.value("ui/rail/lastAccountId").toULongLong());
    savedRailGuildId = Core::Snowflake(settings.value("ui/rail/lastGuildId").toULongLong());
    savedRailIsHome = settings.value("ui/rail/lastIsHome", true).toBool();
    hasSavedRailSelection = settings.contains("ui/rail/lastAccountId");
    if (serverRailModel)
        serverRailModel->setExpandedFolderKeys(settings.value("ui/rail/expandedFolders").toStringList());

    int lastChannelCount = settings.beginReadArray("ui/rail/lastChannels");
    for (int i = 0; i < lastChannelCount; ++i) {
        settings.setArrayIndex(i);
        QString key = settings.value("key").toString();
        if (!key.isEmpty())
            channelController->lastViewedChannel.insert(key, Core::Snowflake(settings.value("channel").toULongLong()));
    }
    settings.endArray();

    int recentCount = settings.beginReadArray("ui/recentChannels");
    for (int i = 0; i < recentCount; ++i) {
        settings.setArrayIndex(i);
        TabEntry entry;
        entry.channelId = Core::Snowflake(settings.value("channelId").toULongLong());
        entry.guildId = Core::Snowflake(settings.value("guildId").toULongLong());
        entry.accountId = Core::Snowflake(settings.value("accountId").toULongLong());
        entry.name = settings.value("name").toString();
        entry.iconUrl = settings.value("iconUrl").toUrl();
        entry.isDm = settings.value("isDm").toBool();
        if (entry.channelId.isValid())
            channelController->recentChannels.append(entry);
    }
    settings.endArray();

    QList<TabEntry> entries;
    int count = settings.beginReadArray("layout/tabs");
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        TabEntry entry;
        entry.channelId = Core::Snowflake(settings.value("channelId").toULongLong());
        entry.guildId = Core::Snowflake(settings.value("guildId").toULongLong());
        entry.accountId = Core::Snowflake(settings.value("accountId").toULongLong());
        entry.name = settings.value("name").toString();
        entry.iconUrl = settings.value("iconUrl").toUrl();
        entry.isDm = settings.value("isDm").toBool();
        entry.pinned = settings.value("pinned").toBool();
        entry.isForum = settings.value("isForum").toBool();
        if (entry.channelId.isValid())
            entries.append(entry);
    }
    settings.endArray();

    if (!entries.isEmpty()) {
        int activeTab = qBound(0, settings.value("layout/activeTab", 0).toInt(), entries.size() - 1);
        tabBar->restoreTabs(entries, activeTab);
        // wait for READY
        channelController->pendingActiveEntry = entries[activeTab];
    }
}

void MainWindow::setupMenu()
{
    auto *menuBar = this->menuBar();
    QMenu *viewMenu = menuBar->addMenu(tr("&View"));
    auto openQuickSwitch = [this]() {
        Discord::Client *discordClient = nullptr;
        if (currentInstance)
            discordClient = currentInstance->discord();
        ChannelQuickSwitch dialog(channelTreeModel, serverRailModel, currentChannelEntry(),
                                  channelController->recentChannels, discordClient, this);
        if (dialog.exec() != QDialog::Accepted)
            return;

        TabEntry entry = dialog.selectedEntry();
        if (!entry.channelId.isValid())
            return;

        if (!currentInstance || currentInstance->accountId() != entry.accountId) {
            if (auto *instance = session->client(entry.accountId))
                switchActiveInstance(instance);
        }
        tabBar->openNewTab(entry);
    };

    auto handleNewTab = [this, openQuickSwitch]() {
        const QString behavior = QSettings().value("ui/newTabBehavior", "picker").toString();
        if (behavior == QLatin1String("duplicate")) {
            TabEntry entry = currentChannelEntry();
            if (entry.channelId.isValid())
                tabBar->openNewTab(entry);
            return;
        }
        openQuickSwitch();
    };

    auto *newTabAction = new QAction(tr("New &Tab"), this);
    newTabAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    connect(newTabAction, &QAction::triggered, this, handleNewTab);
    viewMenu->addAction(newTabAction);

    auto *newWindowAction = new QAction(tr("New &Window"), this);
    newWindowAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    connect(newWindowAction, &QAction::triggered, this, [this]() {
        openDetachedWindow(false);
    });
    viewMenu->addAction(newWindowAction);

    auto *tileWindowAction = new QAction(tr("&Tile Window"), this);
    tileWindowAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
    connect(tileWindowAction, &QAction::triggered, this, [this]() {
        openDetachedWindow(true);
    });
    viewMenu->addAction(tileWindowAction);

    viewMenu->addSeparator();

    auto *accountsAction = new QAction(tr("&Accounts"), this);
    connect(accountsAction, &QAction::triggered, this, &MainWindow::openAccountsWindow);
    viewMenu->addAction(accountsAction);

    auto *friendsAction = new QAction(tr("&Friends"), this);
    connect(friendsAction, &QAction::triggered, this, &MainWindow::openFriendsWindow);
    viewMenu->addAction(friendsAction);

    auto *editProfileAction = new QAction(tr("Edit &Profile"), this);
    connect(editProfileAction, &QAction::triggered, this, [this]() {
        if (currentInstance && currentInstance->discord()) {
            auto *dialog = new EditProfileDialog(currentInstance->discord(), this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->exec();
        }
    });
    viewMenu->addAction(editProfileAction);

    auto *settingsAction = new QAction(tr("&Settings"), this);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettingsWindow);
    viewMenu->addAction(settingsAction);

    windowMenu = menuBar->addMenu(tr("&Window"));
    connect(windowMenu, &QMenu::aboutToShow, this, &MainWindow::populateWindowMenu);

    auto *quickSwitchAction = new QAction(tr("Quick &Switch"), this);
    quickSwitchAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    addAction(quickSwitchAction);
    connect(quickSwitchAction, &QAction::triggered, this, openQuickSwitch);
    connect(tabBar, &TabBar::addTabRequested, this, handleNewTab);

    auto *shortcutSheetAction = new QAction(tr("Keyboard &Shortcuts"), this);
    shortcutSheetAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Slash));
    shortcutSheetAction->setShortcutContext(Qt::ApplicationShortcut);
    addAction(shortcutSheetAction);
    connect(shortcutSheetAction, &QAction::triggered, this, [this]() {
        ShortcutSheet dialog(this);
        dialog.exec();
    });
#ifndef QT_NO_DEBUG

    // DEBUG: Ctrl+Shift+R to force a Gateway reconnect
    auto *debugReconnect = new QAction(this);
    debugReconnect->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    addAction(debugReconnect);
    connect(debugReconnect, &QAction::triggered, this, [this]() {
        if (currentInstance)
            currentInstance->discord()->debugForceReconnect();
    });
#endif
}

void MainWindow::populateWindowMenu()
{
    windowManager->populateWindowMenu();
}

void MainWindow::mergeAllWindows()
{
    windowManager->mergeAllWindows();
}

void MainWindow::closeAllWindows()
{
    windowManager->closeAllWindows();
}

TabEntry MainWindow::currentChannelEntry() const
{
    return windowManager->currentChannelEntry();
}

void MainWindow::openChannelInNewWindow(const TabEntry &entry, bool tileToSide)
{
    windowManager->openChannelInNewWindow(entry, tileToSide);
}

void MainWindow::openDetachedWindow(bool tileToSide)
{
    windowManager->openDetachedWindow(tileToSide);
}

void MainWindow::setDetachedWindow(bool detached)
{
    windowManager->setDetachedWindow(detached);
}

void MainWindow::openAccountsWindow()
{
    windowManager->openAccountsWindow();
}

void MainWindow::openSettingsWindow()
{
    windowManager->openSettingsWindow();
}

void MainWindow::applyCustomStatus(const QString &status)
{
    if (!customStatusLabel)
        return;

    QString trimmed = status.trimmed();
    customStatusLabel->setText(trimmed.isEmpty() ? QString() : tr("Status: %1").arg(trimmed));
    customStatusLabel->setVisible(!trimmed.isEmpty());
}

void MainWindow::openFriendsWindow()
{
    windowManager->openFriendsWindow();
}

void MainWindow::onTypingStart(const Discord::TypingStart &event)
{
    if (!currentInstance)
        return;

    if (event.member.hasValue() && event.guildId.hasValue()) {
        currentInstance->users()->saveMemberWithUser(event.guildId.get(), event.member.get());
    }

    std::optional<Snowflake> guildId =
            event.guildId.hasValue() ? std::optional(event.guildId.get()) : std::nullopt;

    typingTracker->addTyper(event.channelId.get(), event.userId.get(), guildId);
}

void MainWindow::onChannelPermissionsChanged(Core::Snowflake channelId)
{
    if (chatModel->getActiveChannelId() != channelId)
        return;

    if (!currentInstance)
        return;

    Core::Snowflake userId = currentInstance->accountId();
    bool canSend = currentInstance->permissions()->hasChannelPermission(
            userId, channelId, Discord::Permission::SEND_MESSAGES);
    bool canPin = currentInstance->permissions()->hasChannelPermission(
            userId, channelId, Discord::Permission::PIN_MESSAGES);
    bool canManage = currentInstance->permissions()->hasChannelPermission(
            userId, channelId, Discord::Permission::MANAGE_MESSAGES);

    int rateLimit = currentInstance->getChannelRateLimit(channelId);
    bool canBypass = currentInstance->permissions()->hasChannelPermission(
            userId, channelId, Discord::Permission::BYPASS_SLOWMODE);
    slowModeIndicator->setSlowMode(channelId, rateLimit, canBypass);

    bool onCooldown = rateLimit > 0 && !canBypass && slowModeIndicator->isOnCooldown(channelId);
    messageInput->setEnabled(canSend);
    messageInput->setSendBlocked(onCooldown);
    chatView->setCanPinMessages(canPin);
    chatView->setCanManageMessages(canManage);

    if (!canSend) {
        messageInput->setPlaceholder("You do not have permission to send messages");
    } else if (onCooldown) {
        messageInput->setPlaceholder("Slowmode is active");
    } else {
        QString channelName;
        QModelIndex current = channelTree->currentIndex();
        if (current.isValid()) {
            auto *node = channelTreeModel->nodeFromIndex(channelFilterProxy->mapToSource(current));
            if (node)
                channelName = node->name;
        }
        if (channelName.isEmpty())
            channelName = tabBar->activeTabName();
        if (!channelName.isEmpty())
            messageInput->setPlaceholder("Message #" + channelName);
    }
}

QColor MainWindow::resolveRoleColor(Snowflake userId, Snowflake guildId)
{
    return channelController->resolveRoleColor(userId, guildId);
}

void MainWindow::refreshGuildRoleData(Snowflake guildId)
{
    channelController->refreshGuildRoleData(guildId);
}

void MainWindow::showUserContextMenu(Snowflake userId, Snowflake guildId, QPoint globalPos)
{
    contextMenuFactory->showUserContextMenu(userId, guildId, globalPos);
}

void MainWindow::selectChannelInTree(Snowflake channelId)
{
    channelController->selectChannelInTree(channelId);
}

void MainWindow::showUserProfile(Core::Snowflake userId, Core::Snowflake guildId)
{
    windowManager->showUserProfile(userId, guildId);
}

void MainWindow::setThreadBrowserTarget(Core::Snowflake channelId)
{
    channelController->setThreadBrowserTarget(channelId);
}

void MainWindow::openThreadBrowser()
{
    channelController->openThreadBrowser();
}

void MainWindow::openPinnedMessages()
{
    channelController->openPinnedMessages();
}

void MainWindow::navigateToChannel(Core::Snowflake channelId)
{
    channelController->navigateToChannel(channelId);
}

} // namespace UI
} // namespace Acheron
