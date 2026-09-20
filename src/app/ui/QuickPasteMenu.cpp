#include "QuickPasteMenu.h"

#include "ClipboardListModel.h"
#include "ClipboardRecord.h"
#include "ContentType.h"
#include "DesignTokens.h"
#include "SearchEngine.h"
#include "StorageManager.h"
#include "UiHelpers.h"
#include "../KWinCursorTracker.h"
#include "../LayerShellHelper.h"
#include "../SettingsManager.h"

#include <QDateTime>
#include <QFrame>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

QuickPasteMenu::QuickPasteMenu(StorageManager *storage, int itemCount, QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_storage(storage)
    , m_itemCount(qBound(1, itemCount, 9))
{
    setAttribute(Qt::WA_ShowWithoutActivating, false);
    // Frameless and rounded: the card below carries the frame, the margin
    // around it holds the shadow.
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFocusPolicy(Qt::StrongFocus);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(DesignTokens::SpaceM, DesignTokens::SpaceM, DesignTokens::SpaceM,
                              DesignTokens::SpaceM);

    auto *card = UiHelpers::makePopupPanel(this);
    card->setObjectName(QStringLiteral("quickPasteCard"));
    outer->addWidget(card);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(DesignTokens::SpaceL, DesignTokens::SpaceM, DesignTokens::SpaceL,
                               DesignTokens::SpaceM);
    layout->setSpacing(DesignTokens::SpaceS);

    auto *header = new QLabel(tr("Quick paste"), card);
    QFont headerFont = header->font();
    headerFont.setWeight(QFont::DemiBold);
    header->setFont(headerFont);
    layout->addWidget(header);

    // R3: search-as-you-type on top; focus starts here.
    m_search = new QLineEdit(card);
    m_search->setPlaceholderText(tr("Type to filter…  •  type: app:"));
    m_search->setClearButtonEnabled(true);
    m_search->setAccessibleName(tr("Filter quick paste entries"));
    UiHelpers::styleSearchField(m_search);
    layout->addWidget(m_search);

    m_list = new QListWidget(card);
    m_list->setWordWrap(false);
    m_list->setUniformItemSizes(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setAccessibleName(tr("Quick paste entries"));
    m_list->setAccessibleDescription(tr("Recent entries; press 1-9 or Enter to paste"));
    UiHelpers::styleItemList(m_list);
    layout->addWidget(m_list, 1);

    layout->addWidget(UiHelpers::makeHint(tr("Type to filter · 1–9 paste · ↑↓ move · Esc close"), card));

    m_autoHide = new QTimer(this);
    m_autoHide->setSingleShot(true);
    m_autoHide->setInterval(20000);
    connect(m_autoHide, &QTimer::timeout, this, &QuickPasteMenu::hide);

    connect(m_list, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *item) { activateRow(m_list->row(item)); });
    connect(m_search, &QLineEdit::textChanged, this, [this] { refreshList(); });
    // ↑↓ in the search field move the selection; Enter pastes it.
    m_search->installEventFilter(this);

    refresh();
}

void QuickPasteMenu::setItemCount(int count)
{
    const int clamped = qBound(1, count, 9);
    if (clamped == m_itemCount)
        return;
    m_itemCount = clamped;
    if (isVisible())
        refresh();
}

void QuickPasteMenu::setTwoLine(bool twoLine)
{
    if (twoLine == m_twoLine)
        return;
    m_twoLine = twoLine;
    m_list->setUniformItemSizes(!twoLine); // two-line rows vary in height
    if (isVisible())
        refreshList();
    else
        adjustSize();
}

QString QuickPasteMenu::metaLine(const ClipboardRecord &record)
{
    QStringList parts;
    switch (record.type) {
    case ContentType::Image:
        parts << QuickPasteMenu::tr("image");
        break;
    case ContentType::RichText:
        parts << QuickPasteMenu::tr("rich text");
        break;
    case ContentType::Files:
        parts << QuickPasteMenu::tr("files");
        break;
    case ContentType::Text:
    default:
        parts << QuickPasteMenu::tr("text");
        break;
    }
    if (!record.sourceApp.isEmpty())
        parts << record.sourceApp;
    const qint64 secs = QDateTime::fromMSecsSinceEpoch(record.timestamp).secsTo(
        QDateTime::currentDateTime());
    if (secs < 50)
        parts << QuickPasteMenu::tr("just now");
    else if (secs < 90 * 60)
        parts << QuickPasteMenu::tr("%1 min ago").arg(qRound(secs / 60.0));
    else if (secs < 24 * 3600)
        parts << QuickPasteMenu::tr("%1 h ago").arg(qRound(secs / 3600.0));
    else
        parts << QuickPasteMenu::tr("%1 d ago").arg(qRound(secs / 86400.0));
    return parts.join(QStringLiteral(" · "));
}

QString QuickPasteMenu::screenNameFor(const QPoint &cursorPos)
{
    if (QScreen *screen = QGuiApplication::screenAt(cursorPos))
        return screen->name();
    if (QScreen *primary = QGuiApplication::primaryScreen())
        return primary->name();
    return QString();
}

void QuickPasteMenu::refresh()
{
    if (m_search)
        m_search->clear();
    refreshList();
}

void QuickPasteMenu::refreshList()
{
    if (!m_list || !m_storage)
        return;
    // R3: search-as-you-type over a bounded recent window. The popup only ever
    // shows m_itemCount rows; filtering 200 recents in memory keeps typing
    // instant even at 50k entries without a per-keystroke SQL query.
    const QString query = m_search ? m_search->text().trimmed() : QString();
    const SearchEngine::ParsedQuery parsed = SearchEngine::parseQuery(query);
    QVector<ClipboardRecord> recents = m_storage->fetchPage(FilterSpec{}, {}, 200);

    m_rows.clear();
    const QString needle = parsed.text.trimmed().toLower();
    const QString typeNeedle = parsed.filter.contentType >= 0
        ? QString::number(parsed.filter.contentType)
        : QString();
    for (const ClipboardRecord &record : recents) {
        if (!needle.isEmpty()
            && !record.preview.toLower().contains(needle)
            && !record.textData.toLower().contains(needle)
            && !record.sourceApp.toLower().contains(needle))
            continue;
        if (parsed.filter.contentType >= 0 && int(record.type) != parsed.filter.contentType)
            continue;
        if (!parsed.filter.sourceApp.isEmpty()
            && !record.sourceApp.contains(parsed.filter.sourceApp, Qt::CaseInsensitive))
            continue;
        if (!parsed.filter.tags.isEmpty())
            continue; // tags need a DB join; popup stays fast without them
        m_rows.append(record);
        if (m_rows.size() >= m_itemCount && needle.isEmpty() && typeNeedle.isEmpty()
            && parsed.filter.sourceApp.isEmpty())
            break;
        if (m_rows.size() >= m_itemCount * 4)
            break; // filtered: scan a wider window, still bounded
    }
    if (!needle.isEmpty() || !typeNeedle.isEmpty() || !parsed.filter.sourceApp.isEmpty()) {
        while (m_rows.size() > m_itemCount)
            m_rows.removeLast();
    }

    m_list->clear();
    const QFontMetrics metrics(m_list->font());
    for (int i = 0; i < m_rows.size(); ++i) {
        const ClipboardRecord &record = m_rows.at(i);
        auto *item = new QListWidgetItem(m_list);
        QString title = QStringLiteral("%1  %2").arg(i + 1).arg(
            record.preview.isEmpty() ? tr("—") : record.preview);
        if (m_twoLine) {
            title = metrics.elidedText(title, Qt::ElideRight, 420);
            item->setText(title + QLatin1Char('\n') + metaLine(record));
            item->setData(Qt::AccessibleDescriptionRole, metaLine(record));
        } else {
            item->setText(metrics.elidedText(title, Qt::ElideRight, 460));
        }
        item->setData(Qt::UserRole, record.id);
        item->setToolTip(metaLine(record));
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
    adjustSize();
}

void QuickPasteMenu::popupAtCursor()
{
    refresh();
    // Size drives LayerShellQt's desiredSize — compute before attaching.
    adjustSize();

    const auto showAt = [this](const QPoint &cursorPos) {
        QScreen *screen = QGuiApplication::screenAt(cursorPos);
        if (!screen)
            screen = QGuiApplication::primaryScreen();

        // R3: per-screen placement memory — reuse the last position on this
        // screen when it still fits, otherwise anchor at the cursor.
        m_lastScreen = screenNameFor(cursorPos);
        m_lastCursor = cursorPos;
        QPoint pos = cursorPos + QPoint(12, 12);
        if (m_settings && !m_lastScreen.isEmpty()) {
            const QPoint remembered = m_settings->quickPastePos(m_lastScreen);
            if (!remembered.isNull() && remembered.x() >= 0 && screen
                && screen->availableGeometry().contains(
                       QRect(remembered, QSize(64, 64)), /*proper=*/true))
                pos = remembered;
        }
        if (screen) {
            // Cursor-anchored position, clamped inside the screen (multi-monitor safe).
            const QRect available = screen->availableGeometry();
            pos.setX(qMin(pos.x(), available.right() - width() - 8));
            pos.setY(qMin(pos.y(), available.bottom() - height() - 8));
            pos.setX(qMax(pos.x(), available.left() + 8));
            pos.setY(qMax(pos.y(), available.top() + 8));
        }

        const QSize desired = size();
        const bool useLayerShell = LayerShellHelper::isAvailable() && screen;

        if (useLayerShell) {
            // LayerShellQt requires a native QWindow before configuring.
            // windowHandle() is null until the widget has a native window;
            // WA_NativeWindow + winId() forces creation without yet showing.
            if (!windowHandle()) {
                setAttribute(Qt::WA_NativeWindow, true);
                // Ensure the window handle exists before configuring layer-shell.
                // winId() creates it; createWindowContainer is not needed.
                (void)winId();
            }
            if (QWindow *win = windowHandle()) {
                LayerShellHelper::configureForQuickPaste(win, screen, desired, pos);
                m_layerShellConfigured = true;
            } else {
                // Should not happen — fallback to cursor move.
                if (screen) move(pos);
                m_layerShellConfigured = false;
            }
        } else {
            // X11 / offscreen / no LayerShellQt — classic cursor popup.
            if (screen)
                move(pos);
            m_layerShellConfigured = false;
        }

        UiHelpers::fadeIn(this); // capped, skippable (Settings ▸ Appearance)
        show();
        raise();
        activateWindow();
        m_autoHide->start();
    };

    // Wayland hides the global pointer position from clients: QCursor::pos()
    // only knows where the pointer last touched OUR windows, so a hotkey
    // triggered over another app would anchor to a stale spot. Ask KWin for
    // the real position (X11 and non-KWin sessions fall back to QCursor).
    KWinCursorTracker::queryGlobal([this, showAt](const QPoint &pos) { showAt(pos); });
}

void QuickPasteMenu::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // R3: focus starts in the search field so typing filters immediately.
    if (m_search) {
        m_search->setFocus();
        m_search->selectAll();
    } else {
        setFocus();
    }
}

void QuickPasteMenu::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    // Keep flag for diagnostics; LayerShellQt cleans its layer surface on hide.
    // Don't reset m_layerShellConfigured here — it reflects last popup mode.
    Q_UNUSED(event)
}

bool QuickPasteMenu::eventFilter(QObject *watched, QEvent *event)
{
    // R3: ↑↓ in the search field move the selection, Enter pastes it —
    // the list never takes focus away from typing.
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        switch (keyEvent->key()) {
        case Qt::Key_Up:
            moveSelection(-1);
            return true;
        case Qt::Key_Down:
            moveSelection(1);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            activateRow(m_list ? m_list->currentRow() : 0);
            return true;
        case Qt::Key_Escape:
            hide();
            return true;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void QuickPasteMenu::moveSelection(int delta)
{
    if (!m_list || m_list->count() == 0)
        return;
    const int next = qBound(0, m_list->currentRow() + delta, m_list->count() - 1);
    m_list->setCurrentRow(next);
}

void QuickPasteMenu::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    if (key == Qt::Key_Escape) {
        hide();
        return;
    }
    if (key == Qt::Key_Up) {
        moveSelection(-1);
        return;
    }
    if (key == Qt::Key_Down) {
        moveSelection(1);
        return;
    }
    if (key >= Qt::Key_1 && key <= Qt::Key_9) {
        const int row = key - Qt::Key_1;
        if (row < m_list->count()) {
            activateRow(row);
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

void QuickPasteMenu::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    if (isVisible())
        hide(); // the user clicked somewhere else
}

void QuickPasteMenu::activateRow(int row)
{
    QListWidgetItem *item = m_list->item(row);
    if (!item)
        return;
    const qint64 id = item->data(Qt::UserRole).toLongLong();
    hide();
    if (id != 0)
        emit pasteRequested(id);
}

void QuickPasteMenu::hide()
{
    // R3: remember where the popup was on this screen for next time.
    if (m_settings && !m_lastScreen.isEmpty() && !pos().isNull())
        m_settings->setQuickPastePos(m_lastScreen, pos());
    m_lastScreen.clear();
    m_autoHide->stop();
    QWidget::hide();
    emit hidden();
}
