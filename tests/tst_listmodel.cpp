#include <QtTest>

#include "ClipboardListModel.h"
#include "StorageManager.h"

#include <QDataStream>
#include <QMimeData>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>

class TestListModel : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void emptyModel();
    void dataRoles();
    void roleNames();
    void keysetPagination();
    void filterResetsAndRefetches();
    void lookupsByIdAndRow();
    void dragAndDropMime();
    void pinnedChangedUpdatesRowWithoutFullReset();
    void removalAndResetRefreshes();

private:
    ClipboardRecord makeRecord(const QByteArray &hash, const QString &text, qint64 timestamp);
    QTemporaryDir m_dir;
    StorageManager *m_storage = nullptr;
};

void TestListModel::init()
{
    delete m_storage;
    const QString path = m_dir.filePath(
        QStringLiteral("model-%1.db").arg(QRandomGenerator::global()->generate64()));
    m_storage = new StorageManager(path);
}

ClipboardRecord TestListModel::makeRecord(const QByteArray &hash, const QString &text,
                                         qint64 timestamp)
{
    ClipboardRecord record;
    record.hash = hash;
    record.type = ContentType::Text;
    record.textData = text;
    record.preview = text.left(40);
    record.timestamp = timestamp;
    record.sizeBytes = text.size();
    record.sourceApp = QStringLiteral("test-app");
    record.sourceWindow = QStringLiteral("Window Title");
    return record;
}

void TestListModel::emptyModel()
{
    ClipboardListModel model(m_storage);
    QCOMPARE(model.rowCount(), 0);
    // Initially canFetchMore is true until the first fetch attempt
    QCOMPARE(model.canFetchMore({}), true);
    model.fetchMore({});
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.canFetchMore({}), false);
    QCOMPARE(model.idAt(0), 0);
    QCOMPARE(model.rowForId(42), -1);
    QVERIFY(!model.recordForId(42).has_value());
}

void TestListModel::dataRoles()
{
    ClipboardRecord rec = makeRecord(QByteArrayLiteral("role_hash"), QStringLiteral("Role test"), 5000);
    rec.pinned = true;
    rec.sensitive = true;
    rec.useCount = 3;
    const qint64 id = m_storage->insertOrUpdate(rec);
    QVERIFY(id > 0);

    ClipboardListModel model(m_storage);
    model.refresh();
    QCOMPARE(model.rowCount(), 1);

    const QModelIndex idx = model.index(0, 0);
    QVERIFY(idx.isValid());

    QCOMPARE(model.data(idx, ClipboardListModel::IdRole).toLongLong(), id);
    QCOMPARE(model.data(idx, ClipboardListModel::TimestampRole).toLongLong(), qint64(5000));
    QCOMPARE(model.data(idx, ClipboardListModel::TypeRole).toInt(), static_cast<int>(ContentType::Text));
    QCOMPARE(model.data(idx, ClipboardListModel::PreviewRole).toString(), QStringLiteral("Role test"));
    QCOMPARE(model.data(idx, Qt::DisplayRole).toString(), QStringLiteral("Role test"));
    QCOMPARE(model.data(idx, ClipboardListModel::SourceAppRole).toString(), QStringLiteral("test-app"));
    QCOMPARE(model.data(idx, ClipboardListModel::SourceWindowRole).toString(), QStringLiteral("Window Title"));
    QCOMPARE(model.data(idx, ClipboardListModel::PinnedRole).toBool(), true);
    QCOMPARE(model.data(idx, ClipboardListModel::SensitiveRole).toBool(), true);
    QCOMPARE(model.data(idx, ClipboardListModel::SizeRole).toLongLong(), qint64(9));
    QCOMPARE(model.data(idx, ClipboardListModel::UseCountRole).toInt(), 0); // initial insert useCount is 0
    QCOMPARE(model.data(idx, ClipboardListModel::HashRole).toByteArray(), QByteArrayLiteral("role_hash"));
    QCOMPARE(model.data(idx, Qt::UserRole + 999), QVariant());
}

void TestListModel::roleNames()
{
    ClipboardListModel model(m_storage);
    const auto names = model.roleNames();
    QCOMPARE(names.value(ClipboardListModel::IdRole), QByteArray("id"));
    QCOMPARE(names.value(ClipboardListModel::TimestampRole), QByteArray("timestamp"));
    QCOMPARE(names.value(ClipboardListModel::TypeRole), QByteArray("type"));
    QCOMPARE(names.value(ClipboardListModel::PreviewRole), QByteArray("preview"));
    QCOMPARE(names.value(ClipboardListModel::SourceAppRole), QByteArray("sourceApp"));
    QCOMPARE(names.value(ClipboardListModel::SourceWindowRole), QByteArray("sourceWindow"));
    QCOMPARE(names.value(ClipboardListModel::PinnedRole), QByteArray("pinned"));
    QCOMPARE(names.value(ClipboardListModel::SensitiveRole), QByteArray("sensitive"));
    QCOMPARE(names.value(ClipboardListModel::SizeRole), QByteArray("size"));
    QCOMPARE(names.value(ClipboardListModel::UseCountRole), QByteArray("useCount"));
    QCOMPARE(names.value(ClipboardListModel::HashRole), QByteArray("hash"));
}

void TestListModel::keysetPagination()
{
    // Insert 250 items. ClipboardListModel::kPageSize is 200.
    const int total = 250;
    for (int i = 0; i < total; ++i) {
        m_storage->insertOrUpdate(
            makeRecord(QByteArray::number(i), QStringLiteral("item-%1").arg(i), 1000 + i));
    }

    ClipboardListModel model(m_storage);
    model.refresh();

    // First page should be exactly kPageSize (200)
    QCOMPARE(model.rowCount(), ClipboardListModel::kPageSize);
    QVERIFY(model.canFetchMore({}));

    // Fetch next page
    model.fetchMore({});
    QCOMPARE(model.rowCount(), total);
    QVERIFY(!model.canFetchMore({}));

    // Calling fetchMore again when canFetchMore is false does nothing
    model.fetchMore({});
    QCOMPARE(model.rowCount(), total);
}

void TestListModel::filterResetsAndRefetches()
{
    for (int i = 0; i < 10; ++i) {
        ClipboardRecord rec = makeRecord(QByteArray::number(i), QStringLiteral("text %1").arg(i), 1000 + i);
        if (i % 2 == 0)
            rec.pinned = true;
        m_storage->insertOrUpdate(rec);
    }

    ClipboardListModel model(m_storage);
    model.refresh();
    QCOMPARE(model.rowCount(), 10);

    // Apply pinnedOnly filter
    FilterSpec pinnedFilter;
    pinnedFilter.pinnedOnly = true;
    model.setFilter(pinnedFilter);
    QCOMPARE(model.rowCount(), 5);
    for (int r = 0; r < model.rowCount(); ++r) {
        QVERIFY(model.data(model.index(r, 0), ClipboardListModel::PinnedRole).toBool());
    }

    // Apply search filter
    FilterSpec searchFilter;
    searchFilter.searchText = QStringLiteral("text 3");
    model.setFilter(searchFilter);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), ClipboardListModel::PreviewRole).toString(),
             QStringLiteral("text 3"));

    // Reset filter
    model.setFilter(FilterSpec{});
    QCOMPARE(model.rowCount(), 10);
}

void TestListModel::lookupsByIdAndRow()
{
    const qint64 id1 = m_storage->insertOrUpdate(
        makeRecord(QByteArrayLiteral("look1"), QStringLiteral("lookup 1"), 1000));
    const qint64 id2 = m_storage->insertOrUpdate(
        makeRecord(QByteArrayLiteral("look2"), QStringLiteral("lookup 2"), 2000));

    ClipboardListModel model(m_storage);
    model.refresh();

    // Row 0 is the newest (id2), Row 1 is older (id1)
    QCOMPARE(model.idAt(0), id2);
    QCOMPARE(model.idAt(1), id1);
    QCOMPARE(model.idAt(99), 0);
    QCOMPARE(model.idAt(-1), 0);

    QCOMPARE(model.rowForId(id2), 0);
    QCOMPARE(model.rowForId(id1), 1);
    QCOMPARE(model.rowForId(9999), -1);

    auto recOpt = model.recordForId(id2);
    QVERIFY(recOpt.has_value());
    QCOMPARE(recOpt->preview, QStringLiteral("lookup 2"));

    QCOMPARE(model.recordAt(0).preview, QStringLiteral("lookup 2"));
    QCOMPARE(model.recordAt(99).id, 0);
}

void TestListModel::dragAndDropMime()
{
    const qint64 id1 = m_storage->insertOrUpdate(
        makeRecord(QByteArrayLiteral("m1"), QStringLiteral("item one"), 1000));
    const qint64 id2 = m_storage->insertOrUpdate(
        makeRecord(QByteArrayLiteral("m2"), QStringLiteral("item two"), 2000));

    ClipboardListModel model(m_storage);
    model.refresh();

    QVERIFY(model.flags(model.index(0, 0)) & Qt::ItemIsDragEnabled);
    QVERIFY(!(model.flags(QModelIndex()) & Qt::ItemIsDragEnabled));

    const QStringList mimes = model.mimeTypes();
    QVERIFY(mimes.contains(QStringLiteral("application/x-egoboard-entry-ids")));
    QVERIFY(mimes.contains(QStringLiteral("text/plain")));

    QModelIndexList selection;
    selection.append(model.index(0, 0)); // id2
    selection.append(model.index(1, 0)); // id1

    QMimeData *data = model.mimeData(selection);
    QVERIFY(data != nullptr);
    QVERIFY(data->hasFormat(QStringLiteral("application/x-egoboard-entry-ids")));
    QVERIFY(data->hasText());

    // Check deserialized IDs
    QByteArray encoded = data->data(QStringLiteral("application/x-egoboard-entry-ids"));
    QDataStream stream(&encoded, QIODevice::ReadOnly);
    QList<qint64> ids;
    stream >> ids;
    QCOMPARE(ids.size(), 2);
    QCOMPARE(ids.at(0), id2);
    QCOMPARE(ids.at(1), id1);

    // Check plain text formatting
    const QString text = data->text();
    QVERIFY(text.contains(QStringLiteral("item two")));
    QVERIFY(text.contains(QStringLiteral("item one")));

    delete data;

    // Empty indexes yields null
    QVERIFY(model.mimeData({}) == nullptr);
}

void TestListModel::pinnedChangedUpdatesRowWithoutFullReset()
{
    const qint64 id = m_storage->insertOrUpdate(
        makeRecord(QByteArrayLiteral("pin_test"), QStringLiteral("pin me"), 1000));

    ClipboardListModel model(m_storage);
    model.refresh();

    QSignalSpy dataChangedSpy(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy modelResetSpy(&model, &QAbstractItemModel::modelReset);

    // Toggle pin in storage
    QVERIFY(m_storage->setPinned(id, true));

    // pinnedChanged causes dataChanged for that row, not full modelReset
    QCOMPARE(dataChangedSpy.count(), 1);
    QCOMPARE(modelResetSpy.count(), 0);

    const QModelIndex changedIdx = dataChangedSpy.first().at(0).toModelIndex();
    QCOMPARE(changedIdx.row(), 0);
    QCOMPARE(model.data(changedIdx, ClipboardListModel::PinnedRole).toBool(), true);
}

void TestListModel::removalAndResetRefreshes()
{
    const qint64 id = m_storage->insertOrUpdate(
        makeRecord(QByteArrayLiteral("rem"), QStringLiteral("remove me"), 1000));

    ClipboardListModel model(m_storage);
    model.refresh();
    QCOMPARE(model.rowCount(), 1);

    // Remove entry
    m_storage->remove(id);
    QTRY_COMPARE(model.rowCount(), 0);

    // Insert again and reset
    m_storage->insertOrUpdate(makeRecord(QByteArrayLiteral("rem2"), QStringLiteral("new item"), 2000));
    QTRY_COMPARE(model.rowCount(), 1);

    m_storage->clearHistory(true);
    QTRY_COMPARE(model.rowCount(), 0);
}

QTEST_GUILESS_MAIN(TestListModel)
#include "tst_listmodel.moc"
