#include "CommandPalette.h"

#include "ClipboardListModel.h"
#include "IClipboardStorage.h"

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
        m_recs = recs;
        m_query = query;
        endResetModel();
    }
    int rowCount(const QModelIndex &p = {}) const override { return p.isValid() ? 0 : m_recs.size(); }
    QVariant data(const QModelIndex &idx, int role) const override {
        if (!idx.isValid() || idx.row() >= m_recs.size()) return {};
        const auto &r = m_recs.at(idx.row());
        if (role == Qt::DisplayRole) return r.preview.isEmpty() ? QStringLiteral("—") : r.preview;
        if (role == Qt::UserRole) return r.id;
        if (role == Qt::UserRole + 1) return r.timestamp;
        if (role == Qt::UserRole + 2) return static_cast<int>(r.type);
        if (role == Qt::UserRole + 3) return r.sourceApp;
        return {};
    }
private:
    QVector<ClipboardRecord> m_recs;
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
    m_input->setPlaceholderText(tr("Type to search history…  •  Try  >pin  >copy  >delete"));
    m_input->setClearButtonEnabled(true);
    QFont f = m_input->font();
    f.setPointSizeF(f.pointSizeF() + 1.5);
    m_input->setFont(f);
    layout->addWidget(m_input);

    m_list = new QListView(this);
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
    connect(m_list, &QListView::activated, this, &CommandPalette::onActivated);
    connect(m_list, &QListView::clicked, this, &CommandPalette::onActivated);

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
    // Command mode: >pin, >copy, >delete etc. For now treat as no-op hint.
    const bool isCommand = trimmed.startsWith(QLatin1Char('>'));

    FilterSpec filter;
    if (!isCommand && !trimmed.isEmpty())
        filter.searchText = trimmed;

    // Palette shows top 30, ordered by recency (StorageManager handles FTS5).
    const auto page = m_storage->fetchPage(filter, {}, 30);
    QVector<ClipboardRecord> scored = page;

    // Light secondary fuzzy re-rank when query is short (typo tolerance).
    if (!isCommand && trimmed.size() >= 2 && trimmed.size() <= 6 && !scored.isEmpty()) {
        // Simple fuzzy: boost exact substring matches
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
    if (m_results.isEmpty()) {
        if (m_currentQuery.trimmed().startsWith(QLatin1Char('>')))
            m_hint->setText(tr("Commands:  >pin  >copy  >delete  — (coming soon, Enter pastes for now)"));
        else if (m_currentQuery.trimmed().isEmpty())
            m_hint->setText(tr("Showing recent entries  •  ⏎ paste  •  Esc close  •  Type > for commands"));
        else
            m_hint->setText(tr("No matches — try fewer words or check spelling (prefix search)"));
    } else {
        m_hint->setText(tr("%1 result(s)  •  ⏎ paste  •  Esc close  •  Ctrl+C copy only").arg(m_results.size()));
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
    const qint64 id = index.data(Qt::UserRole).toLongLong();
    if (id == 0) return;
    accept();
    emit pasteRequested(id);
}

void CommandPalette::executeCurrent()
{
    const QModelIndex cur = m_list->currentIndex();
    if (cur.isValid()) { onActivated(cur); return; }
    if (m_model->rowCount() > 0) onActivated(m_model->index(0,0));
}
