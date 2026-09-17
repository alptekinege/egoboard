#include "SystemThemeWatcher.h"

#include <KSharedConfig>

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QStandardPaths>
#include <QTimer>

namespace {
constexpr int kCoalesceMs = 200; // KConfig writes key by key; wait for the batch
constexpr QLatin1String kConfigFileName("kdeglobals");
constexpr QLatin1String kGeneralGroup("General");
constexpr QLatin1String kIconsGroup("Icons");

QString configFilePath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath(kConfigFileName);
}

// Cheap "did the file change" marker: KConfig replaces kdeglobals atomically, so
// the directory watch alone cannot tell whether it was ours that moved.
QString fingerprintOf(const QString &path)
{
    const QFileInfo info(path);
    return QStringLiteral("%1:%2").arg(info.lastModified().toMSecsSinceEpoch()).arg(info.size());
}
} // namespace

SystemThemeWatcher::SystemThemeWatcher(QObject *parent)
    : QObject(parent)
    , m_lastFingerprint(fingerprintOf(configFilePath()))
{
    // Channel 1: KDE's own notification, sent when another client writes
    // kdeglobals with the Notify flag (System Settings does).
    m_configWatcher = KConfigWatcher::create(KSharedConfig::openConfig(kConfigFileName));
    connect(m_configWatcher.data(), &KConfigWatcher::configChanged, this,
            [this](const KConfigGroup &group, const QByteArrayList &keys) {
                if (touchesTheme(group.name(), keys))
                    emit changed();
            });

    // Channel 2: the file itself, for writers that never touch D-Bus.
    m_fileWatcher = new QFileSystemWatcher(this);
    m_coalesce = new QTimer(this);
    m_coalesce->setSingleShot(true);
    m_coalesce->setInterval(kCoalesceMs);
    connect(m_coalesce, &QTimer::timeout, this, &SystemThemeWatcher::onFileActivity);
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &) { m_coalesce->start(); });
    connect(m_fileWatcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString &) { m_coalesce->start(); });
    armFileWatch();
}

bool SystemThemeWatcher::touchesTheme(const QString &group, const QByteArrayList &keys)
{
    const auto namesKey = [&keys](const QByteArray &key) {
        return keys.isEmpty() || keys.contains(key);
    };
    if (group == kGeneralGroup && namesKey(QByteArrayLiteral("ColorScheme")))
        return true;
    return group == kIconsGroup && namesKey(QByteArrayLiteral("Theme"));
}

void SystemThemeWatcher::armFileWatch()
{
    const QString path = configFilePath();
    if (!m_fileWatcher->files().contains(path) && QFileInfo::exists(path))
        m_fileWatcher->addPath(path);
    // The directory keeps being watched across the atomic replace KConfig does.
    const QString directory = QFileInfo(path).absolutePath();
    if (QDir(directory).exists() && !m_fileWatcher->directories().contains(directory))
        m_fileWatcher->addPath(directory);
}

void SystemThemeWatcher::onFileActivity()
{
    const QString path = configFilePath();
    const QString fingerprint = fingerprintOf(path);
    armFileWatch(); // a replace drops the file watch; re-arm it for next time

    // The directory watch fires for every sibling file in ~/.config, so make
    // sure kdeglobals itself moved before waking the app up.
    if (fingerprint == m_lastFingerprint)
        return;
    m_lastFingerprint = fingerprint;
    emit changed();
}
