#include "SettingsManager.h"

#include "ColorSchemeIndex.h"
#include "IconThemeIndex.h"
#include "SensitiveDataDetector.h"

#include <KConfig>
#include <KConfigGroup>

#include <QCoreApplication>
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
const QString kGroupBackups = QStringLiteral("Backups");

constexpr int kDefaultQuickPasteCount = 9;
constexpr int kDefaultDebounceMs = 250;
// Automatic backups: keep a week of daily files by default.
constexpr int kDefaultBackupKeep = 7;
constexpr int kMaxBackupKeep = 100;
// Text size can be nudged up for readability, but not far enough to wreck the
// carefully sized list rows and toolbar.
constexpr int kMinFontPointDelta = -2;
constexpr int kMaxFontPointDelta = 6;
constexpr qint64 kDefaultMaxItemBytes = 5 * 1024 * 1024; // 5 MiB
constexpr qint64 kDefaultMaxImageBytes = 8 * 1024 * 1024; // 8 MiB
constexpr qint64 kDefaultDiskCapBytes = 0; // unlimited
constexpr int kDefaultOcrMaxChars = 8192;
// Search box history: how many committed queries are kept.
constexpr int kMaxRecentSearches = 10;
// Bump when a migration step is added below; the file records this number.
constexpr int kCurrentConfigVersion = 1;

constexpr SettingsManager::SensitiveMode kDefaultSensitiveMode =
    SettingsManager::SensitiveMode::Exclude;

// Colors are stored as "#rrggbb". Anything unparsable reads back as "follow the
// color scheme", so a hand-edited config cannot blank out the UI text.
QString normalizedColor(const QString &value)
{
    const QColor color = QColor::fromString(value);
    return color.isValid() ? color.name(QColor::HexRgb) : QString();
}

// Desktop-entry argument quoting: Exec= is parsed without a shell, so quotes,
// backslashes and the like inside the path have to be escaped. The argument is
// always quoted, which the parser (and the systemd autostart generator) accepts.
QString desktopExecQuoted(const QString &path)
{
    QString escaped;
    escaped.reserve(path.size() + 2);
    escaped += QLatin1Char('"');
    for (const QChar character : path) {
        if (character == QLatin1Char('"') || character == QLatin1Char('\\')
            || character == QLatin1Char('$') || character == QLatin1Char('`')) {
            escaped += QLatin1Char('\\');
        }
        escaped += character;
    }
    escaped += QLatin1Char('"');
    return escaped;
}

// The autostart entry must name the executable: a bare "egoboard" is resolved
// against the PATH of whoever reads the file, and the systemd xdg-autostart
// generator runs with a minimal environment - it then skips the entry with
// "executable specified in Exec= does not exist" and the app never starts.
QByteArray autostartEntryContents(const QString &executable)
{
    QByteArray entry;
    entry += "[Desktop Entry]\n";
    entry += "Type=Application\n";
    entry += "Name=Egoboard\n";
    entry += "Comment=Clipboard history manager\n";
    entry += "Exec=" + desktopExecQuoted(executable).toUtf8() + "\n";
    entry += "Icon=egoboard\n";
    entry += "Terminal=false\n";
    entry += "X-KDE-autostart-phase=2\n";
    entry += "X-GNOME-Autostart-enabled=true\n";
    return entry;
}
} // namespace

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
    , m_config(new KConfig(QStringLiteral("egoboardrc"), KConfig::NoGlobals))
{
    migrateConfig();
}

int SettingsManager::currentConfigVersion()
{
    return kCurrentConfigVersion;
}

int SettingsManager::configVersion() const
{
    return m_config->group(kGroupGeneral).readEntry("ConfigVersion", 0);
}

void SettingsManager::migrateConfig()
{
    KConfigGroup general = m_config->group(kGroupGeneral);
    const int stored = general.readEntry("ConfigVersion", 0);
    if (stored >= kCurrentConfigVersion)
        return; // current, or written by a newer build (never downgrade)

    if (stored < 1) {
        // v0 -> v1: values written before the version existed were never
        // validated, so bring them into the ranges the UI accepts.
        KConfigGroup history = m_config->group(kGroupHistory);
        const int debounce = history.readEntry("DebounceMs", kDefaultDebounceMs);
        if (debounce < 50 || debounce > 5000)
            history.writeEntry("DebounceMs", qBound(50, debounce, 5000));
        const char *const byteCaps[] = {"MaxItemBytes", "MaxImageBytes", "DiskCapBytes", "MaxEntries"};
        for (const char *key : byteCaps) {
            const qint64 value = history.readEntry<qint64>(key, qint64(0));
            if (value < 0)
                history.writeEntry<qint64>(key, qint64(0));
        }
        const int quickPaste = general.readEntry("QuickPasteCount", kDefaultQuickPasteCount);
        if (quickPaste < 1 || quickPaste > 9)
            general.writeEntry("QuickPasteCount", qBound(1, quickPaste, 9));
        KConfigGroup ocr = m_config->group(kGroupOcr);
        const int maxChars = ocr.readEntry("MaxChars", kDefaultOcrMaxChars);
        if (maxChars < 512 || maxChars > 65536)
            ocr.writeEntry("MaxChars", qBound(512, maxChars, 65536));
    }

    general.writeEntry("ConfigVersion", kCurrentConfigVersion);
    m_config->sync();
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

namespace {
// Reads a TrayClick from the config, falling back to `fallback` for values that
// are out of range (a hand-edited or future config).
SettingsManager::TrayClick readTrayClick(const KConfigGroup &group, const char *key,
                                         SettingsManager::TrayClick fallback)
{
    const int value = group.readEntry(key, int(fallback));
    if (value < int(SettingsManager::TrayClick::ShowWindow)
        || value > int(SettingsManager::TrayClick::Nothing))
        return fallback;
    return static_cast<SettingsManager::TrayClick>(value);
}
} // namespace

bool SettingsManager::pauseOnLock() const
{
    return m_config->group(kGroupCapture).readEntry("PauseOnLock", true);
}

void SettingsManager::setPauseOnLock(bool pause)
{
    m_config->group(kGroupCapture).writeEntry("PauseOnLock", pause);
    save();
}

SettingsManager::TrayClick SettingsManager::trayPrimaryClick() const
{
    // A left click has always toggled the window: keep that as the default.
    return readTrayClick(m_config->group(kGroupUi), "TrayPrimaryClick", TrayClick::ShowWindow);
}

void SettingsManager::setTrayPrimaryClick(TrayClick action)
{
    m_config->group(kGroupUi).writeEntry("TrayPrimaryClick", int(action));
    save();
}

SettingsManager::TrayClick SettingsManager::traySecondaryClick() const
{
    // Middle click opened quick paste before this setting existed.
    return readTrayClick(m_config->group(kGroupUi), "TraySecondaryClick", TrayClick::QuickPaste);
}

void SettingsManager::setTraySecondaryClick(TrayClick action)
{
    m_config->group(kGroupUi).writeEntry("TraySecondaryClick", int(action));
    save();
}

bool SettingsManager::trayWheelCycles() const
{
    return m_config->group(kGroupUi).readEntry("TrayWheelCycles", true);
}

void SettingsManager::setTrayWheelCycles(bool enabled)
{
    m_config->group(kGroupUi).writeEntry("TrayWheelCycles", enabled);
    save();
}

bool SettingsManager::captureTypeEnabled(ContentType type) const
{
    switch (type) {
    case ContentType::Text: return captureText();
    case ContentType::RichText: return captureRichText();
    case ContentType::Image: return captureImages();
    case ContentType::Files: return captureFiles();
    }
    return true;
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

bool SettingsManager::quickPasteTwoLine() const
{
    return m_config->group(kGroupUi).readEntry("QuickPasteTwoLine", false);
}

void SettingsManager::setQuickPasteTwoLine(bool enabled)
{
    m_config->group(kGroupUi).writeEntry("QuickPasteTwoLine", enabled);
    save();
}

QPoint SettingsManager::quickPastePos(const QString &screen) const
{
    if (screen.isEmpty())
        return QPoint();
    const QStringList parts =
        m_config->group(kGroupUi).readEntry(QStringLiteral("QuickPastePos_") + screen,
                                            QStringList());
    if (parts.size() != 2)
        return QPoint();
    bool okX = false, okY = false;
    const int x = parts.at(0).toInt(&okX);
    const int y = parts.at(1).toInt(&okY);
    if (!okX || !okY)
        return QPoint(-1, -1); // sentinel: stored but unparsable, never (0,0)
    return QPoint(x, y);
}

void SettingsManager::setQuickPastePos(const QString &screen, const QPoint &pos)
{
    if (screen.isEmpty())
        return;
    m_config->group(kGroupUi).writeEntry(QStringLiteral("QuickPastePos_") + screen,
                                         QStringList{QString::number(pos.x()),
                                                     QString::number(pos.y())});
    save();
}

void SettingsManager::clearQuickPastePos(const QString &screen)
{
    if (screen.isEmpty())
        return;
    m_config->group(kGroupUi).deleteEntry(QStringLiteral("QuickPastePos_") + screen);
    save();
}

bool SettingsManager::autostartEnabled() const
{
    return m_config->group(kGroupGeneral).readEntry("Autostart", false);
}

QString SettingsManager::autostartExecutablePath()
{
    // An AppImage mounts at a path that changes on every launch, so the entry
    // has to point at the .AppImage file itself.
    const QByteArray appImage = qgetenv("APPIMAGE");
    if (!appImage.isEmpty())
        return QString::fromLocal8Bit(appImage);
    return QCoreApplication::applicationFilePath();
}

void SettingsManager::setAutostartEnabled(bool enabled)
{
    m_config->group(kGroupGeneral).writeEntry("Autostart", enabled);

    const QString path = autostartDesktopFilePath();
    if (enabled) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(autostartEntryContents(effectiveAutostartCommand()));
    } else {
        QFile::remove(path);
    }
    save();
}

void SettingsManager::ensureAutostartEntry()
{
    if (!autostartEnabled())
        return;

    const QString path = autostartDesktopFilePath();
    QFile file(path);
    const QByteArray expected = autostartEntryContents(effectiveAutostartCommand());

    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        const bool upToDate = file.readAll() == expected;
        file.close();
        if (upToDate)
            return; // already points at the right executable
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(expected);
}

QString SettingsManager::autostartCommand() const
{
    const QString stored = m_config->group(kGroupGeneral).readEntry("AutostartCommand", QString());
    if (stored.isEmpty())
        return QString();
    return QFileInfo(stored).absoluteFilePath();
}

void SettingsManager::setAutostartCommand(const QString &path)
{
    const QString normalized = path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
    m_config->group(kGroupGeneral).writeEntry("AutostartCommand", normalized);
    // Keep the entry in step with the choice right away, not just on next start.
    ensureAutostartEntry();
    save();
}

QString SettingsManager::effectiveAutostartCommand() const
{
    const QString chosen = autostartCommand();
    if (!chosen.isEmpty()) {
        const QFileInfo info(chosen);
        if (info.isFile() && info.isExecutable())
            return chosen;
        // The chosen file moved away: fall back to the running binary rather than
        // writing an entry the session would silently skip at login.
    }
    return autostartExecutablePath();
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

bool SettingsManager::captureSoundEnabled() const
{
    return m_config->group(kGroupUi).readEntry("CaptureSound", true);
}

void SettingsManager::setCaptureSoundEnabled(bool enabled)
{
    m_config->group(kGroupUi).writeEntry("CaptureSound", enabled);
    save();
}

bool SettingsManager::captureNotificationEnabled() const
{
    return m_config->group(kGroupUi).readEntry("CaptureNotification", true);
}

void SettingsManager::setCaptureNotificationEnabled(bool enabled)
{
    m_config->group(kGroupUi).writeEntry("CaptureNotification", enabled);
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

int SettingsManager::fontPointDelta() const
{
    return qBound(kMinFontPointDelta, m_config->group(kGroupUi).readEntry("FontPointDelta", 0),
                  kMaxFontPointDelta);
}

void SettingsManager::setFontPointDelta(int delta)
{
    m_config->group(kGroupUi).writeEntry("FontPointDelta",
                                         qBound(kMinFontPointDelta, delta, kMaxFontPointDelta));
    save();
}

QString SettingsManager::textColor() const
{
    return normalizedColor(m_config->group(kGroupUi).readEntry("TextColor", QString()));
}

void SettingsManager::setTextColor(const QString &color)
{
    m_config->group(kGroupUi).writeEntry("TextColor", normalizedColor(color));
    save();
}

QString SettingsManager::dimTextColor() const
{
    return normalizedColor(m_config->group(kGroupUi).readEntry("DimTextColor", QString()));
}

void SettingsManager::setDimTextColor(const QString &color)
{
    m_config->group(kGroupUi).writeEntry("DimTextColor", normalizedColor(color));
    save();
}

TextAppearance::Overrides SettingsManager::textAppearance() const
{
    TextAppearance::Overrides overrides;
    overrides.fontPointDelta = fontPointDelta();
    overrides.textColor = QColor::fromString(textColor());
    overrides.customText = overrides.textColor.isValid();
    overrides.dimTextColor = QColor::fromString(dimTextColor());
    overrides.customDimText = overrides.dimTextColor.isValid();
    return overrides;
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

bool SettingsManager::reduceMotion() const
{
    return m_config->group(kGroupUi).readEntry("ReduceMotion", false);
}

void SettingsManager::setReduceMotion(bool reduce)
{
    m_config->group(kGroupUi).writeEntry("ReduceMotion", reduce);
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

bool SettingsManager::groupByDay() const
{
    return m_config->group(kGroupUi).readEntry("GroupByDay", false);
}

void SettingsManager::setGroupByDay(bool enabled)
{
    m_config->group(kGroupUi).writeEntry("GroupByDay", enabled);
    save();
}

bool SettingsManager::showEntryIndex() const
{
    return m_config->group(kGroupUi).readEntry("ShowEntryIndex", false);
}

void SettingsManager::setShowEntryIndex(bool show)
{
    m_config->group(kGroupUi).writeEntry("ShowEntryIndex", show);
    save();
}

bool SettingsManager::showUseCountBadge() const
{
    return m_config->group(kGroupUi).readEntry("ShowUseCountBadge", false);
}

void SettingsManager::setShowUseCountBadge(bool show)
{
    m_config->group(kGroupUi).writeEntry("ShowUseCountBadge", show);
    save();
}

bool SettingsManager::privacyBlur() const
{
    return m_config->group(kGroupUi).readEntry("PrivacyBlur", false);
}

void SettingsManager::setPrivacyBlur(bool blur)
{
    m_config->group(kGroupUi).writeEntry("PrivacyBlur", blur);
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

int SettingsManager::searchScope() const
{
    const int v = m_config->group(kGroupUi).readEntry("SearchScope", 0);
    return (v >= 0 && v <= 3) ? v : 0;
}

void SettingsManager::setSearchScope(int scope)
{
    m_config->group(kGroupUi).writeEntry("SearchScope", (scope >= 0 && scope <= 3) ? scope : 0);
    save();
}

QStringList SettingsManager::recentSearches() const
{
    QStringList list = m_config->group(kGroupUi).readEntry("RecentSearches", QStringList());
    if (list.size() > kMaxRecentSearches)
        list = list.mid(0, kMaxRecentSearches);
    return list;
}

void SettingsManager::addRecentSearch(const QString &query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty())
        return;
    QStringList list = recentSearches();
    list.removeAll(trimmed);
    list.prepend(trimmed);
    if (list.size() > kMaxRecentSearches)
        list = list.mid(0, kMaxRecentSearches);
    m_config->group(kGroupUi).writeEntry("RecentSearches", list);
    save();
}

void SettingsManager::clearRecentSearches()
{
    m_config->group(kGroupUi).writeEntry("RecentSearches", QStringList());
    save();
}

QStringList SettingsManager::recentPaletteCommands() const
{
    QStringList list = m_config->group(kGroupUi).readEntry("RecentPaletteCommands", QStringList());
    if (list.size() > kMaxRecentSearches)
        list = list.mid(0, kMaxRecentSearches);
    return list;
}

void SettingsManager::addRecentPaletteCommand(const QString &commandId)
{
    const QString trimmed = commandId.trimmed();
    if (trimmed.isEmpty())
        return;
    QStringList list = recentPaletteCommands();
    list.removeAll(trimmed);
    list.prepend(trimmed);
    if (list.size() > kMaxRecentSearches)
        list = list.mid(0, kMaxRecentSearches);
    m_config->group(kGroupUi).writeEntry("RecentPaletteCommands", list);
    save();
}

bool SettingsManager::backupsEnabled() const
{
    return m_config->group(kGroupBackups).readEntry("Enabled", false);
}

void SettingsManager::setBackupsEnabled(bool enabled)
{
    m_config->group(kGroupBackups).writeEntry("Enabled", enabled);
    save();
}

QString SettingsManager::backupFolder() const
{
    return m_config->group(kGroupBackups).readEntry("Folder", QString());
}

void SettingsManager::setBackupFolder(const QString &folder)
{
    m_config->group(kGroupBackups).writeEntry("Folder", folder.trimmed());
    save();
}

int SettingsManager::backupKeep() const
{
    const int keep = m_config->group(kGroupBackups).readEntry("Keep", kDefaultBackupKeep);
    return (keep >= 1 && keep <= kMaxBackupKeep) ? keep : kDefaultBackupKeep;
}

void SettingsManager::setBackupKeep(int keep)
{
    m_config->group(kGroupBackups).writeEntry("Keep", qBound(1, keep, kMaxBackupKeep));
    save();
}

qint64 SettingsManager::lastBackupMs() const
{
    return m_config->group(kGroupBackups).readEntry<qint64>("LastRunMs", qint64(0));
}

void SettingsManager::setLastBackupMs(qint64 ms)
{
    m_config->group(kGroupBackups).writeEntry<qint64>("LastRunMs", ms);
    save();
}

QString SettingsManager::defaultBackupFolder()
{
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (!documents.isEmpty())
        return documents + QStringLiteral("/egoboard-backups");
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/backups");
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

QByteArray SettingsManager::splitterStateForMode(int mode) const
{
    if (mode == 0)
        return splitterState();
    return m_config->group(kGroupUi).readEntry(
        QStringLiteral("SplitterStateMode%1").arg(mode), QByteArray());
}

void SettingsManager::setSplitterStateForMode(int mode, const QByteArray &state)
{
    if (mode == 0) {
        setSplitterState(state);
        return;
    }
    m_config->group(kGroupUi).writeEntry(
        QStringLiteral("SplitterStateMode%1").arg(mode), state);
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
