#include "QuickPasteMenu.h"

#include "ClipboardListModel.h"
#include "DesignTokens.h"
#include "StorageManager.h"
#include "UiHelpers.h"
#include "../KWinCursorTracker.h"
#include "../LayerShellHelper.h"

#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
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

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("quickPasteCard"));
    card->setStyleSheet(QStringLiteral("#quickPasteCard { background: palette(window); "
                                       "border: 1px solid palette(mid); border-radius: %1px; }")
                            .arg(DesignTokens::RadiusL));
    auto *shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(DesignTokens::SpaceL * 2);
    shadow->setOffset(0, DesignTokens::SpaceXs);
    shadow->setColor(QColor(0, 0, 0, 140));
    card->setGraphicsEffect(shadow);
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

    m_list = new QListWidget(card);
    m_list->setWordWrap(false);
    m_list->setUniformItemSizes(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setAccessibleName(tr("Quick paste entries"));
    m_list->setAccessibleDescription(tr("Recent entries; press 1-9 or Enter to paste"));
    // Rounded rows, the same look the settings sidebar uses.
    m_list->setStyleSheet(
        QStringLiteral("QListWidget { border: none; background: transparent; }"
                       "QListWidget::item { padding: %1px 2px; border-radius: %2px; }"
                       "QListWidget::item:selected { background: palette(highlight); "
                       "color: palette(highlighted-text); }")
            .arg(DesignTokens::SpaceXs)
            .arg(DesignTokens::RadiusL));
    layout->addWidget(m_list, 1);

    layout->addWidget(UiHelpers::makeHint(tr("1–9 paste · Esc close"), card));

    m_autoHide = new QTimer(this);
    m_autoHide->setSingleShot(true);
    m_autoHide->setInterval(20000);
    connect(m_autoHide, &QTimer::timeout, this, &QuickPasteMenu::hide);

    connect(m_list, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *item) { activateRow(m_list->row(item)); });

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

void QuickPasteMenu::refresh()
{
    m_list->clear();
    const auto recents = m_storage->fetchPage(FilterSpec{}, {}, m_itemCount);
    for (int i = 0; i < recents.size(); ++i) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1  %2").arg(i + 1).arg(recents.at(i).preview), m_list);
        item->setData(Qt::UserRole, recents.at(i).id);
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

        // Cursor-anchored position, clamped inside the screen (multi-monitor safe).
        QPoint pos = cursorPos + QPoint(12, 12);
        if (screen) {
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
    setFocus();
}

void QuickPasteMenu::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    // Keep flag for diagnostics; LayerShellQt cleans its layer surface on hide.
    // Don't reset m_layerShellConfigured here — it reflects last popup mode.
    Q_UNUSED(event)
}

void QuickPasteMenu::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    if (key == Qt::Key_Escape) {
        hide();
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
    m_autoHide->stop();
    QWidget::hide();
    emit hidden();
}
