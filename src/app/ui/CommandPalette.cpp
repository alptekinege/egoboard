#include "CommandPalette.h"

#include "ClipboardListModel.h"
#include "DesignTokens.h"
#include "IClipboardStorage.h"
#include "SnippetManager.h"
#include "TransformEngine.h"
#include "UiHelpers.h"
#include "../ScriptActionManager.h"

#include <QAbstractListModel>
#include <QApplication>
#include <QSet>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QDateTime>
#include <QFontMetrics>
#include <QFrame>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QVBoxLayout>
#include <QTimer>
#include <QScreen>
#include <QGuiApplication>



// R3 rich rows: type icon + source app + relative time, compact two-line
// delegate in the same language as the history list.
class CommandPalette::PaletteDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        Q_UNUSED(index);
        const QFontMetrics metrics(option.font);
        const int padding = DesignTokens::RowPaddingCompact;
        return QSize(option.rect.width(),
                     metrics.height() * 2 + 2 * padding + DesignTokens::SpaceXs);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        opt.text.clear();
        opt.icon = QIcon();
        QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        const bool selected = option.state & QStyle::State_Selected;
        painter->save();
        const int left = option.rect.left() + DesignTokens::SpaceM;
        const int top = option.rect.top();
        const int height = option.rect.height();
        const QFontMetrics metrics(option.font);

        const int type = index.data(Qt::UserRole + 2).toInt();
        QString iconName = QStringLiteral("text-x-generic");
        if (type == int(ContentType::Image))
            iconName = QStringLiteral("image-x-generic");
        else if (type == int(ContentType::RichText))
            iconName = QStringLiteral("text-html");
        else if (type == int(ContentType::Files))
            iconName = QStringLiteral("folder");
        QIcon::fromTheme(iconName).paint(
            painter, QRect(left, top + (height - DesignTokens::IconM) / 2, DesignTokens::IconM,
                           DesignTokens::IconM));

        const int textLeft = left + DesignTokens::IconM + DesignTokens::SpaceM;
        const int textWidth = option.rect.right() - DesignTokens::SpaceM - textLeft;
        const QColor titleColor = DesignTokens::previewTextColor(option.palette, selected, true);
        const QColor metaColor = DesignTokens::metaTextColor(option.palette, selected, true);

        QFont titleFont = option.font;
        titleFont.setWeight(QFont::DemiBold);
        painter->setFont(titleFont);
        painter->setPen(titleColor);
        const QString title = metrics.elidedText(index.data(Qt::DisplayRole).toString(),
                                                 Qt::ElideRight, qMax(0, textWidth));
        painter->drawText(QRect(textLeft, top + DesignTokens::RowPaddingCompact, textWidth,
                                metrics.height()),
                          Qt::AlignVCenter | Qt::AlignLeft, title);

        painter->setFont(option.font);
        painter->setPen(metaColor);
        const QString app = index.data(Qt::UserRole + 3).toString();
        const qint64 timestamp = index.data(Qt::UserRole + 1).toLongLong();
        QString age;
        if (timestamp > 0) {
            const qint64 secs =
                QDateTime::fromMSecsSinceEpoch(timestamp).secsTo(QDateTime::currentDateTime());
            if (secs < 50)
                age = tr("just now");
            else if (secs < 90 * 60)
                age = tr("%1 min ago").arg(qRound(secs / 60.0));
            else if (secs < 24 * 3600)
                age = tr("%1 h ago").arg(qRound(secs / 3600.0));
            else
                age = tr("%1 d ago").arg(qRound(secs / 86400.0));
        }
        QString meta = app;
        if (!app.isEmpty() && !age.isEmpty())
            meta += QStringLiteral(" · ");
        meta += age;
        painter->drawText(
            QRect(textLeft, top + DesignTokens::RowPaddingCompact + metrics.height() + 2,
                  textWidth, metrics.height()),
            Qt::AlignVCenter | Qt::AlignLeft,
            metrics.elidedText(meta, Qt::ElideRight, qMax(0, textWidth)));
        painter->restore();
    }
};

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
        m_commands.clear();
        m_arguments.clear();
        endResetModel();
    }
    void setTransforms(const QVector<TransformItem> &items, const QString &query) {
        beginResetModel();
        m_mode = Mode::Transforms;
        m_transforms = items;
        m_query = query;
        m_recs.clear();
        m_snippets.clear();
        m_commands.clear();
        m_arguments.clear();
        endResetModel();
    }
    void setSnippets(const QVector<SnippetItem> &items, const QString &query) {
        beginResetModel();
        m_mode = Mode::Snippets;
        m_snippets = items;
        m_query = query;
        m_recs.clear();
        m_transforms.clear();
        m_commands.clear();
        m_arguments.clear();
        endResetModel();
    }
    void setCommands(const QVector<CommandItem> &items, const QString &query) {
        beginResetModel();
        m_mode = Mode::Commands;
        m_commands = items;
        m_query = query;
        m_recs.clear();
        m_transforms.clear();
        m_snippets.clear();
        m_arguments.clear();
        endResetModel();
    }
    void setArguments(const QStringList &items, const QString &query) {
        beginResetModel();
        m_mode = Mode::Argument;
        m_arguments = items;
        m_query = query;
        m_recs.clear();
        m_transforms.clear();
        m_snippets.clear();
        m_commands.clear();
        endResetModel();
    }
    int rowCount(const QModelIndex &p = {}) const override {
        if (p.isValid()) return 0;
        if (m_mode == Mode::Transforms) return m_transforms.size();
        if (m_mode == Mode::Snippets) return m_snippets.size();
        if (m_mode == Mode::Commands) return m_commands.size();
        if (m_mode == Mode::Argument) return m_arguments.size();
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
        if (m_mode == Mode::Commands) {
            if (idx.row() >= m_commands.size()) return {};
            const auto &c = m_commands.at(idx.row());
            if (role == Qt::DisplayRole) return QStringLiteral("%1 — %2").arg(c.usage, c.description);
            if (role == Qt::UserRole) return c.id;
            if (role == Qt::UserRole + 1) return c.usage;
            return {};
        }
        if (m_mode == Mode::Argument) {
            if (idx.row() >= m_arguments.size()) return {};
            const QString value = m_arguments.at(idx.row());
            if (role == Qt::DisplayRole) return value;
            if (role == Qt::UserRole) return value;
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
    QVector<CommandItem> m_commands;
    QStringList m_arguments;
    QString m_query;
};

CommandPalette::CommandPalette(IClipboardStorage *storage, QWidget *parent)
    : QDialog(parent), m_storage(storage)
{
    setWindowTitle(tr("Command Palette"));
    setModal(true);
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    // Borderless, but not edgeless: the rounded card below carries the frame,
    // and the margin around it holds the drop shadow.
    setAttribute(Qt::WA_TranslucentBackground, true);
    resize(640, 420);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(DesignTokens::SpaceM, DesignTokens::SpaceM, DesignTokens::SpaceM,
                              DesignTokens::SpaceM);

    m_card = UiHelpers::makePopupPanel(this);
    m_card->setObjectName(QStringLiteral("paletteCard"));
    outer->addWidget(m_card);

    auto *layout = new QVBoxLayout(m_card);
    layout->setContentsMargins(DesignTokens::SpaceL, DesignTokens::SpaceL, DesignTokens::SpaceL,
                               DesignTokens::SpaceL);
    layout->setSpacing(DesignTokens::SpaceM);

    // Input + ghost completion hint (U9): the dim suffix below the input
    // shows the Tab-completable remainder of the highlighted candidate.
    auto *inputRow = new QWidget(m_card);
    auto *inputStack = new QVBoxLayout(inputRow);
    inputStack->setContentsMargins(0, 0, 0, 0);
    inputStack->setSpacing(0);
    m_input = new QLineEdit(inputRow);
    m_input->setPlaceholderText(tr("Type to search history…  •  > for commands (>tag, >export, >pause…)"));
    m_input->setClearButtonEnabled(true);
    m_input->setAccessibleName(tr("Command palette search"));
    QFont f = m_input->font();
    f.setPointSizeF(f.pointSizeF() + 1.5);
    m_input->setFont(f);
    UiHelpers::styleSearchField(m_input);
    inputStack->addWidget(m_input);
    m_ghost = UiHelpers::makeHint(QString(), inputRow, /*richText=*/false);
    m_ghost->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_ghost->setVisible(false);
    inputStack->addWidget(m_ghost);
    layout->addWidget(inputRow);

    m_list = new QListView(m_card);
    m_list->setAccessibleName(tr("Palette results"));
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(false); // rich two-line rows vary
    UiHelpers::styleItemList(m_list);
    m_model = new PaletteModel(this);
    m_list->setModel(m_model);
    m_delegate = new PaletteDelegate(m_list);
    m_list->setItemDelegate(m_delegate);
    layout->addWidget(m_list, 1);

    m_hint = UiHelpers::makeHint(QString(), m_card);
    layout->addWidget(m_hint);

    connect(m_input, &QLineEdit::textChanged, this, &CommandPalette::onTextChanged);
    m_input->installEventFilter(this); // Tab completes the highlighted argument
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
    UiHelpers::fadeIn(this); // capped, skippable (Settings ▸ Appearance)
    show();
    raise();
    activateWindow();
}

QString CommandPalette::ghostSuffix(const QString &input, const QString &candidate)
{
    // U9: the completable remainder — empty when there is nothing to complete.
    if (candidate.isEmpty() || input.isEmpty())
        return {};
    if (!candidate.startsWith(input, Qt::CaseInsensitive))
        return {};
    if (candidate.size() <= input.size())
        return {};
    return candidate.mid(input.size());
}

void CommandPalette::rebuildRecentRows()
{
    // U9: empty-input section — recent searches first, then recent commands.
    m_recentRows.clear();
    for (const QString &query : m_recentSearches) {
        if (query.trimmed().isEmpty() || m_recentRows.size() >= 10)
            break;
        m_recentRows.append({QStringLiteral("search"),
                             tr("Search: %1").arg(query.left(60)), query});
    }
    for (const QString &id : m_recentCommands) {
        if (m_recentRows.size() >= 10)
            break;
        const PaletteCommands::Command *command = PaletteCommands::find(id);
        if (!command)
            continue;
        m_recentRows.append({QStringLiteral("command"),
                             tr("Run: %1").arg(command->usage), command->id});
    }
}

void CommandPalette::onTextChanged(const QString &text)
{
    m_currentQuery = text;
    refreshResults(text);
    // U9 ghost: show the Tab-completable remainder of the highlighted row.
    if (m_ghost) {
        QString candidate;
        if ((m_mode == Mode::Argument || m_mode == Mode::Commands)
            && m_list->currentIndex().isValid())
            candidate = m_list->currentIndex().data(Qt::UserRole).toString();
        QString suffix;
        if (m_mode == Mode::Argument) {
            const QString argument = PaletteCommands::parse(text).argument;
            suffix = ghostSuffix(argument, candidate);
            if (!suffix.isEmpty())
                suffix = tr("Tab: %1").arg(suffix);
        } else if (m_mode == Mode::Commands) {
            const QString word = PaletteCommands::parse(text).word;
            suffix = ghostSuffix(word, candidate);
            if (!suffix.isEmpty())
                suffix = tr("Tab: %1").arg(suffix);
        }
        m_ghost->setText(suffix);
        m_ghost->setVisible(!suffix.isEmpty());
    }
}

void CommandPalette::refreshResults(const QString &query)
{
    if (!m_storage) return;

    const QString trimmed = query.trimmed();
    const PaletteCommands::Parsed parsed = PaletteCommands::parse(trimmed);

    if (parsed.hasPrefix) {
        const PaletteCommands::Command *command = parsed.command;

        // Unknown (or still ambiguous) word: offer the commands that match, so
        // ">de" leads to ">delete" instead of an empty list.
        if (!command) {
            m_mode = Mode::Commands;
            m_pending = nullptr;
            m_commandItems.clear();
            if (parsed.ambiguous) {
                for (const PaletteCommands::Command &candidate : PaletteCommands::suggest(parsed.word))
                    m_commandItems.append({candidate.id, candidate.usage, candidate.description});
            } else if (parsed.word.isEmpty()) {
                // Bare ">" lists what was used before, then the rest.
                QSet<QString> listed;
                for (const QString &id : m_recentCommands) {
                    const PaletteCommands::Command *recent = PaletteCommands::find(id);
                    if (!recent || listed.contains(recent->id))
                        continue;
                    listed.insert(recent->id);
                    m_commandItems.append({recent->id, recent->usage, recent->description});
                }
                for (const PaletteCommands::Command &candidate : PaletteCommands::all()) {
                    if (listed.contains(candidate.id))
                        continue;
                    m_commandItems.append({candidate.id, candidate.usage, candidate.description});
                }
            }
            m_model->setCommands(m_commandItems, trimmed);
            if (!m_commandItems.isEmpty())
                m_list->setCurrentIndex(m_model->index(0, 0));
            updateHint();
            return;
        }

        if (command->id == QLatin1String("transform")) {
            m_mode = Mode::Transforms;
            m_pending = nullptr;
            QVector<TransformItem> items;
            const QString filter = parsed.argument.toLower();
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
            m_transformItems = items;
            m_model->setTransforms(m_transformItems, trimmed);
            if (!m_transformItems.isEmpty())
                m_list->setCurrentIndex(m_model->index(0, 0));
            updateHint();
            return;
        }
        if (command->id == QLatin1String("snippet")) {
            m_mode = Mode::Snippets;
            m_pending = nullptr;
            QVector<SnippetItem> items;
            const QString filter = parsed.argument.toLower();
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

        if (command->takesArgument()) {
            // Argument completion: tags, groups and profiles come from the
            // window, the export formats are fixed.
            m_mode = Mode::Argument;
            m_pending = command;
            const QStringList candidates = command->argument == PaletteCommands::Argument::Tag
                ? m_tagCandidates
                : command->argument == PaletteCommands::Argument::Group
                    ? m_groupCandidates
                    : command->argument == PaletteCommands::Argument::Profile
                        ? m_profileCandidates
                        : PaletteCommands::staticCandidates(command->argument);
            m_argumentItems =
                PaletteCommands::completions(command->argument, candidates, parsed.argument);
            m_model->setArguments(m_argumentItems, trimmed);
            if (!m_argumentItems.isEmpty())
                m_list->setCurrentIndex(m_model->index(0, 0));
            updateHint();
            return;
        }

        // Argument-less command: it acts on the entry selected in the main
        // window, so there is nothing to list.
        m_mode = Mode::Command;
        m_pending = command;
        m_results.clear();
        m_model->setRecords(m_results, trimmed);
        m_list->setCurrentIndex(QModelIndex());
        updateHint();
        return;
    }
    m_pending = nullptr;

    // History mode — empty input shows the recents section (U9). The recents
    // list reuses the Commands mode so Enter picks a recent row instead of
    // pasting a stale history top hit.
    if (trimmed.isEmpty() && (!m_recentSearches.isEmpty() || !m_recentCommands.isEmpty())) {
        m_mode = Mode::Commands;
        rebuildRecentRows();
        if (!m_recentRows.isEmpty()) {
            m_results.clear();
            m_commandItems.clear();
            for (const RecentRow &row : m_recentRows)
                m_commandItems.append({row.payload, row.text, row.kind});
            m_model->setCommands(m_commandItems, trimmed);
            m_list->setCurrentIndex(m_model->index(0, 0));
            m_hint->setText(tr("Recent — ⏎ run/search  •  type to search history  •  > for "
                                 "commands  •  ? shortcuts"));
            return;
        }
    }
    m_mode = Mode::History;
    // The palette accepts the same query syntax as the main search box.
    m_parsed = SearchEngine::parseQuery(trimmed);

    // Palette shows top 30, ordered by recency (StorageManager handles FTS5).
    const auto page = m_storage->fetchPage(m_parsed.filter, {}, 30);
    QVector<ClipboardRecord> scored = page;

    // Light secondary fuzzy re-rank when query is short (typo tolerance).
    const QString freeText = m_parsed.text;
    if (freeText.size() >= 2 && freeText.size() <= 6 && !scored.isEmpty()) {
        std::stable_sort(scored.begin(), scored.end(), [&](const ClipboardRecord &a, const ClipboardRecord &b){
            return fuzzyScore(freeText, a.preview) > fuzzyScore(freeText, b.preview);
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
        if (!m_pending) {
            m_hint->setText(tr("Esc close"));
            return;
        }
        m_hint->setText(tr("⏎ %1  •  Esc close").arg(m_pending->description));
        return;
    }
    if (m_mode == Mode::Commands) {
        if (m_commandItems.isEmpty())
            m_hint->setText(tr("No command matches — type > to list them all"));
        else
            m_hint->setText(tr("%1 command(s) — ⏎ open/run  •  Esc close  •  Tab completes")
                                .arg(m_commandItems.size()));
        return;
    }
    if (m_mode == Mode::Argument) {
        if (!m_pending) {
            m_hint->setText(tr("Esc close"));
            return;
        }
        const QString example = m_pending->argument == PaletteCommands::Argument::Tag
            ? tr("tags")
            : m_pending->argument == PaletteCommands::Argument::Group
                ? tr("groups")
                : m_pending->argument == PaletteCommands::Argument::Profile
                    ? tr("profiles")
                    : tr("formats");
        if (m_argumentItems.isEmpty()) {
            if (m_pending->argument == PaletteCommands::Argument::Profile) {
                m_hint->setText(tr("%1: unknown profile — save one in Settings ▸ Storage first")
                                    .arg(m_pending->usage));
                return;
            }
            m_hint->setText(tr("%1: type a name — \"%2\" is new and will be created")
                                .arg(m_pending->usage, m_input->text().section(QLatin1Char(' '), 1).trimmed()));
            return;
        }
        m_hint->setText(tr("%1 %2 — Tab or ⏎ completes  •  ⏎ again runs %3")
                            .arg(QString::number(m_argumentItems.size()), example,
                                 m_pending->id));
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
    // History mode: mirror any typed field filters / rejected values.
    QStringList details;
    if (!m_parsed.applied.isEmpty())
        details << tr("filters: %1").arg(m_parsed.applied.join(QStringLiteral(" · ")));
    details += m_parsed.problems;
    const QString detailSuffix = details.isEmpty()
        ? QString()
        : QStringLiteral("  •  ") + details.join(QStringLiteral("  •  "));

    if (m_results.isEmpty()) {
        if (m_currentQuery.trimmed().isEmpty())
            m_hint->setText(tr("Showing recent entries  •  ⏎ paste  •  Esc close  •  Type > for commands (>tag, >export, >pause…)"));
        else
            m_hint->setText(tr("No matches — try fewer words, or use app: type: tag: pinned: has:ocr before:/after: and -exclude"));
    } else {
        m_hint->setText(tr("%1 result(s)  •  ⏎ paste  •  Esc close  •  > for commands")
                            .arg(m_results.size())
                        + detailSuffix);
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
    if (m_mode == Mode::Commands) {
        const int row = index.row();
        if (row < 0 || row >= m_commandItems.size()) return;
        const PaletteCommands::Command *command = PaletteCommands::find(m_commandItems.at(row).id);
        if (!command)
            return;
        if (command->takesArgument()) {
            // Complete the input and let the user type/pick the argument.
            const QString completed = QStringLiteral(">%1 ").arg(command->id);
            m_input->setText(completed);
            m_input->setCursorPosition(completed.size());
            return;
        }
        runCommand(*command, {});
        return;
    }
    if (m_mode == Mode::Argument) {
        completeFromSelection();
        return;
    }
    const qint64 id = index.data(Qt::UserRole).toLongLong();
    if (id == 0) return;
    accept();
    emit pasteRequested(id);
}

bool CommandPalette::completeFromSelection()
{
    const QModelIndex current = m_list->currentIndex();
    if (!current.isValid())
        return false;
    const QString candidate = current.data(Qt::UserRole).toString();
    if (candidate.isEmpty())
        return false;

    if (m_mode == Mode::Commands) {
        // Tab on a command row only completes the word; Enter runs it.
        const QString completed = QStringLiteral(">%1 ").arg(candidate);
        m_input->setText(completed);
        m_input->setCursorPosition(completed.size());
        return true;
    }

    // Replace only the argument, keeping ">command " intact.
    const QString word = PaletteCommands::parse(m_input->text()).word;
    const QString completed = QStringLiteral(">%1 %2").arg(word, candidate);
    m_input->setText(completed);
    m_input->setCursorPosition(completed.size());
    return true;
}

void CommandPalette::runCommand(const PaletteCommands::Command &command, const QString &argument)
{
    accept();
    emit commandExecuted(command.id);

    if (command.id == QLatin1String("copy")) {
        emit copyRequested(0); // 0 = the entry selected in the main window
    } else if (command.id == QLatin1String("pin")) {
        emit pinRequested(0);
    } else if (command.id == QLatin1String("delete")) {
        emit deleteRequested();
    } else if (command.id == QLatin1String("tag")) {
        emit tagRequested(argument);
    } else if (command.id == QLatin1String("group")) {
        emit groupRequested(argument);
    } else if (command.id == QLatin1String("export")) {
        emit exportRequested(argument);
    } else if (command.id == QLatin1String("pause")) {
        emit togglePauseRequested();
    } else if (command.id == QLatin1String("settings")) {
        emit settingsRequested();
    } else if (command.id == QLatin1String("tour")) {
        emit tourRequested();
    } else if (command.id == QLatin1String("profile")) {
        emit profileRequested(argument);
    } else if (command.id == QLatin1String("clean")) {
        emit clearHistoryRequested();
    }
}

void CommandPalette::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Tab && (m_mode == Mode::Argument || m_mode == Mode::Commands)) {
        if (completeFromSelection()) {
            event->accept();
            return;
        }
    }
    QDialog::keyPressEvent(event);
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Tab && keyEvent->modifiers() == Qt::NoModifier
            && (m_mode == Mode::Argument || m_mode == Mode::Commands)) {
            if (completeFromSelection())
                return true; // consume: Tab completes, it does not move focus
        }
    }
    return QDialog::eventFilter(watched, event);
}

void CommandPalette::executeCurrent()
{
    if (m_mode == Mode::Command && m_pending) {
        runCommand(*m_pending, {});
        return;
    }
    if (m_mode == Mode::Argument && m_pending) {
        const QString argument = PaletteCommands::parse(m_input->text()).argument;
        if (m_pending->argument == PaletteCommands::Argument::Format) {
            // The formats are a closed set: run with a value that exists, prefer
            // an exact match, then the highlighted row, then the only one left.
            QString chosen;
            for (const QString &candidate : m_argumentItems) {
                if (candidate.compare(argument, Qt::CaseInsensitive) == 0) {
                    chosen = candidate;
                    break;
                }
            }
            if (chosen.isEmpty() && m_list->currentIndex().isValid())
                chosen = m_list->currentIndex().data(Qt::UserRole).toString();
            if (chosen.isEmpty() && !m_argumentItems.isEmpty())
                chosen = m_argumentItems.first();
            if (!chosen.isEmpty())
                runCommand(*m_pending, chosen);
            return;
        }
        // Tags and groups accept new names, so a typed argument runs as-is;
        // Enter with nothing typed completes the highlighted candidate first.
        if (argument.isEmpty()) {
            completeFromSelection();
            return;
        }
        runCommand(*m_pending, argument);
        return;
    }
    if (m_mode == Mode::Commands) {
        // Empty-input recents section: a search row re-runs the query, a
        // command row completes it for arguments or runs it directly.
        if (m_currentQuery.trimmed().isEmpty() && !m_recentRows.isEmpty()) {
            const QModelIndex current = m_list->currentIndex().isValid()
                ? m_list->currentIndex()
                : m_model->index(0, 0);
            if (current.isValid() && current.row() < m_recentRows.size()) {
                const RecentRow row = m_recentRows.at(current.row());
                if (row.kind == QLatin1String("search")) {
                    // Setting the text re-runs history search via textChanged.
                    QSignalBlocker blocker(m_input);
                    m_input->setText(row.payload);
                    m_input->setCursorPosition(row.payload.size());
                    blocker.unblock();
                    onTextChanged(row.payload);
                    updateHint();
                    return;
                }
                if (const PaletteCommands::Command *command = PaletteCommands::find(row.payload)) {
                    if (command->takesArgument()) {
                        const QString completed = QStringLiteral(">%1 ").arg(command->id);
                        m_input->setText(completed);
                        m_input->setCursorPosition(completed.size());
                    } else {
                        runCommand(*command, {});
                    }
                    return;
                }
            }
        }
        const QModelIndex current = m_list->currentIndex().isValid() ? m_list->currentIndex()
                                                                     : (m_model->rowCount() > 0 ? m_model->index(0, 0) : QModelIndex());
        if (current.isValid())
            onActivated(current);
        return;
    }
    const QModelIndex cur = m_list->currentIndex();
    if (cur.isValid()) { onActivated(cur); return; }
    if (m_model->rowCount() > 0) onActivated(m_model->index(0,0));
}
