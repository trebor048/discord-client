#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>

#include "Core/Snowflake.hpp"
#include "Discord/Entities.hpp"
#include "Discord/Events.hpp"

namespace Acheron {

namespace Discord {
class Client;
}

namespace Storage {
class ChannelRepository;
}

namespace Core {

class ReadStateManager;

enum class ForumSortMode {
    LATEST_ACTIVITY = 0, // last_message_time
    CREATION_DATE = 1, // creation_time
};

struct ForumBadge
{
    int count = 0;
    bool isNew = false;
};

class ForumManager : public QObject
{
    Q_OBJECT
public:
    explicit ForumManager(Discord::Client *client, Storage::ChannelRepository &channelRepo,
                          ReadStateManager *readState, QObject *parent = nullptr);

    void openForum(Snowflake forumId);
    void setCurrentForum(Snowflake forumId);
    void loadMorePosts(Snowflake forumId);

    void setSortMode(Snowflake forumId, ForumSortMode mode);
    [[nodiscard]] ForumSortMode sortMode(Snowflake forumId) const;

    [[nodiscard]] const QList<Discord::Channel> &posts(Snowflake forumId) const;
    [[nodiscard]] const Discord::Channel *post(Snowflake threadId) const;
    [[nodiscard]] bool isLoading(Snowflake forumId) const;
    [[nodiscard]] bool hasMore(Snowflake forumId) const;
    [[nodiscard]] const Discord::Message *firstMessagePtr(Snowflake threadId) const;
    void addStarterMessage(Snowflake threadId, const Discord::Message &msg);
    void ensureStarter(Snowflake forumId, Snowflake threadId);
    [[nodiscard]] QList<Discord::Channel::ForumTag> availableTags(Snowflake forumId) const;
    [[nodiscard]] bool requiresTag(Snowflake forumId) const;
    [[nodiscard]] ForumBadge badge(Snowflake forumId) const;
    void loadFromReady(const QList<Discord::GatewayGuild> &guilds);
    [[nodiscard]] QList<Discord::Channel> joinedPosts(Snowflake forumId) const;

    struct PostsContribution
    {
        bool unread = false;
        int mentions = 0;
    };
    [[nodiscard]] PostsContribution joinedPostsContribution(Snowflake forumId) const;

    struct PostReadState
    {
        bool unread = false;
        bool isNew = false;
    };
    [[nodiscard]] PostReadState postReadState(Snowflake threadId) const;

    struct UnreadMarker
    {
        bool show = false;
        int count = 0;
    };
    [[nodiscard]] UnreadMarker unreadMarker(Snowflake threadId) const;
    void ensureUnreadCount(Snowflake forumId, Snowflake threadId);

signals:
    void badgeChanged(Snowflake forumId);
    void joinedPostsChanged(Snowflake forumId);
    void postsReset(Snowflake forumId);
    void postsAppended(Snowflake forumId, int startRow, int count);
    void postInserted(Snowflake forumId, Snowflake threadId, int row);
    void postUpdated(Snowflake forumId, Snowflake threadId);
    void postRemoved(Snowflake forumId, Snowflake threadId);
    void loadingChanged(Snowflake forumId, bool loading);

public slots:
    void onForumUnreads(const Discord::ForumUnreads &event);
    void onThreadCreated(const Discord::ChannelCreate &event);
    void onThreadUpdated(const Discord::ChannelUpdate &event);
    void onThreadDeleted(const Discord::ThreadDelete &event);
    void onThreadListSync(const Discord::ThreadListSync &event);
    void onThreadMemberUpdate(const Discord::ThreadMemberUpdate &event);
    void onMessageCreated(const Discord::Message &msg);

private:
    struct ForumState
    {
        QList<Discord::Channel> posts;
        int offset = 0;
        bool hasMore = true;
        bool loading = false;
        bool loaded = false;
        int retryCount = 0;
        ForumSortMode sortMode = ForumSortMode::LATEST_ACTIVITY;
        bool sortInitialized = false;
        // bumped on sort change
        int generation = 0;
    };

    class RequestBatch
    {
    public:
        bool add(Snowflake forumId, Snowflake threadId);
        QList<Snowflake> take(Snowflake &forumId);
        // Anything left to hand out — the pending batch plus any cross-forum
        // requests parked in `overflow` (see add()/take()).
        [[nodiscard]] bool hasPending() const { return !pending.isEmpty() || !overflow.isEmpty(); }
        [[nodiscard]] bool contains(Snowflake threadId) const { return requested.contains(threadId); }
        void forget(Snowflake threadId)
        {
            requested.remove(threadId);
            pending.removeOne(threadId);
            // Purge any still-parked copy so a thread removed while queued for a
            // different forum is not re-queued by the next take().
            for (int i = overflow.size() - 1; i >= 0; --i) {
                if (overflow[i].second == threadId)
                    overflow.removeAt(i);
            }
        }
        // True while the batch most recently handed out by take() is still
        // awaiting its reply. Only meaningful for the single-flight unread
        // flush (see flushUnreadRequests): the gateway FORUM_UNREADS reply
        // carries no request/forum id, so at most one forum's unread request
        // may be outstanding at a time.
        [[nodiscard]] bool hasOutstanding() const { return !lastTaken.isEmpty(); }
        // Wall-clock ms when the outstanding batch was handed out (0 when none
        // is outstanding); lets the reply watchdog drop a batch whose reply
        // never arrived instead of stalling every later unread request.
        [[nodiscard]] qint64 outstandingSinceMs() const { return outstandingSince; }
        // Clears the in-flight markers for EXACTLY \a ids. Each completion
        // handler captures the ids it asked for and clears only those — never a
        // different forum's still-outstanding batch when several forums'
        // requests are in flight from one flush turn.
        void forgetTaken(const QList<Snowflake> &ids)
        {
            for (Snowflake id : ids)
                requested.remove(id);
        }
        // Clears the in-flight markers for the batch most recently handed out by
        // take(). Call ONLY when that batch is the one being answered: the unread
        // reply handler relies on flushUnreadRequests keeping at most one forum's
        // batch in flight. Runs on success and failure (permission denied, empty
        // payload) so threads answered with nothing — or never answered at all —
        // can be re-requested later instead of being stuck in `requested`
        // forever, and so `requested` never grows unboundedly across a session.
        void forgetTaken()
        {
            for (Snowflake id : lastTaken)
                requested.remove(id);
            lastTaken.clear();
            outstandingSince = 0;
        }

    private:
        QSet<Snowflake> requested; // asked but not yet answered
        QList<Snowflake> pending;
        // Ids handed out by the latest take() and not yet answered (see
        // forgetTaken()/hasOutstanding()).
        QList<Snowflake> lastTaken;
        qint64 outstandingSince = 0;
        // (forumId, threadId) pairs queued by add() while `pending` was holding
        // a batch for a different forum; take() re-queues them into `pending`
        // once the current batch drains, so cross-forum requests are never
        // dropped.
        QList<QPair<Snowflake, Snowflake>> overflow;
        Snowflake forumId;
        bool flushScheduled = false;
    };

    ForumState &state(Snowflake forumId);
    void fetchPage(Snowflake forumId, bool reset);
    void applySearchReset(Snowflake forumId, ForumState &st, const QList<Discord::Channel> &threads);
    void applySearchAppend(Snowflake forumId, ForumState &st, const QList<Discord::Channel> &threads);
    void flushStarterRequests();
    void flushUnreadRequests();
    // Fires kUnreadReplyTimeoutMs after an unread request is sent when no reply
    // has arrived; drops the stale in-flight markers and serves the next queued
    // forum (see flushUnreadRequests).
    void onUnreadReplyTimeout();
    Discord::Channel *mutablePost(Snowflake threadId, Snowflake &forumId);
    void forgetPost(Snowflake threadId);
    int indexOfPost(const ForumState &st, Snowflake threadId) const;
    [[nodiscard]] Snowflake forumOfPost(Snowflake threadId) const;
    int adoptThread(const Discord::Channel &thread, Snowflake &forumId);
    int addPost(Snowflake forumId, ForumState &st, const Discord::Channel &post);
    void appendPost(Snowflake forumId, ForumState &st, const Discord::Channel &post);
    void removePost(ForumState &st, int index);
    void replacePosts(Snowflake forumId, ForumState &st, QList<Discord::Channel> posts);
    ForumSortMode defaultSortFor(Snowflake forumId) const;
    Snowflake sortKey(const Discord::Channel &post, ForumSortMode mode) const;
    bool sortsBefore(const Discord::Channel &a, const Discord::Channel &b, ForumSortMode mode) const;
    int insertSorted(ForumState &st, const Discord::Channel &thread) const;
    void trackReadState(Snowflake forumId, const Discord::Channel &post);
    void setMembership(Snowflake threadId, const Discord::ThreadMember &member);
    [[nodiscard]] bool isForum(Snowflake channelId) const;
    [[nodiscard]] Snowflake guildOfForum(Snowflake forumId) const;
    [[nodiscard]] int newPostCount(Snowflake forumId, Snowflake guildId, const ForumState &st) const;
    [[nodiscard]] int unreadPostCount(Snowflake forumId, Snowflake guildId, const ForumState &st) const;

    Discord::Client *client;
    Storage::ChannelRepository &channelRepo;
    ReadStateManager *readState;
    mutable QHash<Snowflake, bool> forumTypeCache;
    mutable QHash<Snowflake, Snowflake> forumGuildCache;
    QHash<Snowflake, ForumState> forums;
    QHash<Snowflake, Snowflake> postToForum;
    QHash<Snowflake, Discord::Message> starterMessages;
    QHash<Snowflake, qint64> joinedAt;
    QHash<Snowflake, int> unreadCounts;
    RequestBatch unreadBatch;
    RequestBatch starterBatch;
    Snowflake currentForumId;

    static constexpr int kMaxUnreadRequest = 180;
    static constexpr int kMaxRetries = 5;
    static constexpr int kPostDataBatch = 10;
    // How long to wait for a FORUM_UNREADS reply before treating the batch as
    // lost (gateway dropped the request mid-session and silently resumed).
    static constexpr int kUnreadReplyTimeoutMs = 30000;
};

} // namespace Core
} // namespace Acheron
