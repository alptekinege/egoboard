#pragma once

#include "EntryRow.h"

#include <QObject>
#include <QStringList>

class IClipboardStorage;

/**
 * @brief Minimal D-Bus adaptor for KRunner / automation.
 *
 * Exposes org.egoboard.Egoboard at /org/egoboard/Egoboard.
 *   Search(query, limit)          → "id\tpreview" rows (plain scripting)
 *   SearchDetailed(query, limit)  → EntryRow rows with type/app/window/pinned
 *   Preview(id)                   → the entry text, for a preview panel
 *   Paste(id) / Copy(id)          → put an entry on the clipboard (Copy does not
 *                                   simulate a paste keystroke)
 *   Pin(id, pinned) / Delete(id)  → KRunner's per-match actions
 * Local only, no network.
 */
class EgoboardDbusAdaptor : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.egoboard.Egoboard")
public:
    explicit EgoboardDbusAdaptor(IClipboardStorage *storage, QObject *parent = nullptr);
    bool registerService();

signals:
    void pasteRequested(qint64 id);
    void copyRequested(qint64 id);
    void pinRequested(qint64 id, bool pinned);
    void deleteRequested(qint64 id);
    void showQuickPasteRequested();
    void cursorPosReported(int x, int y); // rounded, from KWin scripting (Wayland)

public slots:
    // Returns previews (joined as "id<TAB>preview" strings) for quick scripting.
    QStringList Search(const QString &query, int limit);
    // Richer variant of Search(): one EntryRow per entry (see EntryRow.h).
    QStringList SearchDetailed(const QString &query, int limit);
    // The entry's text (capped), for previews. Empty when the id is unknown.
    QString Preview(qint64 id);
    bool Paste(qint64 id);
    // Clipboard only: no keystroke is simulated, so it is safe from KRunner.
    bool Copy(qint64 id);
    // Returns false when the entry does not exist (KRunner actions report that).
    bool Pin(qint64 id, bool pinned);
    bool Delete(qint64 id);
    // Opens the quick-paste popup at the cursor (automation / debugging).
    bool ShowQuickPaste();
    // KWin scripting reports the global cursor position here (Wayland).
    // The script rounds the coordinates so they arrive as D-Bus integers.
    void ReportCursorPos(int x, int y);
    int Ping(int v) { return v; }

private:
    bool entryExists(qint64 id) const;

    IClipboardStorage *m_storage = nullptr;
};
