#pragma once

#include "BookmarkManager.h"
#include "ClipboardRecord.h"
#include "SnippetManager.h"
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

    // JSON is the only round-trip format; the others are for reading, sharing
    // and spreadsheets and export the entries of the selected scope.
    enum class ExportFormat {
        Json,
        Markdown,
        Csv,
        Html,
    };
    Q_ENUM(ExportFormat)

    struct ExportRequest {
        Scope scope = Scope::Everything;
        ExportFormat format = ExportFormat::Json;
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
        int tagsImported = 0; // tag links attached to imported/merged entries
        int snippetsImported = 0;
        int savedSearchesImported = 0;
    };

    explicit ExportImportManager(StorageManager *storage, BookmarkManager *bookmarks,
                                 SnippetManager *snippets = nullptr, QObject *parent = nullptr);

    bool exportToFile(const ExportRequest &request, QString *error = nullptr);
    ImportResult importFromFile(const QString &path, ImportMode mode);

    // Imports the text entries of a Klipper database (`history3.sqlite`, the
    // current Klipper format). Starred items become pinned and Klipper's copy
    // times are preserved; duplicates merge through the normal content hash.
    // The file is opened read-only, so a running Klipper is not disturbed.
    ImportResult importKlipperHistory(const QString &databasePath);

    // Where Klipper keeps its history on this system (may not exist).
    static QString defaultKlipperPath();

    // --- automatic backups ---------------------------------------------------
    struct BackupResult {
        bool ok = false;
        QString path; // the file written
        int pruned = 0; // old backups deleted
        QString error;
    };

    // Writes a full JSON backup into `folder` as
    // egoboard-backup-YYYYMMDD-HHmmss.json and keeps only the newest `keep`
    // backups (0 = keep everything). The folder is created when missing.
    BackupResult writeBackup(const QString &folder, int keep);

    // Backup files in `folder`, newest first (name order: timestamps sort).
    static QStringList listBackups(const QString &folder);
    // Deletes all but the newest `keep` backups; returns how many were removed.
    static int pruneBackups(const QString &folder, int keep);

    static QString exportFormatTag() { return QStringLiteral("egoboard-export"); }
    // v2 adds ocrText + tags per entry, the snippet library and saved searches.
    // v1 files stay importable (missing arrays are simply empty).
    static int exportFormatVersion() { return 2; }

private:
    // Entry-only writers for the reading formats (tags are looked up per entry).
    bool writeCsvExport(const ExportRequest &request, const QVector<ClipboardRecord> &entries,
                        QString *error) const;
    bool writeMarkdownExport(const ExportRequest &request, const QVector<ClipboardRecord> &entries,
                             QString *error) const;
    bool writeHtmlExport(const ExportRequest &request, const QVector<ClipboardRecord> &entries,
                         QString *error) const;

    StorageManager *m_storage = nullptr;
    BookmarkManager *m_bookmarks = nullptr;
    SnippetManager *m_snippets = nullptr;
};
