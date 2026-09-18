#include "TrayController.h"

#include "SettingsManager.h"
#include "StorageManager.h"

#include <KStatusNotifierItem>

#include <QApplication>
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
        const QIcon appIcon = QIcon::fromTheme(QStringLiteral("egoboard"),
                                               QIcon(QStringLiteral(":/icons/egoboard.svg")));
        m_sni->setIconByPixmap(appIcon);
        m_sni->setStatus(KStatusNotifierItem::Active);
        m_sni->setToolTip(QStringLiteral("egoboard"), QStringLiteral("Egoboard"),
                          tr("Clipboard history"));
        m_sni->setToolTipIconByPixmap(appIcon);
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
        m_fallbackIcon = new QSystemTrayIcon(QIcon::fromTheme(QStringLiteral("egoboard"),
                                                             QIcon(QStringLiteral(":/icons/egoboard.svg"))),
                                             this);
        m_fallbackIcon->setContextMenu(m_menu);
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
    m_paused = paused;
    if (m_pauseAction) {
        // State is set here; the toggled() signal is for user clicks only.
        const QSignalBlocker blocker(m_pauseAction);
        m_pauseAction->setChecked(paused);
    }
}

void TrayController::rebuildMenu()
{
    // Rebuild the whole menu on aboutToShow (the DBusMenu host asks for the
    // layout then), so captures never pay for a menu nobody is looking at.
    const auto actions = m_menu->actions();
    for (QAction *action : actions) {
        m_menu->removeAction(action);
        action->deleteLater();
    }

    m_menu->addSection(tr("Recent"));
    // The summary page already carries the preview shown here; no per-entry
    // fetchFull round trip needed.
    const auto recents = m_storage->fetchPage(FilterSpec{}, {}, kRecentCount);
    for (const ClipboardRecord &record : recents) {
        QAction *action = new QAction(record.preview.isEmpty() ? tr("(empty)") : record.preview,
                                      m_menu);
        connect(action, &QAction::triggered, this,
                [this, id = record.id] { emit pasteRequested(id); });
        m_menu->addAction(action);
    }

    m_menu->addSeparator();
    QAction *toggle = m_menu->addAction(tr("Show Clipboard History"));
    connect(toggle, &QAction::triggered, this, &TrayController::toggleRequested);

    QAction *quick = m_menu->addAction(tr("Quick Paste…"));
    connect(quick, &QAction::triggered, this, &TrayController::quickPasteRequested);

    m_pauseAction = m_menu->addAction(tr("Pause capture"));
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
