#pragma once

#include <QObject>

class IClipboardStorage;

/**
 * @brief Minimal D-Bus adaptor for future KRunner / automation.
 *
 * Exposes org.egoboard.Egoboard at /org/egoboard/Egoboard.
 * Phase 3: Search(String query, int limit) -> list of previews
 * intended for `qdbus` and a future KRunner plugin (Track D). Local only,
 * no network. Deferred full KRunner plugin to Phase 4.
 */
class EgoboardDbusAdaptor : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.egoboard.Egoboard")
public:
    explicit EgoboardDbusAdaptor(IClipboardStorage *storage, QObject *parent = nullptr);
    bool registerService();

signals:
    void pasteRequested(qint64 id);
    void showQuickPasteRequested();
    void cursorPosReported(int x, int y); // rounded, from KWin scripting (Wayland)

public slots:
    // Returns previews (joined as "id<TAB>preview" strings) for quick scripting.
    QStringList Search(const QString &query, int limit);
    bool Paste(qint64 id);
    // Opens the quick-paste popup at the cursor (automation / debugging).
    bool ShowQuickPaste();
    // KWin scripting reports the global cursor position here (Wayland).
    // The script rounds the coordinates so they arrive as D-Bus integers.
    void ReportCursorPos(int x, int y);
    int Ping(int v) { return v; }

private:
    IClipboardStorage *m_storage = nullptr;
};
