#include "TrayController.h"

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

TrayController::TrayController(StorageManager *storage, QObject *parent)
    : QObject(parent)
    , m_storage(storage)
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
                        emit toggleRequested();
                });
        connect(m_sni, &KStatusNotifierItem::secondaryActivateRequested, this,
                [this](const QPoint &) { emit quickPasteRequested(); });
    } else if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_fallbackIcon = new QSystemTrayIcon(QIcon::fromTheme(QStringLiteral("egoboard"),
                                                             QIcon(QStringLiteral(":/icons/egoboard.svg"))),
                                             this);
        m_fallbackIcon->setContextMenu(m_menu);
        m_fallbackIcon->show();
        connect(m_fallbackIcon, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason) {
                    if (reason == QSystemTrayIcon::Trigger)
                        emit toggleRequested();
                    else if (reason == QSystemTrayIcon::MiddleClick)
                        emit quickPasteRequested();
                });
    } else {
        qWarning("egoboard: no system tray available");
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
