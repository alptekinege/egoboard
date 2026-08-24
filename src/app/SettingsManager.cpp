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
const QString kGroupPreview = QStringLiteral("Preview");
const QString kGroupOcr = QStringLiteral("Ocr");
const QString kGroupAutomation = QStringLiteral("Automation");
const QString kGroupUi = QStringLiteral("Ui");

constexpr int kDefaultQuickPasteCount = 9;
constexpr int kDefaultDebounceMs = 250;
constexpr qint64 kDefaultMaxItemBytes = 5 * 1024 * 1024; // 5 MiB
constexpr qint64 kDefaultMaxImageBytes = 8 * 1024 * 1024; // 8 MiB
constexpr qint64 kDefaultDiskCapBytes = 0; // unlimited
constexpr int kDefaultOcrMaxChars = 8192;

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

qint64 SettingsManager::maxImageBytes() const
{
    // fallback to maxItemBytes for old configs
    if (!m_config->group(kGroupHistory).hasKey("MaxImageBytes"))
        return maxItemBytes();
    return m_config->group(kGroupHistory).readEntry<qint64>("MaxImageBytes", kDefaultMaxImageBytes);
}

void SettingsManager::setMaxImageBytes(qint64 bytes)
{
    m_config->group(kGroupHistory).writeEntry<qint64>("MaxImageBytes", qMax<qint64>(0, bytes));
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
        if (pat.contains(QLatin1Char('*'))) {
            QRegularExpression re(QRegularExpression::wildcardToRegularExpression(pat), QRegularExpression::CaseInsensitiveOption);
            if (re.match(app).hasMatch()) return true;
        }
    }
    return false;
}

QStringList SettingsManager::customSensitivePatterns() const
{
    return m_config->group(kGroupHistory).readEntry("CustomSensitivePatterns", QStringList());
}

void SettingsManager::setCustomSensitivePatterns(const QStringList &patterns)
{
    QStringList cleaned;
    for (QString p : patterns) {
        p = p.trimmed();
        if (!p.isEmpty()) cleaned << p;
    }
    m_config->group(kGroupHistory).writeEntry("CustomSensitivePatterns", cleaned);
    save();
}

bool SettingsManager::ocrEnabled() const
{
    return m_config->group(kGroupHistory).readEntry("OcrEnabled", true);
}

void SettingsManager::setOcrEnabled(bool enabled)
{
    m_config->group(kGroupHistory).writeEntry("OcrEnabled", enabled);
    save();
}

QString SettingsManager::ocrLanguage() const
{
    return m_config->group(kGroupOcr).readEntry("Language", QStringLiteral("eng"));
}

void SettingsManager::setOcrLanguage(const QString &lang)
{
    const QString v = lang.trimmed().isEmpty() ? QStringLiteral("eng") : lang.trimmed();
    m_config->group(kGroupOcr).writeEntry("Language", v);
    save();
}

int SettingsManager::ocrMaxChars() const
{
    return qBound(512, m_config->group(kGroupOcr).readEntry("MaxChars", kDefaultOcrMaxChars), 65536);
}

void SettingsManager::setOcrMaxChars(int chars)
{
    m_config->group(kGroupOcr).writeEntry("MaxChars", qBound(512, chars, 65536));
    save();
}

bool SettingsManager::previewCodeHighlight() const
{
    return m_config->group(kGroupPreview).readEntry("CodeHighlight", true);
}

void SettingsManager::setPreviewCodeHighlight(bool enabled)
{
    m_config->group(kGroupPreview).writeEntry("CodeHighlight", enabled);
    save();
}

bool SettingsManager::previewLinkify() const
{
    return m_config->group(kGroupPreview).readEntry("Linkify", true);
}

void SettingsManager::setPreviewLinkify(bool enabled)
{
    m_config->group(kGroupPreview).writeEntry("Linkify", enabled);
    save();
}

bool SettingsManager::previewColorSwatches() const
{
    return m_config->group(kGroupPreview).readEntry("ColorSwatches", true);
}

void SettingsManager::setPreviewColorSwatches(bool enabled)
{
    m_config->group(kGroupPreview).writeEntry("ColorSwatches", enabled);
    save();
}

QStringList SettingsManager::disabledScripts() const
{
    return m_config->group(kGroupAutomation).readEntry("DisabledScripts", QStringList());
}

void SettingsManager::setDisabledScripts(const QStringList &ids)
{
    m_config->group(kGroupAutomation).writeEntry("DisabledScripts", ids);
    save();
}

bool SettingsManager::isScriptDisabled(const QString &id) const
{
    return disabledScripts().contains(id, Qt::CaseSensitive);
}

void SettingsManager::setScriptDisabled(const QString &id, bool disabled)
{
    QStringList cur = disabledScripts();
    if (disabled) {
        if (!cur.contains(id)) cur << id;
    } else {
        cur.removeAll(id);
    }
    setDisabledScripts(cur);
}

QStringList SettingsManager::hiddenTransforms() const
{
    return m_config->group(kGroupAutomation).readEntry("HiddenTransforms", QStringList());
}

void SettingsManager::setHiddenTransforms(const QStringList &names)
{
    m_config->group(kGroupAutomation).writeEntry("HiddenTransforms", names);
    save();
}

bool SettingsManager::isTransformHidden(const QString &name) const
{
    return hiddenTransforms().contains(name, Qt::CaseInsensitive);
}

QString SettingsManager::trayMode() const
{
    const QString v = m_config->group(kGroupUi).readEntry("TrayMode", QStringLiteral("auto"));
    if (v == QLatin1String("always") || v == QLatin1String("hidden")) return v;
    return QStringLiteral("auto");
}

void SettingsManager::setTrayMode(const QString &mode)
{
    QString v = mode;
    if (v != QLatin1String("always") && v != QLatin1String("hidden")) v = QStringLiteral("auto");
    m_config->group(kGroupUi).writeEntry("TrayMode", v);
    save();
}

bool SettingsManager::notificationsEnabled() const
{
    return m_config->group(kGroupUi).readEntry("NotificationsEnabled", true);
}

void SettingsManager::setNotificationsEnabled(bool enabled)
{
    m_config->group(kGroupUi).writeEntry("NotificationsEnabled", enabled);
    save();
}

QString SettingsManager::theme() const
{
    const QString v = m_config->group(kGroupUi).readEntry("Theme", QStringLiteral("system"));
    if (v == QLatin1String("light") || v == QLatin1String("dark"))
        return v;
    return QStringLiteral("system");
}

void SettingsManager::setTheme(const QString &theme)
{
    QString v = theme;
    if (v != QLatin1String("light") && v != QLatin1String("dark"))
        v = QStringLiteral("system");
    m_config->group(kGroupUi).writeEntry("Theme", v);
    save();
}

bool SettingsManager::toolbarIconOnly() const
{
    return m_config->group(kGroupUi).readEntry("ToolbarIconOnly", false);
}

void SettingsManager::setToolbarIconOnly(bool iconOnly)
{
    m_config->group(kGroupUi).writeEntry("ToolbarIconOnly", iconOnly);
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
