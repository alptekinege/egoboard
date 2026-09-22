#pragma once

#include "BookmarkManager.h"
#include "ClipboardRecord.h"
#include "SnippetManager.h"
#include "StorageManager.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <functional>

// JSON export/import of history + groups. The format is self-contained so a
// file written on one machine can be merged into another instance.
//
// Image export (U17) is a separate, read-only flow: stored PNG blobs are
// written as files into a folder plus a small manifest. It never touches the
// history and never embeds payload text unless explicitly asked.
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

    // Lowercase id of a format ("json", "markdown", "csv", "html") — the value
    // the palette completes for ">export" and the one the dialog round-trips.
    static QString formatId(ExportFormat format);

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

    // Image-only export (U17): which entries are candidates for dumping.
    struct ImageExportRequest {
        enum class Scope {
            Selection, // the explicit entryIds (bulk bar)
            CurrentFilter, // the caller's live FilterSpec (list filter)
            Everything, // whole database
            PinnedOnly, // pinned entries
            GroupSubtree, // a group, its descendants and their entries
        };

        Scope scope = Scope::Everything;
        QList<qint64> entryIds; // Selection scope
        FilterSpec filter; // CurrentFilter scope
        qint64 groupId = 0; // GroupSubtree scope
        QString dir; // target folder (created when missing)
        // Explicit sensitive policy (shown in the dialog before export):
        // false skips sensitive entries and reports them, true exports them.
        bool includeSensitive = false;
        // The manifest carries metadata only; text payloads are added solely
        // when the user explicitly opts in (e.g. for local search indexing).
        bool includeText = false;
    };

    struct ImageExportResult {
        bool ok = false;
        bool canceled = false;
        QString error;
        int exported = 0;
        int skippedNoBlob = 0; // image entries with no stored blob
        int skippedNonImage = 0; // entries of another content type in scope
        int skippedSensitive = 0; // sensitive entries left out by policy
        qint64 bytesWritten = 0;
        QString manifestPath;
        QStringList files; // absolute paths written by this run
    };

    // Per-batch progress (exported/skipped/bytes so far); called on the
    // caller's thread. Returning callers pump the event loop here so a
    // progress dialog stays responsive and Cancel takes effect promptly.
    using ImageExportProgress = std::function<void(int exportedSoFar, int skippedSoFar,
                                                   qint64 bytesSoFar)>;

    static QString imageExportFormatTag() { return QStringLiteral("egoboard-image-export"); }
    static int imageExportFormatVersion() { return 1; }

    explicit ExportImportManager(StorageManager *storage, BookmarkManager *bookmarks,
                                 SnippetManager *snippets = nullptr, QObject *parent = nullptr);

    bool exportToFile(const ExportRequest &request, QString *error = nullptr);
    ImportResult importFromFile(const QString &path, ImportMode mode);

    // Writes the stored PNG blobs of the requested scope into `request.dir`
    // (created when missing) with collision-safe deterministic filenames plus
    // a `manifest.json` describing them. Streams bounded pages so a 50k-image
    // history stays within bounded memory; read-only (the history is never
    // modified, no transaction needed). Existing files are never overwritten.
    // On cancel or write failure no manifest is written and already-written
    // images stay in place; the result message says how far the run got.
    ImageExportResult exportImages(const ImageExportRequest &request,
                                   std::atomic<bool> *cancel = nullptr,
                                   ImageExportProgress progress = {});

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
