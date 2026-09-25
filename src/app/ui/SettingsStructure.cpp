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
    sidebar->setViewMode(QListView::IconMode);
    sidebar->setFlow(QListView::TopToBottom);
    sidebar->setMovement(QListView::Static);
    sidebar->setWrapping(false);
    sidebar->setResizeMode(QListView::Adjust);
    // No setUniformItemSizes: that flag is for huge virtualized lists (the
    // history view). On this 12-row IconMode sidebar it forces every label
    // into the first row's narrow text rect, eliding all of them.
    sidebar->setIconSize(QSize(28, 28));
    sidebar->setGridSize(QSize(146, 64));
    sidebar->setWordWrap(true);
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
    for (const SidebarRow &row : sidebarRows()) {
        if (row.header) {
            auto *header = new QListWidgetItem(row.label, sidebar);
            header->setFlags(Qt::NoItemFlags); // section title: visible, never current
            header->setTextAlignment(Qt::AlignHCenter);
            QFont headerFont = header->font();
            headerFont.setWeight(QFont::DemiBold);
            header->setFont(headerFont);
            continue;
        }
        auto *item =
            new QListWidgetItem(QIcon::fromTheme(row.iconName), row.label, sidebar);
        item->setTextAlignment(Qt::AlignHCenter);
    }
}

} // namespace SettingsStructure
