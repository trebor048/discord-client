#include "Discord/Entities.hpp"
#include "Discord/Events.hpp"

#include "Core/Snowflake.hpp"

#include <QJsonDocument>
#include <QTest>

using namespace Acheron::Discord;
using Acheron::Core::Snowflake;

class TestDeserialization : public QObject
{
    Q_OBJECT
private slots:
    void testChannelRequiredFields();
    void testChannelAllFields();
    void testChannelThreadMetadata();
    void testChannelForumTags();
    void testUser();
    void testMessageBotEmbed();
    void testEmoji();
    void testEmojiUnicode();
    void testGuildDelete();
    void testGuildBan();
    void testGuildEmojisUpdate();
    void testThreadCreate();
    void testThreadDelete();
    void testPresenceUpdate();
    void testInviteCreate();
    void testStageInstance();
    void testChannelPinsUpdate();
    void testMalformedJson();
    void testMissingFields();
    void testLargeSnowflake();
    void testNullFields();
    void testEmptyStringFields();
};

void TestDeserialization::testChannelRequiredFields()
{
    QJsonObject obj;
    obj["id"] = "123456789";
    obj["type"] = 0;

    Channel channel = Channel::fromJson(obj);

    QCOMPARE(static_cast<quint64>(channel.id.get()), 123456789ull);
    QCOMPARE(channel.type.get(), ChannelType::GUILD_TEXT);
    QVERIFY(channel.name.isUndefined());
    QVERIFY(channel.threadMetadata.isUndefined());
}

void TestDeserialization::testChannelAllFields()
{
    QJsonObject obj;
    obj["id"] = "111";
    obj["type"] = 0;
    obj["name"] = "general";
    obj["position"] = 2;
    obj["guild_id"] = "222";
    obj["parent_id"] = "333";
    obj["last_message_id"] = "444";
    obj["rate_limit_per_user"] = 5;
    obj["user_limit"] = 0;
    obj["flags"] = 0;

    Channel channel = Channel::fromJson(obj);

    QCOMPARE(static_cast<quint64>(channel.id.get()), 111ull);
    QCOMPARE(channel.type.get(), ChannelType::GUILD_TEXT);
    QCOMPARE(channel.name.get(), QString("general"));
    QCOMPARE(channel.position.get(), 2);
    QCOMPARE(static_cast<quint64>(channel.guildId.get()), 222ull);
    QCOMPARE(static_cast<quint64>(channel.parentId.get()), 333ull);
    QCOMPARE(static_cast<quint64>(channel.lastMessageId.get()), 444ull);
    QCOMPARE(channel.rateLimitPerUser.get(), 5);
    QCOMPARE(channel.userLimit.get(), 0);
    QCOMPARE(channel.flags.get(), 0);
}

void TestDeserialization::testChannelThreadMetadata()
{
    QJsonObject meta;
    meta["archived"] = false;
    meta["auto_archive_duration"] = 1440;
    meta["locked"] = false;

    QJsonObject obj;
    obj["id"] = "555";
    obj["type"] = 11; // PUBLIC_THREAD
    obj["name"] = "thread-1";
    obj["guild_id"] = "666";
    obj["parent_id"] = "777";
    obj["thread_metadata"] = meta;
    obj["member_count"] = 3;
    obj["message_count"] = 42;
    obj["total_message_sent"] = 50;

    Channel channel = Channel::fromJson(obj);

    QCOMPARE(static_cast<quint64>(channel.id.get()), 555ull);
    QCOMPARE(channel.type.get(), ChannelType::PUBLIC_THREAD);
    QVERIFY(channel.isThread());
    QVERIFY(!channel.isForum());

    QVERIFY(channel.threadMetadata.hasValue());
    Channel::ThreadMetadata tmeta = channel.threadMetadata.get();
    QCOMPARE(tmeta.archived.get(), false);
    QCOMPARE(tmeta.autoArchiveDuration.get(), 1440);
    QCOMPARE(tmeta.locked.get(), false);

    QCOMPARE(channel.memberCount.get(), 3);
    QCOMPARE(channel.messageCount.get(), 42);
    QCOMPARE(channel.totalMessageSent.get(), 50);
}

void TestDeserialization::testChannelForumTags()
{
    QJsonObject tag1;
    tag1["id"] = "100";
    tag1["name"] = "Question";
    tag1["moderated"] = false;

    QJsonObject tag2;
    tag2["id"] = "101";
    tag2["name"] = "Solved";
    tag2["moderated"] = true;
    tag2["emoji_id"] = "888";

    QJsonArray tags;
    tags.append(tag1);
    tags.append(tag2);

    QJsonObject obj;
    obj["id"] = "999";
    obj["type"] = 15; // GUILD_FORUM
    obj["name"] = "forum-channel";
    obj["available_tags"] = tags;
    obj["default_sort_order"] = 0;
    obj["default_forum_layout"] = 1;
    obj["default_thread_rate_limit_per_user"] = 30;

    Channel channel = Channel::fromJson(obj);

    QCOMPARE(static_cast<quint64>(channel.id.get()), 999ull);
    QCOMPARE(channel.type.get(), ChannelType::GUILD_FORUM);
    QVERIFY(channel.isForum());

    QVERIFY(channel.availableTags.hasValue());
    QCOMPARE(channel.availableTags->size(), 2);

    Channel::ForumTag parsed = channel.availableTags->at(0);
    QCOMPARE(static_cast<quint64>(parsed.id.get()), 100ull);
    QCOMPARE(parsed.name.get(), QString("Question"));
    QCOMPARE(parsed.moderated.get(), false);

    parsed = channel.availableTags->at(1);
    QCOMPARE(static_cast<quint64>(parsed.id.get()), 101ull);
    QCOMPARE(parsed.name.get(), QString("Solved"));
    QCOMPARE(parsed.moderated.get(), true);

    QCOMPARE(channel.defaultSortOrder.get(), 0);
    QCOMPARE(channel.defaultForumLayout.get(), 1);
    QCOMPARE(channel.defaultThreadRateLimitPerUser.get(), 30);
}

void TestDeserialization::testUser()
{
    QJsonObject obj;
    obj["id"] = "123";
    obj["username"] = "testuser";
    obj["avatar"] = "abc123";

    User user = User::fromJson(obj);

    QCOMPARE(static_cast<quint64>(user.id.get()), 123ull);
    QCOMPARE(user.username.get(), QString("testuser"));
    QCOMPARE(user.avatar.get(), QString("abc123"));
}

void TestDeserialization::testMessageBotEmbed()
{
    QJsonObject author;
    author["name"] = "ModBot";

    QJsonObject footer;
    footer["text"] = "Acme Bot";

    QJsonObject field;
    field["name"] = "Reason";
    field["value"] = "Spam";
    field["inline"] = true;

    QJsonArray fields;
    fields.append(field);

    QJsonObject embed;
    embed["type"] = "rich";
    embed["title"] = "Warning";
    embed["description"] = "You have been warned.";
    embed["color"] = 15158332;
    embed["author"] = author;
    embed["footer"] = footer;
    embed["fields"] = fields;

    QJsonArray embeds;
    embeds.append(embed);

    QJsonObject user;
    user["id"] = "789";
    user["username"] = "botuser";

    QJsonObject obj;
    obj["id"] = "123";
    obj["channel_id"] = "456";
    obj["author"] = user;
    obj["content"] = "";
    obj["type"] = 0;
    obj["embeds"] = embeds;

    Message msg = Message::fromJson(obj);

    QVERIFY(msg.embeds.hasValue());
    QCOMPARE(msg.embeds->size(), 1);

    const Embed &e = msg.embeds->at(0);
    // Bot embeds carry no top-level URL; they must still be parsed intact.
    QVERIFY(!e.url.hasValue());
    QCOMPARE(e.type.get(), QString("rich"));
    QCOMPARE(e.title.get(), QString("Warning"));
    QCOMPARE(e.description.get(), QString("You have been warned."));
    QCOMPARE(e.color.get(), 15158332);

    QVERIFY(e.author.hasValue());
    QCOMPARE(e.author->name.get(), QString("ModBot"));

    QVERIFY(e.footer.hasValue());
    QCOMPARE(e.footer->text.get(), QString("Acme Bot"));

    QVERIFY(e.fields.hasValue());
    QCOMPARE(e.fields->size(), 1);
    QCOMPARE(e.fields->at(0).name.get(), QString("Reason"));
    QCOMPARE(e.fields->at(0).value.get(), QString("Spam"));
    QCOMPARE(e.fields->at(0).isInline.get(), true);
}

void TestDeserialization::testEmoji()
{
    QJsonObject obj;
    obj["id"] = "456";
    obj["name"] = "smile";
    obj["animated"] = true;
    obj["available"] = true;
    obj["managed"] = false;
    obj["require_colons"] = true;

    Emoji emoji = Emoji::fromJson(obj);

    QCOMPARE(static_cast<quint64>(emoji.id.get()), 456ull);
    QCOMPARE(emoji.name.get(), QString("smile"));
    QCOMPARE(emoji.animated.get(), true);
    QCOMPARE(emoji.available.get(), true);
    QCOMPARE(emoji.managed.get(), false);
    QCOMPARE(emoji.requireColons.get(), true);
    QVERIFY(!emoji.isUnicode());

    QString url = emoji.getImageUrl(48);
    QVERIFY(url.contains("456"));
    QVERIFY(url.contains("48"));
}

void TestDeserialization::testEmojiUnicode()
{
    QJsonObject obj;
    obj["name"] = "😀";

    Emoji emoji = Emoji::fromJson(obj);

    QVERIFY(emoji.isUnicode());
    QVERIFY(emoji.getImageUrl().isEmpty());
}

void TestDeserialization::testGuildDelete()
{
    QJsonObject obj;
    obj["id"] = "789";

    GuildDelete event = GuildDelete::fromJson(obj);

    QCOMPARE(static_cast<quint64>(event.id.get()), 789ull);
    QVERIFY(event.unavailable.isUndefined());

    obj["unavailable"] = true;
    event = GuildDelete::fromJson(obj);
    QCOMPARE(event.unavailable.get(), true);
}

void TestDeserialization::testGuildBan()
{
    QJsonObject userObj;
    userObj["id"] = "999";
    userObj["username"] = "banned";

    QJsonObject obj;
    obj["guild_id"] = "777";
    obj["user"] = userObj;

    GuildBan event = GuildBan::fromJson(obj);

    QCOMPARE(static_cast<quint64>(event.guildId.get()), 777ull);
    QCOMPARE(static_cast<quint64>(event.user->id.get()), 999ull);
    QCOMPARE(event.user->username.get(), QString("banned"));
}

void TestDeserialization::testGuildEmojisUpdate()
{
    QJsonObject emoji1;
    emoji1["id"] = "10";
    emoji1["name"] = "foo";
    emoji1["animated"] = false;

    QJsonObject emoji2;
    emoji2["id"] = "11";
    emoji2["name"] = "bar";
    emoji2["animated"] = true;

    QJsonArray emojis;
    emojis.append(emoji1);
    emojis.append(emoji2);

    QJsonObject obj;
    obj["guild_id"] = "555";
    obj["emojis"] = emojis;

    GuildEmojisUpdate event = GuildEmojisUpdate::fromJson(obj);

    QCOMPARE(static_cast<quint64>(event.guildId.get()), 555ull);
    QCOMPARE(event.emojis->size(), 2);
    QCOMPARE(event.emojis->at(0).name.get(), QString("foo"));
    QCOMPARE(event.emojis->at(1).name.get(), QString("bar"));
    QCOMPARE(event.emojis->at(1).animated.get(), true);
}

void TestDeserialization::testThreadCreate()
{
    QJsonObject obj;
    obj["id"] = "111";
    obj["type"] = 11; // PUBLIC_THREAD
    obj["name"] = "my-thread";
    obj["guild_id"] = "222";
    obj["parent_id"] = "333";
    obj["member_count"] = 5;

    QJsonObject meta;
    meta["archived"] = false;
    meta["auto_archive_duration"] = 1440;
    obj["thread_metadata"] = meta;

    ThreadCreate event = ThreadCreate::fromJson(obj);

    QCOMPARE(static_cast<quint64>(event.channel->id.get()), 111ull);
    QCOMPARE(event.channel->type.get(), ChannelType::PUBLIC_THREAD);
    QCOMPARE(event.channel->name.get(), QString("my-thread"));
    QCOMPARE(event.channel->memberCount.get(), 5);
    QVERIFY(event.channel->threadMetadata.hasValue());
}

void TestDeserialization::testThreadDelete()
{
    QJsonObject obj;
    obj["id"] = "444";
    obj["guild_id"] = "555";
    obj["parent_id"] = "666";
    obj["type"] = 11;

    ThreadDelete event = ThreadDelete::fromJson(obj);

    QCOMPARE(static_cast<quint64>(event.id.get()), 444ull);
    QCOMPARE(static_cast<quint64>(event.guildId.get()), 555ull);
    QCOMPARE(static_cast<quint64>(event.parentId.get()), 666ull);
    QCOMPARE(event.type.get(), ChannelType::PUBLIC_THREAD);
}

void TestDeserialization::testPresenceUpdate()
{
    QJsonObject userObj;
    userObj["id"] = "123";
    userObj["username"] = "present";

    QJsonObject activity;
    activity["name"] = "Playing";
    activity["type"] = 0;

    QJsonArray activities;
    activities.append(activity);

    QJsonObject obj;
    obj["user"] = userObj;
    obj["guild_id"] = "777";
    obj["status"] = "online";
    obj["activities"] = activities;
    obj["client_status"] = "online";

    PresenceUpdate event = PresenceUpdate::fromJson(obj);

    QCOMPARE(static_cast<quint64>(event.user->id.get()), 123ull);
    QCOMPARE(static_cast<quint64>(event.guildId.get()), 777ull);
    QCOMPARE(event.status.get(), QString("online"));
    QVERIFY(event.activities.hasValue());
    QCOMPARE(event.activities->size(), 1);
    QCOMPARE(event.activities->at(0).name.get(), QString("Playing"));
}

void TestDeserialization::testInviteCreate()
{
    QJsonObject obj;
    obj["channel_id"] = "111";
    obj["code"] = "abc123";
    obj["created_at"] = "2024-01-01T00:00:00+00:00";
    obj["max_age"] = 86400;
    obj["max_uses"] = 0;
    obj["uses"] = 0;
    obj["temporary"] = false;
    obj["channel_type"] = 0;

    InviteCreate event = InviteCreate::fromJson(obj);

    QCOMPARE(static_cast<quint64>(event.channelId.get()), 111ull);
    QCOMPARE(event.code.get(), QString("abc123"));
    QCOMPARE(event.maxAge.get(), 86400);
    QCOMPARE(event.maxUses.get(), 0);
    QCOMPARE(event.uses.get(), 0);
    QCOMPARE(event.temporary.get(), false);
}

void TestDeserialization::testStageInstance()
{
    QJsonObject obj;
    obj["id"] = "111";
    obj["guild_id"] = "222";
    obj["channel_id"] = "333";
    obj["topic"] = "Town Hall";
    obj["privacy_level"] = 2;

    StageInstance instance = StageInstance::fromJson(obj);

    QCOMPARE(static_cast<quint64>(instance.id.get()), 111ull);
    QCOMPARE(static_cast<quint64>(instance.guildId.get()), 222ull);
    QCOMPARE(static_cast<quint64>(instance.channelId.get()), 333ull);
    QCOMPARE(instance.topic.get(), QString("Town Hall"));
    QCOMPARE(instance.privacyLevel.get(), 2);
}

void TestDeserialization::testChannelPinsUpdate()
{
    QJsonObject obj;
    obj["channel_id"] = "111";
    obj["guild_id"] = "222";
    obj["last_pin_timestamp"] = "2024-06-01T12:00:00+00:00";

    ChannelPinsUpdate event = ChannelPinsUpdate::fromJson(obj);

    QCOMPARE(static_cast<quint64>(event.channelId.get()), 111ull);
    QCOMPARE(static_cast<quint64>(event.guildId.get()), 222ull);
    QVERIFY(event.lastPinTimestamp.hasValue());
}

void TestDeserialization::testMalformedJson()
{
    // Empty object — all missing fields default or become undefined
    QJsonObject empty;
    Channel emptyChannel = Channel::fromJson(empty);

    // id and type are non-optional → default constructed (Invalid / GUILD_TEXT)
    QCOMPARE(static_cast<quint64>(emptyChannel.id.get()), static_cast<quint64>(Snowflake::Invalid));
    QCOMPARE(emptyChannel.type.get(), ChannelType::GUILD_TEXT);
    // optional fields missing from JSON → undefined
    QVERIFY(emptyChannel.name.isUndefined());
    QVERIFY(emptyChannel.guildId.isUndefined());

    // Non-numeric snowflake string should yield 0
    QJsonObject badId;
    badId["id"] = "not_a_number";
    badId["type"] = 0;
    Channel badChannel = Channel::fromJson(badId);
    QCOMPARE(static_cast<quint64>(badChannel.id.get()), 0ull);
}

void TestDeserialization::testMissingFields()
{
    // Channel with only "type" — no "id", no optional fields
    QJsonObject obj;
    obj["type"] = 2; // GUILD_VOICE

    Channel channel = Channel::fromJson(obj);

    // id is non-optional → default Invalid
    QCOMPARE(static_cast<quint64>(channel.id.get()), static_cast<quint64>(Snowflake::Invalid));
    QCOMPARE(channel.type.get(), ChannelType::GUILD_VOICE);
    // All optional fields that were omitted must be undefined
    QVERIFY(channel.name.isUndefined());
    QVERIFY(channel.guildId.isUndefined());
    QVERIFY(channel.parentId.isUndefined());
    QVERIFY(channel.lastMessageId.isUndefined());
    QVERIFY(channel.threadMetadata.isUndefined());
}

void TestDeserialization::testLargeSnowflake()
{
    // Discord snowflakes are transmitted as strings to preserve full 64-bit
    // precision beyond JSON's 2^53 safe integer limit.
    QJsonObject obj;
    obj["id"] = "9999999999999999999";
    obj["type"] = 0;

    Channel channel = Channel::fromJson(obj);

    QCOMPARE(static_cast<quint64>(channel.id.get()), 9999999999999999999ull);
}

void TestDeserialization::testNullFields()
{
    QJsonObject obj;
    obj["id"] = "123";
    obj["type"] = 0;
    obj["name"] = QJsonValue::Null;          // nullable QString
    obj["last_message_id"] = QJsonValue::Null; // nullable Snowflake
    obj["icon"] = QJsonValue::Null;           // nullable QString
    obj["position"] = QJsonValue::Null;       // optional but NOT nullable

    Channel channel = Channel::fromJson(obj);

    QCOMPARE(static_cast<quint64>(channel.id.get()), 123ull);
    QVERIFY(channel.name.isNull());          // explicitly null → nullable → State::Null
    QVERIFY(channel.lastMessageId.isNull()); // explicitly null → nullable → State::Null
    QVERIFY(channel.icon.isNull());          // explicitly null → nullable → State::Null
    // position is optional but not nullable → stays Undefined (default for optional)
    QVERIFY(channel.position.isUndefined());
}

void TestDeserialization::testEmptyStringFields()
{
    // An empty string field is a valid value, distinct from a missing key
    QJsonObject obj;
    obj["id"] = "123";
    obj["type"] = 0;
    obj["name"] = "";

    Channel channel = Channel::fromJson(obj);

    QCOMPARE(static_cast<quint64>(channel.id.get()), 123ull);
    QVERIFY(channel.name.hasValue());         // present, so has a value
    QCOMPARE(channel.name.get(), QString("")); // but it is the empty string

    // Contrast: a completely omitted string field is Undefined
    QJsonObject obj2;
    obj2["id"] = "456";
    obj2["type"] = 0;
    // name not present at all

    Channel channel2 = Channel::fromJson(obj2);
    QVERIFY(channel2.name.isUndefined());
}

QTEST_APPLESS_MAIN(TestDeserialization)
#include "tst_Deserialization.moc"

