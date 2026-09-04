#include <QtTest>
#include <QSignalSpy>

#include "Core/ReadStateManager.hpp"
#include "Core/PermissionManager.hpp"
#include "Core/PermissionComputer.hpp"
#include "Discord/Entities.hpp"

using namespace Acheron;

// Tests for the Core state/runtime managers owned by the runtime slice.
//
// 1. ReadStateManager::handleMessageCreated must emit readStateUpdated exactly
//    once per incoming message (a mention used to emit twice, which ran the
//    channel-tree read-state refresh + tab refresh twice per message).
// 2. PermissionComputer channel role-overwrite handling is aggregation based
//    (OR all role allows, OR all role denies, deny then allow) and therefore
//    independent of the order member roles are supplied in — the invariant the
//    permission computation relies on, locked here so a future "fix" that
//    re-introduces a position sort has a regression test to satisfy.
class TestCoreManagers : public QObject
{
    Q_OBJECT

private:
    static Core::Snowflake sid(quint64 v) { return Core::Snowflake(v); }

    struct ReadStateHarness
    {
        Core::PermissionManager perms{ sid(1) };
        Core::ReadStateManager manager{ sid(1), &perms };
    };

    static Discord::Role makeRole(quint64 id, Discord::Permissions perms, int position = 0)
    {
        Discord::Role role;
        role.id = sid(id);
        role.name = QStringLiteral("role-%1").arg(id);
        role.permissions = perms;
        role.position = position;
        return role;
    }

    static Discord::PermissionOverwrite roleOverwrite(quint64 roleId,
                                                      Discord::Permissions allow,
                                                      Discord::Permissions deny)
    {
        Discord::PermissionOverwrite ow;
        ow.id = sid(roleId);
        ow.type = Discord::PermissionOverwrite::Type::Role;
        ow.allow = allow;
        ow.deny = deny;
        return ow;
    }

    static Discord::PermissionOverwrite memberOverwrite(quint64 userId,
                                                        Discord::Permissions allow,
                                                        Discord::Permissions deny)
    {
        Discord::PermissionOverwrite ow;
        ow.id = sid(userId);
        ow.type = Discord::PermissionOverwrite::Type::Member;
        ow.allow = allow;
        ow.deny = deny;
        return ow;
    }

private slots:
    void mentionMessageEmitsExactlyOnce()
    {
        ReadStateHarness h;
        h.manager.setActiveChannel(sid(1)); // active channel is elsewhere
        QSignalSpy spy(&h.manager, &Core::ReadStateManager::readStateUpdated);

        h.manager.handleMessageCreated(sid(10), sid(100), /*isMention=*/true, false);
        QCOMPARE(spy.count(), 1);
        QVERIFY(spy.at(0).at(0).value<Core::Snowflake>() == sid(10));
        QCOMPARE(h.manager.getMentionCount(sid(10)), 1);
        QCOMPARE(h.manager.unreadMessageCount(sid(10)), 1);

        // A second mention must also produce exactly one emission.
        h.manager.handleMessageCreated(sid(10), sid(101), /*isMention=*/true, false);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(h.manager.getMentionCount(sid(10)), 2);
        QCOMPARE(h.manager.unreadMessageCount(sid(10)), 2);
    }

    void nonMentionMessageEmitsOnce()
    {
        ReadStateHarness h;
        h.manager.setActiveChannel(sid(1));
        QSignalSpy spy(&h.manager, &Core::ReadStateManager::readStateUpdated);

        h.manager.handleMessageCreated(sid(10), sid(100), /*isMention=*/false, false);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(h.manager.getMentionCount(sid(10)), 0);
        QCOMPARE(h.manager.unreadMessageCount(sid(10)), 1);
    }

    void ownMessageEmitsOnceAndStaysUncounted()
    {
        ReadStateHarness h;
        h.manager.setActiveChannel(sid(1));
        QSignalSpy spy(&h.manager, &Core::ReadStateManager::readStateUpdated);

        // Own message into a non-active channel: counts stay untouched, the
        // read cursor advances, and the UI is told exactly once.
        h.manager.handleMessageCreated(sid(10), sid(100), /*isMention=*/true, /*own=*/true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(h.manager.unreadMessageCount(sid(10)), 0);
        QCOMPARE(h.manager.getMentionCount(sid(10)), 0);
        QVERIFY(h.manager.getChannelLastMessageId(sid(10)) == sid(100));
    }

    void roleOverwriteAggregationIsOrderIndependent()
    {
        using Discord::Permission;

        const Discord::Permissions base =
                Discord::Permission::VIEW_CHANNEL | Discord::Permission::SEND_MESSAGES;
        const QList<Core::Snowflake> orderAB{ sid(100), sid(200) }; // roleA, roleB
        const QList<Core::Snowflake> orderBA{ sid(200), sid(100) };

        QList<Discord::PermissionOverwrite> overwrites;
        // roleA allows SEND_MESSAGES, roleB denies it.
        overwrites.append(roleOverwrite(100, Discord::Permission::SEND_MESSAGES,
                                        Discord::NO_PERMISSIONS));
        overwrites.append(roleOverwrite(200, Discord::NO_PERMISSIONS,
                                        Discord::Permission::SEND_MESSAGES));

        const auto resAB = Core::PermissionComputer::computeOverwrites(base, sid(9),
                                                                       sid(500), orderAB,
                                                                       overwrites);
        const auto resBA = Core::PermissionComputer::computeOverwrites(base, sid(9),
                                                                       sid(500), orderBA,
                                                                       overwrites);
        QVERIFY(resAB == resBA);
    }

    void allowBeatsDenyAcrossRoles()
    {
        using Discord::Permission;

        const Discord::Permissions base = Discord::Permission::VIEW_CHANNEL;
        QList<Discord::PermissionOverwrite> overwrites;
        overwrites.append(roleOverwrite(100, Discord::Permission::SEND_MESSAGES,
                                        Discord::NO_PERMISSIONS));
        overwrites.append(roleOverwrite(200, Discord::NO_PERMISSIONS,
                                        Discord::Permission::SEND_MESSAGES));

        const auto res = Core::PermissionComputer::computeOverwrites(
                base, sid(9), sid(500), { sid(100), sid(200) }, overwrites);

        // Aggregation applies all role denies first, then all role allows, so
        // the allow in roleA restores SEND_MESSAGES denied by roleB.
        QVERIFY(res.testFlag(Discord::Permission::VIEW_CHANNEL));
        QVERIFY(res.testFlag(Discord::Permission::SEND_MESSAGES));
    }

    void memberOverwriteAppliesLast()
    {
        using Discord::Permission;

        const Discord::Permissions base = Discord::Permission::VIEW_CHANNEL;
        QList<Discord::PermissionOverwrite> overwrites;
        // Listed member-first to prove @everyone is not order dependent.
        overwrites.append(memberOverwrite(9, Discord::NO_PERMISSIONS,
                                          Discord::Permission::SEND_MESSAGES));
        overwrites.append(roleOverwrite(500, Discord::Permission::SEND_MESSAGES,
                                        Discord::NO_PERMISSIONS)); // @everyone (guild id)

        const auto res = Core::PermissionComputer::computeOverwrites(
                base, sid(9), sid(500), {}, overwrites);

        QVERIFY(res.testFlag(Discord::Permission::VIEW_CHANNEL));
        QVERIFY(!res.testFlag(Discord::Permission::SEND_MESSAGES));
    }

    void administratorAndOwnerShortCircuit()
    {
        using Discord::Permission;

        const auto guildId = sid(500);
        QList<Discord::Role> roles;
        roles.append(makeRole(500, Discord::Permission::VIEW_CHANNEL)); // @everyone
        roles.append(makeRole(100, Discord::Permission::ADMINISTRATOR));

        QList<Discord::PermissionOverwrite> overwrites;
        overwrites.append(roleOverwrite(500, Discord::NO_PERMISSIONS,
                                        Discord::Permission::VIEW_CHANNEL));

        // Owner: unconditional.
        const auto ownerRes = Core::PermissionComputer::computeChannelPermissions(
                7, 7, guildId, false, { 100 }, roles, overwrites);
        QVERIFY(ownerRes == Discord::ALL_PERMISSIONS);

        // Administrator ignores the @everyone VIEW_CHANNEL deny.
        const auto adminRes = Core::PermissionComputer::computeChannelPermissions(
                999, 7, guildId, false, { 100 }, roles, overwrites);
        QVERIFY(adminRes == Discord::ALL_PERMISSIONS);
    }

    void channelPermissionsIndependentOfRoleOrder()
    {
        using Discord::Permission;

        const auto guildId = sid(500);
        QList<Discord::Role> roles;
        roles.append(makeRole(500, Discord::Permission::VIEW_CHANNEL
                                           | Discord::Permission::SEND_MESSAGES));
        roles.append(makeRole(100, Discord::Permission::MENTION_EVERYONE, 10));
        roles.append(makeRole(200, Discord::Permission::ATTACH_FILES, 20));

        QList<Discord::PermissionOverwrite> overwrites;
        overwrites.append(roleOverwrite(100, Discord::NO_PERMISSIONS,
                                        Discord::Permission::READ_MESSAGE_HISTORY));
        overwrites.append(roleOverwrite(200, Discord::Permission::READ_MESSAGE_HISTORY,
                                        Discord::NO_PERMISSIONS));

        const auto forward = Core::PermissionComputer::computeChannelPermissions(
                999, 7, guildId, false, { 100, 200 }, roles, overwrites);
        const auto reverse = Core::PermissionComputer::computeChannelPermissions(
                999, 7, guildId, false, { 200, 100 }, roles, overwrites);

        QVERIFY(forward == reverse);
        // Every permission contributed by roles @everyone + roleA + roleB is
        // present (allow from roleB restores the history read denied by roleA).
        QVERIFY(forward.testFlag(Discord::Permission::VIEW_CHANNEL));
        QVERIFY(forward.testFlag(Discord::Permission::SEND_MESSAGES));
        QVERIFY(forward.testFlag(Discord::Permission::MENTION_EVERYONE));
        QVERIFY(forward.testFlag(Discord::Permission::ATTACH_FILES));
        QVERIFY(forward.testFlag(Discord::Permission::READ_MESSAGE_HISTORY));
    }
};

QTEST_GUILESS_MAIN(TestCoreManagers)
#include "tst_CoreManagers.moc"
