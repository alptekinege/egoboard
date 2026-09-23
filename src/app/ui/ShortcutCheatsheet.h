#pragma once

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

class QTableWidget;

// U15 shortcut cheatsheet: every keyboard shortcut in one reference table,
// opened with `?` in the main window. Content is plain data (no managers),
// so the dialog constructs anywhere — including offscreen tests.
class ShortcutCheatsheet : public QDialog {
    Q_OBJECT
public:
    struct Section {
        QString title;
        QList<QPair<QString, QString>> rows; // shortcut keys, what it does
    };

    // The documented map. Global shortcuts match HotkeyManager's defaults and
    // stay user-reconfigurable in Plasma's shortcut editor (noted in-dialog).
    static QList<Section> defaultSections();

    explicit ShortcutCheatsheet(const QList<Section> &sections, QWidget *parent = nullptr);

private:
    QTableWidget *m_table = nullptr;
};
