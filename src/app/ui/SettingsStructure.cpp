#include "SettingsStructure.h"

#include "UiHelpers.h"

#include <QCoreApplication>
#include <QFont>
#include <QIcon>
#include <QListView>
#include <QListWidget>

namespace SettingsStructure {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("SettingsStructure", text);
}

} // namespace

QVector<SidebarRow> sidebarRows()
{
    // Builder order (the dialog's page vector): General, Capture, History,
    // Usage, Shortcuts, Storage, Privacy, Search & Preview, Automation,
    // Diagnostics, About.
    return {
        {false, QStringLiteral("configure"), tr("General"), 0},
        {false, QStringLiteral("edit-copy"), tr("Capture"), 1},
        {false, QStringLiteral("document-open-recent"), tr("History"), 2},
        {false, QStringLiteral("view-statistics"), tr("Usage"), 3},
        {false, QStringLiteral("preferences-desktop-keyboard"), tr("Shortcuts"), 4},
        {false, QStringLiteral("drive-harddisk"), tr("Storage"), 5},
        {true, {}, tr("Advanced"), -1},
        {false, QStringLiteral("security-medium"), tr("Privacy"), 6},
        {false, QStringLiteral("system-search"), tr("Search & Preview"), 7},
        {false, QStringLiteral("applications-engineering"), tr("Automation"), 8},
        {false, QStringLiteral("utilities-system-monitor"), tr("Diagnostics"), 9},
        {false, QStringLiteral("help-about"), tr("About"), 10},
    };
}

QVector<bool> filterSidebarRows(const QVector<QStringList> &rowTexts,
                                const QVector<SidebarRow> &rows, const QString &query)
{
    QVector<bool> visible;
    visible.reserve(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        if (rows.at(i).header) {
            visible.append(false); // decided below, from the member pages
            continue;
        }
        const QStringList texts = i < rowTexts.size() ? rowTexts.at(i) : QStringList{};
        visible.append(UiHelpers::settingQueryMatches(texts, query));
    }
    // A header stays while any member page below it (up to the next header)
    // matches; trailing rows after the last header (About) stand alone.
    for (int h = 0; h < rows.size(); ++h) {
        if (!rows.at(h).header)
            continue;
        bool any = false;
        for (int j = h + 1; j < rows.size() && !rows.at(j).header; ++j) {
            if (j < visible.size() && visible.at(j)) {
                any = true;
                break;
            }
        }
        visible[h] = any;
    }
    return visible;
}

int firstContentRow(const QVector<bool> &visible, const QVector<SidebarRow> &rows)
{
    const int n = qMin(visible.size(), rows.size());
    for (int i = 0; i < n; ++i) {
        if (visible.at(i) && !rows.at(i).header)
            return i;
    }
    return -1;
}

AboutInfo aboutInfo(const QString &version)
{
    AboutInfo info;
    info.title = tr("Egoboard");
    info.version = version;
    info.paragraphs = {
        tr("Local-first clipboard history for KDE Plasma."),
        tr("Free software released under the MIT License."),
        tr("Private by default: everything stays on this machine — no network, "
           "no telemetry, no accounts."),
    };
    return info;
}

QListWidget *createSidebar(QWidget *parent)
{
    auto *sidebar = new QListWidget(parent);
    sidebar->setViewMode(QListView::ListMode);
    sidebar->setFlow(QListView::TopToBottom);
    sidebar->setMovement(QListView::Static);
    sidebar->setWrapping(false);
    sidebar->setResizeMode(QListView::Adjust);
    sidebar->setIconSize(QSize(kSidebarIconSize, kSidebarIconSize));
    sidebar->setFixedWidth(kSidebarWideWidth);
    sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sidebar->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    UiHelpers::styleItemList(sidebar);
    return sidebar;
}

void populateSidebar(QListWidget *sidebar)
{
    if (!sidebar)
        return;
    // Last-resort icon when the active theme lacks a page icon: a blank
    // hole in the list reads as broken, so fall back instead of showing
    // nothing.
    const QIcon missingIcon =
        QIcon::fromTheme(QStringLiteral("preferences-other"));
    for (const SidebarRow &row : sidebarRows()) {
        if (row.header) {
            auto *header = new QListWidgetItem(row.label, sidebar);
            // Enabled but never selectable/current: NoItemFlags would render
            // the title with the disabled (washed-out) palette.
            header->setFlags(Qt::ItemIsEnabled);
            QFont headerFont = header->font();
            headerFont.setWeight(QFont::DemiBold);
            header->setFont(headerFont);
            continue;
        }
        QIcon icon = QIcon::fromTheme(row.iconName);
        if (icon.isNull())
            icon = missingIcon;
        // ListMode rows are icon-left/label-right with the default (left)
        // alignment — full-width rows on every style.
        new QListWidgetItem(icon, row.label, sidebar);
    }
}

} // namespace SettingsStructure
