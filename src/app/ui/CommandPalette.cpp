#include "CommandPalette.h"

#include "ClipboardListModel.h"
#include "IClipboardStorage.h"
#include "SnippetManager.h"
#include "TransformEngine.h"
#include "../ScriptActionManager.h"

#include <QAbstractListModel>
#include <QApplication>
#include <QDateTime>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QVBoxLayout>
#include <QTimer>
#include <QScreen>
#include <QGuiApplication>



class CommandPalette::PaletteModel : public QAbstractListModel {
public:
    explicit PaletteModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    void setRecords(const QVector<ClipboardRecord> &recs, const QString &query) {
        beginResetModel();
        m_mode = Mode::History;
        m_recs = recs;
        m_query = query;
        m_transforms.clear();
        m_snippets.clear();
        endResetModel();
    }
    void setTransforms(const QVector<TransformItem> &items, const QString &query) {
        beginResetModel();
        m_mode = Mode::Transforms;
        m_transforms = items;
        m_query = query;
        m_recs.clear();
        m_snippets.clear();
        endResetModel();
    }
    void setSnippets(const QVector<SnippetItem> &items, const QString &query) {
        beginResetModel();
        m_mode = Mode::Snippets;
        m_snippets = items;
        m_query = query;
        m_recs.clear();
        m_transforms.clear();
        endResetModel();
    }
    int rowCount(const QModelIndex &p = {}) const override {
        if (p.isValid()) return 0;
        if (m_mode == Mode::Transforms) return m_transforms.size();
        if (m_mode == Mode::Snippets) return m_snippets.size();
        return m_recs.size();
    }
    QVariant data(const QModelIndex &idx, int role) const override {
        if (!idx.isValid()) return {};
        if (m_mode == Mode::Transforms) {
            if (idx.row() >= m_transforms.size()) return {};
            const auto &t = m_transforms.at(idx.row());
            if (role == Qt::DisplayRole) return QStringLiteral("%1 — %2").arg(t.label, t.desc);
            if (role == Qt::UserRole) return t.name;
            if (role == Qt::UserRole + 1) return t.label;
            return {};
        }
        if (m_mode == Mode::Snippets) {
            if (idx.row() >= m_snippets.size()) return {};
            const auto &s = m_snippets.at(idx.row());
            if (role == Qt::DisplayRole) return QStringLiteral("%1 — %2").arg(s.name, s.templateText.left(80).replace(QLatin1Char('\n'), QLatin1Char(' ')));
            if (role == Qt::UserRole) return s.id;
            if (role == Qt::UserRole + 1) return s.name;
            return {};
        }
        if (idx.row() >= m_recs.size()) return {};
        const auto &r = m_recs.at(idx.row());
        if (role == Qt::DisplayRole) return r.preview.isEmpty() ? QStringLiteral("—") : r.preview;
        if (role == Qt::UserRole) return r.id;
        if (role == Qt::UserRole + 1) return r.timestamp;
        if (role == Qt::UserRole + 2) return static_cast<int>(r.type);
        if (role == Qt::UserRole + 3) return r.sourceApp;
        return {};
    }
private:
    Mode m_mode = Mode::History;
    QVector<ClipboardRecord> m_recs;
    QVector<TransformItem> m_transforms;
    QVector<SnippetItem> m_snippets;
    QString m_query;
};

CommandPalette::CommandPalette(IClipboardStorage *storage, QWidget *parent)
    : QDialog(parent), m_storage(storage)
{
    setWindowTitle(tr("Command Palette"));
    setModal(true);
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    resize(640, 420);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("Type to search history…  •  >transform  >snippet  •  >pin etc."));
    m_input->setClearButtonEnabled(true);
    m_input->setAccessibleName(tr("Command palette search"));
    QFont f = m_input->font();
    f.setPointSizeF(f.pointSizeF() + 1.5);
    m_input->setFont(f);
    layout->addWidget(m_input);

    m_list = new QListView(this);
    m_list->setAccessibleName(tr("Palette results"));
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(true);
    m_model = new PaletteModel(this);
    m_list->setModel(m_model);
    layout->addWidget(m_list, 1);

    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    layout->addWidget(m_hint);

    connect(m_input, &QLineEdit::textChanged, this, &CommandPalette::onTextChanged);
    // activated() only. clicked() must NOT also be connected: on KDE/Plasma
    // (activate-on-single-click) one mouse click emits BOTH, double-executing
    // the entry (double clipboard set + double paste).
    connect(m_list, &QListView::activated, this, &CommandPalette::onActivated);

    // Enter in the input executes the top result
    connect(m_input, &QLineEdit::returnPressed, this, &CommandPalette::executeCurrent);

    updateHint();
}

void CommandPalette::openPalette()
{
    // Center over parent or over cursor screen
    QWidget *p = parentWidget();
    if (p && p->window()) {
        const QRect geo = p->window()->geometry();
        move(geo.center() - rect().center());
    } else if (QScreen *scr = QGuiApplication::screenAt(QCursor::pos())) {
        move(scr->geometry().center() - rect().center());
    }
    m_input->clear();
    refreshResults({});
    m_input->setFocus();
    show();
    raise();
    activateWindow();
}

void CommandPalette::onTextChanged(const QString &text)
{
    m_currentQuery = text;
    refreshResults(text);
}

void CommandPalette::refreshResults(const QString &query)
{
    if (!m_storage) return;

    const QString trimmed = query.trimmed();
    const bool isCommand = trimmed.startsWith(QLatin1Char('>'));

    if (isCommand) {
        QString after = trimmed.mid(1).trimmed();
        // Split command + args
        QString cmd;
        QString args;
        const int sp = after.indexOf(QLatin1Char(' '));
        if (sp >= 0) {
            cmd = after.left(sp).trimmed().toLower();
            args = after.mid(sp + 1).trimmed();
        } else {
            cmd = after.toLower();
            args = {};
        }

        // Normalize aliases
        const bool isTransform = (cmd == QStringLiteral("transform") || cmd == QStringLiteral("t") || cmd == QStringLiteral("tr") || cmd.startsWith(QStringLiteral("transform")) || cmd == QStringLiteral("xform"));
        const bool isSnippet = (cmd == QStringLiteral("snippet") || cmd == QStringLiteral("s") || cmd == QStringLiteral("snip") || cmd.startsWith(QStringLiteral("snippet")));

        if (isTransform || cmd.isEmpty()) {
            // If bare ">" with no command, show hint but also treat as transform? We show history hint.
            if (cmd.isEmpty() && args.isEmpty()) {
                // Show all transforms as preview? Instead show history with hint
                // Fall through to history hint handling below — but we set mode to Transforms with no filter still useful
                // We'll show transforms when cmd empty? Better show hint only.
                // For now if user typed just ">", show transform+snippet hint in updateHint, but keep history results
            }
            if (isTransform) {
                m_mode = Mode::Transforms;
                QVector<TransformItem> items;
                const QString filter = args.toLower();
                for (const auto &d : TransformEngine::allDescriptors()) {
                    if (!filter.isEmpty() && !d.label.toLower().contains(filter) && !d.name.contains(filter) && !d.description.toLower().contains(filter))
                        continue;
                    items.append({d.name, d.label, d.description});
                }
                if (m_scripts) {
                    // reload to pick up new files
                    m_scripts->reload();
                    for (const auto &sa : m_scripts->actions()) {
                        if (!filter.isEmpty() && !sa.label.toLower().contains(filter) && !sa.id.toLower().contains(filter))
                            continue;
                        items.append({sa.id, QStringLiteral("[JS] %1").arg(sa.label), sa.filePath});
                    }
                }
                // Also if filter empty show all; if no matches show empty
                m_transformItems = items;
                m_model->setTransforms(m_transformItems, trimmed);
                if (!m_transformItems.isEmpty())
                    m_list->setCurrentIndex(m_model->index(0, 0));
                updateHint();
                return;
            }
        }
        if (isSnippet) {
            m_mode = Mode::Snippets;
            QVector<SnippetItem> items;
            const QString filter = args.toLower();
            if (m_snippets) {
                for (const auto &s : m_snippets->snippets()) {
                    if (!filter.isEmpty() && !s.name.toLower().contains(filter) && !s.templateText.toLower().contains(filter))
                        continue;
                    items.append({s.id, s.name, s.templateText});
                }
            }
            m_snippetItems = items;
            m_model->setSnippets(m_snippetItems, trimmed);
            if (!m_snippetItems.isEmpty())
                m_list->setCurrentIndex(m_model->index(0, 0));
            updateHint();
            return;
        }
        // Commands that act on the entry selected in the main window.
        const bool isCopy = (cmd == QStringLiteral("copy") || cmd == QStringLiteral("c"));
        const bool isPin = (cmd == QStringLiteral("pin") || cmd == QStringLiteral("p"));
        if (isCopy || isPin) {
            m_mode = Mode::Command;
            m_pendingCommand = isCopy ? QStringLiteral("copy") : QStringLiteral("pin");
            m_results.clear();
            m_model->setRecords(m_results, trimmed);
            m_list->setCurrentIndex(QModelIndex());
            updateHint();
            return;
        }

        // Unknown command — show no results, the hint lists the commands.
        m_mode = Mode::History;
        m_results.clear();
        m_model->setRecords(m_results, trimmed);
        updateHint();
        return;
    }
    m_pendingCommand.clear();

    // History mode
    m_mode = Mode::History;
    FilterSpec filter;
    if (!trimmed.isEmpty())
        filter.searchText = trimmed;

    // Palette shows top 30, ordered by recency (StorageManager handles FTS5).
    const auto page = m_storage->fetchPage(filter, {}, 30);
    QVector<ClipboardRecord> scored = page;

    // Light secondary fuzzy re-rank when query is short (typo tolerance).
    if (trimmed.size() >= 2 && trimmed.size() <= 6 && !scored.isEmpty()) {
        std::stable_sort(scored.begin(), scored.end(), [&](const ClipboardRecord &a, const ClipboardRecord &b){
            return fuzzyScore(trimmed, a.preview) > fuzzyScore(trimmed, b.preview);
        });
    }

    m_results = scored;
    m_model->setRecords(m_results, trimmed);
    if (!m_results.isEmpty())
        m_list->setCurrentIndex(m_model->index(0, 0));
    updateHint();
}

void CommandPalette::updateHint()
{
    if (m_mode == Mode::Command) {
        m_hint->setText(m_pendingCommand == QLatin1String("copy")
                            ? tr("⏎ copy the entry selected in the main window to the clipboard  •  Esc close")
                            : tr("⏎ pin/unpin the entry selected in the main window  •  Esc close"));
        return;
    }
    if (m_mode == Mode::Transforms) {
        if (m_transformItems.isEmpty())
            m_hint->setText(tr("No transforms match — try fewer letters. Built-ins + JS scripts from ~/.local/share/egoboard/actions/"));
        else
            m_hint->setText(tr("%1 transform(s) — ⏎ apply to selected entry / clipboard  •  Esc close  •  Type >snippet to switch").arg(m_transformItems.size()));
        return;
    }
    if (m_mode == Mode::Snippets) {
        if (m_snippetItems.isEmpty())
            m_hint->setText(tr("No snippets match — create via toolbar Snippets. Placeholders: {{clipboard}}, {{date}}…"));
        else
            m_hint->setText(tr("%1 snippet(s) — ⏎ expand with selected entry / clipboard  •  Esc close  •  Type >transform to switch").arg(m_snippetItems.size()));
        return;
    }
    if (m_results.isEmpty()) {
        if (m_currentQuery.trimmed().startsWith(QLatin1Char('>')))
            m_hint->setText(tr("Commands:  >transform [filter]  >snippet [filter]  >pin  >copy  •  Esc close"));
        else if (m_currentQuery.trimmed().isEmpty())
            m_hint->setText(tr("Showing recent entries  •  ⏎ paste  •  Esc close  •  Type > for commands (>transform, >snippet)"));
        else
            m_hint->setText(tr("No matches — try fewer words or check spelling (prefix search)"));
    } else {
        m_hint->setText(tr("%1 result(s)  •  ⏎ paste  •  Esc close  •  >transform / >snippet for actions").arg(m_results.size()));
    }
}

int CommandPalette::fuzzyScore(const QString &query, const QString &candidate)
{
    if (query.isEmpty() || candidate.isEmpty()) return 0;
    const QString q = query.toLower();
    const QString c = candidate.toLower();
    if (c.contains(q)) return 100 + (100 - q.size()); // exact substring bonus
    // Simple subsequence scoring
    int qi = 0, score = 0;
    for (int i = 0; i < c.size() && qi < q.size(); ++i) {
        if (c.at(i) == q.at(qi)) { ++qi; score += 10; if (i < 10) score += 5; }
    }
    return qi == q.size() ? score : 0;
}

void CommandPalette::onActivated(const QModelIndex &index)
{
    if (!index.isValid()) return;
    if (m_mode == Mode::Transforms) {
        const int row = index.row();
        if (row < 0 || row >= m_transformItems.size()) return;
        const QString name = m_transformItems.at(row).name;
        // Need entry context: try top history result's id? For palette we don't have selected entry id from main window.
        // Emit with 0 — MainWindow will resolve to selectedId / clipboard.
        accept();
        emit transformRequested(name, 0);
        return;
    }
    if (m_mode == Mode::Snippets) {
        const int row = index.row();
        if (row < 0 || row >= m_snippetItems.size()) return;
        const qint64 sid = m_snippetItems.at(row).id;
        accept();
        emit snippetRequested(sid, 0);
        return;
    }
    const qint64 id = index.data(Qt::UserRole).toLongLong();
    if (id == 0) return;
    accept();
    emit pasteRequested(id);
}

void CommandPalette::executeCurrent()
{
    if (m_mode == Mode::Command && !m_pendingCommand.isEmpty()) {
        const QString command = m_pendingCommand;
        accept();
        if (command == QLatin1String("copy"))
            emit copyRequested(0); // 0 = the entry selected in the main window
        else
            emit pinRequested(0);
        return;
    }
    const QModelIndex cur = m_list->currentIndex();
    if (cur.isValid()) { onActivated(cur); return; }
    if (m_model->rowCount() > 0) onActivated(m_model->index(0,0));
}
