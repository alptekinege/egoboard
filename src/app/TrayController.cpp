#include "TrayController.h"

#include "SettingsManager.h"
#include "StorageManager.h"
#include "TrayMenuModel.h"

#include <KStatusNotifierItem>

#include <QApplication>
#include <QDateTime>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>

bool TrayController::statusNotifierHostAvailable()
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    return bus.isConnected()
        && bus.interface()->isServiceRegistered(QStringLiteral("org.kde.StatusNotifierWatcher"));
}

TrayController::TrayController(StorageManager *storage, SettingsManager *settings, QObject *parent)
    : QObject(parent)
    , m_storage(storage)
    , m_settings(settings)
{
    m_menu = new QMenu();
    m_menu->setSeparatorsCollapsible(false);
    connect(m_menu, &QMenu::aboutToShow, this, &TrayController::rebuildMenu);

    if (statusNotifierHostAvailable()) {
        m_sni = new KStatusNotifierItem(QStringLiteral("egoboard"), this);
        m_sni->setTitle(QStringLiteral("Egoboard"));
        m_appIcon = QIcon::fromTheme(QStringLiteral("egoboard"),
                                     QIcon(QStringLiteral(":/icons/egoboard.svg")));
        // Paused state reuses a theme icon — no bundled artwork involved.
        m_pausedIcon = QIcon::fromTheme(QStringLiteral("media-playback-paused"), m_appIcon);
        m_sni->setIconByPixmap(m_appIcon);
        m_sni->setStatus(KStatusNotifierItem::Active);
        m_sni->setToolTip(QStringLiteral("egoboard"), QStringLiteral("Egoboard"),
                          tr("Clipboard history"));
        m_sni->setToolTipIconByPixmap(m_appIcon);
        m_sni->setStandardActionsEnabled(false);
        m_sni->setContextMenu(m_menu);
        connect(m_sni, &KStatusNotifierItem::activateRequested, this,
                [this](bool active, const QPoint &) {
                    if (active)
                        runClickAction(false);
                });
        connect(m_sni, &KStatusNotifierItem::secondaryActivateRequested, this,
                [this](const QPoint &) { runClickAction(true); });
        // The wheel needs an SNI host; the QSystemTrayIcon fallback has no
        // equivalent signal at all.
        connect(m_sni, &KStatusNotifierItem::scrollRequested, this,
                [this](int delta, Qt::Orientation orientation) {
                    if (!m_settings || !m_settings->trayWheelCycles())
                        return;
                    if (orientation != Qt::Vertical || delta == 0)
                        return;
                    // Wheel up walks back in history, like Klipper.
                    emit wheelSteps(delta > 0 ? 1 : -1);
                });
    } else if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_appIcon = QIcon::fromTheme(QStringLiteral("egoboard"),
                                     QIcon(QStringLiteral(":/icons/egoboard.svg")));
        m_pausedIcon = QIcon::fromTheme(QStringLiteral("media-playback-paused"), m_appIcon);
        m_fallbackIcon = new QSystemTrayIcon(m_appIcon, this);
        m_fallbackIcon->setContextMenu(m_menu);
        m_fallbackIcon->setToolTip(tr("Egoboard — Clipboard history"));
        m_fallbackIcon->show();
        connect(m_fallbackIcon, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason) {
                    if (reason == QSystemTrayIcon::Trigger)
                        runClickAction(false);
                    else if (reason == QSystemTrayIcon::MiddleClick)
                        runClickAction(true);
                });
    } else {
        qWarning("egoboard: no system tray available");
    }

    // TrayMode is live: re-evaluate visibility on every settings change, and
    // refresh tooltip/icon on every capture — no restart, no menu rebuild
    // storm (the menu itself stays lazily built on aboutToShow).
    if (m_settings)
        connect(m_settings, &SettingsManager::changed, this, &TrayController::applyVisibility);
    if (m_storage) {
        connect(m_storage, &StorageManager::entryAdded, this,
                &TrayController::refreshTooltipAndIcon);
        connect(m_storage, &StorageManager::entryTouched, this,
                &TrayController::refreshTooltipAndIcon);
        connect(m_storage, &StorageManager::entriesRemoved, this,
                &TrayController::refreshTooltipAndIcon);
        connect(m_storage, &StorageManager::storageReset, this,
                &TrayController::refreshTooltipAndIcon);
        connect(m_storage, &StorageManager::entryAdded, this, &TrayController::applyVisibility);
        connect(m_storage, &StorageManager::entriesRemoved, this, &TrayController::applyVisibility);
        connect(m_storage, &StorageManager::storageReset, this, &TrayController::applyVisibility);
    }
    refreshTooltipAndIcon();
    applyVisibility();
}

void TrayController::runClickAction(bool secondary)
{
    if (!m_settings) {
        // No settings (tests, early shutdown): keep the historical behaviour.
        if (secondary)
            emit quickPasteRequested();
        else
            emit toggleRequested();
        return;
    }
    const SettingsManager::TrayClick action = secondary ? m_settings->traySecondaryClick()
                                                        : m_settings->trayPrimaryClick();
    switch (action) {
    case SettingsManager::TrayClick::ShowWindow:
        emit toggleRequested();
        break;
    case SettingsManager::TrayClick::QuickPaste:
        emit quickPasteRequested();
        break;
    case SettingsManager::TrayClick::TogglePause:
        emit pauseToggled(!m_paused);
        break;
    case SettingsManager::TrayClick::Nothing:
        break;
    }
}

void TrayController::setPaused(bool paused)
{
    if (m_paused == paused && m_pauseAction)
        return;
    m_paused = paused;
    if (m_pauseAction) {
        // State is set here; the toggled() signal is for user clicks only.
        const QSignalBlocker blocker(m_pauseAction);
        m_pauseAction->setChecked(paused);
    }
    refreshTooltipAndIcon();
}

void TrayController::applyVisibility()
{
    if (!m_settings || (!m_sni && !m_fallbackIcon))
        return;
    const bool visible = TrayMenuModel::isVisible(
        TrayMenuModel::parseMode(m_settings->trayMode()),
        m_storage ? m_storage->stats().entryCount : 0);
    if (visible == m_visible)
        return; // no signal/visual storm on unrelated settings changes
    m_visible = visible;
    if (m_sni)
        m_sni->setStatus(visible ? KStatusNotifierItem::Active : KStatusNotifierItem::Passive);
    else if (m_fallbackIcon)
        m_fallbackIcon->setVisible(visible);
}

void TrayController::refreshTooltipAndIcon()
{
    if (!m_storage)
        return;
    const qint64 entryCount = m_storage->stats().entryCount;
    qint64 lastCaptureMs = 0;
    const auto newest = m_storage->fetchPage(FilterSpec{}, {}, 1);
    if (!newest.isEmpty())
        lastCaptureMs = newest.first().timestamp;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const QString header = TrayMenuModel::headerText(m_paused, entryCount);
    const QString body =
        TrayMenuModel::tooltipText(m_paused, entryCount, lastCaptureMs, nowMs);
    if (m_sni) {
        m_sni->setToolTip(QStringLiteral("egoboard"), header, body);
        if (m_paused != m_iconPaused) {
            m_iconPaused = m_paused;
            m_sni->setIconByPixmap(m_paused ? m_pausedIcon : m_appIcon);
        }
    } else if (m_fallbackIcon) {
        m_fallbackIcon->setToolTip(QStringLiteral("Egoboard — %1\n%2").arg(header, body));
        if (m_paused != m_iconPaused) {
            m_iconPaused = m_paused;
            m_fallbackIcon->setIcon(m_paused ? m_pausedIcon : m_appIcon);
        }
    }
}

void TrayController::rebuildMenu()
{
    // Rebuild the whole menu on aboutToShow (the DBusMenu host asks for the
    // layout then), so captures never pay for a menu nobody is looking at.
    // Both surfaces share this menu, so SNI and fallback always match.
    const auto actions = m_menu->actions();
    for (QAction *action : actions) {
        if (action == m_pauseAction)
            m_pauseAction = nullptr; // no dangling check state below
        m_menu->removeAction(action);
        action->deleteLater();
    }

    // State group: capture state + history count.
    const qint64 entryCount = m_storage ? m_storage->stats().entryCount : 0;
    m_menu->addSection(TrayMenuModel::headerText(m_paused, entryCount));

    // Recent group: bounded, lazy, type-aware rows with an explicit empty state.
    // The summary page already carries the preview shown here; no per-entry
    // fetchFull round trip needed.
    const auto recents =
        m_storage ? m_storage->fetchPage(FilterSpec{}, {}, kRecentCount) : QVector<ClipboardRecord>{};
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const auto rows = recents.isEmpty()
        ? TrayMenuModel::emptyRows()
        : TrayMenuModel::buildRecentRows(recents, nowMs);
    for (const TrayMenuModel::RecentRow &row : rows) {
        QAction *action = new QAction(QIcon::fromTheme(row.iconName), row.label, m_menu);
        action->setToolTip(row.toolTip);
        action->setEnabled(row.enabled);
        if (row.enabled && row.id != 0)
            connect(action, &QAction::triggered, this,
                    [this, id = row.id] { emit pasteRequested(id); });
        m_menu->addAction(action);
    }

    // Quick-action group.
    m_menu->addSeparator();
    QAction *toggle = m_menu->addAction(QIcon::fromTheme(QStringLiteral("document-open-recent")),
                                        tr("Show Clipboard History"));
    connect(toggle, &QAction::triggered, this, &TrayController::toggleRequested);

    QAction *quick = m_menu->addAction(QIcon::fromTheme(QStringLiteral("edit-paste")),
                                       tr("Quick Paste…"));
    connect(quick, &QAction::triggered, this, &TrayController::quickPasteRequested);

    m_pauseAction = m_menu->addAction(QIcon::fromTheme(QStringLiteral("media-playback-paused")),
                                      tr("Pause capture"));
    m_pauseAction->setCheckable(true);
    {
        const QSignalBlocker blocker(m_pauseAction); // rebuilding must not re-toggle
        m_pauseAction->setChecked(m_paused);
    }
    m_pauseAction->setToolTip(tr("Stops recording new clipboard entries until resumed. "
                                 "Also pinned to a global shortcut."));
    connect(m_pauseAction, &QAction::toggled, this, [this](bool paused) {
        m_paused = paused;
        emit pauseToggled(paused);
    });

    m_menu->addSeparator();
    QAction *settings = m_menu->addAction(QIcon::fromTheme(QStringLiteral("configure")),
                                        tr("Settings…"));
    connect(settings, &QAction::triggered, this, &TrayController::settingsRequested);

    QAction *clear = m_menu->addAction(QIcon::fromTheme(QStringLiteral("edit-clear-all")),
                                     tr("Clear History…"));
    connect(clear, &QAction::triggered, this, &TrayController::clearRequested);

    m_menu->addSeparator();
    QAction *quit = m_menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")),
                                    tr("Quit"));
    connect(quit, &QAction::triggered, this, &TrayController::quitRequested);
}
