#pragma once

#include "ClipboardRecord.h"
#include "FilterSpec.h"
#include "IClipboardStorage.h"

#include <QAbstractListModel>
#include <QVector>

// Infinite-scroll model over the clipboard history. Rows are fetched in pages
// from the storage layer via canFetchMore()/fetchMore(); only summary fields
// are kept in memory so tens of thousands of records stay cheap.
class ClipboardListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TimestampRole,
        TypeRole,
        PreviewRole,
        SourceAppRole,
        SourceWindowRole,
        PinnedRole,
        SensitiveRole,
        SizeRole,
        UseCountRole,
        HashRole,
    };

    static constexpr int kPageSize = 200;

    explicit ClipboardListModel(IClipboardStorage *storage, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;

    void setFilter(const FilterSpec &filter);
    FilterSpec filter() const { return m_filter; }
    void refresh(); // reload from page 1 (keeps nothing; selection restored by view)

    qint64 idAt(int row) const;
    int rowForId(qint64 id) const;
    ClipboardRecord recordAt(int row) const;
    std::optional<ClipboardRecord> recordForId(qint64 id) const;

signals:
    void initialPageLoaded(bool empty);

private:
    void connectStorage();

    IClipboardStorage *m_storage = nullptr;
    FilterSpec m_filter;
    QVector<ClipboardRecord> m_rows;
    bool m_canFetchMore = true;
};
