#include "Core/Notification/NotificationDecider.hpp"

#include <QTest>

using namespace Acheron::Core::Notification;
using namespace Acheron::Discord;

namespace {

NotificationSettings defaultSettings()
{
    NotificationSettings s;
    s.enabled = true;
    s.notifyMentions = true;
    s.notifyDirectMessages = true;
    s.notifyGroupMessages = true;
    s.notifyFriendServerMessages = true;
    s.respectServerSettings = true;
    s.disableInStreamerMode = true;
    s.streamingTreatment = NotificationSettings::StreamingTreatment::Normal;
    return s;
}

DecideContext serverMessage(bool mention = false, bool friendAuthor = false)
{
    DecideContext c;
    c.channelType = ChannelType::GUILD_TEXT;
    c.isMention = mention;
    c.isFriend = friendAuthor;
    c.channelLevel = MessageNotificationLevel::ONLY_MENTIONS;
    return c;
}

} // namespace

class TestNotificationDecider : public QObject
{
    Q_OBJECT
private slots:
    void testDisabled();
    void testOwnMessage();
    void testBot();
    void testIgnoreList();
    void testActiveChannel();
    void testDirectMessage();
    void testGroupDm();
    void testMention();
    void testServerAllMessages();
    void testServerOnlyMentionsNoMention();
    void testFriendServerMessage();
    void testNotifyListOverridesPolicy();
    void testMutedChannel();
    void testMentionBypassesMute();
    void testFriendBypassesMute();
    void testStreamerIgnore();
    void testStreamerRedact();
};

void TestNotificationDecider::testDisabled()
{
    auto s = defaultSettings();
    s.enabled = false;
    QVERIFY(!decide(serverMessage(true), s, false).notify);
}

void TestNotificationDecider::testOwnMessage()
{
    auto c = serverMessage(true);
    c.isOwnMessage = true;
    QVERIFY(!decide(c, defaultSettings(), false).notify);
}

void TestNotificationDecider::testBot()
{
    auto c = serverMessage(true);
    c.isBot = true;
    QVERIFY(!decide(c, defaultSettings(), false).notify);
}

void TestNotificationDecider::testIgnoreList()
{
    auto c = serverMessage(true);
    c.authorOnIgnoreList = true;
    QVERIFY(!decide(c, defaultSettings(), false).notify);
}

void TestNotificationDecider::testActiveChannel()
{
    auto c = serverMessage(true);
    c.isActiveChannel = true;
    QVERIFY(!decide(c, defaultSettings(), false).notify);
}

void TestNotificationDecider::testDirectMessage()
{
    DecideContext c;
    c.channelType = ChannelType::DM;
    Decision d = decide(c, defaultSettings(), false);
    QVERIFY(d.notify);
    QCOMPARE(d.type, NotificationType::DirectMessage);
}

void TestNotificationDecider::testGroupDm()
{
    DecideContext c;
    c.channelType = ChannelType::GROUP_DM;
    Decision d = decide(c, defaultSettings(), false);
    QVERIFY(d.notify);
    QCOMPARE(d.type, NotificationType::GroupMessage);
}

void TestNotificationDecider::testMention()
{
    Decision d = decide(serverMessage(true), defaultSettings(), false);
    QVERIFY(d.notify);
    QCOMPARE(d.type, NotificationType::Mention);
}

void TestNotificationDecider::testServerAllMessages()
{
    auto c = serverMessage(false);
    c.channelLevel = MessageNotificationLevel::ALL_MESSAGES;
    QVERIFY(decide(c, defaultSettings(), false).notify);
}

void TestNotificationDecider::testServerOnlyMentionsNoMention()
{
    QVERIFY(!decide(serverMessage(false), defaultSettings(), false).notify);
}

void TestNotificationDecider::testFriendServerMessage()
{
    QVERIFY(decide(serverMessage(false, true), defaultSettings(), false).notify);
}

void TestNotificationDecider::testNotifyListOverridesPolicy()
{
    auto c = serverMessage(false);
    c.authorOnNotifyList = true;
    QVERIFY(decide(c, defaultSettings(), false).notify);
}

void TestNotificationDecider::testMutedChannel()
{
    auto c = serverMessage(false);
    c.channelLevel = MessageNotificationLevel::NO_MESSAGES;
    QVERIFY(!decide(c, defaultSettings(), false).notify);
}

void TestNotificationDecider::testMentionBypassesMute()
{
    auto c = serverMessage(true);
    c.channelLevel = MessageNotificationLevel::NO_MESSAGES;
    QVERIFY(decide(c, defaultSettings(), false).notify);
}

void TestNotificationDecider::testFriendBypassesMute()
{
    auto c = serverMessage(false, true);
    c.channelLevel = MessageNotificationLevel::NO_MESSAGES;
    QVERIFY(decide(c, defaultSettings(), false).notify);
}

void TestNotificationDecider::testStreamerIgnore()
{
    auto s = defaultSettings();
    s.streamingTreatment = NotificationSettings::StreamingTreatment::Ignore;
    QVERIFY(!decide(serverMessage(true), s, true).notify);
}

void TestNotificationDecider::testStreamerRedact()
{
    auto s = defaultSettings();
    s.disableInStreamerMode = false;
    s.streamingTreatment = NotificationSettings::StreamingTreatment::NoContent;
    Decision d = decide(serverMessage(true), s, true);
    QVERIFY(d.notify);
    QVERIFY(d.redact);
}

QTEST_APPLESS_MAIN(TestNotificationDecider)
#include "tst_NotificationDecider.moc"
