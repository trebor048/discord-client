#include "ForumManager.hpp"

#include <QDateTime>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <utility>

#include "Core/Logging.hpp"
#include "Core/ReadStateManager.hpp"
#include "Discord/Client.hpp"
#include "Storage/ChannelRepository.hpp"

namespace Acheron {
namespace Core {

bool ForumManager::RequestBatch::add(Snowflake forumId, Snowflake threadId)
{
    if (requested.contains(threadId))
        return false;
    if (!pending.isEmpty() && this->forumId != forumId) {
        // The batch is already holding threads for a different forum. Don't
        // drop the request: park it so take() re-queues it once this batch
        // drains — two forums enqueuing in the same event-loop turn must both
        // be answered, not silently lose one.
        overflow.append({ forumId, threadId });
        return false;
    }

    requested.insert(threadId);
    pending.append(threadId);
    this->forumId = forumId;
    if (flushScheduled)
        return false;

    flushScheduled = true;
    return true;
}

QList<Snowflake> ForumManager::RequestBatch::take(Snowflake &outForumId)
{
    flushScheduled = false;
    outForumId = forumId;

    QList<Snowflake> ids;
    ids.swap(pending);

    // Re-queue requests parked by add() while this batch was busy with a
    // different forum. flushStarterRequests drains the re-queued forum in the
    // same pass; flushUnreadRequests (single-flight, see there) hands it out on
    // the flush that follows this batch's reply. Either way the parked request
    // is not dropped.
    if (!overflow.isEmpty()) {
        const QList<QPair<Snowflake, Snowflake>> parked = overflow;
        overflow.clear();
        for (const auto &entry : parked)
            add(entry.first, entry.second);
    }

    lastTaken = ids; // track for the no-arg forgetTaken()/hasOutstanding()
    if (!ids.isEmpty())
        outstandingSince = QDateTime::currentMSecsSinceEpoch();
    return ids;
}

ForumManager::ForumManager(Discord::Client *client, Storage::ChannelRepository &channelRepo,
                           ReadStateManager *readState, QObject *parent)
    : QObject(parent), client(client), channelRepo(channelRepo), readState(readState)
{
    connect(readState, &ReadStateManager::readStateUpdated, this, [this](Snowflake channelId) {
        if (forums.contains(channelId)) {
            emit badgeChanged(channelId);
            return;
        }
        Snowflake forumId = forumOfPost(channelId);
        if (!forumId.isValid())
            return;

        unreadCounts.remove(channelId);
        unreadBatch.forget(channelId);
        emit badgeChanged(forumId);
    });
}

void ForumManager::trackReadState(Snowflake forumId, const Discord::Channel &post)
{
    readState->updateChannelLastMessageId(post.id.get(), post.effectiveLastMessageId());
    readState->updateChannelLastMessageId(forumId, post.id.get());
}

int ForumManager::addPost(Snowflake forumId, ForumState &st, const Discord::Channel &post)
{
    postToForum.insert(post.id.get(), forumId);
    return insertSorted(st, post);
}

void ForumManager::appendPost(Snowflake forumId, ForumState &st, const Discord::Channel &post)
{
    postToForum.insert(post.id.get(), forumId);
    st.posts.append(post);
}

void ForumManager::removePost(ForumState &st, int index)
{
    postToForum.remove(st.posts[index].id.get());
    st.posts.removeAt(index);
}

void ForumManager::replacePosts(Snowflake forumId, ForumState &st, QList<Discord::Channel> posts)
{
    for (const auto &post : st.posts)
        postToForum.remove(post.id.get());

    st.posts = std::move(posts);
    for (const auto &post : st.posts)
        postToForum.insert(post.id.get(), forumId);
}

void ForumManager::setMembership(Snowflake threadId, const Discord::ThreadMember &member)
{
    if (member.joinTimestamp.hasValue())
        joinedAt.insert(threadId, member.joinTimestamp.get().toMSecsSinceEpoch());
}

int ForumManager::adoptThread(const Discord::Channel &thread, Snowflake &forumId)
{
    if (!thread.parentId.hasValue() || !isForum(thread.parentId.get()))
        return -1;
    forumId = thread.parentId.get();

    if (thread.member.hasValue())
        setMembership(thread.id.get(), thread.member.get());

    ForumState &st = state(forumId);
    if (indexOfPost(st, thread.id.get()) >= 0)
        return -1;

    trackReadState(forumId, thread);
    return addPost(forumId, st, thread);
}

void ForumManager::loadFromReady(const QList<Discord::GatewayGuild> &guilds)
{
    QSet<Snowflake> touched;
    for (const auto &guild : guilds) {
        if (!guild.threads.hasValue())
            continue;

        for (const auto &thread : guild.threads.get()) {
            Snowflake forumId;
            if (adoptThread(thread, forumId) >= 0)
                touched.insert(forumId);
        }
    }

    for (Snowflake forumId : touched) {
        emit badgeChanged(forumId);
        emit joinedPostsChanged(forumId);
    }
}

const Discord::Channel *ForumManager::post(Snowflake threadId) const
{
    auto fit = forums.constFind(forumOfPost(threadId));
    if (fit == forums.constEnd())
        return nullptr;

    int idx = indexOfPost(fit.value(), threadId);
    return idx >= 0 ? &fit->posts[idx] : nullptr;
}

Discord::Channel *ForumManager::mutablePost(Snowflake threadId, Snowflake &forumId)
{
    forumId = forumOfPost(threadId);
    return const_cast<Discord::Channel *>(post(threadId));
}

ForumManager::UnreadMarker ForumManager::unreadMarker(Snowflake threadId) const
{
    if (!readState->hasBeenRead(threadId))
        return {};

    const Discord::Channel *p = post(threadId);
    if (!p || !readState->isForumPostUnread(threadId, p->effectiveLastMessageId(), p->isArchived()))
        return {};

    UnreadMarker marker;
    marker.show = true;
    auto it = unreadCounts.constFind(threadId);
    if (it != unreadCounts.constEnd() && it.value() > 0) {
        marker.count = it.value();
        if (p->messageCount.hasValue())
            marker.count = qMin(marker.count, p->messageCount.get());
    }
    return marker;
}

void ForumManager::ensureUnreadCount(Snowflake forumId, Snowflake threadId)
{
    if (unreadCounts.contains(threadId) || unreadBatch.contains(threadId))
        return;
    if (!unreadMarker(threadId).show)
        return;

    if (unreadBatch.add(forumId, threadId))
        QTimer::singleShot(0, this, [this]() { flushUnreadRequests(); });
}

void ForumManager::flushUnreadRequests()
{
    // Single-flight unread requests: the gateway FORUM_UNREADS reply does not
    // carry the request/forum id it answers, so if two forums' requests were
    // outstanding at once the first reply could not be attributed and would
    // clear the wrong forum's in-flight markers. Hand out ONE forum per
    // invocation; onForumUnreads (or the reply watchdog below) re-invokes this
    // when the round trip completes, so requests parked for other forums (the
    // overflow add()/take() re-queue) are still served — one at a time.
    if (unreadBatch.hasOutstanding()) {
        // A reply that never arrives (gateway dropped the request mid-session
        // and silently resumed) must not stall every later unread request.
        if (QDateTime::currentMSecsSinceEpoch() - unreadBatch.outstandingSinceMs()
            >= kUnreadReplyTimeoutMs) {
            qCWarning(LogCore) << "Forum unread request timed out; clearing in-flight markers";
            unreadBatch.forgetTaken();
        } else {
            return;
        }
    }

    while (unreadBatch.hasPending()) {
        Snowflake forumId;
        const QList<Snowflake> ids = unreadBatch.take(forumId);
        if (ids.isEmpty() || !forumId.isValid())
            continue;

        for (int i = 0; i < ids.size(); i += kMaxUnreadRequest) {
            QList<QPair<Snowflake, Snowflake>> request;
            for (Snowflake threadId : ids.mid(i, kMaxUnreadRequest))
                request.append({ threadId, readState->effectiveAckId(threadId, Snowflake()) });
            client->requestForumUnreads(forumId, request);
        }

        // Watchdog: drop this batch if no reply ever arrives (see the guard at
        // the top of this function). One-shot per handed-out batch.
        QTimer::singleShot(kUnreadReplyTimeoutMs, this, [this]() { onUnreadReplyTimeout(); });
        return; // exactly one forum in flight; further queued forums await its reply
    }
}

void ForumManager::onUnreadReplyTimeout()
{
    if (!unreadBatch.hasOutstanding())
        return; // answered in time — nothing stale

    // Only drop the batch this watchdog belongs to; if a reply already advanced
    // the queue to a newer generation, that one gets its own, later watchdog.
    if (QDateTime::currentMSecsSinceEpoch() - unreadBatch.outstandingSinceMs()
        < kUnreadReplyTimeoutMs)
        return;

    qCWarning(LogCore) << "Forum unread request timed out; clearing in-flight markers";
    unreadBatch.forgetTaken();
    if (unreadBatch.hasPending())
        flushUnreadRequests();
}

void ForumManager::onForumUnreads(const Discord::ForumUnreads &event)
{
    // This reply answers the single outstanding unread batch (see
    // flushUnreadRequests): clearing the markers of the batch most recently
    // handed out by take() is exact because no other forum's request can be in
    // flight. Clear on success and failure (permission denied, no threads
    // payload) so threads answered with nothing can be re-requested when needed
    // and `requested` never accumulates across a session.
    unreadBatch.forgetTaken();

    if (!(event.permissionDenied.hasValue() && event.permissionDenied.get())
        && event.threads.hasValue()) {
        for (const auto &unread : event.threads.get()) {
            const Snowflake threadId = unread.threadId.get();
            unreadBatch.forget(threadId);
            if (!unread.count.hasValue())
                continue;

            unreadCounts.insert(threadId, unread.count.get());
            Snowflake forumId = forumOfPost(threadId);
            if (forumId.isValid())
                emit postUpdated(forumId, threadId);
        }
    }

    // The outstanding batch is now answered; serve the next parked forum so
    // every forum queued in one event-loop turn is eventually answered.
    if (unreadBatch.hasPending())
        flushUnreadRequests();
}

QList<Discord::Channel> ForumManager::joinedPosts(Snowflake forumId) const
{
    auto it = forums.constFind(forumId);
    if (it == forums.constEnd())
        return {};

    QList<Discord::Channel> result;
    for (const auto &post : it->posts) {
        if (!post.isArchived() && joinedAt.contains(post.id.get()) && readState->isThreadRelevant(post))
            result.append(post);
    }

    std::stable_sort(result.begin(), result.end(), [this](const auto &a, const auto &b) {
        return joinedAt.value(a.id.get()) > joinedAt.value(b.id.get());
    });
    return result;
}

ForumManager::PostsContribution ForumManager::joinedPostsContribution(Snowflake forumId) const
{
    PostsContribution c;
    for (const auto &post : joinedPosts(forumId)) {
        const Snowflake postId = post.id.get();
        c.mentions += readState->getMentionCount(postId);
        if (!c.unread && readState->isForumPostUnread(postId, post.effectiveLastMessageId(), false) && !readState->isChannelMuted(postId))
            c.unread = true;
    }
    return c;
}

bool ForumManager::isForum(Snowflake channelId) const
{
    auto cached = forumTypeCache.constFind(channelId);
    if (cached != forumTypeCache.constEnd())
        return cached.value();

    auto ch = channelRepo.getChannel(channelId);
    if (!ch)
        return false; // idk

    bool forum = ch->type == Discord::ChannelType::GUILD_FORUM
                   || ch->type == Discord::ChannelType::GUILD_MEDIA;
    forumTypeCache.insert(channelId, forum);
    return forum;
}

Snowflake ForumManager::guildOfForum(Snowflake forumId) const
{
    auto cached = forumGuildCache.constFind(forumId);
    if (cached != forumGuildCache.constEnd())
        return cached.value();

    auto ch = channelRepo.getChannel(forumId);
    if (!ch)
        return Snowflake();

    Snowflake guildId = ch->guildId.hasValue() ? ch->guildId.get() : Snowflake();
    forumGuildCache.insert(forumId, guildId);
    return guildId;
}

ForumManager::PostReadState ForumManager::postReadState(Snowflake threadId) const
{
    const Discord::Channel *p = post(threadId);
    Snowflake forumId = forumOfPost(threadId);
    if (!p || !forumId.isValid())
        return {};

    return { readState->isForumPostUnread(threadId, p->effectiveLastMessageId(), p->isArchived()),
             readState->isForumPostNew(threadId, forumId, guildOfForum(forumId),
                                       p->isArchived()) };
}

ForumManager::ForumState &ForumManager::state(Snowflake forumId)
{
    return forums[forumId];
}

int ForumManager::indexOfPost(const ForumState &st, Snowflake threadId) const
{
    for (int i = 0; i < st.posts.size(); i++)
        if (st.posts[i].id.get() == threadId)
            return i;
    return -1;
}

Snowflake ForumManager::forumOfPost(Snowflake threadId) const
{
    auto it = postToForum.constFind(threadId);
    return it != postToForum.constEnd() ? it.value() : Snowflake();
}

Snowflake ForumManager::sortKey(const Discord::Channel &post, ForumSortMode mode) const
{
    return mode == ForumSortMode::LATEST_ACTIVITY ? post.effectiveLastMessageId() : post.id.get();
}

bool ForumManager::sortsBefore(const Discord::Channel &a, const Discord::Channel &b,
                               ForumSortMode mode) const
{
    const bool pinnedA = a.isPinned();
    if (pinnedA != b.isPinned())
        return pinnedA;
    return sortKey(a, mode) > sortKey(b, mode);
}

int ForumManager::insertSorted(ForumState &st, const Discord::Channel &thread) const
{
    int i = 0;
    while (i < st.posts.size() && !sortsBefore(thread, st.posts[i], st.sortMode))
        i++;
    st.posts.insert(i, thread);
    return i;
}

void ForumManager::setCurrentForum(Snowflake forumId)
{
    if (currentForumId == forumId)
        return;

    const Snowflake previous = currentForumId;
    currentForumId = forumId;
    if (previous.isValid())
        emit badgeChanged(previous);
    if (forumId.isValid())
        emit badgeChanged(forumId);
}

void ForumManager::openForum(Snowflake forumId)
{
    setCurrentForum(forumId);
    ForumState &st = state(forumId);
    if (!st.sortInitialized) {
        st.sortMode = defaultSortFor(forumId);
        st.sortInitialized = true;
    }
    client->ensureSubscriptionByChannel(forumId);
    if (!st.loaded && !st.loading)
        fetchPage(forumId, true);
    else
        emit postsReset(forumId);
}

void ForumManager::setSortMode(Snowflake forumId, ForumSortMode mode)
{
    ForumState &st = state(forumId);
    st.sortInitialized = true;
    if (st.sortMode == mode && st.loaded)
        return;

    st.sortMode = mode;
    st.generation++;
    replacePosts(forumId, st, {});
    st.offset = 0;
    st.hasMore = true;
    st.loaded = false;
    st.retryCount = 0;
    emit postsReset(forumId);
    fetchPage(forumId, true);
}

ForumSortMode ForumManager::sortMode(Snowflake forumId) const
{
    auto it = forums.constFind(forumId);
    if (it != forums.constEnd() && it.value().sortInitialized)
        return it.value().sortMode;
    return defaultSortFor(forumId);
}

ForumSortMode ForumManager::defaultSortFor(Snowflake forumId) const
{
    auto ch = channelRepo.getChannel(forumId);
    if (ch && ch->defaultSortOrder.hasValue() && ch->defaultSortOrder.get() == 1)
        return ForumSortMode::CREATION_DATE;
    return ForumSortMode::LATEST_ACTIVITY;
}

void ForumManager::loadMorePosts(Snowflake forumId)
{
    ForumState &st = state(forumId);
    if (st.loading || !st.hasMore || !st.loaded)
        return;
    fetchPage(forumId, false);
}

void ForumManager::fetchPage(Snowflake forumId, bool reset)
{
    ForumState &st = state(forumId);
    st.loading = true;
    emit loadingChanged(forumId, true);

    const int generation = st.generation;
    int offset = reset ? 0 : st.offset;
    const QString sortBy = st.sortMode == ForumSortMode::CREATION_DATE
                                   ? QStringLiteral("creation_time")
                                   : QStringLiteral("last_message_time");

    client->searchForumThreads(
            forumId, offset, sortBy,
            [this, forumId, reset, generation](const Core::Result<Discord::Client::ForumThreadSearchResult> &res) {
                ForumState &st = state(forumId);
                if (st.generation != generation)
                    return;

                if (!res.success()) {
                    st.loading = false;
                    emit loadingChanged(forumId, false);
                    qCWarning(LogCore) << "Forum posts fetch failed:" << res.error;
                    return;
                }

                const auto &data = *res.value;

                if (data.indexNotReady) {
                    if (st.retryCount++ >= kMaxRetries) {
                        st.loading = false;
                        emit loadingChanged(forumId, false);
                        qCWarning(LogCore) << "Forum search index not ready after retries for" << forumId;
                        return;
                    }
                    int delayMs = qBound(1000, data.retryAfterSeconds * 1000, 60000);
                    QTimer::singleShot(delayMs, this, [this, forumId, reset, generation]() {
                        if (state(forumId).generation == generation)
                            fetchPage(forumId, reset);
                    });
                    return;
                }

                st.retryCount = 0;
                st.loading = false;
                st.loaded = true;
                st.hasMore = data.hasMore;

                for (auto it = data.firstMessages.constBegin();
                     it != data.firstMessages.constEnd(); ++it)
                    starterMessages.insert(it.key(), it.value());

                for (const auto &t : data.threads)
                    trackReadState(forumId, t);

                emit loadingChanged(forumId, false);
                if (reset)
                    applySearchReset(forumId, st, data.threads);
                else
                    applySearchAppend(forumId, st, data.threads);

                emit badgeChanged(forumId);
                emit joinedPostsChanged(forumId);
            });
}

void ForumManager::applySearchReset(Snowflake forumId, ForumState &st, const QList<Discord::Channel> &threads)
{
    // On reset the server result is authoritative; do not resurrect deleted posts.
    st.offset = threads.size();
    replacePosts(forumId, st, QList<Discord::Channel>(threads));

    std::stable_sort(st.posts.begin(), st.posts.end(), [this, &st](const auto &a, const auto &b) {
        return sortsBefore(a, b, st.sortMode);
    });

    emit postsReset(forumId);
}

void ForumManager::applySearchAppend(Snowflake forumId, ForumState &st, const QList<Discord::Channel> &threads)
{
    QList<Discord::Channel> fresh;
    for (const auto &t : threads)
        if (indexOfPost(st, t.id.get()) < 0)
            fresh.append(t);

    for (const auto &t : fresh) {
        if (!t.isPinned())
            continue;
        int row = addPost(forumId, st, t);
        emit postInserted(forumId, t.id.get(), row);
    }

    int start = st.posts.size();
    for (const auto &t : fresh)
        if (!t.isPinned())
            appendPost(forumId, st, t);

    st.offset += threads.size();
    if (st.posts.size() > start) {
        // Non-pinned pages may not be tail-sorted relative to existing data
        // after a sort-mode switch; restore global order (bounded by page size).
        std::stable_sort(st.posts.begin(), st.posts.end(),
                         [this, &st](const auto &a, const auto &b) { return sortsBefore(a, b, st.sortMode); });
        // Stable-sort may have moved appended rows away from the tail, so the
        // bulk-append emission would be stale; emit a reset to force correct
        // view order (cost is one model reset per page, page size small).
        emit postsReset(forumId);
    }
}

void ForumManager::onThreadCreated(const Discord::ChannelCreate &event)
{
    const Discord::Channel &thread = event.channel.get();
    Snowflake forumId;
    const int row = adoptThread(thread, forumId);
    if (!forumId.isValid())
        return;

    if (joinedAt.contains(thread.id.get()))
        emit joinedPostsChanged(forumId);
    if (row < 0)
        return; // already had it

    emit postInserted(forumId, thread.id.get(), row);
    emit badgeChanged(forumId);
}

void ForumManager::onThreadUpdated(const Discord::ChannelUpdate &event)
{
    const Discord::Channel &thread = event.channel.get();
    Snowflake forumId;
    Discord::Channel *post = mutablePost(thread.id.get(), forumId);
    if (!post)
        return;

    // THREAD_UPDATE is partial: any field the gateway omitted arrives as
    // undefined. Merge field-wise so a partial update doesn't wipe locally
    // cached state (name, applied_tags, thread_metadata, member_count, etc.).
    Discord::Channel updated = *post;
    updated.mergeFrom(thread);

    *post = updated;
    trackReadState(forumId, updated);
    emit postUpdated(forumId, thread.id.get());
    emit badgeChanged(forumId);
    emit joinedPostsChanged(forumId);
}

void ForumManager::onThreadDeleted(const Discord::ThreadDelete &event)
{
    if (!event.parentId.hasValue())
        return;

    Snowflake forumId = event.parentId.get();
    auto fit = forums.find(forumId);
    if (fit == forums.end())
        return;

    ForumState &st = fit.value();
    int idx = indexOfPost(st, event.id.get());
    if (idx < 0)
        return;

    removePost(st, idx);
    forgetPost(event.id.get());
    emit postRemoved(forumId, event.id.get());
    emit badgeChanged(forumId);
    emit joinedPostsChanged(forumId);
}

void ForumManager::onThreadListSync(const Discord::ThreadListSync &event)
{
    if (event.members.hasValue())
        for (const auto &member : event.members.get())
            if (member.id.hasValue())
                setMembership(member.id.get(), member);

    if (!event.threads.hasValue())
        return;

    QSet<Snowflake> touched;
    for (const auto &thread : event.threads.get()) {
        Snowflake forumId;
        const int row = adoptThread(thread, forumId);
        if (row < 0)
            continue;

        emit postInserted(forumId, thread.id.get(), row);
        touched.insert(forumId);
    }

    for (Snowflake forumId : touched) {
        emit badgeChanged(forumId);
        emit joinedPostsChanged(forumId);
    }
}

void ForumManager::onThreadMemberUpdate(const Discord::ThreadMemberUpdate &event)
{
    if (event.userId.hasValue() && event.userId.get() != client->getMe().id.get())
        return;
    if (!event.member.id.hasValue())
        return;

    const Snowflake threadId = event.member.id.get();
    setMembership(threadId, event.member);

    Snowflake forumId = forumOfPost(threadId);
    if (forumId.isValid())
        emit joinedPostsChanged(forumId);
}

void ForumManager::onMessageCreated(const Discord::Message &msg)
{
    if (!msg.channelId.hasValue())
        return;

    const Snowflake threadId = msg.channelId.get();
    Snowflake forumId;
    Discord::Channel *post = mutablePost(threadId, forumId);
    if (!post)
        return;

    const bool isStarter = msg.id.hasValue() && msg.id.get() == threadId;
    if (isStarter) {
        starterMessages.insert(threadId, msg);
    } else {
        int count = post->messageCount.hasValue() ? post->messageCount.get() : 0;
        post->messageCount = count + 1;
        auto unread = unreadCounts.find(threadId);
        if (unread != unreadCounts.end())
            unread.value()++;
    }
    if (msg.id.hasValue())
        post->lastMessageId = msg.id.get();

    emit postUpdated(forumId, threadId);
    emit badgeChanged(forumId);
}

const QList<Discord::Channel> &ForumManager::posts(Snowflake forumId) const
{
    static const QList<Discord::Channel> empty;
    auto it = forums.constFind(forumId);
    return it == forums.constEnd() ? empty : it.value().posts;
}

bool ForumManager::isLoading(Snowflake forumId) const
{
    auto it = forums.constFind(forumId);
    return it != forums.constEnd() && it.value().loading;
}

bool ForumManager::hasMore(Snowflake forumId) const
{
    auto it = forums.constFind(forumId);
    return it != forums.constEnd() && it.value().hasMore;
}

void ForumManager::forgetPost(Snowflake threadId)
{
    starterMessages.remove(threadId);
    joinedAt.remove(threadId);
    unreadCounts.remove(threadId);
    unreadBatch.forget(threadId);
    starterBatch.forget(threadId);
}

const Discord::Message *ForumManager::firstMessagePtr(Snowflake threadId) const
{
    auto it = starterMessages.constFind(threadId);
    return it != starterMessages.constEnd() ? &it.value() : nullptr;
}

void ForumManager::addStarterMessage(Snowflake threadId, const Discord::Message &msg)
{
    starterMessages.insert(threadId, msg);
    Snowflake forumId = forumOfPost(threadId);
    if (forumId.isValid())
        emit postUpdated(forumId, threadId);
}

void ForumManager::ensureStarter(Snowflake forumId, Snowflake threadId)
{
    if (starterMessages.contains(threadId) || starterBatch.contains(threadId))
        return;

    if (starterBatch.add(forumId, threadId))
        QTimer::singleShot(0, this, [this]() { flushStarterRequests(); });
}

void ForumManager::flushStarterRequests()
{
    // Drain every pending forum in this pass: unlike unread replies, each post-
    // data reply is correlated to its request (the callback captures its forum
    // and chunk), so several forums may be in flight at once. take() re-queues
    // parked cross-forum requests, so stopping early would lose the later
    // forum's fetch.
    while (starterBatch.hasPending()) {
        Snowflake forumId;
        const QList<Snowflake> ids = starterBatch.take(forumId);
        if (ids.isEmpty() || !forumId.isValid())
            continue;

        for (int i = 0; i < ids.size(); i += kPostDataBatch) {
            const QList<Snowflake> batch = ids.mid(i, kPostDataBatch);
            client->fetchForumPostData(
                    forumId, batch,
                    [this, forumId, batch](const Core::Result<QHash<Snowflake, Discord::Message>> &res) {
                        // Clear the in-flight markers for EXACTLY this chunk on
                        // success and failure: several forums' (and this forum's
                        // other chunks') requests can be outstanding from one
                        // flush turn, and clearing a global "last batch" would
                        // wipe a still-in-flight forum's markers while this one's
                        // leaked. Threads the API answered with nothing have no
                        // starter message; re-requesting them later is harmless
                        // (starterMessages gates re-fetches for ones we have).
                        starterBatch.forgetTaken(batch);
                        if (!res.success())
                            return;
                        for (auto it = res.value->constBegin(); it != res.value->constEnd(); ++it) {
                            starterMessages.insert(it.key(), it.value());
                            emit postUpdated(forumId, it.key());
                        }
                    });
        }
    }
}

QList<Discord::Channel::ForumTag> ForumManager::availableTags(Snowflake forumId) const
{
    auto ch = channelRepo.getChannel(forumId);
    if (ch && ch->availableTags.hasValue())
        return ch->availableTags.get();
    return {};
}

bool ForumManager::requiresTag(Snowflake forumId) const
{
    auto ch = channelRepo.getChannel(forumId);
    return ch && ch->flags.hasValue() && ch->flags->testFlag(Discord::ChannelFlag::REQUIRE_TAG);
}

int ForumManager::newPostCount(Snowflake forumId, Snowflake guildId, const ForumState &st) const
{
    const bool open = forumId == currentForumId;
    const Snowflake ack = readState->effectiveAckId(forumId, guildId);

    int count = 0;
    for (const auto &post : st.posts) {
        if (post.isArchived())
            continue;

        const Snowflake threadId = post.id.get();
        if (open ? readState->isForumPostNew(threadId, forumId, guildId, false)
                 : (threadId > ack && !readState->hasBeenRead(threadId)))
            count++;
    }
    return count;
}

int ForumManager::unreadPostCount(Snowflake forumId, Snowflake guildId,
                                  const ForumState &st) const
{
    if (!readState->hasBeenRead(forumId))
        return 0;
    const Snowflake ack = readState->effectiveAckId(forumId, guildId);

    int count = 0;
    for (const auto &post : st.posts) {
        if (post.isArchived())
            continue;

        const Snowflake last = post.effectiveLastMessageId();
        if (last > ack && readState->isForumPostUnread(post.id.get(), last, false))
            count++;
    }
    return count;
}

ForumBadge ForumManager::badge(Snowflake forumId) const
{
    ForumBadge badge;
    auto it = forums.constFind(forumId);
    if (it == forums.constEnd())
        return badge;

    const Snowflake guildId = guildOfForum(forumId);
    if (!guildId.isValid())
        return badge;

    badge.count = newPostCount(forumId, guildId, it.value());
    if (badge.count > 0) {
        badge.isNew = true;
        return badge;
    }

    badge.count = unreadPostCount(forumId, guildId, it.value());
    return badge;
}

} // namespace Core
} // namespace Acheron
