#pragma once

#include "BookmarkManager.h"
#include "ClipboardRecord.h"
#include "StorageManager.h"

#include <QObject>
#include <QString>

// JSON export/import of history + groups. The format is self-contained so a
// file written on one machine can be merged into another instance.
class ExportImportManager : public QObject {
    Q_OBJECT
public:
    enum class Scope {
        Everything, // whole database
        PinnedOnly, // pinned entries (+ their memberships)
        GroupSubtree, // a group, its descendants and their entries
    };
    Q_ENUM(Scope)

    enum class ImportMode {
        Merge, // insert new hashes, keep newer timestamp, union memberships
        Overwrite, // wipe database, then insert everything from the file
        SkipDuplicates, // insert only hashes that do not exist yet
    };
    Q_ENUM(ImportMode)

    struct ExportRequest {
        Scope scope = Scope::Everything;
        qint64 groupId = 0; // for Scope::GroupSubtree
        QString path;
    };

    struct ImportResult {
        bool ok = false;
        QString error;
        int entriesImported = 0;
        int entriesMerged = 0;
        int entriesSkipped = 0;
        int groupsImported = 0;
    };

    explicit ExportImportManager(StorageManager *storage, BookmarkManager *bookmarks,
                                 QObject *parent = nullptr);

    bool exportToFile(const ExportRequest &request, QString *error = nullptr);
    ImportResult importFromFile(const QString &path, ImportMode mode);

    static QString exportFormatTag() { return QStringLiteral("egoboard-export"); }
    static int exportFormatVersion() { return 1; }

private:
    StorageManager *m_storage = nullptr;
    BookmarkManager *m_bookmarks = nullptr;
};
