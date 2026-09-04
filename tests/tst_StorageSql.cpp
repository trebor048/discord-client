#include <QtTest>

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QVariant>

// Regression tests for the SQL contracts the chat cache depends on. They mirror
// the statements in src/Storage/MessageRepository.cpp (upsert / retention
// prune) and assert the semantics that the rest of the app relies on:
//   * an upsert of a real message must NOT resurrect a tombstone (deleted=1),
//   * a retention prune must only ever evict live rows beyond the newest N,
//     and must never count tombstones/context_only stubs toward the cap.
// Standalone QtTest: no app code is linked, only the QSQLITE driver.
class TestStorageSql : public QObject
{
    Q_OBJECT

    // Matches src/Storage/MessageRepository.cpp saveMessages main-path upsert
    // (subset of columns; deleted/context_only semantics identical).
    const QString kMessageUpsert = QStringLiteral(R"(
        INSERT INTO messages
        (id, channel_id, content, deleted, context_only)
        VALUES (:id, :channel_id, :content, 0, 0)
        ON CONFLICT(id) DO UPDATE SET
            content = excluded.content,
            deleted = deleted,
            context_only = 0
    )");

    // Matches pruneChannelIfOverCap's fast gate.
    const QString kLiveCountQuery = QStringLiteral(R"(
        SELECT COUNT(*) FROM messages
        WHERE channel_id = :channel_id AND deleted = 0 AND context_only = 0
    )");

    // Matches pruneChannel's live-row eviction (subset, no attachments).
    const QString kPruneSql = QStringLiteral(R"(
        DELETE FROM messages
        WHERE channel_id = :channel_id AND deleted = 0 AND context_only = 0 AND id NOT IN (
            SELECT id FROM messages
            WHERE channel_id = :channel_id AND deleted = 0 AND context_only = 0
            ORDER BY id DESC
            LIMIT :limit
        )
    )");

private slots:
    void init()
    {
        QVERIFY(m_dir.isValid());
        // A fresh connection name and database file per test: Qt refuses to
        // removeDatabase() while any QSqlDatabase handle is still alive, and a
        // reused file would carry the previous test's schema ("table messages
        // already exists"). Uniquifying both keeps every test independent even
        // if a previous cleanup leaked a connection.
        const int n = m_fileCounter++;
        m_conn = QStringLiteral("tst_storage_%1_%2").arg(quintptr(this)).arg(n);
        m_path = m_dir.filePath(QStringLiteral("cache_%1.sqlite").arg(n));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
            db.setDatabaseName(m_path);
            QVERIFY2(db.open(), qPrintable(db.lastError().text()));
        }

        QSqlDatabase db = QSqlDatabase::database(m_conn);
        QSqlQuery q(db);
        QVERIFY2(q.exec(QStringLiteral(R"(
            CREATE TABLE messages (
                id INTEGER NOT NULL,
                channel_id INTEGER NOT NULL,
                content TEXT NOT NULL,
                deleted INTEGER NOT NULL,
                context_only INTEGER NOT NULL DEFAULT 0,
                PRIMARY KEY(id)
            )
        )")), qPrintable(q.lastError().text()));
    }

    void cleanup()
    {
        if (QSqlDatabase::contains(m_conn)) {
            {
                // Scope the handle: removeDatabase() must not see any live
                // QSqlDatabase object or it keeps the connection registered.
                QSqlDatabase db = QSqlDatabase::database(m_conn, false);
                if (db.isOpen())
                    db.close();
            }
            QSqlDatabase::removeDatabase(m_conn);
        }
    }

    // Seeded bug C: a re-save of a message whose row is a tombstone must not
    // resurrect it (ON CONFLICT keeps deleted=deleted).
    void messageUpsertDoesNotResurrectTombstone()
    {
        QSqlDatabase db = QSqlDatabase::database(m_conn);

        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO messages (id, channel_id, content, deleted, context_only) "
                "VALUES (1, 10, 'hello', 0, 0)")));

        // Simulate MessageRepository::markMessageDeleted.
        QSqlQuery tombstone(db);
        QVERIFY(tombstone.exec(QStringLiteral("UPDATE messages SET deleted = 1 WHERE id = 1")));

        // Re-save the same message with fresh content (gateway re-delivery).
        QSqlQuery upsert(db);
        upsert.prepare(kMessageUpsert);
        upsert.bindValue(":id", 1);
        upsert.bindValue(":channel_id", 10);
        upsert.bindValue(":content", QStringLiteral("edited after delete"));
        QVERIFY2(upsert.exec(), qPrintable(upsert.lastError().text()));

        QSqlQuery check(db);
        QVERIFY(check.exec(QStringLiteral("SELECT content, deleted FROM messages WHERE id = 1")));
        QVERIFY(check.next());
        QCOMPARE(check.value(0).toString(), QStringLiteral("edited after delete"));
        QCOMPARE(check.value(1).toInt(), 1); // still tombstoned
    }

    // A real (live) save promotes a previously context_only stub but never
    // resurrects a tombstone.
    void contextOnlyStubIsPromotedByLiveSave()
    {
        QSqlDatabase db = QSqlDatabase::database(m_conn);

        QSqlQuery stub(db);
        QVERIFY(stub.exec(QStringLiteral(
                "INSERT INTO messages (id, channel_id, content, deleted, context_only) "
                "VALUES (7, 10, 'reply preview', 0, 1)")));

        QSqlQuery upsert(db);
        upsert.prepare(kMessageUpsert);
        upsert.bindValue(":id", 7);
        upsert.bindValue(":channel_id", 10);
        upsert.bindValue(":content", QStringLiteral("full message"));
        QVERIFY2(upsert.exec(), qPrintable(upsert.lastError().text()));

        QSqlQuery check(db);
        QVERIFY(check.exec(QStringLiteral("SELECT context_only FROM messages WHERE id = 7")));
        QVERIFY(check.next());
        QCOMPARE(check.value(0).toInt(), 0);
    }

    // Seeded bug D: the retention prune must evict only LIVE rows past the cap;
    // tombstones and context_only stubs are neither counted nor evicted.
    void retentionPruneEvictsOnlyLiveRowsPastCap()
    {
        QSqlDatabase db = QSqlDatabase::database(m_conn);
        QSqlQuery q(db);

        // 8 live rows in channel 10, plus one tombstone and one context_only stub.
        for (int i = 1; i <= 8; ++i) {
            QVERIFY(q.exec(QStringLiteral(
                    "INSERT INTO messages (id, channel_id, content, deleted, context_only) "
                    "VALUES (%1, 10, 'live %1', 0, 0)")
                              .arg(i)));
        }
        QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO messages (id, channel_id, content, deleted, context_only) "
                "VALUES (90, 10, 'deleted msg', 1, 0)")));
        QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO messages (id, channel_id, content, deleted, context_only) "
                "VALUES (95, 10, 'reply stub', 0, 1)")));

        // Fast gate: the tombstone/stub must not count toward the cap.
        QSqlQuery count(db);
        count.prepare(kLiveCountQuery);
        count.bindValue(":channel_id", 10);
        QVERIFY2(count.exec(), qPrintable(count.lastError().text()));
        QVERIFY(count.next());
        QCOMPARE(count.value(0).toInt(), 8); // 90/95 excluded from the count

        // Prune with cap 5.
        QSqlQuery prune(db);
        prune.prepare(kPruneSql);
        prune.bindValue(":channel_id", 10);
        prune.bindValue(":limit", 5);
        QVERIFY2(prune.exec(), qPrintable(prune.lastError().text()));

        QSqlQuery check(db);
        QVERIFY(check.exec(QStringLiteral(
                "SELECT id, deleted, context_only FROM messages "
                "WHERE channel_id = 10 ORDER BY id")));
        QList<int> remainingIds;
        while (check.next())
            remainingIds.append(check.value(0).toInt());

        // Newest 5 live (4..8) survive; old live (1..3) evicted; the tombstone
        // (90) and stub (95) survive regardless of their id.
        const QList<int> expected = { 4, 5, 6, 7, 8, 90, 95 };
        QCOMPARE(remainingIds.size(), expected.size());
        for (int i = 0; i < expected.size(); ++i)
            QCOMPARE(remainingIds[i], expected[i]);
    }

    // Under-cap channels keep every live row (prune is a no-op).
    void retentionPruneIsNoOpUnderCap()
    {
        QSqlDatabase db = QSqlDatabase::database(m_conn);
        QSqlQuery q(db);
        for (int i = 1; i <= 4; ++i) {
            QVERIFY(q.exec(QStringLiteral(
                    "INSERT INTO messages (id, channel_id, content, deleted, context_only) "
                    "VALUES (%1, 20, 'live %1', 0, 0)")
                              .arg(i)));
        }
        QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO messages (id, channel_id, content, deleted, context_only) "
                "VALUES (50, 20, 'tombstone', 1, 0)")));

        QSqlQuery count(db);
        count.prepare(kLiveCountQuery);
        count.bindValue(":channel_id", 20);
        QVERIFY2(count.exec(), qPrintable(count.lastError().text()));
        QVERIFY(count.next());
        QCOMPARE(count.value(0).toInt(), 4);

        QSqlQuery prune(db);
        prune.prepare(kPruneSql);
        prune.bindValue(":channel_id", 20);
        prune.bindValue(":limit", 5);
        QVERIFY2(prune.exec(), qPrintable(prune.lastError().text()));

        QSqlQuery check(db);
        QVERIFY(check.exec(QStringLiteral("SELECT COUNT(*) FROM messages WHERE channel_id = 20")));
        QVERIFY(check.next());
        QCOMPARE(check.value(0).toInt(), 5); // 4 live + tombstone untouched
    }

private:
    QTemporaryDir m_dir;
    int m_fileCounter = 0;
    QString m_path;
    QString m_conn;
};

// QSqlDatabase needs a QCoreApplication, so use the GUILESS main (APPLESS
// would make every addDatabase/open call abort).
QTEST_GUILESS_MAIN(TestStorageSql)

#include "tst_StorageSql.moc"
