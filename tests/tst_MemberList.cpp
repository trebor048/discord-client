#include <QtTest>

#include "Core/MemberListManager.hpp"
#include "Storage/ChannelRepository.hpp"
#include "Storage/RoleRepository.hpp"

using namespace Acheron;

// Regression tests for MemberListManager list-cache handling.
//
// The repositories are constructed against a cache connection that is never
// opened: queries fail and return empty results, which is exactly what the
// manager needs (no roles, no permission overwrites -> list id is the murmur
// hash of an empty input, i.e. "0").
class TestMemberList : public QObject
{
    Q_OBJECT

private:
    static Discord::GuildMemberListUpdate makeUpdate(const QString &listId, int itemCount)
    {
        QJsonArray items;
        // first row is the group header
        items.append(QJsonObject{ { "group", QJsonObject{ { "id", "online" }, { "count", itemCount } } } });
        for (int i = 0; i < itemCount; i++) {
            items.append(QJsonObject{
                    { "member",
                      QJsonObject{ { "user",
                                     QJsonObject{ { "id", QString::number(100 + i) },
                                                  { "username", QString("user%1").arg(i) } } } } } });
        }

        QJsonObject root{
            { "id", listId },
            { "guild_id", "1" },
            { "member_count", itemCount },
            { "online_count", itemCount },
            { "groups", QJsonArray{ QJsonObject{ { "id", "online" }, { "count", itemCount } } } },
            { "ops",
              QJsonArray{ QJsonObject{ { "op", "SYNC" },
                                       { "range", QJsonArray{ 0, itemCount } },
                                       { "items", items } } } },
        };
        return Discord::GuildMemberListUpdate::fromJson(root);
    }

private slots:
    // A SYNC for the active list must materialize rows the model can read.
    void syncPopulatesActiveList()
    {
        Storage::ChannelRepository channelRepo(Core::Snowflake(9001));
        Storage::RoleRepository roleRepo(Core::Snowflake(9001));
        Core::MemberListManager manager(channelRepo, roleRepo);

        QSignalSpy resetSpy(&manager, &Core::MemberListManager::listReset);

        manager.setActiveChannel(Core::Snowflake(1), Core::Snowflake(10));
        manager.handleMemberListUpdate(makeUpdate(QStringLiteral("0"), 3));

        QCOMPARE(manager.totalItemCount(), 4); // 1 group header + 3 members
        QVERIFY(manager.isLoaded(0));
        QVERIFY(manager.isLoaded(3));
        const auto *member = manager.itemAt(1);
        QVERIFY(member);
        QCOMPARE(member->type, Core::MemberListItem::Type::Member);
        QCOMPARE(member->displayName, QStringLiteral("user0"));
        QVERIFY(resetSpy.count() >= 2); // reset from setActiveChannel + update
    }

    // An empty SYNC must drop previously materialized rows in its range.
    void emptySyncClearsRange()
    {
        Storage::ChannelRepository channelRepo(Core::Snowflake(9002));
        Storage::RoleRepository roleRepo(Core::Snowflake(9002));
        Core::MemberListManager manager(channelRepo, roleRepo);

        manager.setActiveChannel(Core::Snowflake(1), Core::Snowflake(10));
        manager.handleMemberListUpdate(makeUpdate(QStringLiteral("0"), 2));
        QVERIFY(manager.isLoaded(1));

        QJsonObject root{
            { "id", "0" },
            { "guild_id", "1" },
            { "member_count", 0 },
            { "online_count", 0 },
            { "groups", QJsonArray{ QJsonObject{ { "id", "online" }, { "count", 0 } } } },
            { "ops",
              QJsonArray{ QJsonObject{ { "op", "SYNC" },
                                       { "range", QJsonArray{ 0, 2 } },
                                       { "items", QJsonArray{} } } } },
        };
        manager.handleMemberListUpdate(Discord::GuildMemberListUpdate::fromJson(root));

        QVERIFY(!manager.isLoaded(0));
        QVERIFY(!manager.isLoaded(1));
    }

    // Regression: when the per-guild list cache exceeds its cap, eviction must
    // not corrupt the update currently being applied. Previously eviction ran
    // while a reference into gs.lists was still in use; QHash::remove can
    // relocate sibling elements, so the incoming SYNC items were written into
    // a dangling ListData and the active list stayed empty.
    void syncSurvivesListEviction()
    {
        Storage::ChannelRepository channelRepo(Core::Snowflake(9003));
        Storage::RoleRepository roleRepo(Core::Snowflake(9003));
        Core::MemberListManager manager(channelRepo, roleRepo);

        manager.setActiveChannel(Core::Snowflake(1), Core::Snowflake(10));

        // active list "0" plus 9 filler lists exceeds the cap of 8
        for (int i = 0; i < 9; i++)
            manager.handleMemberListUpdate(makeUpdate(QStringLiteral("filler%1").arg(i), 1));

        manager.handleMemberListUpdate(makeUpdate(QStringLiteral("0"), 5));

        QCOMPARE(manager.totalItemCount(), 6); // 1 group header + 5 members
        for (int row = 0; row < 6; row++)
            QVERIFY2(manager.isLoaded(row), qPrintable(QStringLiteral("row %1 missing").arg(row)));

        const auto *member = manager.itemAt(4);
        QVERIFY(member);
        QCOMPARE(member->type, Core::MemberListItem::Type::Member);
        QCOMPARE(member->displayName, QStringLiteral("user3"));
    }

    // Regression: presence-only GUILD_MEMBER_LIST_UPDATE batches (in-place
    // UPDATE ops, no structural changes) must repaint only the affected rows
    // instead of resetting the whole model. Resetting every presence change
    // made the member list relayout and lose scroll position.
    void updateOnlyRepaintsTargetedRows()
    {
        Storage::ChannelRepository channelRepo(Core::Snowflake(9004));
        Storage::RoleRepository roleRepo(Core::Snowflake(9004));
        Core::MemberListManager manager(channelRepo, roleRepo);

        manager.setActiveChannel(Core::Snowflake(1), Core::Snowflake(10));
        manager.handleMemberListUpdate(makeUpdate(QStringLiteral("0"), 3));

        QSignalSpy resetSpy(&manager, &Core::MemberListManager::listReset);
        QSignalSpy rowsSpy(&manager, &Core::MemberListManager::listRowsChanged);

        // A pure UPDATE batch touching index 1 (second member row).
        QJsonObject member{
            { "user", QJsonObject{ { "id", "101" }, { "username", "renamed" } } }
        };
        QJsonObject root{
            { "id", "0" },
            { "guild_id", "1" },
            { "member_count", 3 },
            { "online_count", 3 },
            { "ops",
              QJsonArray{ QJsonObject{ { "op", "UPDATE" },
                                       { "index", 1 },
                                       { "item", QJsonObject{ { "member", member } } } } } },
        };
        manager.handleMemberListUpdate(Discord::GuildMemberListUpdate::fromJson(root));

        QCOMPARE(resetSpy.count(), 0); // no full model reset
        QCOMPARE(rowsSpy.count(), 1);
        QCOMPARE(rowsSpy.at(0).at(0).toList().at(0).toInt(), 1);

        const auto *updated = manager.itemAt(1);
        QVERIFY(updated);
        QCOMPARE(updated->type, Core::MemberListItem::Type::Member);
        QCOMPARE(updated->displayName, QStringLiteral("renamed"));
    }

    // Real Discord payloads always carry the full `groups` array (a required
    // field), and presence changes bump the group *counts*. The ordered set of
    // group ids is unchanged, so this must still be treated as a cosmetic
    // update — repaint the affected rows (including the group header) instead
    // of resetting the model, which would lose scroll position.
    void updateWithCountOnlyGroupChangeRepaintsWithoutReset()
    {
        Storage::ChannelRepository channelRepo(Core::Snowflake(9005));
        Storage::RoleRepository roleRepo(Core::Snowflake(9005));
        Core::MemberListManager manager(channelRepo, roleRepo);

        manager.setActiveChannel(Core::Snowflake(1), Core::Snowflake(10));
        manager.handleMemberListUpdate(makeUpdate(QStringLiteral("0"), 3));

        QSignalSpy resetSpy(&manager, &Core::MemberListManager::listReset);
        QSignalSpy rowsSpy(&manager, &Core::MemberListManager::listRowsChanged);

        QJsonObject member{
            { "user", QJsonObject{ { "id", "101" }, { "username", "renamed" } } }
        };
        QJsonObject root{
            { "id", "0" },
            { "guild_id", "1" },
            { "member_count", 3 },
            { "online_count", 2 },
            // Same group ids, new counts (presence changed one member offline).
            { "groups", QJsonArray{ QJsonObject{ { "id", "online" }, { "count", 2 } } } },
            { "ops",
              QJsonArray{ QJsonObject{ { "op", "UPDATE" },
                                       { "index", 1 },
                                       { "item", QJsonObject{ { "member", member } } } } } },
        };
        manager.handleMemberListUpdate(Discord::GuildMemberListUpdate::fromJson(root));

        QCOMPARE(resetSpy.count(), 0); // same group ids -> no structural reset
        QVERIFY(rowsSpy.count() >= 1);
        const QList<QVariant> rows = rowsSpy.at(0).at(0).toList();
        QVERIFY2(rows.contains(1),
                 qPrintable(QStringLiteral("expected row 1 in %1 rows").arg(rows.size())));
        // The group header row must be refreshed so its count stays current.
        QVERIFY2(rows.contains(0),
                 qPrintable(QStringLiteral("expected row 0 in %1 rows").arg(rows.size())));
    }
};

QTEST_MAIN(TestMemberList)
#include "tst_MemberList.moc"
