#include "ChannelSelectionController.hpp"

#include <QSettings>
#include <QPointer>
#include <QSignalBlocker>

#include "MainWindow.hpp"
#include "NotificationController.hpp"
#include "Chat/ChatModel.hpp"
#include "Chat/ChatView.hpp"
#include "UI/Widgets/Chat/MentionAutocompletePopup.hpp"
#include "Forum/ForumBrowser.hpp"
#include "Forum/ForumPostModel.hpp"
#include "Forum/NewPostDialog.hpp"
#include "ThreadBrowser/ThreadBrowserPopup.hpp"
#include "Core/ForumManager.hpp"
#include "Core/ReadStateManager.hpp"
#include "ChannelList/ChannelTreeModel.hpp"
#include "ChannelList/ChannelFilterProxyModel.hpp"
#include "ChannelList/ChannelTreeView.hpp"
#include "ChannelList/ServerRailModel.hpp"
#include "TabBar/TabBar.hpp"
#include "Dialogs/PinnedMessagesPanel.hpp"
#include "Input/MessageInput.hpp"
#include "SlowModeIndicator.hpp"
#include "MemberList/MemberListView.hpp"
#include "Core/ClientInstance.hpp"
#include "Core/Session.hpp"
#include "Core/TypingTracker.hpp"
#include "Core/Logging.hpp"
#include "Discord/CdnUrls.hpp"

using namespace Acheron::Core;

namespace Acheron {
namespace UI {

namespace {

QString guildChannelKey(Core::Snowflake accountId, Core::Snowflake guildId)
{
    return QStringLiteral("%1:%2")
            .arg(static_cast<quint64>(accountId))
            .arg(static_cast<quint64>(guildId));
}

} // namespace

ChannelSelectionController::ChannelSelectionController(MainWindow *window)
    : QObject(window), m_window(window)
{
}

void ChannelSelectionController::onChannelSelectionChanged(const QModelIndex &current,
                                                           const QModelIndex &previous)
{
    if (!current.isValid())
        return;

    QModelIndex sourceIndex = m_window->channelFilterProxy->mapToSource(current);
    auto node = static_cast<ChannelNode *>(sourceIndex.internalPointer());

    if (!node || !node->opensChat())
        return;

    ChannelNode *accountNode = m_window->channelTreeModel->getAccountNodeFor(node);
    if (!accountNode) {
        m_window->messageInput->setEnabled(false);
        return;
    }

    m_window->channelFilterProxy->setSelectedChannel(node->id, accountNode->id);
    m_window->channelTree->viewport()->update();

    ClientInstance *selectedInstance = m_window->session->client(accountNode->id);
    if (!selectedInstance) {
        m_window->messageInput->setEnabled(false);
        return;
    }

    if (selectedInstance != m_window->currentInstance)
        m_window->switchActiveInstance(selectedInstance);

    m_window->channelTreeModel->clearTemporaryThread(node->id);

    ChannelNode *forumNode = node->type == ChannelNode::Type::Thread ? node->parent : node;

    if (forumNode && forumNode->type == ChannelNode::Type::Forum) {
        ChannelNode *gNode = ChannelTreeModel::findGuildNode(node);
        Snowflake gId = gNode ? gNode->id : Snowflake::Invalid;

        setThreadBrowserTarget(Snowflake::Invalid);
        openForumChannel(selectedInstance, forumNode->id, gId);
        m_window->tabBar->updateCurrentTab(makeTabEntry(forumNode, accountNode));

        if (gNode)
            recordLastViewedChannel(accountNode->id, gNode->id, forumNode->id);

        if (node->type == ChannelNode::Type::Thread)
            openForumPost(node->id, gId);
        return;
    }

    // Update notification manager active channel
    m_window->notificationController->setActiveChannel(node->id);

    selectedInstance->readState()->setActiveChannel(node->id);
    setViewMode(ViewMode::TextChannel);

    ChannelNode *guildNode = ChannelTreeModel::findGuildNode(node);
    Snowflake guildId = guildNode ? guildNode->id : Snowflake::Invalid;
    bool isDm = node->type == ChannelNode::Type::DMChannel;

    applyChannelChrome(selectedInstance, node->id, node->name, isDm, guildId);
    switchChatChannel(node->id, guildId);
    selectedInstance->messages()->requestLoadChannel(node->id);

    if (node->isUnread && node->lastMessageId.isValid())
        selectedInstance->readState()->markChannelAsRead(node->id, node->lastMessageId);

    m_window->tabBar->updateCurrentTab(makeTabEntry(node, accountNode));

    if (node->type == ChannelNode::Type::Channel && guildNode)
        recordLastViewedChannel(accountNode->id, guildNode->id, node->id);
    else if (isDm)
        recordLastViewedChannel(accountNode->id, Snowflake::Invalid, node->id);
}

void ChannelSelectionController::applyChannelChrome(Core::ClientInstance *instance,
                                                    Core::Snowflake channelId,
                                                    const QString &name, bool isDm,
                                                    Core::Snowflake guildId)
{
    Snowflake userId = instance->accountId();
    m_window->messageInput->setMaxUploadSize(instance->discord()->getMaxUploadSize(channelId));

    Snowflake threadParentId = Snowflake::Invalid;
    bool archived = false;
    if (auto ch = instance->getChannel(channelId); ch && ch->isThread()) {
        threadParentId = ch->parentId.hasValue() ? ch->parentId.get() : Snowflake::Invalid;
        archived = ch->isArchived();
    }
    bool isThread = threadParentId.isValid();

    m_window->setChannelName(isDm ? QStringLiteral("@") + name
                                  : (isThread ? name : QStringLiteral("#") + name));

    setThreadBrowserTarget(isDm ? Snowflake::Invalid : (isThread ? threadParentId : channelId));

    if (isDm) {
        m_window->messageInput->setEnabled(true);
        m_window->messageInput->setSendBlocked(false);
        m_window->messageInput->setPlaceholder("Message @" + name);
        m_window->chatView->setCanPinMessages(true);
        m_window->chatView->setCanManageMessages(false);
        m_window->slowModeIndicator->setSlowMode(channelId, 0, false);
        setMemberListVisible(false);
        instance->memberList()->clear();
        return;
    }

    Snowflake permChannel = isThread ? threadParentId : channelId;
    Discord::Permission sendPerm = isThread ? Discord::Permission::SEND_MESSAGES_IN_THREADS : Discord::Permission::SEND_MESSAGES;

    bool canSend = instance->permissions()->hasChannelPermission(userId, permChannel, sendPerm);
    bool canPin = instance->permissions()->hasChannelPermission(userId, permChannel, Discord::Permission::PIN_MESSAGES);
    bool canManage = instance->permissions()->hasChannelPermission(userId, permChannel, Discord::Permission::MANAGE_MESSAGES);

    int rateLimit = instance->getChannelRateLimit(channelId);
    bool canBypass = instance->permissions()->hasChannelPermission(userId, permChannel, Discord::Permission::BYPASS_SLOWMODE);
    m_window->slowModeIndicator->setSlowMode(channelId, rateLimit, canBypass);

    bool onCooldown = rateLimit > 0 && !canBypass && m_window->slowModeIndicator->isOnCooldown(channelId);
    m_window->messageInput->setEnabled(canSend && !archived);
    m_window->messageInput->setSendBlocked(onCooldown);
    m_window->chatView->setCanPinMessages(canPin);
    m_window->chatView->setCanManageMessages(canManage);

    if (archived)
        m_window->messageInput->setPlaceholder("This thread is archived");
    else if (!canSend)
        m_window->messageInput->setPlaceholder("You do not have permission to send messages");
    else if (onCooldown)
        m_window->messageInput->setPlaceholder("Slowmode is active");
    else
        m_window->messageInput->setPlaceholder((isThread ? "Message " : "Message #") + name);

    if (guildId.isValid()) {
        setMemberListVisible(true);
        instance->memberList()->setActiveChannel(guildId, isThread ? threadParentId : channelId);
    }
}

void ChannelSelectionController::switchChatChannel(Core::Snowflake channelId,
                                                   Core::Snowflake guildId)
{
    // Keep the emoji picker in sync with the composing context: the current
    // guild floats to the top of its Server tab and the remaining guilds
    // follow the guild-sidebar order.
    if (m_window->currentInstance) {
        const QStringList order =
                m_window->channelTreeModel->orderedGuildIds(m_window->currentInstance->accountId());
        m_window->messageInput->setGuildOrder(order);
        m_window->chatView->setGuildOrder(order);
    }
    m_window->messageInput->setCurrentGuildId(guildId);
    m_window->chatView->setCurrentGuildId(guildId);

    if (channelId == m_window->chatModel->getActiveChannelId())
        return;

    if (guildId != cachedGuildId) {
        cachedGuildId = guildId;
        userColorCache.clear();
    }

    // Snapshot the outgoing channel and fade it out to smooth the switch.
    m_window->chatView->beginChannelCrossfade();

    m_window->chatModel->setActiveChannel(channelId, guildId);
    m_window->typingTracker->setActiveChannel(channelId);
    m_window->messageInput->clearReplyTarget();

    // Build the mention autocomplete list (`@` roles, `#` channels) for the
    // newly active guild.
    if (m_window->currentInstance) {
        QList<MentionItem> mentions;

        const auto channels = m_window->currentInstance->getChannelsForGuild(guildId);
        for (const auto &channel : channels) {
            const auto type = channel.type.get();
            switch (type) {
            case Discord::ChannelType::GUILD_TEXT:
            case Discord::ChannelType::GUILD_VOICE:
            case Discord::ChannelType::GUILD_NEWS:
            case Discord::ChannelType::GUILD_FORUM:
            case Discord::ChannelType::GUILD_MEDIA:
            case Discord::ChannelType::GUILD_STAGE_VOICE:
            case Discord::ChannelType::NEWS_THREAD:
            case Discord::ChannelType::PUBLIC_THREAD:
            case Discord::ChannelType::PRIVATE_THREAD:
                break;
            default:
                continue;
            }
            MentionItem item;
            item.id = channel.id.get();
            item.name = channel.name.getOr(QString());
            item.kind = MentionItem::Kind::Channel;
            mentions.append(item);
        }

        const auto roles = m_window->currentInstance->getRolesForGuild(guildId);
        for (const auto &role : roles) {
            if (role.name.get().isEmpty())
                continue;
            MentionItem item;
            item.id = role.id.get();
            item.name = role.name.get();
            item.kind = MentionItem::Kind::Role;
            mentions.append(item);
        }

        // '@' mentions: prefer the guild's actual member ids (cached), falling
        // back to the global user cache. Cap the list so channel switches stay
        // fast; the inserted `<@id>` is what matters.
        QList<Core::Snowflake> userIds = m_window->currentInstance->guildMemberIds(guildId);
        if (userIds.isEmpty())
            userIds = m_window->currentInstance->users()->cachedUserIds();
        constexpr int kMaxUserMentions = 100;
        int userMentionsAdded = 0;
        for (const auto userId : userIds) {
            if (userId == m_window->currentInstance->accountId())
                continue;
            auto user = m_window->currentInstance->users()->getUser(userId);
            if (!user || user->username.get().isEmpty())
                continue;
            MentionItem item;
            item.id = userId;
            item.name = user->getDisplayName();
            item.kind = MentionItem::Kind::User;
            mentions.append(item);
            if (++userMentionsAdded >= kMaxUserMentions)
                break;
        }

        m_window->messageInput->setAvailableMentions(mentions);
    }

    // Fetch slash commands available in this channel for `/` autocomplete.
    // Clear the previous channel's list synchronously so stale commands don't
    // linger while the fetch is in flight or on error.
    m_window->messageInput->setAvailableCommands({});
    if (m_window->currentInstance && m_window->currentInstance->discord()) {
        m_window->currentInstance->discord()->fetchApplicationCommands(
                channelId, QString(),
                [guard = QPointer<ChannelSelectionController>(this),
                 channelId](const Core::Result<QList<Discord::ApplicationCommand>> &res) {
                    // The controller (and its MainWindow) can be destroyed while
                    // the HTTP response is in flight (detached windows).
                    if (guard.isNull())
                        return;
                    if (!guard->m_window->currentInstance)
                        return;
                    // Drop results for a channel we've since navigated away from.
                    if (guard->m_window->chatModel->getActiveChannelId() != channelId)
                        return;
                    if (res.success())
                        guard->m_window->messageInput->setAvailableCommands(*res.value);
                });
    }
}

void ChannelSelectionController::setViewMode(ViewMode mode)
{
    viewMode = mode;

    if (m_window->currentInstance)
        m_window->currentInstance->forums()->setCurrentForum(
                mode == ViewMode::TextChannel ? Core::Snowflake() : currentForumId);

    switch (mode) {
    case ViewMode::TextChannel:
        m_window->forumBrowser->setVisible(false);
        m_window->threadPane->setVisible(true);
        m_window->threadHeader->setVisible(false);
        break;
    case ViewMode::ForumBrowse:
        m_window->forumBrowser->setVisible(true);
        m_window->threadPane->setVisible(false);
        m_window->threadHeader->setVisible(false);
        break;
    case ViewMode::ForumSplit:
        m_window->forumBrowser->setVisible(true);
        m_window->threadPane->setVisible(true);
        m_window->threadHeader->setVisible(true);
        m_window->popOutButton->setText(tr("Open as channel"));
        break;
    case ViewMode::ThreadPopout:
        m_window->forumBrowser->setVisible(false);
        m_window->threadPane->setVisible(true);
        m_window->threadHeader->setVisible(true);
        m_window->popOutButton->setText(tr("Back to forum"));
        break;
    }

    updateMemberListVisibility();
}

void ChannelSelectionController::setMemberListVisible(bool visible)
{
    memberListWanted = visible;
    updateMemberListVisibility();
}

void ChannelSelectionController::updateMemberListVisibility()
{
    m_window->memberListView->setVisible(memberListWanted && viewMode == ViewMode::TextChannel);
}

TabEntry ChannelSelectionController::makeTabEntry(ChannelNode *node, ChannelNode *accountNode)
{
    ChannelNode *guildNode = ChannelTreeModel::findGuildNode(node);

    TabEntry entry;
    entry.channelId = node->id;
    entry.guildId = guildNode ? guildNode->id : Snowflake::Invalid;
    entry.accountId = accountNode->id;
    entry.name = node->name;
    entry.isDm = (node->type == ChannelNode::Type::DMChannel);
    entry.isForum = (node->type == ChannelNode::Type::Forum);
    if (guildNode && !guildNode->iconHash.isEmpty())
        entry.iconUrl = Discord::Cdn::guildIcon(guildNode->id, guildNode->iconHash, 64);
    return entry;
}

void ChannelSelectionController::openForumChannel(Core::ClientInstance *instance,
                                                  Core::Snowflake forumId,
                                                  Core::Snowflake guildId)
{
    instance->readState()->setActiveChannel(forumId);

    QString forumName;
    if (auto ch = instance->getChannel(forumId); ch && ch->name.hasValue())
        forumName = ch->name.get();
    m_window->setChannelName(forumName.isEmpty() ? QString() : QStringLiteral("#") + forumName);

    currentForumId = forumId;
    currentForumGuildId = guildId;
    m_window->forumBrowser->setSortMode(static_cast<int>(instance->forums()->sortMode(forumId)));
    m_window->forumModel->setForum(forumId, guildId);
    setViewMode(ViewMode::ForumBrowse);
    m_window->forumBrowser->setLoading(instance->forums()->isLoading(forumId));

    Snowflake newestPost = instance->readState()->getChannelLastMessageId(forumId);
    if (newestPost.isValid())
        instance->readState()->markChannelAsRead(forumId, newestPost);
}

void ChannelSelectionController::openForumPost(Core::Snowflake threadId, Core::Snowflake guildId)
{
    if (!m_window->currentInstance)
        return;

    setViewMode(ViewMode::ForumSplit);

    QString postName;
    if (auto ch = m_window->currentInstance->getChannel(threadId); ch && ch->name.hasValue())
        postName = ch->name.get();
    if (!postName.isEmpty())
        m_window->setChannelName(postName);

    Core::Snowflake userId = m_window->currentInstance->accountId();
    bool canSend = m_window->currentInstance->permissions()->hasChannelPermission(userId, currentForumId, Discord::Permission::SEND_MESSAGES_IN_THREADS);
    m_window->messageInput->setEnabled(canSend);
    m_window->messageInput->setSendBlocked(false);
    m_window->messageInput->setMaxUploadSize(m_window->currentInstance->discord()->getMaxUploadSize(threadId));
    m_window->messageInput->setPlaceholder(canSend ? tr("Message this post")
                                                   : tr("You do not have permission to send messages"));
    m_window->chatView->setCanPinMessages(false);
    m_window->chatView->setCanManageMessages(false);

    switchChatChannel(threadId, guildId);
    m_window->currentInstance->readState()->setActiveChannel(threadId);
    Snowflake lastMsgId = m_window->currentInstance->readState()->getChannelLastMessageId(threadId);
    m_window->currentInstance->readState()->markForumPostAsRead(threadId, lastMsgId.isValid() ? lastMsgId : threadId);
    m_window->currentInstance->messages()->requestLoadChannel(threadId);
}

void ChannelSelectionController::onNewPostRequested()
{
    if (!m_window->currentInstance || !currentForumId.isValid())
        return;

    Core::ClientInstance *instance = m_window->currentInstance;
    Core::Snowflake forumId = currentForumId;
    Core::Snowflake guildId = currentForumGuildId;

    auto *dialog = new NewPostDialog(instance->forums()->availableTags(forumId), instance->forums()->requiresTag(forumId), m_window);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setMaxUploadSize(instance->discord()->getMaxUploadSize(forumId));

    connect(dialog, &NewPostDialog::submitted, this, [this, dialog, instance, forumId, guildId]() {
        dialog->setBusy(true);

        QPointer<NewPostDialog> guard(dialog);
        QPointer<ChannelSelectionController> selfGuard(this);
        instance->discord()->createForumThread(
                forumId, dialog->title(), dialog->selectedTagIds(), dialog->content(),
                Core::Snowflake::generateNonce().toString(), dialog->attachments(),
                [this, selfGuard, guard, instance, forumId,
                 guildId](const Core::Result<Discord::Client::CreatedForumThread> &res) {
                    if (selfGuard.isNull())
                        return;
                    if (!res.success()) {
                        qCWarning(LogUI) << "Failed to create forum post:" << res.error;
                        if (guard)
                            guard->showError(tr("Could not post: %1").arg(res.error));
                        return;
                    }

                    if (guard)
                        guard->accept();

                    Core::Snowflake threadId = res.value->thread.id.get();
                    if (res.value->starterMessage)
                        instance->forums()->addStarterMessage(threadId, *res.value->starterMessage);
                    if (m_window->currentInstance == instance && currentForumId == forumId)
                        openForumPost(threadId, guildId);
                });
    });

    dialog->open();
}

void ChannelSelectionController::onRailGuildSelected(Snowflake accountId, Snowflake guildId)
{
    QModelIndex src = m_window->channelTreeModel->serverIndex(accountId, guildId);
    if (!src.isValid())
        return;
    QModelIndex proxy = m_window->channelFilterProxy->mapFromSource(src);
    if (!proxy.isValid())
        return;

    m_window->channelTree->setRootIndex(proxy);
    m_window->channelTree->performDefaultExpansion();

    m_window->serverRailModel->setSelected(ServerRailModel::Kind::Server, accountId, guildId);
    QString guildName = src.data(Qt::DisplayRole).toString();
    m_window->guildHeaderLabel->setText(guildName);
    m_window->guildHeaderLabel->setVisible(!guildName.isEmpty());
    railHasSelection = true;
    railSelectedIsHome = false;
    railSelectedAccountId = accountId;
    railSelectedGuildId = guildId;

    QSettings s;
    s.setValue("ui/rail/lastAccountId", static_cast<qulonglong>(accountId));
    s.setValue("ui/rail/lastGuildId", static_cast<qulonglong>(guildId));
    s.setValue("ui/rail/lastIsHome", false);
}

void ChannelSelectionController::onRailAccountHomeSelected(Snowflake accountId)
{
    QModelIndex src = m_window->channelTreeModel->dmHeaderIndex(accountId);
    if (!src.isValid())
        return;
    QModelIndex proxy = m_window->channelFilterProxy->mapFromSource(src);
    if (!proxy.isValid())
        return;

    m_window->channelTree->setRootIndex(proxy);

    m_window->serverRailModel->setSelected(ServerRailModel::Kind::AccountHome, accountId, accountId);
    m_window->guildHeaderLabel->setText(tr("Direct Messages"));
    m_window->guildHeaderLabel->show();
    railHasSelection = true;
    railSelectedIsHome = true;
    railSelectedAccountId = accountId;
    railSelectedGuildId = Snowflake();

    QSettings s;
    s.setValue("ui/rail/lastAccountId", static_cast<qulonglong>(accountId));
    s.setValue("ui/rail/lastIsHome", true);
}

void ChannelSelectionController::onRailAccountHomeClicked(Snowflake accountId)
{
    onRailAccountHomeSelected(accountId);

    auto it = lastViewedChannel.constFind(guildChannelKey(accountId, Snowflake::Invalid));
    if (it == lastViewedChannel.cend())
        return;

    ChannelNode *node = m_window->channelTreeModel->findChannelTreeNode(it.value(), accountId);
    if (!node || node->type != ChannelNode::Type::DMChannel)
        return;

    m_window->channelFilterProxy->setSelectedChannel(node->id, accountId);
    QModelIndex proxy = m_window->channelFilterProxy->mapFromSource(m_window->channelTreeModel->indexForNode(node));
    if (proxy.isValid())
        m_window->channelTree->setCurrentIndex(proxy);
}

void ChannelSelectionController::selectInitialRailItem()
{
    if (m_window->currentInstance) {
        Snowflake channelId = m_window->chatModel->getActiveChannelId();
        if (channelId.isValid()) {
            Snowflake accountId = m_window->currentInstance->accountId();
            if (ChannelNode *node = m_window->channelTreeModel->findChannelTreeNode(channelId, accountId)) {
                if (node->type == ChannelNode::Type::DMChannel) {
                    onRailAccountHomeSelected(accountId);
                    return;
                }
                ChannelNode *guild = ChannelTreeModel::findGuildNode(node);
                if (guild) {
                    onRailGuildSelected(accountId, guild->id);
                    return;
                }
            }
        }
    }

    if (m_window->hasSavedRailSelection) {
        if (!m_window->savedRailIsHome &&
            m_window->channelTreeModel->serverIndex(m_window->savedRailAccountId, m_window->savedRailGuildId).isValid()) {
            onRailGuildSelected(m_window->savedRailAccountId, m_window->savedRailGuildId);
            return;
        }
        if (m_window->channelTreeModel->dmHeaderIndex(m_window->savedRailAccountId).isValid()) {
            onRailAccountHomeSelected(m_window->savedRailAccountId);
            return;
        }
    }

    for (int i = 0; i < m_window->channelTreeModel->rowCount({}); ++i) {
        QModelIndex accIdx = m_window->channelTreeModel->index(i, 0, {});
        if (static_cast<ChannelNode::Type>(accIdx.data(ChannelTreeModel::TypeRole).toInt()) == ChannelNode::Type::Account) {
            onRailAccountHomeSelected(Snowflake(accIdx.data(ChannelTreeModel::IdRole).toULongLong()));
            return;
        }
    }

    m_window->guildHeaderLabel->clear();
    m_window->guildHeaderLabel->hide();
}

void ChannelSelectionController::applyPendingRailSelection(Snowflake accountId)
{
    if (m_window->channelListMode != MainWindow::ChannelListMode::Classic)
        return;

    if (railHasSelection) {
        m_window->hasSavedRailSelection = false;
        return;
    }

    if (m_window->hasSavedRailSelection) {
        if (m_window->savedRailAccountId != accountId)
            return;
        m_window->hasSavedRailSelection = false;
        if (!m_window->savedRailIsHome &&
            m_window->channelTreeModel->serverIndex(accountId, m_window->savedRailGuildId).isValid())
            onRailGuildSelected(accountId, m_window->savedRailGuildId);
        else
            onRailAccountHomeSelected(accountId);
        return;
    }

    onRailAccountHomeSelected(accountId);
}

void ChannelSelectionController::onRailGuildClicked(Snowflake accountId, Snowflake guildId)
{
    onRailGuildSelected(accountId, guildId);

    Snowflake channelId = resolveRailChannel(accountId, guildId);
    if (!channelId.isValid())
        return;

    ChannelNode *node = m_window->channelTreeModel->findChannelTreeNode(channelId, accountId);
    if (!node)
        return;

    m_window->channelFilterProxy->setSelectedChannel(channelId, accountId);
    QModelIndex proxy = m_window->channelFilterProxy->mapFromSource(m_window->channelTreeModel->indexForNode(node));
    if (proxy.isValid())
        m_window->channelTree->setCurrentIndex(proxy);
}

Core::Snowflake ChannelSelectionController::resolveRailChannel(Snowflake accountId, Snowflake guildId)
{
    auto it = lastViewedChannel.constFind(guildChannelKey(accountId, guildId));
    if (it != lastViewedChannel.cend() && channelReadable(accountId, guildId, it.value()))
        return it.value();

    QModelIndex guildSrc = m_window->channelTreeModel->serverIndex(accountId, guildId);
    ChannelNode *guildNode = guildSrc.isValid() ? m_window->channelTreeModel->nodeFromIndex(guildSrc) : nullptr;
    if (guildNode && channelReadable(accountId, guildId, guildNode->rulesChannelId))
        return guildNode->rulesChannelId;

    return firstReadableChannel(accountId, guildId);
}

bool ChannelSelectionController::channelReadable(Snowflake accountId, Snowflake guildId, Snowflake channelId)
{
    if (!channelId.isValid())
        return false;
    ChannelNode *node = m_window->channelTreeModel->findChannelTreeNode(channelId, accountId);
    if (!node || node->type != ChannelNode::Type::Channel)
        return false;
    ChannelNode *guild = ChannelTreeModel::findGuildNode(node);
    if (!guild || guild->id != guildId)
        return false;
    ClientInstance *instance = m_window->session->client(accountId);
    if (instance && instance->permissions())
        return instance->permissions()->hasChannelPermission(accountId, channelId, Discord::Permission::VIEW_CHANNEL);
    return true;
}

Core::Snowflake ChannelSelectionController::firstReadableChannel(Snowflake accountId, Snowflake guildId)
{
    QModelIndex guildSrc = m_window->channelTreeModel->serverIndex(accountId, guildId);
    if (!guildSrc.isValid())
        return {};
    ChannelNode *guildNode = m_window->channelTreeModel->nodeFromIndex(guildSrc);
    if (!guildNode)
        return {};

    ClientInstance *instance = m_window->session->client(accountId);
    auto *perms = instance ? instance->permissions() : nullptr;
    auto readable = [&](Snowflake ch) {
        return !perms || perms->hasChannelPermission(accountId, ch, Discord::Permission::VIEW_CHANNEL);
    };

    for (const auto &child : guildNode->children) {
        if (child->type == ChannelNode::Type::Category)
            break;
        if (child->type == ChannelNode::Type::Channel && readable(child->id))
            return child->id;
    }

    for (const auto &child : guildNode->children) {
        if (child->type != ChannelNode::Type::Category)
            continue;
        for (const auto &cc : child->children)
            if (cc->type == ChannelNode::Type::Channel && readable(cc->id))
                return cc->id;
    }
    return {};
}

void ChannelSelectionController::markIndexAsRead(Snowflake accountId, const QModelIndex &sourceIndex)
{
    ClientInstance *instance = m_window->session->client(accountId);
    if (!instance || !sourceIndex.isValid())
        return;

    const auto pairs = m_window->channelTreeModel->getMarkableChannels(sourceIndex);
    if (pairs.size() == 1)
        instance->readState()->markChannelAsRead(pairs.first().first, pairs.first().second);
    else if (!pairs.isEmpty())
        instance->readState()->markChannelsAsRead(pairs);
}

void ChannelSelectionController::recordLastViewedChannel(Snowflake accountId, Snowflake guildId,
                                                         Snowflake channelId)
{
    lastViewedChannel.insert(guildChannelKey(accountId, guildId), channelId);
}

void ChannelSelectionController::recordRecentChannel(const TabEntry &entry)
{
    if (!entry.channelId.isValid())
        return;

    recentChannels.removeAll(entry);
    recentChannels.prepend(entry);
    while (recentChannels.size() > 5)
        recentChannels.removeLast();
}

void ChannelSelectionController::switchToTabEntry(const TabEntry &entry)
{
    if (!entry.channelId.isValid())
        return;

    activateChannel(entry);
}

void ChannelSelectionController::activateChannel(const TabEntry &entry)
{
    m_window->channelTreeModel->clearTemporaryThread(entry.channelId);

    // update the proxy selected channel so the delegate highlights correctly,
    // and clear the trees own selection so no stale highlight remains
    m_window->channelFilterProxy->setSelectedChannel(entry.channelId, entry.accountId);
    {
        QSignalBlocker blocker(m_window->channelTree->selectionModel());
        m_window->channelTree->selectionModel()->clearSelection();
        m_window->channelTree->selectionModel()->clearCurrentIndex();
    }
    m_window->channelTree->viewport()->update();

    if (m_window->channelListMode == MainWindow::ChannelListMode::Classic) {
        if (entry.isDm) {
            if (!(railSelectedIsHome && railSelectedAccountId == entry.accountId))
                onRailAccountHomeSelected(entry.accountId);
        } else if (entry.guildId.isValid()) {
            if (railSelectedIsHome || railSelectedAccountId != entry.accountId ||
                railSelectedGuildId != entry.guildId)
                onRailGuildSelected(entry.accountId, entry.guildId);
        }
    }

    ClientInstance *instance = m_window->session->client(entry.accountId);
    if (!instance) {
        m_window->messageInput->setEnabled(false);
        return;
    }

    if (instance != m_window->currentInstance)
        m_window->switchActiveInstance(instance);

    if (entry.isForum) {
        setThreadBrowserTarget(Snowflake::Invalid);
        openForumChannel(instance, entry.channelId, entry.guildId);
        if (entry.guildId.isValid())
            recordLastViewedChannel(entry.accountId, entry.guildId, entry.channelId);
        refreshTabReadStates();
        return;
    }

    instance->readState()->setActiveChannel(entry.channelId);
    setViewMode(ViewMode::TextChannel);

    // Update notification manager active channel
    m_window->notificationController->setActiveChannel(entry.channelId);

    if (!entry.isDm && entry.guildId.isValid())
        recordLastViewedChannel(entry.accountId, entry.guildId, entry.channelId);
    else if (entry.isDm)
        recordLastViewedChannel(entry.accountId, Snowflake::Invalid, entry.channelId);

    applyChannelChrome(instance, entry.channelId, entry.name, entry.isDm, entry.guildId);
    switchChatChannel(entry.channelId, entry.guildId);
    instance->messages()->requestLoadChannel(entry.channelId);

    Snowflake lastMsgId = instance->readState()->getChannelLastMessageId(entry.channelId);
    if (lastMsgId.isValid())
        instance->readState()->markChannelAsRead(entry.channelId, lastMsgId);

    refreshTabReadStates();
}

void ChannelSelectionController::refreshTabReadStates()
{
    for (int i = 0; i < m_window->tabBar->tabCount(); ++i) {
        const TabEntry &entry = m_window->tabBar->tabEntry(i);
        if (!entry.channelId.isValid())
            continue;

        ClientInstance *inst = m_window->session->client(entry.accountId);
        if (!inst)
            continue;

        auto state = inst->readState()->computeChannelReadState(
                entry.channelId, entry.guildId, Snowflake::Invalid, entry.isDm);
        if (entry.isForum) {
            auto posts = inst->forums()->joinedPostsContribution(entry.channelId);
            state.isUnread = state.isUnread || posts.unread;
            state.mentionCount += posts.mentions;
        }
        m_window->tabBar->updateChannelReadState(entry.channelId, state.isUnread, state.mentionCount);
    }
}

void ChannelSelectionController::maybeActivatePendingChannel(Core::Snowflake accountId)
{
    if (!pendingActiveEntry.has_value() || pendingActiveEntry->accountId != accountId)
        return;

    TabEntry entry = *pendingActiveEntry;
    pendingActiveEntry.reset();
    activateChannel(entry);
}

QColor ChannelSelectionController::resolveRoleColor(Snowflake userId, Snowflake guildId)
{
    if (!m_window->currentInstance || guildId == Snowflake::Invalid)
        return QColor();

    if (cachedGuildId == guildId && userColorCache.contains(userId))
        return userColorCache.value(userId);

    QColor result;
    for (const auto &role : m_window->currentInstance->getMemberRolesSorted(guildId, userId)) {
        if (role.hasColor()) {
            result = role.getColor();
            break;
        }
    }

    if (cachedGuildId == guildId)
        userColorCache[userId] = result;

    return result;
}

void ChannelSelectionController::refreshGuildRoleData(Snowflake guildId)
{
    m_window->channelTreeModel->invalidateGuildData(guildId);

    if (cachedGuildId == guildId) {
        userColorCache.clear();
        m_window->chatModel->refreshUsersInView({});
        m_window->forumModel->refreshAuthors();
    }
}

void ChannelSelectionController::selectChannelInTree(Snowflake channelId)
{
    if (!m_window->currentInstance)
        return;
    ChannelNode *node = m_window->channelTreeModel->findChannelTreeNode(channelId,
                                                                        m_window->currentInstance->accountId());
    if (!node)
        return;

    if (m_window->channelListMode == MainWindow::ChannelListMode::Classic) {
        Snowflake accountId = m_window->currentInstance->accountId();
        if (node->type == ChannelNode::Type::DMChannel) {
            onRailAccountHomeSelected(accountId);
        } else {
            ChannelNode *guild = ChannelTreeModel::findGuildNode(node);
            if (guild)
                onRailGuildSelected(accountId, guild->id);
        }
    }

    QModelIndex sourceIndex = m_window->channelTreeModel->indexForNode(node);
    if (!sourceIndex.isValid())
        return;
    QModelIndex proxyIndex = m_window->channelFilterProxy->mapFromSource(sourceIndex);
    if (proxyIndex.isValid())
        m_window->channelTree->setCurrentIndex(proxyIndex);
}

void ChannelSelectionController::setThreadBrowserTarget(Core::Snowflake channelId)
{
    threadBrowserChannelId = channelId;
    m_window->threadBrowserButton->setVisible(channelId.isValid());
    updateChannelToolbarVisibility();
}

void ChannelSelectionController::updateChannelToolbarVisibility()
{
    m_window->channelToolbar->setVisible(threadBrowserChannelId.isValid() ||
                                         !m_window->channelFullName.isEmpty());
}

void ChannelSelectionController::openThreadBrowser()
{
    if (!m_window->currentInstance || !threadBrowserChannelId.isValid())
        return;

    if (!m_window->threadBrowser) {
        m_window->threadBrowser = new ThreadBrowserPopup(m_window);
        connect(m_window->threadBrowser, &ThreadBrowserPopup::threadActivated, m_window,
                &MainWindow::navigateToChannel);
    }

    QString name;
    if (auto ch = m_window->currentInstance->getChannel(threadBrowserChannelId); ch && ch->name.hasValue())
        name = ch->name.get();

    m_window->threadBrowser->configure(m_window->currentInstance, threadBrowserChannelId, name);
    m_window->threadBrowser->show();
    m_window->threadBrowser->raise();
}

void ChannelSelectionController::openPinnedMessages(Snowflake channelId)
{
    if (!m_window->currentInstance)
        return;

    // Fall back to the active channel when no specific channel is requested
    // (e.g. the toolbar button). Right-click "View Pins" passes the target
    // channel explicitly, which may differ from the active one.
    if (!channelId.isValid())
        channelId = m_window->chatModel->getActiveChannelId();
    if (!channelId.isValid())
        return;

    Snowflake guildId = m_window->chatModel->getActiveGuildId();
    bool isDm = false;
    if (auto ch = m_window->currentInstance->getChannel(channelId); ch) {
        const Discord::ChannelType type = ch->type.get();
        isDm = type == Discord::ChannelType::DM || type == Discord::ChannelType::GROUP_DM;
    }

    if (!m_window->pinnedMessagesPanel)
        m_window->pinnedMessagesPanel = new PinnedMessagesPanel(m_window->session->getImageManager(), m_window);

    m_window->pinnedMessagesPanel->configure(m_window->currentInstance, channelId, guildId, isDm);
    m_window->pinnedMessagesPanel->show();
    m_window->pinnedMessagesPanel->raise();
    m_window->pinnedMessagesPanel->activateWindow();
}

void ChannelSelectionController::navigateToChannel(Core::Snowflake channelId)
{
    if (!m_window->currentInstance)
        return;
    Core::Snowflake acc = m_window->currentInstance->accountId();

    if (!m_window->channelTreeModel->findChannelTreeNode(channelId, acc)) {
        auto chOpt = m_window->currentInstance->getChannel(channelId);
        if (chOpt && chOpt->isThread())
            m_window->channelTreeModel->showTemporaryThread(*chOpt, acc);
    }
    selectChannelInTree(channelId);
}

} // namespace UI
} // namespace Acheron
