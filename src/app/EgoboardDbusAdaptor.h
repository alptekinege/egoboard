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

public slots:
    // Returns previews (joined as "id<TAB>preview" strings) for quick scripting.
    QStringList Search(const QString &query, int limit);
    bool Paste(qint64 id);
    int Ping(int v) { return v; }

private:
    IClipboardStorage *m_storage = nullptr;
};
