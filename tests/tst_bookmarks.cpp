#include <QtTest>

#include "BookmarkManager.h"
#include "StorageManager.h"

#include <QRandomGenerator>
#include <QSignalSpy>
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
    void signalsAndForeignKeys();
    void deepCyclePrevention();
    void rejectsInvalidGroupOperations();
    void rejectsDuplicateSiblingNames();
    void membershipCascadesWithEntryAndGroupDeletion();

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

void TestBookmarks::signalsAndForeignKeys()
{
    QSignalSpy groupsSpy(m_bookmarks, &BookmarkManager::groupsChanged);
    QSignalSpy membershipSpy(m_bookmarks, &BookmarkManager::membershipChanged);

    const qint64 group = m_bookmarks->createGroup(QStringLiteral("Signals"));
    QVERIFY(group > 0);
    QCOMPARE(groupsSpy.count(), 1);

    QVERIFY(!m_bookmarks->assignEntry(9999, group));
    QCOMPARE(membershipSpy.count(), 0);

    ClipboardRecord record;
    record.hash = QByteArrayLiteral("signal-entry");
    record.textData = QStringLiteral("entry");
    record.preview = record.textData;
    record.timestamp = 1;
    const qint64 entryId = m_storage->insertOrUpdate(record);
    QVERIFY(m_bookmarks->assignEntry(entryId, group));
    QCOMPARE(membershipSpy.count(), 1);
    QCOMPARE(membershipSpy.at(0).at(0).toLongLong(), entryId);
}

void TestBookmarks::deepCyclePrevention()
{
    const qint64 root = m_bookmarks->createGroup(QStringLiteral("Root"));
    const qint64 child = m_bookmarks->createGroup(QStringLiteral("Child"), root);
    const qint64 grandchild = m_bookmarks->createGroup(QStringLiteral("Grandchild"), child);

    QVERIFY(!m_bookmarks->moveGroup(root, grandchild));
    QVERIFY(!m_bookmarks->moveGroup(child, grandchild));
    QVERIFY(m_bookmarks->moveGroup(grandchild, 0));

    const auto moved = m_bookmarks->group(grandchild);
    QVERIFY(moved.has_value());
    QCOMPARE(moved->parentId, qint64(0));
}

void TestBookmarks::rejectsInvalidGroupOperations()
{
    QVERIFY(m_bookmarks->createGroup(QStringLiteral("Orphan"), 99999) == 0);
    QVERIFY(!m_bookmarks->updateGroup(99999, QStringLiteral("Missing"), {}, {}));
    QVERIFY(!m_bookmarks->deleteGroup(99999));
    QVERIFY(!m_bookmarks->moveGroup(99999, 0));
    QVERIFY(!m_bookmarks->moveGroup(99999, 1));
    QVERIFY(!m_bookmarks->group(99999).has_value());
    QVERIFY(!m_bookmarks->isDescendantOf(99999, 1));
    QVERIFY(!m_bookmarks->isDescendantOf(1, 99999));

    const qint64 group = m_bookmarks->createGroup(QStringLiteral("Valid"));
    QVERIFY(group > 0);
    QVERIFY(!m_bookmarks->assignEntry(99999, group));
    QVERIFY(!m_bookmarks->assignEntry(99999, 99999));
    QCOMPARE(m_bookmarks->entryIdsForGroup(99999), QList<qint64>{});
    QCOMPARE(m_bookmarks->groupIdsForEntry(99999), QList<qint64>{});
}

void TestBookmarks::rejectsDuplicateSiblingNames()
{
    const qint64 work = m_bookmarks->createGroup(QStringLiteral("Work"));
    QVERIFY(work > 0);
    // Same name (any case) under the same parent is rejected.
    QCOMPARE(m_bookmarks->createGroup(QStringLiteral("work")), qint64(0));
    QCOMPARE(m_bookmarks->createGroup(QStringLiteral("Work")), qint64(0));

    // A different parent may reuse the name.
    const qint64 personal = m_bookmarks->createGroup(QStringLiteral("Personal"));
    QVERIFY(personal > 0);
    const qint64 nested = m_bookmarks->createGroup(QStringLiteral("Work"), personal);
    QVERIFY(nested > 0);

    // Renaming into a sibling collision fails and leaves the original name.
    const qint64 other = m_bookmarks->createGroup(QStringLiteral("Other"), personal);
    QVERIFY(other > 0);
    QCOMPARE(m_bookmarks->updateGroup(other, QStringLiteral("work"), {}, {}), false);
    QCOMPARE(m_bookmarks->group(other)->name, QStringLiteral("Other"));
    QCOMPARE(m_bookmarks->updateGroup(work, QStringLiteral("personal"), {}, {}), false);
    QCOMPARE(m_bookmarks->group(work)->name, QStringLiteral("Work"));
    // Renaming to the same name (self) is not a collision.
    QVERIFY(m_bookmarks->updateGroup(work, QStringLiteral("Work"), {}, {}));
}

void TestBookmarks::membershipCascadesWithEntryAndGroupDeletion()
{
    ClipboardRecord record;
    record.hash = QByteArrayLiteral("cascade-entry");
    record.textData = QStringLiteral("cascade");
    record.preview = record.textData;
    record.timestamp = 1;
    const qint64 entryId = m_storage->insertOrUpdate(record);
    const qint64 parent = m_bookmarks->createGroup(QStringLiteral("Parent"));
    const qint64 child = m_bookmarks->createGroup(QStringLiteral("Child"), parent);
    QVERIFY(m_bookmarks->assignEntry(entryId, parent));
    QVERIFY(m_bookmarks->assignEntry(entryId, child));
    QCOMPARE(m_bookmarks->groupIdsForEntry(entryId).size(), 2);

    QVERIFY(m_storage->remove(entryId));
    QCOMPARE(m_bookmarks->groupIdsForEntry(entryId), QList<qint64>{});
    QCOMPARE(m_bookmarks->entryCount(parent), 0);
    QCOMPARE(m_bookmarks->entryCount(child), 0);

    const qint64 secondEntry = m_storage->insertOrUpdate(
        [&] {
            ClipboardRecord second;
            second.hash = QByteArrayLiteral("cascade-entry-2");
            second.textData = QStringLiteral("second");
            second.preview = second.textData;
            second.timestamp = 2;
            return second;
        }());
    QVERIFY(m_bookmarks->assignEntry(secondEntry, child));
    QVERIFY(m_bookmarks->deleteGroup(parent));
    QVERIFY(!m_bookmarks->group(parent).has_value());
    const auto survivingChild = m_bookmarks->group(child);
    QVERIFY(survivingChild.has_value());
    QCOMPARE(survivingChild->parentId, qint64(0));
    QCOMPARE(m_bookmarks->groupIdsForEntry(secondEntry), QList<qint64>{child});

    QVERIFY(m_bookmarks->deleteGroup(child));
    QVERIFY(!m_bookmarks->group(child).has_value());
    QCOMPARE(m_bookmarks->groupIdsForEntry(secondEntry), QList<qint64>{});
}

QTEST_GUILESS_MAIN(TestBookmarks)
#include "tst_bookmarks.moc"
