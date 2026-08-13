# Acheron — Comprehensive Improvement Plan

**Generated**: 2026-07-07  
**Scope**: All areas for upgrade, improvement, and new features across the entire codebase.  
**Legend**: 🔴 Critical | 🟡 Medium | 🟢 Low priority

## Completion Summary (2026-07-07)

### ✅ Section 2 — Missing Gateway Events (🔴)
Added 24 new gateway event types across the entire pipeline:
- **Enum values**: `GUILD_UPDATE`, `GUILD_DELETE`, `GUILD_BAN_ADD/REMOVE`, `GUILD_EMOJIS_UPDATE`, `GUILD_STICKERS_UPDATE`, all thread events, `PRESENCE_UPDATE`, `WEBHOOKS_UPDATE`, `INVITE_CREATE/DELETE`, stage instance events, scheduled event events, integration events, `CHANNEL_PINS_UPDATE`
- **Events.hpp**: Added 24 event structs with full field coverage and `fromJson` deserialization (`GuildDelete`, `GuildBan`, `GuildEmojisUpdate`, `GuildStickersUpdate`, `ThreadCreate/Update/Delete/ListSync/MemberUpdate`, `PresenceUpdate` + `Activity`, `WebhooksUpdate`, `InviteCreate/Delete`, `StageInstance`, `GuildScheduledEvent`, `IntegrationCreate/Update/Delete`, `ChannelPinsUpdate`)
- **Entities.hpp**: Expanded `Emoji` with `available`, `managed`, `requireColons`, `roles` fields
- **Gateway.hpp/cpp**: Signals, dispatch routing, and handler implementations for all 24 events
- **Client.hpp/cpp**: Relay signals and `connect()` wiring for all events

### ✅ Section 1.1 — Entity Deserialization Tests (🔴)
Created `tests/tst_Deserialization.cpp` with 15 test cases:
- Channel required fields, all fields, thread metadata, forum tags
- User deserialization
- Emoji (all new fields + unicode fallback)
- GuildDelete, GuildBan, GuildEmojisUpdate, ThreadCreate, ThreadDelete
- PresenceUpdate, InviteCreate, StageInstance, ChannelPinsUpdate
- Added `TestDeserialization` target to `CMakeLists.txt`

### ✅ Section 3.1–3.3 — Thread & Forum Properties (🔴)
Extended `Channel` entity with:
- `ThreadMetadata` (nested struct): `archived`, `archiveTimestamp`, `autoArchiveDuration`, `locked`, `invitable`, `createTimestamp`
- `ForumTag` (nested struct): `id`, `name`, `moderated`, `emojiId`, `emojiName`
- `DefaultReaction` (nested struct): `emojiId`, `emojiName`
- Fields: `threadMetadata`, `memberCount`, `messageCount`, `totalMessageSent`, `defaultAutoArchiveDuration`, `flags`, `availableTags`, `defaultReactionEmoji`, `defaultSortOrder`, `defaultForumLayout`, `defaultThreadRateLimitPerUser`
- Helper methods: `isThread()`, `isForum()`

### ✅ QOL T1–T3 (🟢)
- **T1 (Scrollbar toggle)**: Added `"ui/alwaysShowScrollbars"` QSettings key with startup apply to `chatView` and `channelTree`
- **T2 (Splitter persistence)**: Already implemented — `mainSplitter->saveState()` / `restoreState()` called in `saveWindowState()` / `restoreWindowState()`
- **T3 (Typing ellipsis)**: Already implemented — `dotTimer` (400ms cycle) drives animated circles in `paintEvent` with bounce/size oscillation

---

## Table of Contents

1. [Test Coverage (🔴)](#1-test-coverage)
2. [Missing Gateway Events (🔴)](#2-missing-gateway-events)
3. [Threads & Forums (🔴)](#3-threads--forums)
4. [Message Display Gaps (🟡)](#4-message-display-gaps)
5. [Voice System (🟡)](#5-voice-system)
6. [Settings UI (🟡)](#6-settings-ui)
7. [User & Social Features (🟡)](#7-user--social-features)
8. [Chat UI & Polish (🟡)](#8-chat-ui--polish)
9. [Code Quality & Architecture (🟡)](#9-code-quality--architecture)
10. [Cross-Platform & Build (🟢)](#10-cross-platform--build)
11. [Performance (🟡)](#11-performance)
12. [Documentation (🟢)](#12-documentation)
13. [New Feature Ideas (🟢)](#13-new-feature-ideas)
14. [Previously Planned QOL (🟢)](#14-previously-planned-qol-not-yet-implemented)
15. [Implementation Roadmap](#15-implementation-roadmap)

---

## 1. Test Coverage (🔴)

### Problem
Only 3 test files exist, containing minimal coverage:
- `tests/tst_Markdown.cpp` — 1 test case
- `tests/tst_EmojiCatalog.cpp` — 4 test cases
- `tests/tst_EmojiPickerDialog.cpp` — 4 test cases

Entire subsystems have **zero tests**: Gateway, HTTP client, entity deserialization, voice, storage, permissions, theme.

### Required Changes

#### 1.1 Entity Deserialization Tests (`tests/tst_Entities.cpp`)

Test every `fromJson()` parser with real Discord API payloads.

```cpp
// tests/tst_Entities.cpp
#include "Discord/Entities.hpp"
#include "Discord/Events.hpp"
#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>

using namespace Acheron::Discord;

class TestEntities : public QObject {
    Q_OBJECT
private slots:
    void testUserFromJson();
    void testMessageFromJson();
    void testGuildFromJson();
    void testChannelFromJson();
    void testReadyFromJson();
    void testEmbedFromJson();
};

void TestEntities::testUserFromJson()
{
    QJsonObject obj{
        {"id", "123456789"},
        {"username", "testuser"},
        {"global_name", "Test User"},
        {"avatar", "abc123"},
        {"bot", false},
        {"public_flags", 0},
        {"premium_type", 2}
    };

    User user = User::fromJson(obj);
    QCOMPARE(user.id.get(), Snowflake(123456789ull));
    QCOMPARE(user.username.get(), QString("testuser"));
    QCOMPARE(user.globalName.get(), QString("Test User"));
    QCOMPARE(user.avatar.get(), QString("abc123"));
    QCOMPARE(user.bot.hasValue(), true);
    QCOMPARE(user.bot.get(), false);
    QCOMPARE(user.getDisplayName(), QString("Test User"));
}

void TestEntities::testMessageFromJson()
{
    QJsonObject author{
        {"id", "111"},
        {"username", "author"},
        {"avatar", nullptr}
    };
    QJsonObject obj{
        {"id", "999"},
        {"channel_id", "555"},
        {"author", author},
        {"content", "Hello world"},
        {"timestamp", "2026-01-01T00:00:00+00:00"},
        {"type", 0},
        {"flags", 0}
    };

    Message msg = Message::fromJson(obj);
    QCOMPARE(msg.id.get(), Snowflake(999ull));
    QCOMPARE(msg.channelId.get(), Snowflake(555ull));
    QCOMPARE(msg.content.get(), QString("Hello world"));
    QVERIFY(msg.author.hasValue());
    QCOMPARE(msg.author->username.get(), QString("author"));
}

QTEST_MAIN(TestEntities)
#include "tst_Entities.moc"
```

**CMakeLists.txt addition:**
```cmake
qt_add_executable(TestEntities
    tests/tst_Entities.cpp
    src/Discord/Entities.hpp
    src/Discord/Events.hpp
    src/Core/JsonUtils.hpp
)
target_link_libraries(TestEntities PRIVATE Qt::Core Qt::Test)
target_include_directories(TestEntities PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME EntityTests COMMAND TestEntities)
```

#### 1.2 Permission Tests (`tests/tst_Permissions.cpp`)

Test the permission computation logic in `PermissionComputer`.

```cpp
// tests/tst_Permissions.cpp
#include "Core/PermissionComputer.hpp"
#include <QTest>

using namespace Acheron::Core;
using namespace Acheron::Discord;

class TestPermissions : public QObject {
    Q_OBJECT
private slots:
    void testBasicPermissions();
    void testAdminOverride();
    void testRoleHierarchy();
    void testChannelOverwrites();
};

void TestPermissions::testBasicPermissions()
{
    PermissionComputer computer;
    // ... setup roles, member, overwrites ...
    // Permissions result = computer.compute(channelId, memberId, guildRoles, memberRoles, overwrites);
    // QVERIFY(result.has(Permission::VIEW_CHANNEL));
}

QTEST_MAIN(TestPermissions)
#include "tst_Permissions.moc"
```

#### 1.3 Gateway Heartbeat/Reconnect Logic Tests

Test the reconnection state machine without a real WebSocket (use mocked CURL or injectable transport).

#### 1.4 Storage Repository Tests (`tests/tst_Storage.cpp`)

Use in-memory SQLite to test CRUD for each repository:
- AccountRepository
- GuildRepository
- ChannelRepository
- MessageRepository
- UserRepository
- RoleRepository
- MemberRepository

#### 1.5 Markdown Parser Expansion

Current test only covers newlines. Add tests for:
- Bold/italic
- Code blocks (inline + fenced)
- Links
- Strikethrough
- Headers
- Lists (ordered/unordered)
- Blockquotes
- Mixed formatting

#### 1.6 Voice System Tests

Test RTP packet serialization, jitter buffer behavior, Opus encode/decode roundtrip, noise suppression.

#### 1.7 Theme System Tests

Test token resolution, stylesheet generation, font loading, dark/light switching.

### Files to Create
- `tests/tst_Entities.cpp`
- `tests/tst_Permissions.cpp`
- `tests/tst_Storage.cpp`
- `tests/tst_MarkdownExtended.cpp`
- `tests/tst_Voice.cpp`
- `tests/tst_Theme.cpp`

### Estimated Effort
- **2-3 weeks** for comprehensive coverage
- Add each test alongside the subsystem changes below

---

## 2. Missing Gateway Events (🔴)

### Problem
`Gateway.cpp` handles ~20 dispatch event types. Discord's API defines **50+** event types. Many are silently ignored, meaning the client misses critical state changes.

### Current handlers in `Gateway.cpp` (from `Gateway.hpp` lines 50-78):
- `READY`, `READY_SUPPLEMENTAL`
- `MESSAGE_CREATE`, `MESSAGE_UPDATE`, `MESSAGE_DELETE`
- `TYPING_START`
- `CHANNEL_CREATE`, `CHANNEL_UPDATE`, `CHANNEL_DELETE`
- `GUILD_CREATE`, `GUILD_MEMBERS_CHUNK`, `GUILD_MEMBER_UPDATE`
- `GUILD_ROLE_CREATE`, `GUILD_ROLE_UPDATE`, `GUILD_ROLE_DELETE`
- `MESSAGE_ACK`
- `MESSAGE_REACTION_ADD/MANY/REMOVE/REMOVE_ALL/REMOVE_EMOJI`
- `USER_GUILD_SETTINGS_UPDATE`
- `GUILD_MEMBER_LIST_UPDATE`
- `VOICE_STATE_UPDATE`, `VOICE_SERVER_UPDATE`
- `RELATIONSHIP_ADD/UPDATE/REMOVE`
- `USER_NOTE_UPDATE`

### Missing Events & Implementation

#### 2.1 Guild Lifecycle Events

**`GUILD_UPDATE`** — Guild name, icon, owner changed.

```cpp
// In Gateway.hpp
void gatewayGuildUpdate(const GatewayGuild &data);

// In Gateway.cpp::handleDispatch
if (data.type == "GUILD_UPDATE")
    return handleGuildUpdate(data);

// New handler
void Gateway::handleGuildUpdate(const Inbound &data)
{
    GatewayGuild guild = GatewayGuild::fromJson(data.data->toObject());
    emit gatewayGuildUpdate(guild);
}
```

**`GUILD_DELETE`** — Guild removed or became unavailable.
```cpp
struct GuildDelete : Core::JsonUtils::JsonObject {
    Field<Core::Snowflake> id;
    Field<bool, true> unavailable;

    static GuildDelete fromJson(const QJsonObject &obj) {
        GuildDelete event;
        get(obj, "id", event.id);
        get(obj, "unavailable", event.unavailable);
        return event;
    }
};
```

**`GUILD_BAN_ADD` / `GUILD_BAN_REMOVE`** — For audit display.
```cpp
struct GuildBan : Core::JsonUtils::JsonObject {
    Field<Core::Snowflake> guildId;
    Field<User> user;

    static GuildBan fromJson(const QJsonObject &obj) {
        GuildBan event;
        get(obj, "guild_id", event.guildId);
        get(obj, "user", event.user);
        return event;
    }
};
```

#### 2.2 Emoji & Sticker Updates

```cpp
struct GuildEmojisUpdate : Core::JsonUtils::JsonObject {
    Field<Core::Snowflake> guildId;
    Field<QList<Emoji>> emojis;  // Emoji struct needs updating

    static GuildEmojisUpdate fromJson(const QJsonObject &obj) {
        GuildEmojisUpdate event;
        get(obj, "guild_id", event.guildId);
        get(obj, "emojis", event.emojis);
        return event;
    }
};
```

`Emoji` in `Entities.hpp` needs expansion with `Field<bool, true> available;`, `Field<QString, true> requireColons;`, `Field<QString, true, true> roleIds;`, `Field<bool, true> managed;`.

#### 2.3 Thread Events

See Section 3 below for the full thread event system.

#### 2.4 Presence Update

```cpp
struct Activity : Core::JsonUtils::JsonObject {
    Field<QString> name;
    Field<int> type;
    Field<QString, true, true> url;
    Field<QDateTime, true> createdAt;
    Field<QString, true, true> details;
    Field<QString, true, true> state;
    Field<Core::Snowflake, true, true> applicationId;
    Field<QString, true, true> emoji;
    // ... minimal fields for display

    static Activity fromJson(const QJsonObject &obj) {
        Activity act;
        get(obj, "name", act.name);
        get(obj, "type", act.type);
        get(obj, "url", act.url);
        get(obj, "created_at", act.createdAt);
        get(obj, "details", act.details);
        get(obj, "state", act.state);
        get(obj, "application_id", act.applicationId);
        return act;
    }
};

struct PresenceUpdate : Core::JsonUtils::JsonObject {
    Field<User> user;
    Field<Core::Snowflake, true> guildId;
    Field<QString> status;  // "online", "idle", "dnd", "offline"
    Field<QList<Activity>, true> activities;
    Field<QString, true, true> platform;  // "desktop", "mobile", "web"
    Field<QString, true, true> clientStatus; // JSON object

    static PresenceUpdate fromJson(const QJsonObject &obj) {
        PresenceUpdate presence;
        get(obj, "user", presence.user);
        get(obj, "guild_id", presence.guildId);
        get(obj, "status", presence.status);
        get(obj, "activities", presence.activities);
        get(obj, "client_status", presence.clientStatus);
        return presence;
    }
};
```

#### 2.5 Webhook, Invite, Integration Events

```cpp
struct WebhooksUpdate : Core::JsonUtils::JsonObject {
    Field<Core::Snowflake> guildId;
    Field<Core::Snowflake> channelId;

    static WebhooksUpdate fromJson(const QJsonObject &obj) {
        WebhooksUpdate event;
        get(obj, "guild_id", event.guildId);
        get(obj, "channel_id", event.channelId);
        return event;
    }
};

struct InviteCreate : Core::JsonUtils::JsonObject {
    Field<Core::Snowflake> channelId;
    Field<QString> code;
    Field<QDateTime> createdAt;
    Field<Core::Snowflake, true> guildId;
    Field<User, true> inviter;
    Field<int, true> maxAge;
    Field<int, true> maxUses;
    Field<int, true> uses;
    // ...

    static InviteCreate fromJson(const QJsonObject &obj);
};
```

#### 2.6 Stage Instance & Scheduled Events

```cpp
struct StageInstance : Core::JsonUtils::JsonObject {
    Field<Core::Snowflake> id;
    Field<Core::Snowflake> guildId;
    Field<Core::Snowflake> channelId;
    Field<QString> topic;
    // ...

    static StageInstance fromJson(const QJsonObject &obj);
};

struct GuildScheduledEvent : Core::JsonUtils::JsonObject {
    Field<Core::Snowflake> id;
    Field<Core::Snowflake> guildId;
    Field<QString> name;
    Field<QString, true> description;
    Field<QDateTime> scheduledStartTime;
    Field<QDateTime, true> scheduledEndTime;
    Field<int> privacyLevel;
    Field<int> status;
    Field<int> entityType;
    Field<Core::Snowflake, true> channelId;
    // ...

    static GuildScheduledEvent fromJson(const QJsonObject &obj);
};
```

### Files to Modify
- `src/Discord/Gateway.hpp` — add signal/handler declarations
- `src/Discord/Gateway.cpp` — add dispatch routing + handler implementations
- `src/Discord/Events.hpp` — add new event structs
- `src/Discord/Entities.hpp` — expand `Emoji` struct
- `src/Discord/Client.hpp` — propagate new signals
- `src/Discord/Client.cpp` — relay new signals
- `tests/tst_Entities.cpp` — test new structs

### Estimated Effort
- **1-2 weeks** for all events
- Presence updates are the most complex (activity tracking)

---

## 3. Threads & Forums (🔴)

### Problem
Discord's thread system (public threads, private threads, forum channels) has **zero support**. The `Channel` entity (`Entities.hpp`) has no thread metadata fields, and no thread events are handled.

### Required Changes

#### 3.1 Expand Channel Entity

Add thread-specific fields to `Channel`:

```cpp
// src/Discord/Entities.hpp — Channel struct additions
struct Channel : Core::JsonUtils::JsonObject
{
    // ... existing fields ...

    // Thread-specific
    Field<int, true, true> threadMetadata_autoArchiveDuration;
    Field<QDateTime, true, true> threadMetadata_archiveTimestamp;
    Field<bool, true, true> threadMetadata_archived;
    Field<bool, true, true> threadMetadata_locked;
    Field<bool, true, true> threadMetadata_invitable;
    Field<QDateTime, true, true> threadMetadata_createTimestamp;
    Field<int, true, true> totalMessageSent;    // thread
    Field<int, true, true> memberCount;         // thread
    Field<int, true, true> messageCount;        // thread
    Field<int, true, true> memberIdsPreview;    // thread member preview
    Field<int, true, true> defaultAutoArchiveDuration; // forum
    Field<QString, true, true> topic;           // forum
    Field<int, true, true> defaultSortOrder;    // forum (0 = latest, 1 = creation)
    Field<int, true, true> defaultForumLayout;  // forum (0 = not set, 1 = list, 2 = gallery)
    Field<bool, true, true> newlyCreated;       // gateway only

    static Channel fromJson(const QJsonObject &obj)
    {
        Channel channel;
        // ... existing gets ...
        // Thread metadata (nested object)
        if (obj.contains("thread_metadata")) {
            QJsonObject tm = obj["thread_metadata"].toObject();
            get(tm, "auto_archive_duration", channel.threadMetadata_autoArchiveDuration);
            get(tm, "archive_timestamp", channel.threadMetadata_archiveTimestamp);
            get(tm, "archived", channel.threadMetadata_archived);
            get(tm, "locked", channel.threadMetadata_locked);
            get(tm, "invitable", channel.threadMetadata_invitable);
            get(tm, "create_timestamp", channel.threadMetadata_createTimestamp);
        }
        get(obj, "total_message_sent", channel.totalMessageSent);
        get(obj, "member_count", channel.memberCount);
        get(obj, "message_count", channel.messageCount);
        get(obj, "default_auto_archive_duration", channel.defaultAutoArchiveDuration);
        get(obj, "topic", channel.topic);
        get(obj, "default_sort_order", channel.defaultSortOrder);
        get(obj, "default_forum_layout", channel.defaultForumLayout);
        get(obj, "newly_created", channel.newlyCreated);
        return channel;
    }
};
```

#### 3.2 Add Thread Events

```cpp
// src/Discord/Events.hpp
struct ThreadCreate : Core::JsonUtils::JsonObject {
    Field<Channel> channel;

    static ThreadCreate fromJson(const QJsonObject &obj) {
        ThreadCreate event;
        event.channel = Channel::fromJson(obj);
        return event;
    }
};

struct ThreadUpdate : Core::JsonUtils::JsonObject {
    Field<Channel> channel;

    static ThreadUpdate fromJson(const QJsonObject &obj) {
        ThreadUpdate event;
        event.channel = Channel::fromJson(obj);
        return event;
    }
};

struct ThreadDelete : Core::JsonUtils::JsonObject {
    Field<Core::Snowflake> id;
    Field<Core::Snowflake> guildId;
    Field<Core::Snowflake> parentId;
    Field<ChannelType> type;

    static ThreadDelete fromJson(const QJsonObject &obj) {
        ThreadDelete event;
        get(obj, "id", event.id);
        get(obj, "guild_id", event.guildId);
        get(obj, "parent_id", event.parentId);
        get(obj, "type", event.type);
        return event;
    }
};

struct ThreadListSync : Core::JsonUtils::JsonObject {
    Field<Core::Snowflake> guildId;
    Field<QList<Core::Snowflake>, true> channelIds;        // parent channels
    Field<QList<Channel>> threads;
    Field<QList<Channel>, true> members;                    // thread members

    static ThreadListSync fromJson(const QJsonObject &obj) {
        ThreadListSync event;
        get(obj, "guild_id", event.guildId);
        get(obj, "channel_ids", event.channelIds);
        get(obj, "threads", event.threads);
        get(obj, "members", event.members);
        return event;
    }
};

struct ThreadMemberUpdate : Core::JsonUtils::JsonObject {
    Field<Core::Snowflake> id;          // thread id
    Field<Core::Snowflake> guildId;
    Field<Core::Snowflake> userId;
    Field<QDateTime> joinTimestamp;
    Field<int> flags;

    static ThreadMemberUpdate fromJson(const QJsonObject &obj) {
        ThreadMemberUpdate event;
        get(obj, "id", event.id);
        get(obj, "guild_id", event.guildId);
        get(obj, "user_id", event.userId);
        get(obj, "join_timestamp", event.joinTimestamp);
        get(obj, "flags", event.flags);
        return event;
    }
};
```

#### 3.3 Wire Thread Events in Gateway

```cpp
// Gateway.hpp additions
void gatewayThreadCreate(const ThreadCreate &data);
void gatewayThreadUpdate(const ThreadUpdate &data);
void gatewayThreadDelete(const ThreadDelete &data);
void gatewayThreadListSync(const ThreadListSync &data);
void gatewayThreadMemberUpdate(const ThreadMemberUpdate &data);

// Gateway::handleDispatch additions
if (data.type == "THREAD_CREATE")       return handleThreadCreate(data);
if (data.type == "THREAD_UPDATE")       return handleThreadUpdate(data);
if (data.type == "THREAD_DELETE")       return handleThreadDelete(data);
if (data.type == "THREAD_LIST_SYNC")    return handleThreadListSync(data);
if (data.type == "THREAD_MEMBER_UPDATE") return handleThreadMemberUpdate(data);

// Example handler
void Gateway::handleThreadCreate(const Inbound &data)
{
    ThreadCreate event = ThreadCreate::fromJson(data.data->toObject());
    emit gatewayThreadCreate(event);
}
```

#### 3.4 Thread UI in Channel Tree

Extend `ChannelNode` types to include `Thread`:

```cpp
// src/UI/ChannelList/ChannelNode.hpp
enum class Type {
    Account,
    Guild,
    DMCategory,
    DMChannel,
    Category,
    Channel,
    VoiceChannel,
    Thread,        // NEW
    VoiceChannel,  // existing
};
```

Render threads as nested children under their parent channel in `ChannelDelegate` with a `#` prefix and a thread icon. Add active-thread count badge to parent channels.

#### 3.5 Thread-Specific Controls in ChatView

- "Open Thread" button on messages with thread replies
- "Create Thread" from message context menu
- Thread archive/lock status display
- "Join Thread" / "Leave Thread" controls
- Auto-archive duration indicator

#### 3.6 Forum Channel Rendering

Forum channels should show a list of threads (posts) instead of a normal chat view. This requires a new `ForumView` or a mode switch in `ChannelPane`:

```cpp
// src/UI/ChannelPane.cpp
void ChannelPane::setChannel(Core::Snowflake channelId, Core::Snowflake guildId,
                             Discord::ChannelType type)
{
    if (type == Discord::ChannelType::GUILD_FORUM) {
        forumView->show();
        chatView->hide();
        forumView->loadPosts(channelId);
    } else {
        forumView->hide();
        chatView->show();
        // existing switch logic
    }
}
```

A forum post (thread) should show:
- Title (thread name)
- First message content preview
- Tags
- Reply count
- Last activity timestamp

### Files to Create
- `src/UI/ChannelList/ThreadDelegate.cpp` (+ `.hpp`)
- `src/UI/ForumView.cpp` (+ `.hpp`)

### Files to Modify
- `src/Discord/Entities.hpp` — expand Channel
- `src/Discord/Events.hpp` — add thread event structs
- `src/Discord/Gateway.hpp` + `.cpp` — wire events
- `src/Discord/Client.hpp` + `.cpp` — propagate events
- `src/Core/ClientInstance.cpp` — handle thread changes
- `src/UI/ChannelList/ChannelNode.hpp` — add Thread type
- `src/UI/ChannelList/ChannelTreeModel.cpp` — thread tree insertion
- `src/UI/ChannelList/ChannelDelegate.cpp` — thread rendering
- `src/UI/ChannelPane.cpp` — forum mode

### Estimated Effort
- **3-4 weeks** for full thread + forum support

---

## 4. Message Display Gaps (🟡)

### 4.1 Rich Embed Rendering

**Problem**: `Embed` data is fully parsed (`Entities.hpp` lines 373-407) and stored as `embedsJson` on `Message` (line 588), but never rendered in `ChatDelegate::paint()`.

**Implementation in `ChatDelegate.cpp`**:

```cpp
// New helper in ChatDelegate
void ChatDelegate::paintEmbed(QPainter *painter, const QRect &embedRect,
                              const Discord::Embed &embed, const QModelIndex &index) const
{
    painter->save();
    int y = embedRect.y();

    // Left accent border
    if (embed.color.hasValue()) {
        QColor accent(embed.color.get());
        painter->fillRect(embedRect.x(), y, 4, embedRect.height(), accent);
    }

    // Background
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(30, 30, 30, 200));
    painter->drawRoundedRect(embedRect, 4, 4);
    painter->restore();

    int x = embedRect.x() + 12; // padding after accent

    // Author
    if (embed.author.hasValue()) {
        QFont authorFont = painter->font();
        authorFont.setBold(true);
        authorFont.setPointSize(authorFont.pointSize() - 1);
        painter->setFont(authorFont);
        painter->setPen(palette.link().color());
        if (embed.author->iconUrl.hasValue()) {
            // fetch & draw author icon
        }
        painter->drawText(x + 22, y + 16, embed.author->name.get());
        y += 22;
    }

    // Title
    if (embed.title.hasValue()) {
        QFont titleFont = painter->font();
        titleFont.setBold(true);
        painter->setFont(titleFont);
        painter->setPen(palette.link().color());
        QRect titleRect(x, y, embedRect.width() - 24, 0);
        titleRect = painter->boundingRect(titleRect, Qt::AlignLeft | Qt::TextWordWrap,
                                          embed.title.get());
        painter->drawText(titleRect, embed.title.get());
        y = titleRect.bottom() + 4;
    }

    // Description — use Markdown parser
    if (embed.description.hasValue()) {
        painter->setPen(palette.text().color());
        QRect descRect(x, y, embedRect.width() - 24, 0);
        descRect = painter->boundingRect(descRect, Qt::AlignLeft | Qt::TextWordWrap,
                                         embed.description.get());
        // Convert markdown to rich text via Markdown::Parser
        painter->drawText(descRect, embed.description.get());
        y = descRect.bottom() + 4;
    }

    // Fields (2-column layout for inline fields)
    if (embed.fields.hasValue()) {
        for (const auto &field : embed.fields.get()) {
            // name (bold) + value (normal)
            // inline fields sit side-by-side, others full width
            y += paintEmbedField(painter, x, y, embedRect.width(), field);
        }
    }

    // Image
    if (embed.image.hasValue() && embed.image->url.hasValue()) {
        // Load image via ImageManager, draw scaled to max width
    }

    // Footer + Timestamp
    if (embed.footer.hasValue() || embed.timestamp.hasValue()) {
        y += 4;
        QFont footerFont = painter->font();
        footerFont.setPointSize(footerFont.pointSize() - 2);
        painter->setFont(footerFont);
        painter->setPen(palette.color(QPalette::Mid));
        QString footer;
        if (embed.footer.hasValue())
            footer += embed.footer->text.get();
        if (embed.timestamp.hasValue())
            footer += (footer.isEmpty() ? "" : " • ") + embed.timestamp->toLocalTime().toString();
        painter->drawText(x, y + 12, footer);
    }
}
```

#### 4.2 Sticker Support

Stickers come in the `Message.stickerItems` field (new):

```cpp
struct StickerItem : Core::JsonUtils::JsonObject {
    Field<Core::Snowflake> id;
    Field<QString> name;
    Field<int> formatType;  // 1=PNG, 2=APNG, 3=LOTTIE

    static StickerItem fromJson(const QJsonObject &obj) {
        StickerItem item;
        get(obj, "id", item.id);
        get(obj, "name", item.name);
        get(obj, "format_type", item.formatType);
        return item;
    }
};
```

Add to `Message`:
```cpp
Field<QList<StickerItem>, true> stickerItems;
```

Render in `ChatDelegate`:
```cpp
// After embed rendering, before reactions
if (msg.stickerItems.hasValue()) {
    for (const auto &sticker : msg.stickerItems.get()) {
        QString url = QString("https://cdn.discordapp.com/stickers/%1.png")
                         .arg(static_cast<quint64>(sticker.id.get()));
        // Load via ImageManager, draw at 160x160 or sticker size
    }
}
```

#### 4.3 Code Block Syntax Highlighting

Use a simple line-by-line approach (no external highlighter needed for basic keywords):

```cpp
// In ChatLayout.cpp, when processing markdown code blocks
void ChatLayout::highlightSyntax(QTextDocument *doc, const QString &language)
{
    if (language.isEmpty())
        return;

    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::Start);

    // Simple keyword highlighting
    QStringList keywords;
    if (language == "cpp" || language == "c" || language == "c++") {
        keywords = {"int", "void", "class", "struct", "if", "else", "for",
                    "while", "return", "auto", "const", "static", "namespace",
                    "template", "include", "public", "private", "protected"};
    } else if (language == "python" || language == "py") {
        keywords = {"def", "class", "if", "elif", "else", "for", "while",
                    "import", "from", "return", "as", "try", "except", "finally",
                    "True", "False", "None", "with", "yield"};
    } else if (language == "js" || language == "javascript" || language == "ts") {
        keywords = {"const", "let", "var", "function", "class", "if", "else",
                    "for", "while", "return", "import", "export", "async", "await",
                    "true", "false", "null", "undefined"};
    } else if (language == "json") {
        // Highlight keys vs values
    }

    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(QColor("#569CD6")); // VS Code blue
    keywordFormat.setFontWeight(QFont::Bold);

    for (const auto &kw : keywords) {
        cursor.movePosition(QTextCursor::Start);
        while (cursor.find(kw)) {
            // Only match whole words
            cursor.mergeCharFormat(keywordFormat);
        }
    }

    // String literals (green)
    QTextCharFormat stringFormat;
    stringFormat.setForeground(QColor("#6A9955"));
    // ... find "..." and '...' patterns ...
}
```

#### 4.4 Date Separators

Add date-change detection in `ChatModel`:

```cpp
// When inserting messages, detect date boundary
void ChatModel::insertMessages(const QList<Discord::Message> &messages, int position)
{
    QSet<QDate> dateBoundaries;
    for (const auto &msg : messages) {
        QDate msgDate = msg.timestamp.get().date();
        if (!dateBoundaries.contains(msgDate)) {
            dateBoundaries.insert(msgDate);
            // Insert a "DateSeparator" special row before this message
            MessageDateSeparator sep{msgDate};
            internalMessages.insert(msgIdx, sep);
        }
    }
}
```

In `ChatDelegate`, render date separators as centered text like **--- January 15, 2026 ---**.

### Files to Create
- None new; expand existing files

### Files to Modify
- `src/UI/Chat/ChatDelegate.cpp` — embed, sticker, code block rendering
- `src/UI/Chat/ChatLayout.cpp` — syntax highlighting pass
- `src/UI/Chat/ChatModel.cpp` — date separators
- `src/Discord/Entities.hpp` — add `StickerItem`, expand `Message`

### Estimated Effort
- **2-3 weeks** for full embed + sticker + code highlighting

---

## 5. Voice System (🟡)

### Problem
Voice works for basic connect/listen/speak but lacks many Discord voice features.

### 5.1 Screen Share / Streaming

Integration with DXGI (Windows) or PipeWire (Linux) to capture and encode screen frames:

```cpp
// src/Core/AV/ScreenCapture.hpp
#pragma once
#include <QObject>
#include <QImage>
#include <QTimer>

namespace Acheron { namespace Core { namespace AV {

class ScreenCapture : public QObject {
    Q_OBJECT
public:
    explicit ScreenCapture(QObject *parent = nullptr);
    ~ScreenCapture();

    bool startCapture(int monitorIndex = 0, int fps = 30);
    void stopCapture();
    bool isCapturing() const { return m_capturing; }

    void setFramerate(int fps);

signals:
    void frameCaptured(const QImage &frame);

private slots:
    void captureFrame();

private:
    bool m_capturing = false;
    QTimer *m_timer = nullptr;
    int m_fps = 30;
    int m_monitorIndex = 0;
};

} } } // namespace Acheron::Core::AV
```

Windows implementation using DXGI:
```cpp
// src/Core/AV/ScreenCapture.cpp
#include "ScreenCapture.hpp"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <comdef.h>

namespace Acheron { namespace Core { namespace AV {

ScreenCapture::ScreenCapture(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &ScreenCapture::captureFrame);
}

ScreenCapture::~ScreenCapture()
{
    stopCapture();
}

bool ScreenCapture::startCapture(int monitorIndex, int fps)
{
    m_monitorIndex = monitorIndex;
    m_fps = fps;
    // Initialize DXGI, create duplication interface for the monitor
    // ...
    m_capturing = true;
    m_timer->start(1000 / fps);
    return true;
}

void ScreenCapture::stopCapture()
{
    m_timer->stop();
    m_capturing = false;
    // Release DXGI resources
}

void ScreenCapture::captureFrame()
{
    // Acquire next frame from DXGI duplication
    // Copy to QImage
    // emit frameCaptured(image);
}

} } } // namespace Acheron::Core::AV
```

Voice client needs VP8/VP9/H264 encoding for video frames (libvpx or NVIDIA NVENC).

#### 5.2 Push-to-Talk

```cpp
// src/Core/AV/KeyBindManager.hpp
#pragma once
#include <QObject>
#include <QKeySequence>
#include <QHash>
#include <QSet>

namespace Acheron { namespace Core { namespace AV {

class KeyBindManager : public QObject {
    Q_OBJECT
public:
    enum class Action {
        PushToTalk,
        PushToMute,
        Deafen,
        ToggleMute,
    };

    explicit KeyBindManager(QObject *parent = nullptr);

    void setBinding(Action action, const QKeySequence &key);
    QKeySequence binding(Action action) const;
    void loadFromSettings();
    void saveToSettings();

    bool isActionActive(Action action) const;

signals:
    void actionTriggered(Action action, bool active);

private:
    QHash<Action, QKeySequence> m_bindings;
    QSet<Qt::Key> m_pressedKeys;
};

} } } // namespace Acheron::Core::AV
```

#### 5.3 Per-User Volume Controls

```cpp
// In VoiceManager
QHash<Core::Snowflake, float> userVolumes; // 0.0 to 2.0, default 1.0

void VoiceManager::setUserVolume(Core::Snowflake userId, float volume)
{
    userVolumes[userId] = qBound(0.0f, volume, 2.0f);
    AudioMixer::instance().setGain(userId, userVolumes[userId]);
    QSettings().setValue(
        QString("voice/volume/%1").arg(static_cast<quint64>(userId)),
        static_cast<double>(volume));
}
```

#### 5.4 Voice State UI

Add to `VoiceStatusBar` or a new `VoicePanel`:
- Connection quality indicator (green/yellow/red based on RTT/packet loss)
- Speaking indicator (who is talking)
- Stream indicator (who is sharing screen)
- Server mute/deafen status

### Files to Create
- `src/Core/AV/ScreenCapture.hpp` + `.cpp`
- `src/Core/AV/VideoEncoder.hpp` + `.cpp`
- `src/Core/AV/KeyBindManager.hpp` + `.cpp`
- `src/UI/VoicePanel.cpp` + `.hpp` (expanded voice controls)

### Files to Modify
- `src/Core/AV/VoiceManager.cpp` — user volumes, screen share init
- `src/UI/VoiceStatusBar.cpp` — quality, speaking indicators
- `src/UI/Settings/` — add Voice/Audio settings page
- `CMakeLists.txt` — new sources

### Estimated Effort
- **4-6 weeks** for full screen share; **1-2 weeks** for PTT + volume controls

---

## 6. Settings UI (🟡)

### Problem
Only 2 settings pages (`GeneralPage`, `AppearancePage`). Many essential settings categories are missing.

### 6.1 Architecture

Create an extensible settings page system:

```cpp
// src/UI/Settings/SettingsPage.hpp
#pragma once
#include <QWidget>

namespace Acheron { namespace UI {

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);
    virtual QString title() const = 0;
    virtual QString icon() const = 0;  // icon name/path
    virtual void apply() = 0;
    virtual void reset() = 0;
};

} } // namespace Acheron::UI
```

### 6.2 Required Settings Pages

| Page | Key Settings |
|------|-------------|
| **Voice & Audio** | Input/output device, input sensitivity (VAD), push-to-talk keybind, echo cancellation toggle, noise suppression toggle, per-user volumes, attenuation |
| **Notifications** | Enable/disable sounds, per-guild notification overrides, per-channel overrides, mention highlights, message preview toggle |
| **Keybinds** | List of all shortcuts (editable), chord detection, reset to defaults |
| **Privacy & Safety** | Direct message settings, friend request settings, explicit content filter, activity sharing |
| **Advanced** | Developer mode (copy IDs), logging level, gateway debug, cache settings, connection proxy |
| **Accessibility** | Font scaling, reduce motion, reduced transparency, colorblind mode, screen reader support |
| **Language** | Locale selection (single `.ts` file loaded), translation status |

### 6.3 Voice Settings Page Example

```cpp
// src/UI/Settings/VoicePage.hpp
#pragma once
#include "SettingsPage.hpp"
#include <QComboBox>
#include <QCheckBox>
#include <QSlider>
#include <QLabel>

namespace Acheron { namespace UI { namespace Settings {

class VoicePage : public SettingsPage {
    Q_OBJECT
public:
    explicit VoicePage(QWidget *parent = nullptr);

    QString title() const override { return tr("Voice & Audio"); }
    QString icon() const override { return "mic"; }
    void apply() override;
    void reset() override;

signals:
    void inputDeviceChanged(int index);
    void outputDeviceChanged(int index);
    void sensitivityChanged(float threshold);

private:
    QComboBox *m_inputDevice;
    QComboBox *m_outputDevice;
    QSlider *m_inputVolume;
    QSlider *m_outputVolume;
    QCheckBox *m_echoCancellation;
    QCheckBox *m_noiseSuppression;
    QCheckBox *m_automaticGainControl;
    QCheckBox *m_pushToTalk;
    QSlider *m_sensitivitySlider;
    QLabel *m_sensitivityLabel;
    QSlider *m_attenuationSlider;
};

} } } // namespace Acheron::UI::Settings
```

```cpp
// src/UI/Settings/VoicePage.cpp
#include "VoicePage.hpp"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSettings>

namespace Acheron { namespace UI { namespace Settings {

VoicePage::VoicePage(QWidget *parent)
    : SettingsPage(parent)
{
    auto *layout = new QVBoxLayout(this);

    // Input section
    auto *inputGroup = new QGroupBox(tr("Input Device"), this);
    auto *inputForm = new QFormLayout(inputGroup);
    m_inputDevice = new QComboBox(this);
    // Enumerate audio devices via miniaudio
    m_inputDevice->addItem(tr("Default"), QString());
    m_inputDevice->addItem("Microphone (Realtek Audio)", "realtek-mic");
    inputForm->addRow(tr("Device:"), m_inputDevice);

    m_inputVolume = new QSlider(Qt::Horizontal, this);
    m_inputVolume->setRange(0, 200);
    m_inputVolume->setValue(100);
    inputForm->addRow(tr("Input Volume:"), m_inputVolume);
    layout->addWidget(inputGroup);

    // Output section
    auto *outputGroup = new QGroupBox(tr("Output Device"), this);
    auto *outputForm = new QFormLayout(outputGroup);
    m_outputDevice = new QComboBox(this);
    outputForm->addRow(tr("Device:"), m_outputDevice);

    m_outputVolume = new QSlider(Qt::Horizontal, this);
    m_outputVolume->setRange(0, 200);
    m_outputVolume->setValue(100);
    outputForm->addRow(tr("Output Volume:"), m_outputVolume);
    layout->addWidget(outputGroup);

    // Processing section
    auto *processingGroup = new QGroupBox(tr("Audio Processing"), this);
    auto *processingForm = new QFormLayout(processingGroup);
    m_echoCancellation = new QCheckBox(tr("Echo Cancellation"), this);
    m_echoCancellation->setChecked(true);
    processingForm->addRow(m_echoCancellation);

    m_noiseSuppression = new QCheckBox(tr("Noise Suppression"), this);
    m_noiseSuppression->setChecked(true);
    processingForm->addRow(m_noiseSuppression);

    m_automaticGainControl = new QCheckBox(tr("Automatic Gain Control"), this);
    processingForm->addRow(m_automaticGainControl);
    layout->addWidget(processingGroup);

    // Input sensitivity
    auto *sensitivityGroup = new QGroupBox(tr("Input Sensitivity"), this);
    auto *sensitivityForm = new QFormLayout(sensitivityGroup);
    m_pushToTalk = new QCheckBox(tr("Push to Talk"), this);
    sensitivityForm->addRow(m_pushToTalk);

    m_sensitivitySlider = new QSlider(Qt::Horizontal, this);
    m_sensitivitySlider->setRange(0, 100);
    m_sensitivitySlider->setValue(50);
    m_sensitivityLabel = new QLabel(tr("50%"), this);
    auto *sensRow = new QHBoxLayout;
    sensRow->addWidget(m_sensitivitySlider);
    sensRow->addWidget(m_sensitivityLabel);
    sensitivityForm->addRow(tr("Sensitivity:"), sensRow);

    m_attenuationSlider = new QSlider(Qt::Horizontal, this);
    m_attenuationSlider->setRange(0, 100);
    m_attenuationSlider->setValue(80);
    sensitivityForm->addRow(tr("Attenuation:"), m_attenuationSlider);
    layout->addWidget(sensitivityGroup);

    layout->addStretch();

    // Load settings
    reset();

    connect(m_sensitivitySlider, &QSlider::valueChanged, this, [this](int val) {
        m_sensitivityLabel->setText(QStringLiteral("%1%").arg(val));
    });
}

void VoicePage::apply()
{
    QSettings s;
    s.setValue("voice/inputDevice", m_inputDevice->currentData().toString());
    s.setValue("voice/outputDevice", m_outputDevice->currentData().toString());
    s.setValue("voice/inputVolume", m_inputVolume->value());
    s.setValue("voice/outputVolume", m_outputVolume->value());
    s.setValue("voice/echoCancellation", m_echoCancellation->isChecked());
    s.setValue("voice/noiseSuppression", m_noiseSuppression->isChecked());
    s.setValue("voice/automaticGainControl", m_automaticGainControl->isChecked());
    s.setValue("voice/pushToTalk", m_pushToTalk->isChecked());
    s.setValue("voice/sensitivity", m_sensitivitySlider->value());
    s.setValue("voice/attenuation", m_attenuationSlider->value());
}

void VoicePage::reset()
{
    QSettings s;
    int inputDevIdx = m_inputDevice->findData(s.value("voice/inputDevice").toString());
    if (inputDevIdx >= 0) m_inputDevice->setCurrentIndex(inputDevIdx);
    m_inputVolume->setValue(s.value("voice/inputVolume", 100).toInt());
    m_outputVolume->setValue(s.value("voice/outputVolume", 100).toInt());
    m_echoCancellation->setChecked(s.value("voice/echoCancellation", true).toBool());
    m_noiseSuppression->setChecked(s.value("voice/noiseSuppression", true).toBool());
    m_automaticGainControl->setChecked(s.value("voice/automaticGainControl", false).toBool());
    m_pushToTalk->setChecked(s.value("voice/pushToTalk", false).toBool());
    int sens = s.value("voice/sensitivity", 50).toInt();
    m_sensitivitySlider->setValue(sens);
    m_sensitivityLabel->setText(QStringLiteral("%1%").arg(sens));
    m_attenuationSlider->setValue(s.value("voice/attenuation", 80).toInt());
}

} } } // namespace Acheron::UI::Settings
```

### 6.4 Wire into SettingsWindow

```cpp
// In SettingsWindow constructor
auto *voicePage = new Settings::VoicePage(this);
addPage(voicePage->title(), voicePage->icon(), voicePage);
```

### Files to Create
- `src/UI/Settings/VoicePage.hpp` + `.cpp`
- `src/UI/Settings/NotificationsPage.hpp` + `.cpp`
- `src/UI/Settings/KeybindsPage.hpp` + `.cpp`
- `src/UI/Settings/PrivacyPage.hpp` + `.cpp`
- `src/UI/Settings/AdvancedPage.hpp` + `.cpp`
- `src/UI/Settings/AccessibilityPage.hpp` + `.cpp`
- `src/UI/Settings/LanguagePage.hpp` + `.cpp`
- `src/UI/Settings/SettingsPage.hpp` (base class)

### Files to Modify
- `src/UI/Settings/SettingsWindow.cpp` — add page registration
- `CMakeLists.txt` — new sources

### Estimated Effort
- **2-3 weeks** for all pages

---

## 7. User & Social Features (🟡)

### 7.1 Friend Request Management

```cpp
// In Discord::Client
void sendFriendRequest(const QString &username, const QString &tag);  // POST /users/@me/relationships
void acceptFriendRequest(Snowflake userId);                           // PUT /users/@me/relationships/:id
void removeFriend(Snowflake userId);                                  // DELETE /users/@me/relationships/:id
void blockUser(Snowflake userId);                                     // PUT /users/@me/relationships/:id with type=2
```

Add a `FriendsPage` or integrate into the DM list with a "Friend Requests" tab showing pending incoming/outgoing requests.

### 7.2 Friends Tab in Channel List

Add a "Friends" top-level node in the channel tree showing all friends (relationship type 1) with online status indicators, grouped by status.

### 7.3 Custom Status Support

The `applyCustomStatus()` method in `MainWindow` is already declared but the full UI flow is missing:

```cpp
// In GeneralPage or a new status widget
void GeneralPage::setupCustomStatus()
{
    m_statusInput = new QLineEdit(this);
    m_statusInput->setPlaceholderText(tr("What's on your mind?"));
    m_statusInput->setMaxLength(128);
    m_statusInput->setText(QSettings().value("general/custom_status").toString());

    auto *clearBtn = new QPushButton(tr("Clear"), this);
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        m_statusInput->clear();
        QSettings().remove("general/custom_status");
        emit customStatusCleared();
    });

    connect(m_statusInput, &QLineEdit::textChanged, this, [](const QString &text) {
        QSettings().setValue("general/custom_status", text);
    });

    auto *layout = qobject_cast<QFormLayout*>(this->layout());
    if (layout)
        layout->addRow(tr("Custom Status:"), m_statusInput);
    layout->addRow(clearBtn);
}
```

### 7.4 Blocking/Unblocking from User Context Menu

```cpp
// In MainWindow::showUserContextMenu
if (relationshipType == RelationshipType::BLOCKED) {
    menu->addAction(tr("Unblock User"), [this, userId]() {
        currentInstance->discord()->removeFriend(userId);
    });
} else {
    menu->addAction(tr("Block User"), [this, userId]() {
        currentInstance->discord()->blockUser(userId);
    });
}
```

### Files to Create
- `src/UI/FriendsPage.hpp` + `.cpp`

### Files to Modify
- `src/Discord/Client.hpp` + `.cpp` — relationship management REST calls
- `src/UI/MainWindow.cpp` — context menu blocking, user notes, status
- `src/UI/Settings/GeneralPage.cpp` — custom status widget

### Estimated Effort
- **1-2 weeks**

---

## 8. Chat UI & Polish (🟡)

### 8.1 Pinned Messages Panel

```cpp
// src/UI/PinnedMessagesPanel.hpp
#pragma once
#include <QDialog>
#include <QListWidget>
#include "Core/Snowflake.hpp"

namespace Acheron { namespace UI {

class PinnedMessagesPanel : public QDialog {
    Q_OBJECT
public:
    explicit PinnedMessagesPanel(Core::Snowflake channelId,
                                  const std::function<void(Core::Snowflake)> &jumpTo,
                                  QWidget *parent = nullptr);

private:
    void loadPinned();

    QListWidget *m_list;
    Core::Snowflake m_channelId;
    std::function<void(Core::Snowflake)> m_jumpTo;
};

} } // namespace Acheron::UI
```

```cpp
// src/UI/PinnedMessagesPanel.cpp
#include "PinnedMessagesPanel.hpp"
#include <QVBoxLayout>
#include <QPushButton>
#include <QJsonArray>
#include <QJsonDocument>

namespace Acheron { namespace UI {

PinnedMessagesPanel::PinnedMessagesPanel(Core::Snowflake channelId,
                                          const std::function<void(Core::Snowflake)> &jumpTo,
                                          QWidget *parent)
    : QDialog(parent), m_channelId(channelId), m_jumpTo(jumpTo)
{
    setWindowTitle(tr("Pinned Messages"));
    setMinimumSize(400, 500);

    auto *layout = new QVBoxLayout(this);
    m_list = new QListWidget(this);
    layout->addWidget(m_list);

    auto *closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(closeBtn);

    loadPinned();
}

void PinnedMessagesPanel::loadPinned()
{
    // GET /channels/{channelId}/pins
    // Parse response and populate m_list
    // Each item shows: author name, content preview (truncated), timestamp
}

} } // namespace Acheron::UI
```

### 8.2 Attachment Gallery View

```cpp
// src/UI/AttachmentGallery.hpp
#pragma once
#include <QDialog>
#include <QScrollArea>
#include <QGridLayout>
#include "Core/Snowflake.hpp"

namespace Acheron { namespace UI {

class AttachmentGallery : public QDialog {
    Q_OBJECT
public:
    explicit AttachmentGallery(const QList<Discord::Message> &messagesWithImages,
                               QWidget *parent = nullptr);
};

} } // namespace Acheron::UI
```

### 8.3 System Tray Integration

```cpp
// src/UI/TrayManager.hpp
#pragma once
#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>

namespace Acheron { namespace UI {

class TrayManager : public QObject {
    Q_OBJECT
public:
    explicit TrayManager(QObject *parent = nullptr);
    ~TrayManager();

    void show();
    void hide();
    bool isAvailable() const;

    void setUnreadCount(int total);
    void showNotification(const QString &title, const QString &message);

signals:
    void showWindowRequested();
    void quitRequested();
    void muteToggleRequested();
    void statusChanged(const QString &status);

private:
    QSystemTrayIcon *m_trayIcon;
    QMenu *m_trayMenu;
};

} } // namespace Acheron::UI
```

```cpp
// Wire into main.cpp
#ifdef Q_OS_WINDOWS
    auto *tray = new TrayManager(&app);
    tray->show();
    QObject::connect(tray, &TrayManager::showWindowRequested, &window, &QWidget::show);
    QObject::connect(tray, &TrayManager::quitRequested, &app, &QApplication::quit);
#endif
```

### 8.4 Message Hover Reaction Bar

When hovering over a message, show a small row of 5-6 frequently-used emoji buttons + a "more" button that opens the emoji picker:

```cpp
// In ChatView::mouseMoveEvent, when hoveredRow changes:
if (hoveredRow >= 0 && hoveredRow != m_lastHoveredRow) {
    m_lastHoveredRow = hoveredRow;
    update(m_lastHoveredRow); // trigger repaint for old row
    update(hoveredRow);       // trigger repaint for new row
}

// In ChatDelegate::paint, if row is hovered:
if (isHovered && !isPending) {
    // Paint 6 small emoji buttons at bottom-right of message area
    QRect barRect(msgRect.right() - 180, msgRect.top() - 4, 180, 28);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(30, 30, 30, 230));
    painter->drawRoundedRect(barRect, 6, 6);

    // Draw emoji icons: 😂 👍 ❤️ 😮 😢 😡
    QStringList quickEmojis = {"😂", "👍", "❤️", "😮", "😢", "😡"};
    int x = barRect.x() + 4;
    for (int i = 0; i < quickEmojis.size(); ++i) {
        QRect emojiRect(x, barRect.y() + 2, 24, 24);
        painter->drawText(emojiRect, Qt::AlignCenter, quickEmojis[i]);
        // Store clickable rects for mousePressEvent
        m_reactionButtonRects[hoveredRow].append({emojiRect, quickEmojis[i]});
        x += 28;
    }

    // Plus button for more emoji
    QRect plusRect(x + 2, barRect.y() + 2, 24, 24);
    painter->setPen(QColor(180, 180, 180));
    painter->drawText(plusRect, Qt::AlignCenter, QStringLiteral("+"));
}
```

### Files to Create
- `src/UI/PinnedMessagesPanel.hpp` + `.cpp`
- `src/UI/AttachmentGallery.hpp` + `.cpp`
- `src/UI/TrayManager.hpp` + `.cpp`

### Files to Modify
- `src/UI/Chat/ChatView.cpp` — hover reaction bar
- `src/UI/Chat/ChatDelegate.cpp` — paint reaction bar
- `src/UI/MainWindow.cpp` — tray integration, pinned messages
- `CMakeLists.txt` — new sources

### Estimated Effort
- **2-3 weeks**

---

## 9. Code Quality & Architecture (🟡)

### 9.1 Refactor MainWindow

`MainWindow.cpp` is ~2000 lines. Extract:

| Responsibility | New File(s) |
|---------------|-------------|
| Notifications (sound, tray) | `src/UI/NotificationManager.cpp` |
| Channel selection logic | `src/UI/ChannelSelectionController.cpp` |
| Voice state management | `src/UI/VoiceStateManager.cpp` |
| Window management (detached, merge) | `src/UI/WindowManager.cpp` |
| Context menus | `src/UI/ContextMenuFactory.cpp` |

### 9.2 Database Access Layer

Replace direct `QSqlDatabase` access in `ClientInstance` (lines 58-101, 126-142, 330-339, 354-379, etc.) with proper repository methods. All SQL should go through `Storage::*Repository` classes.

```cpp
// Current bad pattern in ClientInstance.cpp line 58-101:
QSqlDatabase db = QSqlDatabase::database(connName);
db.transaction();
for (size_t i = 0; i < ready.guilds->size(); i++) {
    // ... inline SQL logic ...
}
db.commit();

// Refactored:
guildRepo.transaction([&]() {
    for (const auto &guild : ready.guilds.get())
        guildRepo.saveGuildFromReady(guild, ready.mergedMembers);
    userRepo.saveUsers(ready.users);
    // ...
});
```

### 9.3 Dependency Injection

Instead of `new`ing managers inside `ClientInstance`:

```cpp
// Before
ClientInstance::ClientInstance(const AccountInfo &info, ...) {
    client = new Discord::Client(info.token, info.gatewayUrl, info.restUrl, captchaResolver, this);
    userManager = new UserManager(info.id, this);
    messageManager = new MessageManager(info.id, client, userManager, this);
    // ...
}

// After — pass constructed dependencies
ClientInstance::ClientInstance(const AccountInfo &info,
                                Discord::Client *client,
                                UserManager *userManager,
                                MessageManager *messageManager,
                                PermissionManager *permissionManager,
                                ReadStateManager *readStateManager,
                                QObject *parent);
```

This makes testing trivially easy.

### 9.4 Error Handling

Create a unified error type:

```cpp
// src/Core/Error.hpp
#pragma once
#include <QString>
#include <QDebug>

namespace Acheron { namespace Core {

enum class ErrorCode {
    None,
    NetworkError,
    AuthenticationFailed,
    RateLimited,
    InvalidPayload,
    DatabaseError,
    PermissionDenied,
    NotFound,
    Unknown
};

struct Error {
    ErrorCode code = ErrorCode::None;
    QString message;
    int httpStatus = 0;

    bool isError() const { return code != ErrorCode::None; }
    static Error ok() { return {}; }

    void log() const {
        if (isError())
            qCWarning(LogCore) << "Error:" << static_cast<int>(code) << message;
    }
};

} } // namespace Acheron::Core
```

### Files to Create
- `src/UI/NotificationManager.hpp` + `.cpp`
- `src/UI/ChannelSelectionController.hpp` + `.cpp`
- `src/UI/VoiceStateManager.hpp` + `.cpp`
- `src/UI/WindowManager.hpp` + `.cpp`
- `src/UI/ContextMenuFactory.hpp` + `.cpp`
- `src/Core/Error.hpp`

### Files to Modify
- `src/UI/MainWindow.cpp` — incremental extraction
- `src/Core/ClientInstance.cpp` — use DI
- `src/Storage/*.cpp` — add `transaction()` method

### Estimated Effort
- **3-4 weeks** incremental refactor (do alongside other work)

---

## 10. Cross-Platform & Build (🟢)

### 10.1 Linux Support

- Replace `#ifdef Q_OS_WINDOWS` emoji font loading with a cross-platform fallback
- Use miniaudio's Linux backend (ALSA/PulseAudio/pipewire) — already supported by the library
- Adjust path separators in `DatabaseManager` (uses `QStandardPaths`)
- Add Linux-specific `CMakePresets.json` entry

```cmake
# CMakePresets.json addition
{
    "name": "linux-debug",
    "generator": "Ninja",
    "binaryDir": "${sourceDir}/build-linux",
    "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "USE_VCPKG": "ON",
        "VCPKG_TARGET_TRIPLET": "x64-linux"
    }
}
```

### 10.2 macOS Support

- Audio backend via miniaudio's CoreAudio support
- Bundle as `.app` with `MACOSX_BUNDLE` in CMake
- macOS-specific key handling (Cmd instead of Ctrl)
- Touch Bar support (optional)

```cmake
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.ouwou.acheron"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    )
endif()
```

### 10.3 CI/CD Pipeline

```yaml
# .github/workflows/build.yml
name: Build
on: [push, pull_request]

jobs:
  build-windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v3
        with:
          submodules: recursive
      - uses: lukka/get-cmake@latest
      - uses: lukka/run-vcpkg@v11
      - name: Configure
        run: cmake --preset win-rel
      - name: Build
        run: cmake --build --preset win-rel
      - name: Test
        run: ctest --preset win-rel

  build-linux:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
        with:
          submodules: recursive
      - name: Install Qt
        run: sudo apt-get install -y qt6-base-dev libqt6sql6-sqlite libqt6network6
      - name: Configure
        run: cmake -B build -DUSE_VCPKG=OFF
      - name: Build
        run: cmake --build build
      - name: Test
        run: ctest --test-dir build
```

### 10.4 Code Quality Tooling

Create `.clang-tidy`:
```yaml
# .clang-tidy
Checks: >
    clang-analyzer-*,
    bugprone-*,
    performance-*,
    readability-*,
    modernize-*,
    cppcoreguidelines-*
CheckOptions:
    readability-identifier-naming.ClassCase: CamelCase
    readability-identifier-naming.FunctionCase: camelBack
    readability-identifier-naming.VariableCase: camelBack
    readability-identifier-naming.MemberCase: camelBack
    readability-identifier-naming.MemberPrefix: m_
    readability-identifier-naming.ConstantCase: UPPER_CASE
```

### Estimated Effort
- **1-2 weeks** per platform; **1 week** for CI

---

## 11. Performance (🟡)

### 11.1 Async Database Operations

```cpp
// src/Storage/AsyncDatabase.hpp
#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QThread>
#include <QMutex>
#include <functional>

namespace Acheron { namespace Storage {

class AsyncDatabase : public QObject {
    Q_OBJECT
public:
    explicit AsyncDatabase(const QString &connectionName, QObject *parent = nullptr);
    ~AsyncDatabase();

    template<typename T>
    void exec(const std::function<T(QSqlDatabase &)> &op,
              const std::function<void(const T &)> &callback)
    {
        // Queue operation on worker thread
        // Invoke callback on main thread via QMetaObject::invokeMethod
    }

signals:
    void finished();

private:
    QThread *m_worker;
    QSqlDatabase m_db;
};

} } // namespace Acheron::Storage
```

### 11.2 Image Cache

```cpp
// src/Core/ImageManager.cpp — add disk cache
QString ImageManager::cachePath(const QString &url) const
{
    QString hash = QString(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Md5).toHex());
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
           + "/images/" + hash + ".cache";
}
```

### 11.3 Virtualized Channel List

For large guilds (500+ channels), implement lazy-loading in `ChannelTreeModel`:
- Only load channels as they come into view
- Use a placeholder count for unseen children
- Load async as the user scrolls

### 11.4 Chat Message Limit

Add a configurable message limit to `ChatModel`:
```cpp
void ChatModel::enforceMessageLimit()
{
    constexpr int maxMessages = 2000;
    while (m_messages.size() > maxMessages) {
        // Remove oldest batch (50 at a time)
        beginRemoveRows({}, 0, 49);
        for (int i = 0; i < 50; ++i)
            m_messages.removeFirst();
        endRemoveRows();
    }
}
```

### Estimated Effort
- **1-2 weeks**

---

## 12. Documentation (🟢)

### 12.1 API Documentation

Add Doxygen comments to all public headers:

```cpp
// Example in Client.hpp
/**
 * @brief High-level Discord API client
 *
 * Manages gateway connection, REST API calls, and message/event routing.
 * One instance per user account.
 *
 * @code
 * Client client(token, gatewayUrl, baseUrl);
 * client.start();
 * @endcode
 */
class Client : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Construct a Discord client
     * @param token       Bot or user authentication token
     * @param gatewayUrl  WebSocket gateway URL (wss://gateway.discord.gg)
     * @param baseUrl     REST API base URL (https://discord.com/api/v10)
     * @param captchaResolver Optional captcha handler
     * @param parent      Qt parent object
     */
    explicit Client(const QString &token, const QString &gatewayUrl,
                    const QString &baseUrl,
                    CaptchaResolver *captchaResolver = nullptr,
                    QObject *parent = nullptr);
};
```

### 12.2 Architecture Documentation

`docs/architecture.md` with:
- Module dependency graph
- Data flow diagrams (Gateway → Event → ClientInstance → UI)
- Thread model (which thread each component runs on)
- Database schema documentation
- Voice pipeline diagram

### 12.3 User Documentation

`docs/user-guide.md` covering:
- Installation
- First run / account setup
- Channel navigation
- Voice calls
- Settings explanation
- Keyboard shortcuts
- Troubleshooting

### 12.4 Generate Doxygen

```cmake
# In CMakeLists.txt
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    set(DOXYGEN_INPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
    set(DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/docs)
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_EXTRACT_ALL YES)
    doxygen_add_docs(docs ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()
```

### Estimated Effort
- **1 week** for initial doc pass (can be done incrementally)

---

## 13. New Feature Ideas (🟢)

### 13.1 Plugin System

```cpp
// src/Core/Plugin/PluginInterface.hpp
#pragma once
#include <QString>
#include <QJsonObject>

namespace Acheron { namespace Core { namespace Plugin {

class PluginInterface {
public:
    virtual ~PluginInterface() = default;

    virtual QString name() const = 0;
    virtual QString version() const = 0;
    virtual QString description() const = 0;

    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    virtual void onMessageCreated(const Discord::Message &msg) {}
    virtual void onUiReady(QWidget *mainWindow) {}
};

} } } // namespace Acheron::Core::Plugin
```

Load `.dll`/`.so` plugins at startup from `plugins/` directory using `QLibrary`.

### 13.2 Custom CSS Themes

```cpp
// Already have Theme system; add CSS user-override
// In AppearancePage
void AppearancePage::setupCustomCSS()
{
    auto *cssEdit = new QPlainTextEdit(this);
    cssEdit->setPlaceholderText(tr("/* Custom CSS overrides */\nQWidget { background: red; }"));
    cssEdit->setPlainText(QSettings().value("theme/customCSS").toString());

    auto *applyBtn = new QPushButton(tr("Apply Custom CSS"), this);
    connect(applyBtn, &QPushButton::clicked, this, [this, cssEdit]() {
        QString css = cssEdit->toPlainText();
        QSettings().setValue("theme/customCSS", css);
        qApp->setStyleSheet(qApp->styleSheet() + "\n" + css);
    });
}
```

### 13.3 Split View (Two Channels Side-by-Side)

```cpp
// In MainWindow
QSplitter *channelSplitter;
channelSplitter->addWidget(primaryChatView);
channelSplitter->addWidget(secondaryChatView);

void MainWindow::openSplitView(Snowflake leftChannel, Snowflake rightChannel)
{
    primaryChatView->loadChannel(leftChannel);
    secondaryChatView->loadChannel(rightChannel);
    secondaryChatView->show();
}
```

### 13.4 Message Translation

Use Discord's built-in translation API or integrate Google Translate:
```cpp
// In ChatView context menu
if (msg.content.hasValue() && isNotEnglish(msg.content.get())) {
    menu->addAction(tr("Translate to English"), [this, msg]() {
        currentInstance->discord()->translateMessage(msg.channelId.get(), msg.id.get(), "en");
    });
}
```

### 13.5 Spoiler Reveal Animation

```cpp
// In ChatDelegate, for spoiler-tagged content
struct SpoilerState {
    bool revealed = false;
    QTimer revealTimer;
};

void ChatDelegate::paintSpoiler(QPainter *painter, const QRect &rect, SpoilerState &state)
{
    if (!state.revealed) {
        painter->fillRect(rect, QColor(30, 30, 30));
        painter->setPen(QColor(150, 150, 150));
        painter->drawText(rect, Qt::AlignCenter, tr("SPOILER"));
    } else {
        // Draw actual content with fade animation
        // ...
    }
}
```

### Estimated Effort
- **2-4 weeks** per feature (lower priority)

---

## 14. Previously Planned QOL (Not Yet Implemented)

From `remaining-qol-plan.md`. These are already scoped — just not done yet.

### T1: QScrollBar Styling (🟢)

```cpp
// src/Core/ScrollBarStyle.hpp
#pragma once
#include <QProxyStyle>

namespace Acheron { namespace Core {

class ScrollBarStyle : public QProxyStyle {
public:
    int subControlRect(ComplexControl control, const SubControl subControl,
                       const QStyleOptionComplex *option,
                       const QWidget *widget) const override
    {
        if (control == CC_ScrollBar && subControl == SC_ScrollBarSlider) {
            QRect rect = QProxyStyle::subControlRect(control, subControl, option, widget);
            if (widget && widget->underMouse())
                rect.adjust(0, 0, 0, 0); // expand from 6px to 10px
            else
                rect.setWidth(6);
            return rect;
        }
        return QProxyStyle::subControlRect(control, subControl, option, widget);
    }

    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                            QPainter *painter, const QWidget *widget) const override
    {
        if (control == CC_ScrollBar) {
            // Custom dark theme rendering
            painter->save();
            // ... draw background, slider, arrows ...
            painter->restore();
            return;
        }
        QProxyStyle::drawComplexControl(control, option, painter, widget);
    }
};

} } // namespace Acheron::Core
```

Apply in `main.cpp`:
```cpp
QApplication::setStyle(new ScrollBarStyle());
```

### T2: Splitter Handle Styling (🟢)

```cpp
// src/UI/SplitterHandle.hpp
#pragma once
#include <QSplitterHandle>

namespace Acheron { namespace UI {

class SplitterHandle : public QSplitterHandle {
    Q_OBJECT
public:
    explicit SplitterHandle(Qt::Orientation orientation, QSplitter *parent)
        : QSplitterHandle(orientation, parent) {}

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        int center = orientation() == Qt::Horizontal
                         ? rect().center().x()
                         : rect().center().y();

        QColor base = palette().color(QPalette::Mid);
        QColor hover = palette().color(QPalette::Highlight);

        p.fillRect(rect(), Qt::transparent);
        if (orientation() == Qt::Horizontal) {
            p.fillRect(center - 1, rect().top() + 4, 2, rect().height() - 8,
                       underMouse() ? hover : base);
        } else {
            p.fillRect(rect().left() + 4, center - 1, rect().width() - 8, 2,
                       underMouse() ? hover : base);
        }
    }
};

} } // namespace Acheron::UI
```

Install in MainWindow:
```cpp
QSplitter *mainSplitter = ...;
connect(mainSplitter, &QSplitter::splitterMoved, ...);
// Override createHandle:
mainSplitter->setChildrenCollapsible(false);
// QSplitter::createHandle is not virtual in a useful way,
// so subclass QSplitter:
class AcheronSplitter : public QSplitter {
    QSplitterHandle *createHandle() override {
        return new SplitterHandle(orientation(), this);
    }
};
```

### T3: Typing Indicator Polish (🟢)

```cpp
// In TypingIndicator.cpp — bounce oscillation
void TypingIndicator::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    // ... existing text drawing ...

    if (m_dotPhase >= 0) {
        // Animated bouncing dots
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().color(QPalette::Mid));

        int dotY = textRect.bottom() + 8;
        int dotX = textRect.left();
        for (int i = 0; i < 3; ++i) {
            float bounceOffset = sinf((m_dotPhase + i) * M_PI / 2.0f) * 3.0f;
            QRectF dot(dotX + i * 10, dotY - bounceOffset, 6, 6);
            painter.drawEllipse(dot);
        }
    }
}

// Fade in/out via QGraphicsOpacityEffect
void TypingIndicator::setTypers(const QList<TyperInfo> &typers)
{
    if (typers.isEmpty() && m_typers.isEmpty())
        return;

    if (typers.isEmpty() && !m_typers.isEmpty()) {
        // Fade out
        auto *effect = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(effect);
        auto *anim = new QPropertyAnimation(effect, "opacity", this);
        anim->setDuration(200);
        anim->setStartValue(1.0);
        anim->setEndValue(0.0);
        connect(anim, &QPropertyAnimation::finished, this, [this]() {
            setGraphicsEffect(nullptr);
            hide();
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    } else if (!typers.isEmpty() && m_typers.isEmpty()) {
        // Fade in
        show();
        auto *effect = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(effect);
        effect->setOpacity(0.0);
        auto *anim = new QPropertyAnimation(effect, "opacity", this);
        anim->setDuration(200);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        connect(anim, &QPropertyAnimation::finished, this, [this]() {
            setGraphicsEffect(nullptr);
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    m_typers = typers;
    update();
}
```

### T4: Context Menu Cleanup (🟢)

```cpp
// In ChatView::contextMenuEvent — reorganized with sections
void ChatView::contextMenuEvent(QContextMenuEvent *event)
{
    QModelIndex index = indexAt(event->pos());
    if (!index.isValid())
        return;

    QMenu menu(this);

    // Section 1: Message actions (destructive/content-changing)
    if (isOwnMessage(index)) {
        menu.addAction(QIcon(":/icons/edit.svg"), tr("Edit Message"),
                       [this, index]() { startInlineEdit(index); });
        menu.addAction(QIcon(":/icons/delete.svg"), tr("Delete Message"),
                       [this, index]() {
                           emit deleteMessageRequested(channelId, messageId);
                       });
    }
    menu.addAction(QIcon(":/icons/reply.svg"), tr("Reply"),
                   [this, index]() { emit replyToMessageRequested(channelId, messageId); });
    if (canPinMessages) {
        menu.addAction(QIcon(":/icons/pin.svg"), tr("Pin Message"),
                       [this]() { emit pinMessageRequested(channelId, messageId); });
    }
    menu.addSeparator();

    // Section 2: Utility
    menu.addAction(QIcon(":/icons/copy.svg"), tr("Copy Text"),
                   [this, index]() { copyMessageContent(index); });
    menu.addAction(QIcon(":/icons/copy-id.svg"), tr("Copy ID"),
                   [this, index]() {
                       QGuiApplication::clipboard()->setText(QString::number(messageId));
                   });
    if (hasUrl) {
        menu.addAction(QIcon(":/icons/link.svg"), tr("Copy Link"),
                       [this]() { QGuiApplication::clipboard()->setText(messageUrl); });
    }
    menu.addSeparator();

    // Section 3: Reactions
    auto *reactionMenu = menu.addMenu(QIcon(":/icons/emoji.svg"), tr("Add Reaction"));
    reactionMenu->addAction("👍", [this]() { emit addReactionRequested(channelId, messageId, "👍"); });
    reactionMenu->addAction("❤️", [this]() { emit addReactionRequested(channelId, messageId, "❤️"); });
    reactionMenu->addAction("😄", [this]() { emit addReactionRequested(channelId, messageId, "😄"); });
    reactionMenu->addAction(tr("More..."), [this]() { /* open emoji picker */ });

    menu.exec(event->globalPos());
}
```

### T5: Recent Channels in Quick-Switch (🟢)

```cpp
// In MainWindow
void MainWindow::recordRecentChannel(const TabEntry &entry)
{
    QSettings s;
    QJsonArray recent = QJsonDocument::fromJson(
        s.value("quickSwitch/recentChannels", "[]").toString().toUtf8()
    ).array();

    // Deduplicate
    QJsonArray deduped;
    deduped.append(QJsonObject{
        {"channelId", static_cast<qint64>(entry.channelId)},
        {"guildId", static_cast<qint64>(entry.guildId)},
        {"accountId", static_cast<qint64>(entry.accountId)},
        {"name", entry.name}
    });
    for (const auto &val : recent) {
        QJsonObject obj = val.toObject();
        if (obj["channelId"].toVariant().toLongLong() != static_cast<qint64>(entry.channelId))
            deduped.append(obj);
    }

    // Keep max 10
    while (deduped.size() > 10)
        deduped.removeLast();

    s.setValue("quickSwitch/recentChannels",
               QString::fromUtf8(QJsonDocument(deduped).toJson(QJsonDocument::Compact)));
}
```

### T6: Message Linkification (🟢)

```cpp
// In ChatLayout::layoutMessage()
void ChatLayout::layoutMessage(const Discord::Message &msg, const QSize &constraints)
{
    // ... existing layout ...
    QString text = msg.content.get();

    // URL detection regex
    static QRegularExpression urlRe(
        R"(https?://[^\s<>"']+)",
        QRegularExpression::CaseInsensitiveOption
    );

    QRegularExpressionMatchIterator it = urlRe.globalMatch(text);
    int lastEnd = 0;
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        int start = match.capturedStart();
        int end = match.capturedEnd();

        // Trim trailing punctuation from URL
        QString url = match.captured();
        while (!url.isEmpty() && QString(".,;:!?)").contains(url.back()))
            url.chop(1);

        // Text before URL
        if (start > lastEnd) {
            addTextRegion(text.mid(lastEnd, start - lastEnd));
        }

        // URL region
        HitRegion region;
        region.kind = HitRegion::Kind::TextLink;
        region.url = url;
        addRegion(region);

        lastEnd = start + url.length();
    }

    // Text after last URL
    if (lastEnd < text.length())
        addTextRegion(text.mid(lastEnd));
}
```

### T7: Notification Sounds (🟢)

```cpp
// In MainWindow
void MainWindow::maybePlayMessageNotification(const Discord::Message &msg)
{
    if (!notificationSoundsEnabled)
        return;

    Snowflake activeChannelId = chatModel->getActiveChannelId();
    if (msg.channelId.get() == activeChannelId)
        return; // already viewing this channel

    // Debounce: at most one sound per 2 seconds
    static QElapsedTimer lastSound;
    if (lastSound.isValid() && lastSound.elapsed() < 2000)
        return;

    // Check mute
    if (currentInstance && currentInstance->readState()) {
        if (currentInstance->readState()->isMuted(msg.channelId.get()))
            return;
    }

    lastSound.start();

#ifdef QT_MULTIMEDIA_LIB
    static QSoundEffect effect;
    if (effect.isLoaded() == false) {
        effect.setSource(QUrl("qrc:/sounds/message.wav"));
        effect.setVolume(0.5);
    }
    effect.play();
#else
    QApplication::beep();
#endif
}
```

### T8: Chat View Crossfade (🟢)

```cpp
// In ChannelPane::setChannel()
void ChannelPane::setChannel(Snowflake channelId, Snowflake guildId)
{
    if (m_activeChannelId == channelId)
        return;

    // Crossfade: snapshot current viewport, animate out, then switch
    if (!m_chatView->viewport()->grab().isNull()) {
        auto *overlay = new QLabel(m_chatView);
        overlay->setPixmap(m_chatView->viewport()->grab());
        overlay->setGeometry(m_chatView->rect());
        overlay->show();

        auto *effect = new QGraphicsOpacityEffect(overlay);
        overlay->setGraphicsEffect(effect);
        auto *anim = new QPropertyAnimation(effect, "opacity", this);
        anim->setDuration(100);
        anim->setStartValue(1.0);
        anim->setEndValue(0.0);
        connect(anim, &QPropertyAnimation::finished, overlay, &QObject::deleteLater);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    m_activeChannelId = channelId;
    m_chatModel->setActiveChannel(channelId, guildId);
}
```

### T9: Guild/Folder Colors (🟢)

```cpp
// In ChannelTreeView
void ChannelTreeView::folderContextMenu(Snowflake folderId)
{
    QMenu menu(this);
    menu.addAction(tr("Set Folder Color"), [this, folderId]() {
        QColorDialog dialog(this);
        QString hex = QSettings().value(
            QString("folderColors/%1").arg(static_cast<quint64>(folderId))).toString();
        if (!hex.isEmpty())
            dialog.setCurrentColor(QColor(hex));
        if (dialog.exec() == QDialog::Accepted) {
            QSettings().setValue(
                QString("folderColors/%1").arg(static_cast<quint64>(folderId)),
                dialog.selectedColor().name());
            // Trigger model update
        }
    });
    menu.exec(QCursor::pos());
}
```

### T10: Channel List Drag-and-Drop (🟢)

```cpp
// In ChannelTreeView
void ChannelTreeView::dropEvent(QDropEvent *event)
{
    QModelIndex proxyIndex = indexAt(event->position().toPoint());
    QModelIndex sourceIndex = m_filterModel->mapToSource(proxyIndex);
    if (!sourceIndex.isValid())
        return;

    auto *targetNode = static_cast<ChannelNode *>(sourceIndex.internalPointer());
    if (!targetNode || targetNode->type != ChannelNode::Type::Channel)
        return; // only allow dropping on channels

    // Ensure same parent (category)
    auto *sourceNode = static_cast<ChannelNode *>(m_dragSource.internalPointer());
    if (!sourceNode || sourceNode->parentId != targetNode->parentId)
        return;

    int sourceRow = m_dragSource.row();
    int targetRow = sourceIndex.row();
    m_treeModel->moveNodeWithinParent(sourceRow, targetRow);
    m_treeModel->persistChildOrder(targetNode->parentId);
    event->acceptProposedAction();
}
```

### Files to Create (for T1-T12)
- `src/Core/ScrollBarStyle.hpp` + `.cpp`
- `src/UI/SplitterHandle.hpp` + `.cpp`
- `tests/tst_Linkification.cpp`

### Files to Modify
- `src/UI/TypingIndicator.cpp` — bounce + fade
- `src/UI/Chat/ChatView.cpp` — context menu, linkification
- `src/UI/Chat/ChatLayout.cpp` — URL detection
- `src/UI/Chat/ChatDelegate.cpp` — link styling
- `src/UI/MainWindow.cpp` — notification sounds, recent channels
- `src/UI/ChannelPane.cpp` — crossfade
- `src/UI/ChannelList/ChannelTreeView.cpp` — DnD, folder colors
- `src/UI/Dialogs/ChannelQuickSwitch.cpp` — recents
- `src/UI/Settings/AppearancePage.cpp` — folder colors
- `CMakeLists.txt` — new sources
- `resources.qrc` — notification sound

### Estimated Effort
- **2-3 weeks** for all 12 tasks

---

## 15. Implementation Roadmap

### Phase 1: Foundation (Weeks 1-3)
| Priority | Item | Dependencies |
|----------|------|-------------|
| 🔴 | Gateway event coverage (Section 2) | None |
| 🔴 | Thread/forum parsing (Section 3.1-3.3) | None |
| 🔴 | Entity deserialization tests (Section 1.1) | None |
| 🟢 | QOL T1-T3 (Scrollbar, Splitter, Typing) | None |

### Phase 2: Core Features (Weeks 4-7)
| Priority | Item | Dependencies |
|----------|------|-------------|
| 🔴 | Thread UI (Section 3.4-3.6) | Phase 1 thread parsing |
| 🟡 | Rich embed rendering (Section 4.1) | None |
| 🟡 | Settings pages (Section 6) | None |
| 🟡 | Storage tests (Section 1.4) | None |
| 🟢 | QOL T4-T6 (Menus, Linkification, Recents) | None |

### Phase 3: Voice & Social (Weeks 8-11)
| Priority | Item | Dependencies |
|----------|------|-------------|
| 🟡 | Screen share (Section 5.1) | Voice system works |
| 🟡 | Push-to-talk + user volumes (Section 5.2-5.3) | None |
| 🟡 | Friend management (Section 7) | None |
| 🟡 | Chat UI enhancements (Section 8) | Linkification done |
| 🟢 | QOL T7-T9 (Sounds, Crossfade, Colors) | None |

### Phase 4: Quality & Cross-Platform (Weeks 12-15)
| Priority | Item | Dependencies |
|----------|------|-------------|
| 🟡 | MainWindow refactor (Section 9.1) | None |
| 🟡 | Async DB (Section 11.1) | None |
| 🟢 | Linux support (Section 10.1) | None |
| 🟢 | CI/CD (Section 10.3) | None |
| 🟢 | Documentation (Section 12) | None |
| 🟢 | QOL T10-T12 (DnD, Tests, Build) | None |

### Phase 5: Stretch (Ongoing)
- Plugin system (Section 13.1)
- Custom CSS themes (Section 13.2)
- Split view (Section 13.3)
- Message translation (Section 13.4)
- macOS support (Section 10.2)
- Full accessibility (Section 6, Accessibility)

---

## Summary Statistics

| Category | Items | Estimated Effort |
|----------|-------|-----------------|
| Test coverage | 10+ test files | 2-3 weeks |
| Gateway events | 25+ new event handlers | 1-2 weeks |
| Threads & forums | 5+ new files, 15+ modified | 3-4 weeks |
| Message display | 6 features | 2-3 weeks |
| Voice improvements | 4 features | 4-6 weeks |
| Settings UI | 7 new pages | 2-3 weeks |
| User/social | 5 features | 1-2 weeks |
| Chat UI polish | 4 features | 2-3 weeks |
| Code quality | 6 refactoring areas | 3-4 weeks |
| Performance | 4 improvements | 1-2 weeks |
| Cross-platform | Windows, Linux, macOS, CI | 3-4 weeks |
| Previously planned QOL | 12 tasks | 2-3 weeks |
| New feature ideas | 5 concepts | ~4 weeks each |

**Total estimated effort: 30-45 weeks** (contingent on parallelization)
