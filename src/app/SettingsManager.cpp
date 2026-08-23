#include "SettingsManager.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {
const QString kGroupGeneral = QStringLiteral("General");
const QString kGroupHistory = QStringLiteral("History");

constexpr int kDefaultQuickPasteCount = 9;
constexpr int kDefaultDebounceMs = 250;
constexpr qint64 kDefaultMaxItemBytes = 5 * 1024 * 1024; // 5 MiB
constexpr qint64 kDefaultDiskCapBytes = 0; // unlimited

constexpr SettingsManager::SensitiveMode kDefaultSensitiveMode =
    SettingsManager::SensitiveMode::Exclude;
} // namespace

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
    , m_config(new KConfig(QStringLiteral("egoboardrc"), KConfig::NoGlobals))
{
}

SettingsManager::~SettingsManager()
{
    m_config->sync();
    delete m_config;
}

bool SettingsManager::startVisible() const
{
    return m_config->group(kGroupGeneral).readEntry("StartVisible", false);
}

void SettingsManager::setStartVisible(bool visible)
{
    m_config->group(kGroupGeneral).writeEntry("StartVisible", visible);
    save();
}

bool SettingsManager::hideOnFocusOut() const
{
    return m_config->group(kGroupGeneral).readEntry("HideOnFocusOut", false);
}

void SettingsManager::setHideOnFocusOut(bool hide)
{
    m_config->group(kGroupGeneral).writeEntry("HideOnFocusOut", hide);
    save();
}

bool SettingsManager::monitorPrimarySelection() const
{
    return m_config->group(kGroupGeneral).readEntry("MonitorPrimarySelection", false);
}

void SettingsManager::setMonitorPrimarySelection(bool monitor)
{
    m_config->group(kGroupGeneral).writeEntry("MonitorPrimarySelection", monitor);
    save();
}

int SettingsManager::quickPasteCount() const
{
    return qBound(1, m_config->group(kGroupGeneral).readEntry("QuickPasteCount",
                                                              kDefaultQuickPasteCount), 9);
}

void SettingsManager::setQuickPasteCount(int count)
{
    m_config->group(kGroupGeneral).writeEntry("QuickPasteCount", qBound(1, count, 9));
    save();
}

bool SettingsManager::autostartEnabled() const
{
    return m_config->group(kGroupGeneral).readEntry("Autostart", false);
}

void SettingsManager::setAutostartEnabled(bool enabled)
{
    m_config->group(kGroupGeneral).writeEntry("Autostart", enabled);

    const QString path = autostartDesktopFilePath();
    if (enabled) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write("[Desktop Entry]\n"
                       "Type=Application\n"
                       "Name=Egoboard\n"
                       "Comment=Clipboard history manager\n"
                       "Exec=egoboard\n"
                       "Icon=egoboard\n"
                       "Terminal=false\n"
                       "X-KDE-autostart-phase=2\n"
                       "X-GNOME-Autostart-enabled=true\n");
        }
    } else {
        QFile::remove(path);
    }
    save();
}

int SettingsManager::debounceMs() const
{
    return qBound(50, m_config->group(kGroupHistory).readEntry("DebounceMs",
                                                               kDefaultDebounceMs), 5000);
}

void SettingsManager::setDebounceMs(int ms)
{
    m_config->group(kGroupHistory).writeEntry("DebounceMs", qBound(50, ms, 5000));
    save();
}

SettingsManager::SensitiveMode SettingsManager::sensitiveMode() const
{
    const int value = m_config->group(kGroupHistory).readEntry(
        "SensitiveMode", static_cast<int>(kDefaultSensitiveMode));
    switch (value) {
    case static_cast<int>(SensitiveMode::Off):
        return SensitiveMode::Off;
    case static_cast<int>(SensitiveMode::Mark):
        return SensitiveMode::Mark;
    default:
        return SensitiveMode::Exclude;
    }
}

void SettingsManager::setSensitiveMode(SensitiveMode mode)
{
    m_config->group(kGroupHistory).writeEntry("SensitiveMode", static_cast<int>(mode));
    save();
}

qint64 SettingsManager::maxItemBytes() const
{
    return m_config->group(kGroupHistory).readEntry<qint64>("MaxItemBytes", kDefaultMaxItemBytes);
}

void SettingsManager::setMaxItemBytes(qint64 bytes)
{
    m_config->group(kGroupHistory).writeEntry<qint64>("MaxItemBytes",
                                                      qMax<qint64>(0, bytes));
    save();
}

qint64 SettingsManager::diskCapBytes() const
{
    return m_config->group(kGroupHistory).readEntry<qint64>("DiskCapBytes", kDefaultDiskCapBytes);
}

void SettingsManager::setDiskCapBytes(qint64 bytes)
{
    m_config->group(kGroupHistory).writeEntry<qint64>("DiskCapBytes", qMax<qint64>(0, bytes));
    save();
}

QStringList SettingsManager::ignoredSourceApps() const
{
    return m_config->group(kGroupHistory).readEntry("IgnoredApps", QStringList());
}

void SettingsManager::setIgnoredSourceApps(const QStringList &apps)
{
    QStringList cleaned;
    cleaned.reserve(apps.size());
    for (QString a : apps) {
        a = a.trimmed();
        if (!a.isEmpty()) cleaned << a;
    }
    cleaned.removeDuplicates();
    m_config->group(kGroupHistory).writeEntry("IgnoredApps", cleaned);
    save();
}

bool SettingsManager::isSourceIgnored(const QString &app) const
{
    if (app.isEmpty()) return false;
    const QStringList ignored = ignoredSourceApps();
    for (const QString &pat : ignored) {
        if (pat.compare(app, Qt::CaseInsensitive) == 0) return true;
        // also support wildcard *app* via simple contains
        if (pat.contains(QLatin1Char('*'))) {
            QRegularExpression re(QRegularExpression::wildcardToRegularExpression(pat), QRegularExpression::CaseInsensitiveOption);
            if (re.match(app).hasMatch()) return true;
        }
    }
    return false;
}

bool SettingsManager::ocrEnabled() const
{
    // Default: true if the system has tesseract, false otherwise — but we default to true
    // and let the worker no-op gracefully when the binary is missing.
    return m_config->group(kGroupHistory).readEntry("OcrEnabled", true);
}

void SettingsManager::setOcrEnabled(bool enabled)
{
    m_config->group(kGroupHistory).writeEntry("OcrEnabled", enabled);
    save();
}

void SettingsManager::save()
{
    m_config->sync();
    emit changed();
}

QString SettingsManager::defaultDatabasePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/history.db");
}

QString SettingsManager::autostartDesktopFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/autostart/org.egoboard.Egoboard.desktop");
}
