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
// dialog's responsive restore path.
inline constexpr int kSidebarWideWidth = 148;

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

// The settings sidebar itself (empty): icon-on-top/label-below items in a
// fixed-width column. Single source of truth for the view config — the dialog
// and the readability tests share it. NOTE: uniform item sizes stay OFF on
// purpose (see the dialog note): that flag is for huge virtualized lists and
// elides every label here.
QListWidget *createSidebar(QWidget *parent = nullptr);
// Fills an empty sidebar with every plan row in order (headers + pages).
// Every row is explicitly center-aligned — IconMode centering otherwise
// varies by style and leaves icons/labels left-anchored on some setups.
void populateSidebar(QListWidget *sidebar);

// Pure: the version comes from the caller (EGOBOARD_VERSION at runtime,
// anything in tests), so this stays compilable without the app defines.
AboutInfo aboutInfo(const QString &version);

} // namespace SettingsStructure
