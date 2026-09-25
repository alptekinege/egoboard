#include "ShortcutCheatsheet.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

QList<ShortcutCheatsheet::Section> ShortcutCheatsheet::defaultSections()
{
    return {
        {tr("System shortcuts (change in Plasma's shortcut editor)"),
         {
             {QStringLiteral("Meta+V"), tr("Quick-paste popup")},
             {QStringLiteral("Meta+Shift+V"), tr("Show/hide the history window")},
             {QStringLiteral("Meta+Shift+D"), tr("Delete the newest capture")},
             {QStringLiteral("Meta+Shift+P"), tr("Pause/resume clipboard capture")},
         }},
        {tr("History window"),
         {
             {QStringLiteral("Ctrl+K"), tr("Command palette")},
             {QStringLiteral("Ctrl+F or /"), tr("Focus the search field")},
             {QStringLiteral("1–9"), tr("Paste the Nth visible entry")},
             {QStringLiteral("Alt+1…5"),
              tr("Focus search, list, preview, groups, timeline")},
             {QStringLiteral("Enter"), tr("Paste the selected entry")},
             {QStringLiteral("Esc"), tr("Clear search, then filters, then close")},
             {QStringLiteral("?"), tr("This cheatsheet")},
         }},
        {tr("Quick-paste popup"),
         {
             {QStringLiteral("↑ / ↓"), tr("Move the selection")},
             {QStringLiteral("1–9"), tr("Paste by number")},
             {QStringLiteral("Enter"), tr("Paste the selected entry")},
             {QStringLiteral("Esc"), tr("Close the popup")},
         }},
        {tr("Command palette"),
         {
             {QStringLiteral(">"), tr("Command mode (>tag, >export, >pause…)")},
             {QStringLiteral(">dashboard"), tr("Usage dashboard (local aggregates)")},
             {QStringLiteral("Tab"), tr("Complete the suggestion")},
             {QStringLiteral("Enter"), tr("Run / paste")},
             {QStringLiteral("Esc"), tr("Close the palette")},
         }},
        {tr("Timeline strip"),
         {
             {QStringLiteral("← / →"), tr("Move the day cursor")},
             {QStringLiteral("Home / End"), tr("Oldest / newest day")},
             {QStringLiteral("Enter"), tr("Filter by the day (again: clear)")},
             {QStringLiteral("Esc"), tr("Clear the day filter")},
         }},
    };
}

ShortcutCheatsheet::ShortcutCheatsheet(const QList<Section> &sections, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Keyboard shortcuts"));
    setAccessibleName(tr("Keyboard shortcuts"));
    auto *layout = new QVBoxLayout(this);

    int totalRows = 0;
    for (const Section &section : sections)
        totalRows += 1 + section.rows.size(); // title row + its shortcuts

    m_table = new QTableWidget(totalRows, 2, this);
    m_table->setAccessibleName(tr("Keyboard shortcuts"));
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    const QFont keysFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    int row = 0;
    for (const Section &section : sections) {
        auto *title = new QTableWidgetItem(section.title);
        QFont titleFont = title->font();
        titleFont.setWeight(QFont::DemiBold);
        title->setFont(titleFont);
        m_table->setItem(row, 0, title);
        m_table->setSpan(row, 0, 1, 2);
        ++row;
        for (const auto &[keys, action] : section.rows) {
            auto *keysItem = new QTableWidgetItem(keys);
            keysItem->setFont(keysFont);
            m_table->setItem(row, 0, keysItem);
            m_table->setItem(row, 1, new QTableWidgetItem(action));
            ++row;
        }
    }

    layout->addWidget(m_table, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(520, 480);
}
