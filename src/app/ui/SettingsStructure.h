#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

class QListWidget;
class QWidget;

// Settings sidebar plan + About content (Normal/Advanced split).
//
// Pure data over strings: the dialog builds widgets from this, tests pin the
// plan without constructing the dialog (which needs an ApplicationContext).
// Labels are translated here (QCoreApplication::translate, UiHelpers-style),
// so filtering follows translations with no keyword table.
namespace SettingsStructure {

// Wide-mode sidebar column width, shared by the factory clamp and the
// dialog's responsive restore path. Fits the longest page label
// ("Search & Preview") next to its icon on one line at the default font.
inline constexpr int kSidebarWideWidth = 200;

// Page-row icon size (list rows, icon on the left).
inline constexpr int kSidebarIconSize = 22;

// One sidebar row. Headers are section titles (never selectable, no page);
// pages carry their index into the dialog's builder order below.
struct SidebarRow {
    bool header = false;
    QString iconName; // theme icon, pages only
    QString label;
    int page = -1; // 0..10 in builder order, -1 for headers
};

// Fixed sidebar plan: Normal pages (General, Capture, History, Usage,
// Shortcuts, Storage), the "Advanced" header, advanced pages (Privacy,
// Search & Preview, Automation, Diagnostics), About last.
QVector<SidebarRow> sidebarRows();

// Visibility for every sidebar row under a search query: pages match their
// own texts, headers stay visible while any member page below them (up to the
// next header) matches. An empty query shows everything.
QVector<bool> filterSidebarRows(const QVector<QStringList> &rowTexts,
                                const QVector<SidebarRow> &rows, const QString &query);
// First visible non-header row, or -1 (drives the search jump).
int firstContentRow(const QVector<bool> &visible, const QVector<SidebarRow> &rows);

struct AboutInfo {
    QString title; // "Egoboard"
    QString version; // as passed in
    QStringList paragraphs; // short description, license, local-only note
};

// The settings sidebar itself (empty): full-width list rows, icon on the
// left, label on the right. Single source of truth for the view config —
// the dialog and the readability tests share it. NOTE: this used to be an
// IconMode icon-on-top grid, but Breeze lays IconMode cells out as narrow
// content-width rects hugging the left edge (ragged, uneven rows) instead
// of the intended uniform centered column — reproduced offscreen under
// Breeze. The list layout renders identical full-width rows on every style.
QListWidget *createSidebar(QWidget *parent = nullptr);
// Fills an empty sidebar with every plan row in order (headers + pages).
// Headers are plain section titles (no icon, bold, never selectable);
// pages carry the theme icon on the left.
void populateSidebar(QListWidget *sidebar);

// Pure: the version comes from the caller (EGOBOARD_VERSION at runtime,
// anything in tests), so this stays compilable without the app defines.
AboutInfo aboutInfo(const QString &version);

} // namespace SettingsStructure
