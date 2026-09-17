#include "SettingsManager.h"

#include "ColorSchemeIndex.h"
#include "IconThemeIndex.h"
#include "SensitiveDataDetector.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
namespace {
const QString kGroupCapture = QStringLiteral("Capture");
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

namespace {
// The four capture-type switches live in group Capture and default to true.
bool readCaptureType(const KConfig *config, const char *key)
{
    return config->group(kGroupCapture).readEntry(key, true);
}
} // namespace

bool SettingsManager::captureText() const
{
    return readCaptureType(m_config, "CaptureText");
}

void SettingsManager::setCaptureText(bool enabled)
{
    m_config->group(kGroupCapture).writeEntry("CaptureText", enabled);
    save();
}

bool SettingsManager::captureRichText() const
{
    return readCaptureType(m_config, "CaptureRichText");
}

void SettingsManager::setCaptureRichText(bool enabled)
{
    m_config->group(kGroupCapture).writeEntry("CaptureRichText", enabled);
    save();
}

bool SettingsManager::captureImages() const
{
    return readCaptureType(m_config, "CaptureImages");
}

void SettingsManager::setCaptureImages(bool enabled)
{
    m_config->group(kGroupCapture).writeEntry("CaptureImages", enabled);
    save();
}

bool SettingsManager::captureFiles() const
{
    return readCaptureType(m_config, "CaptureFiles");
}

void SettingsManager::setCaptureFiles(bool enabled)
{
    m_config->group(kGroupCapture).writeEntry("CaptureFiles", enabled);
    save();
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
    case static_cast<int>(SensitiveMode::Redact):
        return SensitiveMode::Redact;
    default:
        return SensitiveMode::Exclude;
    }
}

void SettingsManager::setSensitiveMode(SensitiveMode mode)
{
    m_config->group(kGroupHistory).writeEntry("SensitiveMode", static_cast<int>(mode));
    save();
}

QStringList SettingsManager::redactKinds() const
{
    const QStringList stored = m_config->group(kGroupHistory).readEntry("RedactKinds", QStringList());
    if (stored.isEmpty())
        return SensitiveDataDetector::allKinds(); // empty stored list = redact everything
    return stored;
}

void SettingsManager::setRedactKinds(const QStringList &kinds)
{
    QStringList cleaned;
    cleaned.reserve(kinds.size());
    for (QString kind : kinds) {
        kind = kind.trimmed();
        if (!kind.isEmpty() && !cleaned.contains(kind))
            cleaned << kind;
    }
    m_config->group(kGroupHistory).writeEntry("RedactKinds", cleaned);
    save();
}

QList<ExpireRule> SettingsManager::expireRules() const
{
    return decodeRules(m_config->group(kGroupHistory).readEntry("ExpireRules", QStringList()));
}

void SettingsManager::setExpireRules(const QList<ExpireRule> &rules)
{
    m_config->group(kGroupHistory).writeEntry("ExpireRules", encodeRules(rules));
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

int SettingsManager::maxEntries() const
{
    return qMax(0, m_config->group(kGroupHistory).readEntry("MaxEntries", 0));
}

void SettingsManager::setMaxEntries(int maxEntries)
{
    m_config->group(kGroupHistory).writeEntry("MaxEntries", qMax(0, maxEntries));
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
        if (pat.contains(QLatin1Char('*')) || pat.contains(QLatin1Char('?'))) {
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
    // Theme ids are checked against the color schemes actually installed: a
    // scheme that was uninstalled since the last run collapses to "system".
    const QString v = m_config->group(kGroupUi).readEntry("Theme", QStringLiteral("system"));
    return ColorSchemeIndex::isValid(v) ? v : QStringLiteral("system");
}

void SettingsManager::setTheme(const QString &theme)
{
    const QString v = ColorSchemeIndex::isValid(theme) ? theme : QStringLiteral("system");
    m_config->group(kGroupUi).writeEntry("Theme", v);
    save();
}

QString SettingsManager::iconTheme() const
{
    // Same rule as the color scheme: only icon themes that are actually
    // installed stay selected, anything else follows the desktop again.
    const QString v = m_config->group(kGroupUi).readEntry("IconTheme", QStringLiteral("system"));
    return IconThemeIndex::isValid(v) ? v : QStringLiteral("system");
}

void SettingsManager::setIconTheme(const QString &theme)
{
    const QString v = IconThemeIndex::isValid(theme) ? theme : QStringLiteral("system");
    m_config->group(kGroupUi).writeEntry("IconTheme", v);
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

bool SettingsManager::timelineEnabled() const
{
    return m_config->group(kGroupUi).readEntry("TimelineEnabled", true);
}

void SettingsManager::setTimelineEnabled(bool enabled)
{
    m_config->group(kGroupUi).writeEntry("TimelineEnabled", enabled);
    save();
}

bool SettingsManager::closeAfterPaste() const
{
    return m_config->group(kGroupUi).readEntry("CloseAfterPaste", true);
}

void SettingsManager::setCloseAfterPaste(bool close)
{
    m_config->group(kGroupUi).writeEntry("CloseAfterPaste", close);
    save();
}

bool SettingsManager::bumpOnPaste() const
{
    return m_config->group(kGroupUi).readEntry("BumpOnPaste", true);
}

void SettingsManager::setBumpOnPaste(bool bump)
{
    m_config->group(kGroupUi).writeEntry("BumpOnPaste", bump);
    save();
}

bool SettingsManager::pasteAsPlainText() const
{
    return m_config->group(kGroupUi).readEntry("PasteAsPlainText", false);
}

void SettingsManager::setPasteAsPlainText(bool plain)
{
    m_config->group(kGroupUi).writeEntry("PasteAsPlainText", plain);
    save();
}

QString SettingsManager::listDensity() const
{
    const QString v = m_config->group(kGroupUi).readEntry("ListDensity", QStringLiteral("comfortable"));
    if (v == QLatin1String("compact") || v == QLatin1String("spacious"))
        return v;
    return QStringLiteral("comfortable");
}

void SettingsManager::setListDensity(const QString &density)
{
    QString v = density;
    if (v != QLatin1String("compact") && v != QLatin1String("spacious"))
        v = QStringLiteral("comfortable");
    m_config->group(kGroupUi).writeEntry("ListDensity", v);
    save();
}

int SettingsManager::sortMode() const
{
    const int v = m_config->group(kGroupUi).readEntry("SortMode", 0);
    return (v >= 0 && v <= 2) ? v : 0;
}

void SettingsManager::setSortMode(int mode)
{
    m_config->group(kGroupUi).writeEntry("SortMode", (mode >= 0 && mode <= 2) ? mode : 0);
    save();
}

QString SettingsManager::timestampStyle() const
{
    const QString v = m_config->group(kGroupUi).readEntry("TimestampStyle", QStringLiteral("relative"));
    if (v == QLatin1String("absolute"))
        return v;
    return QStringLiteral("relative");
}

void SettingsManager::setTimestampStyle(const QString &style)
{
    QString v = style;
    if (v != QLatin1String("absolute"))
        v = QStringLiteral("relative");
    m_config->group(kGroupUi).writeEntry("TimestampStyle", v);
    save();
}

bool SettingsManager::clock24h() const
{
    return m_config->group(kGroupUi).readEntry("Clock24h", true);
}

void SettingsManager::setClock24h(bool enable)
{
    m_config->group(kGroupUi).writeEntry("Clock24h", enable);
    save();
}

bool SettingsManager::rememberWindowGeometry() const
{
    return m_config->group(kGroupUi).readEntry("RememberWindowGeometry", true);
}

void SettingsManager::setRememberWindowGeometry(bool remember)
{
    m_config->group(kGroupUi).writeEntry("RememberWindowGeometry", remember);
    save();
}

bool SettingsManager::restoreLastFilter() const
{
    return m_config->group(kGroupUi).readEntry("RestoreLastFilter", false);
}

void SettingsManager::setRestoreLastFilter(bool restore)
{
    m_config->group(kGroupUi).writeEntry("RestoreLastFilter", restore);
    save();
}

QByteArray SettingsManager::windowGeometry() const
{
    return m_config->group(kGroupUi).readEntry("WindowGeometry", QByteArray());
}

void SettingsManager::setWindowGeometry(const QByteArray &geometry)
{
    m_config->group(kGroupUi).writeEntry("WindowGeometry", geometry);
    save();
}

QByteArray SettingsManager::splitterState() const
{
    return m_config->group(kGroupUi).readEntry("SplitterState", QByteArray());
}

void SettingsManager::setSplitterState(const QByteArray &state)
{
    m_config->group(kGroupUi).writeEntry("SplitterState", state);
    save();
}

QString SettingsManager::lastFilter() const
{
    return m_config->group(kGroupUi).readEntry("LastFilter", QString());
}

void SettingsManager::setLastFilter(const QString &filterJson)
{
    m_config->group(kGroupUi).writeEntry("LastFilter", filterJson);
    save();
}

bool SettingsManager::encryptionEnabled() const
{
    return m_config->group(kGroupHistory).readEntry("EncryptionEnabled", false);
}

void SettingsManager::setEncryptionEnabled(bool enabled)
{
    m_config->group(kGroupHistory).writeEntry("EncryptionEnabled", enabled);
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
