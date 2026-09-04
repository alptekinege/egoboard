#include <QtTest>

#include "BookmarkManager.h"
#include "GroupTreeModel.h"
#include "StorageManager.h"

#include <QColor>
#include <QDataStream>
#include <QIODevice>
#include <QMimeData>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>

class TestGroupTreeModel : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void emptyTree();
    void hierarchyAndNavigation();
    void modelRoles();
    void dynamicEntryCountUpdates();
    void indexAndGroupLookups();
    void dropEntriesOntoGroup();
    void dragAndDropGroupReparenting();
    void cyclePreventionInDragAndDrop();
    void rebuildsOnGroupChanges();

private:
    QTemporaryDir m_dir;
    StorageManager *m_storage = nullptr;
    BookmarkManager *m_bookmarks = nullptr;
};

void TestGroupTreeModel::init()
{
    delete m_bookmarks;
    delete m_storage;
    const QString path = m_dir.filePath(
        QStringLiteral("groups-%1.db").arg(QRandomGenerator::global()->generate64()));
    m_storage = new StorageManager(path);
    m_bookmarks = new BookmarkManager(m_storage->database(), this);
}

void TestGroupTreeModel::emptyTree()
{
    GroupTreeModel model(m_bookmarks);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.columnCount(), 1);
    QVERIFY(!model.index(0, 0).isValid());
    QVERIFY(!model.groupForIndex(QModelIndex()).has_value());
    QVERIFY(!model.indexForGroup(1).isValid());
}

void TestGroupTreeModel::hierarchyAndNavigation()
{
    // Create hierarchy:
    // Root 1
    //  └── Child 1A
    //       └── Grandchild 1A1
    // Root 2
    const qint64 r1 = m_bookmarks->createGroup(QStringLiteral("Root 1"), 0);
    const qint64 c1 = m_bookmarks->createGroup(QStringLiteral("Child 1A"), r1);
    const qint64 g1 = m_bookmarks->createGroup(QStringLiteral("Grandchild 1A1"), c1);
    const qint64 r2 = m_bookmarks->createGroup(QStringLiteral("Root 2"), 0);

    GroupTreeModel model(m_bookmarks);

    // Top level has 2 items: Root 1, Root 2 (ordered by name)
    QCOMPARE(model.rowCount(), 2);

    const QModelIndex r1Idx = model.index(0, 0);
    QVERIFY(r1Idx.isValid());
    QCOMPARE(model.data(r1Idx, GroupTreeModel::GroupIdRole).toLongLong(), r1);
    QCOMPARE(model.rowCount(r1Idx), 1);

    const QModelIndex c1Idx = model.index(0, 0, r1Idx);
    QVERIFY(c1Idx.isValid());
    QCOMPARE(model.data(c1Idx, GroupTreeModel::GroupIdRole).toLongLong(), c1);
    QCOMPARE(model.rowCount(c1Idx), 1);

    const QModelIndex g1Idx = model.index(0, 0, c1Idx);
    QVERIFY(g1Idx.isValid());
    QCOMPARE(model.data(g1Idx, GroupTreeModel::GroupIdRole).toLongLong(), g1);
    QCOMPARE(model.rowCount(g1Idx), 0);

    // Verify parent relationships
    QCOMPARE(model.parent(g1Idx), c1Idx);
    QCOMPARE(model.parent(c1Idx), r1Idx);
    QCOMPARE(model.parent(r1Idx), QModelIndex());

    const QModelIndex r2Idx = model.index(1, 0);
    QVERIFY(r2Idx.isValid());
    QCOMPARE(model.data(r2Idx, GroupTreeModel::GroupIdRole).toLongLong(), r2);
    QCOMPARE(model.rowCount(r2Idx), 0);
}

void TestGroupTreeModel::modelRoles()
{
    const qint64 gid = m_bookmarks->createGroup(
        QStringLiteral("Development"), 0, QStringLiteral("#ff5500"), QStringLiteral("code-context"));

    GroupTreeModel model(m_bookmarks);
    const QModelIndex idx = model.index(0, 0);
    QVERIFY(idx.isValid());

    QCOMPARE(model.data(idx, GroupTreeModel::GroupIdRole).toLongLong(), gid);
    QCOMPARE(model.data(idx, GroupTreeModel::ColorRole).value<QColor>(), QColor(QStringLiteral("#ff5500")));
    QCOMPARE(model.data(idx, GroupTreeModel::IconRole).toString(), QStringLiteral("code-context"));
    QCOMPARE(model.data(idx, GroupTreeModel::EntryCountRole).toInt(), 0);
    QCOMPARE(model.data(idx, Qt::DisplayRole).toString(), QStringLiteral("Development (0)"));
    QVERIFY(model.data(idx, Qt::ToolTipRole).toString().contains(QStringLiteral("0 entries")));
    QVERIFY(model.data(idx, Qt::DecorationRole).isValid()); // QIcon

    const auto roleMap = model.roleNames();
    QCOMPARE(roleMap.value(GroupTreeModel::GroupIdRole), QByteArray("groupId"));
    QCOMPARE(roleMap.value(GroupTreeModel::ColorRole), QByteArray("color"));
    QCOMPARE(roleMap.value(GroupTreeModel::EntryCountRole), QByteArray("entryCount"));
}

void TestGroupTreeModel::dynamicEntryCountUpdates()
{
    const qint64 gid = m_bookmarks->createGroup(QStringLiteral("Starred"), 0);

    ClipboardRecord r1;
    r1.hash = QByteArrayLiteral("e1");
    r1.type = ContentType::Text;
    r1.textData = QStringLiteral("Entry 1");
    r1.preview = QStringLiteral("Entry 1");
    r1.timestamp = 1000;
    const qint64 eid1 = m_storage->insertOrUpdate(r1);

    ClipboardRecord r2;
    r2.hash = QByteArrayLiteral("e2");
    r2.type = ContentType::Text;
    r2.textData = QStringLiteral("Entry 2");
    r2.preview = QStringLiteral("Entry 2");
    r2.timestamp = 2000;
    const qint64 eid2 = m_storage->insertOrUpdate(r2);

    GroupTreeModel model(m_bookmarks);
    const QModelIndex idx = model.index(0, 0);
    QCOMPARE(model.data(idx, GroupTreeModel::EntryCountRole).toInt(), 0);

    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);

    // Assign entry 1
    m_bookmarks->assignEntry(eid1, gid);
    QTRY_VERIFY(dataSpy.count() >= 1);
    QCOMPARE(model.data(idx, GroupTreeModel::EntryCountRole).toInt(), 1);
    QCOMPARE(model.data(idx, Qt::DisplayRole).toString(), QStringLiteral("Starred (1)"));

    // Assign entry 2
    m_bookmarks->assignEntry(eid2, gid);
    QCOMPARE(model.data(idx, GroupTreeModel::EntryCountRole).toInt(), 2);
    QCOMPARE(model.data(idx, Qt::DisplayRole).toString(), QStringLiteral("Starred (2)"));

    // Remove entry 1
    m_bookmarks->removeFromGroup(eid1, gid);
    QCOMPARE(model.data(idx, GroupTreeModel::EntryCountRole).toInt(), 1);
}

void TestGroupTreeModel::indexAndGroupLookups()
{
    const qint64 parentId = m_bookmarks->createGroup(QStringLiteral("Parent"), 0);
    const qint64 childId = m_bookmarks->createGroup(QStringLiteral("Child"), parentId);

    GroupTreeModel model(m_bookmarks);

    const QModelIndex pIdx = model.indexForGroup(parentId);
    QVERIFY(pIdx.isValid());
    QCOMPARE(model.data(pIdx, GroupTreeModel::GroupIdRole).toLongLong(), parentId);

    const QModelIndex cIdx = model.indexForGroup(childId);
    QVERIFY(cIdx.isValid());
    QCOMPARE(model.data(cIdx, GroupTreeModel::GroupIdRole).toLongLong(), childId);

    auto pGroup = model.groupForIndex(pIdx);
    QVERIFY(pGroup.has_value());
    QCOMPARE(pGroup->name, QStringLiteral("Parent"));

    auto cGroup = model.groupForIndex(cIdx);
    QVERIFY(cGroup.has_value());
    QCOMPARE(cGroup->name, QStringLiteral("Child"));
    QCOMPARE(cGroup->parentId, parentId);

    QVERIFY(!model.indexForGroup(9999).isValid());
}

void TestGroupTreeModel::dropEntriesOntoGroup()
{
    const qint64 gid = m_bookmarks->createGroup(QStringLiteral("TargetGroup"), 0);
    GroupTreeModel model(m_bookmarks);
    const QModelIndex groupIdx = model.index(0, 0);

    QList<qint64> entryIds = {101, 102, 103};
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream << entryIds;

    QMimeData mimeData;
    mimeData.setData(QStringLiteral("application/x-egoboard-entry-ids"), encoded);

    // Can drop onto the group index
    QVERIFY(model.canDropMimeData(&mimeData, Qt::CopyAction, -1, -1, groupIdx));

    // Dropping onto empty space (no parent / root) is rejected for entry items
    QVERIFY(!model.dropMimeData(&mimeData, Qt::CopyAction, -1, -1, QModelIndex()));

    QSignalSpy spy(&model, &GroupTreeModel::entriesDropped);
    QVERIFY(model.dropMimeData(&mimeData, Qt::CopyAction, -1, -1, groupIdx));
    QCOMPARE(spy.count(), 1);

    const QList<qint64> receivedIds = spy.first().at(0).value<QList<qint64>>();
    const qint64 receivedGid = spy.first().at(1).toLongLong();
    QCOMPARE(receivedIds, entryIds);
    QCOMPARE(receivedGid, gid);
}

void TestGroupTreeModel::dragAndDropGroupReparenting()
{
    const qint64 g1 = m_bookmarks->createGroup(QStringLiteral("Group 1"), 0);
    const qint64 g2 = m_bookmarks->createGroup(QStringLiteral("Group 2"), 0);

    GroupTreeModel model(m_bookmarks);
    const QModelIndex g1Idx = model.indexForGroup(g1);

    // Prepare MIME data representing dragging g2 (QList<qint64>)
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream << QList<qint64>{g2};

    QMimeData mimeData;
    mimeData.setData(QStringLiteral("application/x-egoboard-group-ids"), encoded);

    // Verify dropping g2 onto g1 to make g2 a child of g1
    QVERIFY(model.canDropMimeData(&mimeData, Qt::MoveAction, -1, -1, g1Idx));
    QVERIFY(model.dropMimeData(&mimeData, Qt::MoveAction, -1, -1, g1Idx));

    // Verify g2 is now under g1 in storage
    auto g2Group = m_bookmarks->group(g2);
    QVERIFY(g2Group.has_value());
    QCOMPARE(g2Group->parentId, g1);
}

void TestGroupTreeModel::cyclePreventionInDragAndDrop()
{
    const qint64 parent = m_bookmarks->createGroup(QStringLiteral("Parent"), 0);
    const qint64 child = m_bookmarks->createGroup(QStringLiteral("Child"), parent);
    const qint64 grandchild = m_bookmarks->createGroup(QStringLiteral("Grandchild"), child);

    GroupTreeModel model(m_bookmarks);
    const QModelIndex parentIdx = model.indexForGroup(parent);
    const QModelIndex childIdx = model.indexForGroup(child);
    const QModelIndex grandchildIdx = model.indexForGroup(grandchild);

    // Try to drag Parent onto Child or Grandchild
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream << QList<qint64>{parent};

    QMimeData mimeData;
    mimeData.setData(QStringLiteral("application/x-egoboard-group-ids"), encoded);

    // Dropping parent onto itself is disallowed by moveGroup in dropMimeData
    QVERIFY(!model.dropMimeData(&mimeData, Qt::MoveAction, -1, -1, parentIdx));

    // Dropping parent onto its child or grandchild creates a cycle: MUST be rejected!
    QVERIFY(!model.dropMimeData(&mimeData, Qt::MoveAction, -1, -1, childIdx));
    QVERIFY(!model.dropMimeData(&mimeData, Qt::MoveAction, -1, -1, grandchildIdx));

    // But dragging child onto another root is allowed
    const qint64 other = m_bookmarks->createGroup(QStringLiteral("Other"), 0);
    QByteArray childEncoded;
    QDataStream childStream(&childEncoded, QIODevice::WriteOnly);
    childStream << QList<qint64>{child};
    QMimeData childMime;
    childMime.setData(QStringLiteral("application/x-egoboard-group-ids"), childEncoded);
    QVERIFY(model.dropMimeData(&childMime, Qt::MoveAction, -1, -1, model.indexForGroup(other)));

    auto childGroup = m_bookmarks->group(child);
    QVERIFY(childGroup.has_value());
    QCOMPARE(childGroup->parentId, other);
}

void TestGroupTreeModel::rebuildsOnGroupChanges()
{
    GroupTreeModel model(m_bookmarks);
    QCOMPARE(model.rowCount(), 0);

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

    m_bookmarks->createGroup(QStringLiteral("AutoRebuild"), 0);
    QCOMPARE(resetSpy.count(), 1);
    QCOMPARE(model.rowCount(), 1);

    m_bookmarks->deleteGroup(model.index(0, 0).data(GroupTreeModel::GroupIdRole).toLongLong());
    QCOMPARE(resetSpy.count(), 2);
    QCOMPARE(model.rowCount(), 0);
}

QTEST_GUILESS_MAIN(TestGroupTreeModel)
#include "tst_grouptreemodel.moc"
