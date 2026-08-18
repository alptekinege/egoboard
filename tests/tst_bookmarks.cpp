#include <QtTest>

#include "BookmarkManager.h"
#include "StorageManager.h"

#include <QRandomGenerator>
#include <QTemporaryDir>

class TestBookmarks : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void crudAndNesting();
    void cyclePrevention();
    void membership();
    void deleteGroupReparents();

private:
    QTemporaryDir m_dir;
    StorageManager *m_storage = nullptr;
    BookmarkManager *m_bookmarks = nullptr;
};

void TestBookmarks::init()
{
    delete m_bookmarks;
    delete m_storage; // deleteLater() never runs without an event loop
    m_storage = new StorageManager(m_dir.filePath(QStringLiteral("history-%1.db").arg(
        QRandomGenerator::global()->generate64())));
    m_bookmarks = new BookmarkManager(m_storage->database(), this);
}

void TestBookmarks::crudAndNesting()
{
    const qint64 work = m_bookmarks->createGroup(QStringLiteral("Work"));
    const qint64 logins = m_bookmarks->createGroup(QStringLiteral("Logins"), work,
                                                   QStringLiteral("#3daee9"), QStringLiteral("folder"));
    QVERIFY(work > 0);
    QVERIFY(logins > 0);

    const auto groups = m_bookmarks->groups();
    QCOMPARE(groups.size(), 2);
    const auto found = std::find_if(groups.cbegin(), groups.cend(),
                                     [logins](const BookmarkGroup &g) { return g.id == logins; });
    QVERIFY(found != groups.cend());
    QCOMPARE(found->parentId, work);
    QCOMPARE(found->color, QStringLiteral("#3daee9"));

    QVERIFY(m_bookmarks->updateGroup(logins, QStringLiteral("Credentials"),
                                     QStringLiteral("#f67400"), QStringLiteral("folder")));
    const auto updated = m_bookmarks->group(logins);
    QVERIFY(updated.has_value());
    QCOMPARE(updated->name, QStringLiteral("Credentials"));

    QVERIFY(m_bookmarks->isDescendantOf(logins, work));
    QVERIFY(!m_bookmarks->isDescendantOf(work, logins));
}

void TestBookmarks::cyclePrevention()
{
    const qint64 a = m_bookmarks->createGroup(QStringLiteral("A"));
    const qint64 b = m_bookmarks->createGroup(QStringLiteral("B"), a);
    // Moving A under its own child B would create a cycle.
    QVERIFY(!m_bookmarks->moveGroup(a, b));
    QVERIFY(m_bookmarks->moveGroup(b, 0)); // detach to top level is fine
    QVERIFY(!m_bookmarks->moveGroup(a, a));
}

void TestBookmarks::membership()
{
    ClipboardRecord record;
    record.hash = QByteArrayLiteral("bm1");
    record.type = ContentType::Text;
    record.textData = QStringLiteral("content");
    record.preview = QStringLiteral("content");
    record.timestamp = 1;
    const qint64 entryId = m_storage->insertOrUpdate(record);

    const qint64 group = m_bookmarks->createGroup(QStringLiteral("G"));
    QVERIFY(m_bookmarks->assignEntry(entryId, group));
    QCOMPARE(m_bookmarks->entryIdsForGroup(group), QList<qint64>{entryId});
    QCOMPARE(m_bookmarks->groupIdsForEntry(entryId), QList<qint64>{group});
    QCOMPARE(m_bookmarks->entryCount(group), 1);

    // Assigning twice must not duplicate.
    m_bookmarks->assignEntry(entryId, group);
    QCOMPARE(m_bookmarks->entryCount(group), 1);

    QVERIFY(m_bookmarks->removeFromGroup(entryId, group));
    QCOMPARE(m_bookmarks->entryCount(group), 0);

    // Group filter on storage sees membership.
    m_bookmarks->assignEntry(entryId, group);
    FilterSpec filter;
    filter.groupId = group;
    QCOMPARE(m_storage->fetchPage(filter, {}, 10).size(), 1);
    filter.groupId = m_bookmarks->createGroup(QStringLiteral("Other"));
    QCOMPARE(m_storage->fetchPage(filter, {}, 10).size(), 0);
}

void TestBookmarks::deleteGroupReparents()
{
    const qint64 parent = m_bookmarks->createGroup(QStringLiteral("Parent"));
    const qint64 child = m_bookmarks->createGroup(QStringLiteral("Child"), parent);
    QVERIFY(m_bookmarks->deleteGroup(parent));
    const auto orphan = m_bookmarks->group(child);
    QVERIFY(orphan.has_value());
    QCOMPARE(orphan->parentId, qint64(0));
}

QTEST_GUILESS_MAIN(TestBookmarks)
#include "tst_bookmarks.moc"
