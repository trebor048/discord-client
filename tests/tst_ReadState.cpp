#include <QtTest>
#include <QSignalSpy>

#include "Core/ReadStateManager.hpp"
#include "Core/PermissionManager.hpp"
#include "Discord/Entities.hpp"

using namespace Acheron;

// Unit tests for the session-local unread message counts that drive the
// numbered unread badges on channels and tabs.
class TestReadState : public QObject
{
    Q_OBJECT

private:
    static Core::Snowflake sid(quint64 v) { return Core::Snowflake(v); }

    // A ReadStateManager whose permission manager can never grant VIEW_CHANNEL;
    // tests that need a readable channel pass canViewOverride=true.
    struct Harness
    {
        Core::PermissionManager perms{ sid(1) };
        Core::ReadStateManager manager{ sid(1), &perms };
    };

private slots:
    void countsAccumulateWhileInactive()
    {
        Harness h;
        h.manager.setActiveChannel(sid(1)); // channel A
        QSignalSpy spy(&h.manager, &Core::ReadStateManager::readStateUpdated);

        h.manager.handleMessageCreated(sid(10), sid(100), false, false);
        h.manager.handleMessageCreated(sid(10), sid(101), false, false);
        h.manager.handleMessageCreated(sid(10), sid(102), false, false);

        QCOMPARE(h.manager.unreadMessageCount(sid(10)), 3);
        QCOMPARE(h.manager.unreadMessageCount(sid(1)), 0);
        QVERIFY(spy.count() >= 3);
    }

    void ownMessagesAreNotCounted()
    {
        Harness h;
        h.manager.setActiveChannel(sid(1));
        h.manager.handleMessageCreated(sid(10), sid(100), false, false);
        h.manager.handleMessageCreated(sid(10), sid(101), false, false);
        h.manager.handleMessageCreated(sid(10), sid(102), false, false);

        // The user's own message sent from channel A into channel B.
        h.manager.handleMessageCreated(sid(10), sid(103), false, true);
        QCOMPARE(h.manager.unreadMessageCount(sid(10)), 3);
    }

    void activeChannelStaysZero()
    {
        Harness h;
        h.manager.setActiveChannel(sid(10));
        h.manager.handleMessageCreated(sid(10), sid(100), false, false);
        h.manager.handleMessageCreated(sid(10), sid(101), false, false);
        QCOMPARE(h.manager.unreadMessageCount(sid(10)), 0);
    }

    void readingResetsTheCount()
    {
        Harness h;
        h.manager.setActiveChannel(sid(1));
        h.manager.handleMessageCreated(sid(10), sid(100), false, false);
        h.manager.handleMessageCreated(sid(10), sid(101), false, false);
        QCOMPARE(h.manager.unreadMessageCount(sid(10)), 2);

        h.manager.updateLocalReadState(sid(10), sid(101));
        QCOMPARE(h.manager.unreadMessageCount(sid(10)), 0);
    }

    void switchingToChannelResetsItsCount()
    {
        Harness h;
        h.manager.setActiveChannel(sid(1));
        h.manager.handleMessageCreated(sid(10), sid(100), false, false);
        h.manager.handleMessageCreated(sid(10), sid(101), false, false);
        QCOMPARE(h.manager.unreadMessageCount(sid(10)), 2);

        h.manager.setActiveChannel(sid(10));
        QCOMPARE(h.manager.unreadMessageCount(sid(10)), 0);
    }

    void mutedChannelsReportZero()
    {
        Harness h;
        h.manager.setActiveChannel(sid(1));

        // Guild 2 with channel 10 muted.
        Discord::UserGuildSettings settings = Discord::UserGuildSettings::fromJson(
                QJsonObject{ { "guild_id", "2" },
                             { "channel_overrides",
                               QJsonArray{ QJsonObject{ { "channel_id", "10" },
                                                        { "muted", true } } } } });
        h.manager.onUserGuildSettingsUpdate(settings);

        h.manager.handleMessageCreated(sid(10), sid(100), false, false);
        h.manager.handleMessageCreated(sid(10), sid(101), false, false);
        QCOMPARE(h.manager.unreadMessageCount(sid(10)), 2);

        // Muted: the reported count is zeroed even though it accumulated.
        auto muted = h.manager.computeChannelReadState(sid(10), sid(2), Core::Snowflake::Invalid,
                                                       /*isDM=*/false, /*canViewOverride=*/true);
        QCOMPARE(muted.unreadCount, 0);

        // Control: an unmuted channel in the same guild surfaces the count.
        h.manager.handleMessageCreated(sid(11), sid(200), false, false);
        auto unmuted = h.manager.computeChannelReadState(sid(11), sid(2), Core::Snowflake::Invalid,
                                                         /*isDM=*/false, /*canViewOverride=*/true);
        QCOMPARE(unmuted.unreadCount, 1);
    }

    void seedingSurfacesAndOverwrites()
    {
        Harness h;
        h.manager.seedUnreadCounts({ { sid(10), 5 }, { sid(11), 2 } });
        QCOMPARE(h.manager.unreadMessageCount(sid(10)), 5);
        QCOMPARE(h.manager.unreadMessageCount(sid(11)), 2);

        // Re-seeding overwrites without double-counting.
        h.manager.seedUnreadCount(sid(10), 7);
        QCOMPARE(h.manager.unreadMessageCount(sid(10)), 7);

        auto state = h.manager.computeChannelReadState(sid(10), Core::Snowflake::Invalid,
                                                       Core::Snowflake::Invalid,
                                                       /*isDM=*/true);
        QCOMPARE(state.unreadCount, 7);
    }
};

QTEST_MAIN(TestReadState)
#include "tst_ReadState.moc"
