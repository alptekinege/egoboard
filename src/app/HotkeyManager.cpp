#include "HotkeyManager.h"

#include <KActionCollection>
#include <KGlobalAccel>

#include <QAction>

QList<QKeySequence> HotkeyManager::defaultToggleShortcut()
{
    return {QKeySequence(QStringLiteral("Meta+V"))};
}

QList<QKeySequence> HotkeyManager::defaultQuickPasteShortcut()
{
    return {QKeySequence(QStringLiteral("Meta+Shift+V"))};
}

QList<QKeySequence> HotkeyManager::defaultDeleteLastShortcut()
{
    return {QKeySequence(QStringLiteral("Meta+Shift+D"))};
}

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
{
    auto collection = new KActionCollection(this);
    collection->setComponentName(QStringLiteral("egoboard"));

    m_toggle = collection->addAction(QStringLiteral("toggle"));
    m_toggle->setText(tr("Show Egoboard Clipboard History"));
    KGlobalAccel::self()->setDefaultShortcut(m_toggle, defaultToggleShortcut());
    KGlobalAccel::self()->setShortcut(m_toggle, defaultToggleShortcut());
    connect(m_toggle, &QAction::triggered, this, &HotkeyManager::toggleRequested);

    m_quickPaste = collection->addAction(QStringLiteral("quickpaste"));
    m_quickPaste->setText(tr("Open Quick Paste Menu"));
    KGlobalAccel::self()->setDefaultShortcut(m_quickPaste, defaultQuickPasteShortcut());
    KGlobalAccel::self()->setShortcut(m_quickPaste, defaultQuickPasteShortcut());
    connect(m_quickPaste, &QAction::triggered, this, &HotkeyManager::quickPasteRequested);

    m_deleteLast = collection->addAction(QStringLiteral("deletelast"));
    m_deleteLast->setText(tr("Delete Last Clipboard Entry"));
    KGlobalAccel::self()->setDefaultShortcut(m_deleteLast, defaultDeleteLastShortcut());
    KGlobalAccel::self()->setShortcut(m_deleteLast, defaultDeleteLastShortcut());
    connect(m_deleteLast, &QAction::triggered, this, &HotkeyManager::deleteLastRequested);
}
