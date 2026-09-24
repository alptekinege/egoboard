#include <QtTest>

#include "ColorSchemeIndex.h"
#include "IconThemeIndex.h"
#include "SensitiveDataDetector.h"
#include "SettingsManager.h"
#include "SystemThemeWatcher.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVector>

namespace {
// Restores XDG_CONFIG_HOME when a slot switches homes mid-test (QVERIFY
// aborts the slot on failure, so plain cleanup at the end would not run).
struct XdgConfigHomeRestore {
    explicit XdgConfigHomeRestore(const QString &home)
        : previous(qgetenv("XDG_CONFIG_HOME"))
    {
        qputenv("XDG_CONFIG_HOME", home.toUtf8());
    }
    ~XdgConfigHomeRestore() { qputenv("XDG_CONFIG_HOME", previous); }
    QByteArray previous;
};
} // namespace

class TestSettings : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void defaultValues();
    void mutateGeneralSettingsAndSignal();
    void captureTypesAndRetention();
    void pasteBehaviorAndDensity();
    void sortTimestampsAndGeometry();
    void sensitiveModeAndRedactKinds();
    void expireRulesPersistence();
    void ignoredSourceAppsWildcardMatching();
    void boundsAndLimits();
    void normalizesCollectionsAndUiValues();
    void themeIdsValidateAgainstInstalledSchemes();
    void iconThemeIdsValidateAgainstInstalledThemes();
    void themeChangeNotificationsAreFiltered();
    void detectsPlasmaConfigChange();
    void textReadabilitySettings();
    void autostartEntryPointsAtThisBinary();
    void autostartCommandCanPointAtAnAppImage();
    void persistsAcrossInstances();
    void searchScopeAndRecentsPersist();
    void recentPaletteCommandsPersist();
    void trayBehaviourPersists();
    void pauseOnLockSettingPersists();
    void backupSettingsPersist();
    void listDisplayOptionsPersist();
    void quickPasteOptionsPersist();
    void configMigrationsAreForwardOnly();
    void settingsJsonRoundTrip();
    void settingsJsonIgnoresUnknownAndKeepsMissing();
    void settingsJsonRejectsBadFiles();
    void settingsJsonExcludesBackupSchedule();
    void settingsJsonPositionsRoundTrip();
    void settingsJsonImportEmitsSingleChanged();
    void resetGeneralRestoresDefaults();
    void resetCaptureRestoresDefaults();
    void resetPrivacyHistorySearchAutomationStorage();
    void resetPageEmitsSingleChangedAndPreservesSessionState();
    void profileSaveApplyDeleteRoundTrip();
    void profileRejectsBadNamesAndUnknownApply();
    void profileApplyEmitsSingleChanged();

private:
    QTemporaryDir m_tempDir;
};

void TestSettings::initTestCase()
{
    // Point XDG_CONFIG_HOME to isolated temporary dir
    qputenv("XDG_CONFIG_HOME", m_tempDir.path().toUtf8());
}

void TestSettings::defaultValues()
{
    SettingsManager settings;
    QCOMPARE(settings.startVisible(), false);
    QCOMPARE(settings.hideOnFocusOut(), false);
    QCOMPARE(settings.monitorPrimarySelection(), false);
    QCOMPARE(settings.quickPasteCount(), 9);
    QCOMPARE(settings.debounceMs(), 250);
    QCOMPARE(settings.sensitiveMode(), SettingsManager::SensitiveMode::Exclude);
    QCOMPARE(settings.maxItemBytes(), qint64(5 * 1024 * 1024));
    QCOMPARE(settings.maxImageBytes(), settings.maxItemBytes());
    QCOMPARE(settings.diskCapBytes(), qint64(0));
    QCOMPARE(settings.maxEntries(), 0);
    QCOMPARE(settings.theme(), QStringLiteral("system"));
    QCOMPARE(settings.iconTheme(), QStringLiteral("system"));
    QCOMPARE(settings.toolbarIconOnly(), false);
    QCOMPARE(settings.timelineEnabled(), true);
    QCOMPARE(settings.closeAfterPaste(), true);
    QCOMPARE(settings.bumpOnPaste(), true);
    QCOMPARE(settings.pasteAsPlainText(), false);
    QCOMPARE(settings.listDensity(), QStringLiteral("comfortable"));
    QCOMPARE(settings.captureText(), true);
    QCOMPARE(settings.captureRichText(), true);
    QCOMPARE(settings.captureImages(), true);
    QCOMPARE(settings.captureFiles(), true);
}

void TestSettings::mutateGeneralSettingsAndSignal()
{
    SettingsManager settings;
    QSignalSpy changedSpy(&settings, &SettingsManager::changed);

    settings.setStartVisible(true);
    QCOMPARE(settings.startVisible(), true);
    QCOMPARE(changedSpy.count(), 1);

    settings.setHideOnFocusOut(true);
    QCOMPARE(settings.hideOnFocusOut(), true);
    QCOMPARE(changedSpy.count(), 2);

    settings.setQuickPasteCount(5);
    QCOMPARE(settings.quickPasteCount(), 5);
    QCOMPARE(changedSpy.count(), 3);

    settings.setDebounceMs(500);
    QCOMPARE(settings.debounceMs(), 500);
    QCOMPARE(changedSpy.count(), 4);

    settings.setTheme(QStringLiteral("dark"));
    QCOMPARE(settings.theme(), QStringLiteral("dark"));

    settings.setToolbarIconOnly(true);
    QCOMPARE(settings.toolbarIconOnly(), true);
}

void TestSettings::captureTypesAndRetention()
{
    SettingsManager settings;
    QSignalSpy changedSpy(&settings, &SettingsManager::changed);

    // All four capture types can be switched off independently.
    settings.setCaptureText(false);
    settings.setCaptureRichText(false);
    settings.setCaptureImages(false);
    settings.setCaptureFiles(false);
    QCOMPARE(settings.captureText(), false);
    QCOMPARE(settings.captureRichText(), false);
    QCOMPARE(settings.captureImages(), false);
    QCOMPARE(settings.captureFiles(), false);
    QCOMPARE(changedSpy.count(), 4);

    settings.setCaptureText(true);
    QCOMPARE(settings.captureText(), true);
    QCOMPARE(changedSpy.count(), 5);

    // Negative entry caps collapse to "unlimited".
    settings.setMaxEntries(-10);
    QCOMPARE(settings.maxEntries(), 0);
    settings.setMaxEntries(5000);
    QCOMPARE(settings.maxEntries(), 5000);
    QCOMPARE(changedSpy.count(), 7);

    settings.setTimelineEnabled(false);
    QCOMPARE(settings.timelineEnabled(), false);
}

void TestSettings::pasteBehaviorAndDensity()
{
    SettingsManager settings;
    QSignalSpy changedSpy(&settings, &SettingsManager::changed);

    settings.setCloseAfterPaste(false);
    QCOMPARE(settings.closeAfterPaste(), false);
    settings.setBumpOnPaste(false);
    QCOMPARE(settings.bumpOnPaste(), false);
    settings.setPasteAsPlainText(true);
    QCOMPARE(settings.pasteAsPlainText(), true);
    QCOMPARE(changedSpy.count(), 3);

    // Unknown densities normalize to "comfortable"; the valid set passes through.
    settings.setListDensity(QStringLiteral("ridiculous"));
    QCOMPARE(settings.listDensity(), QStringLiteral("comfortable"));
    settings.setListDensity(QStringLiteral("compact"));
    QCOMPARE(settings.listDensity(), QStringLiteral("compact"));
    settings.setListDensity(QStringLiteral("spacious"));
    QCOMPARE(settings.listDensity(), QStringLiteral("spacious"));
    QCOMPARE(changedSpy.count(), 6);
}

void TestSettings::sortTimestampsAndGeometry()
{
    SettingsManager settings;
    QSignalSpy changedSpy(&settings, &SettingsManager::changed);

    // Sort mode is clamped into 0..2, invalid values normalize to 0.
    settings.setSortMode(2);
    QCOMPARE(settings.sortMode(), 2);
    settings.setSortMode(-3);
    QCOMPARE(settings.sortMode(), 0);
    settings.setSortMode(9);
    QCOMPARE(settings.sortMode(), 0);
    settings.setSortMode(1);
    QCOMPARE(settings.sortMode(), 1);
    QCOMPARE(changedSpy.count(), 4);

    // Timestamp style normalizes like the density strings; clock defaults on.
    settings.setTimestampStyle(QStringLiteral("bogus"));
    QCOMPARE(settings.timestampStyle(), QStringLiteral("relative"));
    settings.setTimestampStyle(QStringLiteral("absolute"));
    QCOMPARE(settings.timestampStyle(), QStringLiteral("absolute"));
    QCOMPARE(settings.clock24h(), true);
    settings.setClock24h(false);
    QCOMPARE(settings.clock24h(), false);

    // Geometry blobs and the serialized last filter round-trip verbatim.
    const QByteArray geometry = QByteArrayLiteral("geometry-blob");
    const QByteArray splitter = QByteArrayLiteral("splitter-blob");
    settings.setWindowGeometry(geometry);
    settings.setSplitterState(splitter);
    settings.setLastFilter(QStringLiteral("{\"searchText\":\"hi\"}"));
    QCOMPARE(settings.windowGeometry(), geometry);
    QCOMPARE(settings.splitterState(), splitter);
    QCOMPARE(settings.lastFilter(), QStringLiteral("{\"searchText\":\"hi\"}"));

    // Per-mode splitter state (R1): Wide aliases the legacy key, Medium and
    // Narrow persist their own blobs independently.
    const QByteArray wide = QByteArrayLiteral("wide-blob");
    const QByteArray medium = QByteArrayLiteral("medium-blob");
    const QByteArray narrow = QByteArrayLiteral("narrow-blob");
    settings.setSplitterStateForMode(0, wide);
    QCOMPARE(settings.splitterState(), wide);
    QCOMPARE(settings.splitterStateForMode(0), wide);
    QVERIFY(settings.splitterStateForMode(1).isEmpty());
    QVERIFY(settings.splitterStateForMode(2).isEmpty());
    settings.setSplitterStateForMode(1, medium);
    settings.setSplitterStateForMode(2, narrow);
    QCOMPARE(settings.splitterStateForMode(0), wide);
    QCOMPARE(settings.splitterStateForMode(1), medium);
    QCOMPARE(settings.splitterStateForMode(2), narrow);
    // The legacy key still tracks Wide only.
    QCOMPARE(settings.splitterState(), wide);
}

void TestSettings::sensitiveModeAndRedactKinds()
{
    SettingsManager settings;
    settings.setSensitiveMode(SettingsManager::SensitiveMode::Redact);
    QCOMPARE(settings.sensitiveMode(), SettingsManager::SensitiveMode::Redact);

    const QStringList kinds = {QStringLiteral("creditcard"), QStringLiteral("aws-key"), QStringLiteral("jwt")};
    settings.setRedactKinds(kinds);
    QCOMPARE(settings.redactKinds(), kinds);

    settings.setSensitiveMode(SettingsManager::SensitiveMode::Mark);
    QCOMPARE(settings.sensitiveMode(), SettingsManager::SensitiveMode::Mark);
}

void TestSettings::expireRulesPersistence()
{
    SettingsManager settings;

    QList<ExpireRule> rules;
    ExpireRule r1;
    r1.contentType = int(ContentType::Text);
    r1.sourceAppWildcard = QStringLiteral("terminal*");
    r1.ageSeconds = 86400;
    r1.keepPinned = true;
    rules.append(r1);

    ExpireRule r2;
    r2.contentType = -1; // any
    r2.sourceAppWildcard = QStringLiteral("temp*");
    r2.ageSeconds = 3600;
    r2.keepPinned = false;
    rules.append(r2);

    settings.setExpireRules(rules);

    const QList<ExpireRule> loaded = settings.expireRules();
    QCOMPARE(loaded.size(), 2);

    QCOMPARE(loaded.at(0).contentType, int(ContentType::Text));
    QCOMPARE(loaded.at(0).sourceAppWildcard, QStringLiteral("terminal*"));
    QCOMPARE(loaded.at(0).ageSeconds, qint64(86400));
    QCOMPARE(loaded.at(0).keepPinned, true);

    QCOMPARE(loaded.at(1).contentType, -1);
    QCOMPARE(loaded.at(1).sourceAppWildcard, QStringLiteral("temp*"));
    QCOMPARE(loaded.at(1).ageSeconds, qint64(3600));
    QCOMPARE(loaded.at(1).keepPinned, false);
}

void TestSettings::ignoredSourceAppsWildcardMatching()
{
    SettingsManager settings;
    const QStringList ignored = {
        QStringLiteral("keepassxc"),
        QStringLiteral("1password*"),
        QStringLiteral("secret-?-tool")
    };
    settings.setIgnoredSourceApps(ignored);
    QCOMPARE(settings.ignoredSourceApps(), ignored);

    // Exact match
    QVERIFY(settings.isSourceIgnored(QStringLiteral("keepassxc")));
    QVERIFY(settings.isSourceIgnored(QStringLiteral("KeePassXC"))); // case-insensitive

    // Asterisk wildcard match
    QVERIFY(settings.isSourceIgnored(QStringLiteral("1password")));
    QVERIFY(settings.isSourceIgnored(QStringLiteral("1password-gui")));

    // Question mark wildcard match
    QVERIFY(settings.isSourceIgnored(QStringLiteral("secret-1-tool")));
    QVERIFY(!settings.isSourceIgnored(QStringLiteral("secret-12-tool")));

    // Unmatched
    QVERIFY(!settings.isSourceIgnored(QStringLiteral("firefox")));
    QVERIFY(!settings.isSourceIgnored(QStringLiteral("kate")));
}

void TestSettings::boundsAndLimits()
{
    SettingsManager settings;

    // quickPasteCount is clamped between 1 and 9
    settings.setQuickPasteCount(0);
    QCOMPARE(settings.quickPasteCount(), 1);
    settings.setQuickPasteCount(25);
    QCOMPARE(settings.quickPasteCount(), 9);

    // debounceMs is clamped >= 50
    settings.setDebounceMs(10);
    QCOMPARE(settings.debounceMs(), 50);

    // disk cap and item sizes
    settings.setDiskCapBytes(100 * 1024 * 1024);
    QCOMPARE(settings.diskCapBytes(), qint64(100 * 1024 * 1024));

    settings.setMaxItemBytes(2 * 1024 * 1024);
    QCOMPARE(settings.maxItemBytes(), qint64(2 * 1024 * 1024));

    settings.setMaxImageBytes(16 * 1024 * 1024);
    QCOMPARE(settings.maxImageBytes(), qint64(16 * 1024 * 1024));
}

void TestSettings::normalizesCollectionsAndUiValues()
{
    SettingsManager settings;

    settings.setRedactKinds({QStringLiteral(" aws-key "), QString(), QStringLiteral("aws-key"),
                              QStringLiteral(" jwt ")});
    const QStringList expectedRedactKinds = {QStringLiteral("aws-key"), QStringLiteral("jwt")};
    QCOMPARE(settings.redactKinds(), expectedRedactKinds);

    settings.setIgnoredSourceApps({QStringLiteral(" firefox* "), QString(), QStringLiteral("firefox*"),
                                   QStringLiteral(" kate ")});
    const QStringList expectedIgnoredApps = {QStringLiteral("firefox*"), QStringLiteral("kate")};
    QCOMPARE(settings.ignoredSourceApps(), expectedIgnoredApps);

    settings.setCustomSensitivePatterns({QStringLiteral(" password "), QString(), QStringLiteral(" token ")});
    const QStringList expectedPatterns = {QStringLiteral("password"), QStringLiteral("token")};
    QCOMPARE(settings.customSensitivePatterns(), expectedPatterns);

    settings.setTrayMode(QStringLiteral("invalid"));
    QCOMPARE(settings.trayMode(), QStringLiteral("auto"));
    settings.setTrayMode(QStringLiteral("hidden"));
    QCOMPARE(settings.trayMode(), QStringLiteral("hidden"));

    settings.setTheme(QStringLiteral("invalid"));
    QCOMPARE(settings.theme(), QStringLiteral("system"));
    settings.setTheme(QStringLiteral("light"));
    QCOMPARE(settings.theme(), QStringLiteral("light"));

    settings.setOcrLanguage(QStringLiteral("   "));
    QCOMPARE(settings.ocrLanguage(), QStringLiteral("eng"));
    settings.setOcrLanguage(QStringLiteral("  deu  "));
    QCOMPARE(settings.ocrLanguage(), QStringLiteral("deu"));

    settings.setOcrMaxChars(1);
    QCOMPARE(settings.ocrMaxChars(), 512);
    settings.setOcrMaxChars(100000);
    QCOMPARE(settings.ocrMaxChars(), 65536);
}

void TestSettings::themeIdsValidateAgainstInstalledSchemes()
{
    SettingsManager settings;

    // "system" and the legacy presets are always accepted.
    settings.setTheme(QStringLiteral("system"));
    QCOMPARE(settings.theme(), QStringLiteral("system"));
    settings.setTheme(QStringLiteral("light"));
    QCOMPARE(settings.theme(), QStringLiteral("light"));

    // An installed KDE color scheme round-trips verbatim...
    const QVector<ColorSchemeIndex::Entry> schemes = ColorSchemeIndex::scan();
    if (!schemes.isEmpty()) {
        settings.setTheme(schemes.first().id);
        QCOMPARE(settings.theme(), schemes.first().id);
    }

    // ...while an id with no scheme behind it collapses back to "system".
    settings.setTheme(QStringLiteral("definitely-not-a-color-scheme"));
    QCOMPARE(settings.theme(), QStringLiteral("system"));
}

void TestSettings::iconThemeIdsValidateAgainstInstalledThemes()
{
    SettingsManager settings;

    // "system" follows the desktop icon theme.
    settings.setIconTheme(QStringLiteral("system"));
    QCOMPARE(settings.iconTheme(), QStringLiteral("system"));

    // An installed icon theme round-trips verbatim...
    const QVector<IconThemeIndex::Entry> themes = IconThemeIndex::scan();
    if (!themes.isEmpty()) {
        settings.setIconTheme(themes.first().id);
        QCOMPARE(settings.iconTheme(), themes.first().id);
    }

    // ...while an id with no theme behind it collapses back to "system".
    settings.setIconTheme(QStringLiteral("definitely-not-an-icon-theme"));
    QCOMPARE(settings.iconTheme(), QStringLiteral("system"));
}

void TestSettings::themeChangeNotificationsAreFiltered()
{
    // Only the two keys Egoboard mirrors count as a Plasma theme change...
    QVERIFY(SystemThemeWatcher::touchesTheme(QStringLiteral("General"), {QByteArrayLiteral("ColorScheme")}));
    QVERIFY(SystemThemeWatcher::touchesTheme(QStringLiteral("Icons"), {QByteArrayLiteral("Theme")}));
    // ...unless the writer did not name its keys at all (assume relevant).
    QVERIFY(SystemThemeWatcher::touchesTheme(QStringLiteral("General"), {}));
    QVERIFY(SystemThemeWatcher::touchesTheme(QStringLiteral("Icons"), {}));
    // Unrelated desktop settings must not repaint the app.
    QVERIFY(!SystemThemeWatcher::touchesTheme(QStringLiteral("General"), {QByteArrayLiteral("font")}));
    QVERIFY(!SystemThemeWatcher::touchesTheme(QStringLiteral("KDE"), {QByteArrayLiteral("widgetStyle")}));
    QVERIFY(!SystemThemeWatcher::touchesTheme(QStringLiteral("Icons"), {QByteArrayLiteral("Theme2")}));
}

void TestSettings::detectsPlasmaConfigChange()
{
    // The file watch is the fallback for writers that do not send KConfig
    // notifications: an edit to kdeglobals must reach the running app.
    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
                             .filePath(QStringLiteral("kdeglobals"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append));
    file.write("[General]\nColorScheme=BreezeLight\n");
    file.close();

    SystemThemeWatcher watcher;
    QSignalSpy changedSpy(&watcher, &SystemThemeWatcher::changed);

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append));
    file.write("[Icons]\nTheme=breeze-dark\n");
    file.close();

    QTRY_VERIFY_WITH_TIMEOUT(changedSpy.count() > 0, 5000);
}

void TestSettings::textReadabilitySettings()
{
    SettingsManager settings;

    QCOMPARE(settings.fontPointDelta(), 0);
    QCOMPARE(settings.textColor(), QString());
    QCOMPARE(settings.dimTextColor(), QString());

    // The size delta stays within what the row layouts can absorb.
    settings.setFontPointDelta(3);
    QCOMPARE(settings.fontPointDelta(), 3);
    settings.setFontPointDelta(99);
    QCOMPARE(settings.fontPointDelta(), 6);
    settings.setFontPointDelta(-99);
    QCOMPARE(settings.fontPointDelta(), -2);

    // Colors are stored as "#rrggbb"; anything else reads back as "follow the
    // color scheme", so a bad value cannot blank out the UI text.
    settings.setTextColor(QStringLiteral("#123456"));
    QCOMPARE(settings.textColor(), QStringLiteral("#123456"));
    settings.setDimTextColor(QStringLiteral("not-a-color"));
    QCOMPARE(settings.dimTextColor(), QString());

    // The struct handed to the theme applier mirrors the stored values.
    const TextAppearance::Overrides overrides = settings.textAppearance();
    QCOMPARE(overrides.fontPointDelta, -2);
    QVERIFY(overrides.customText);
    QCOMPARE(overrides.textColor, QColor(0x12, 0x34, 0x56));
    QVERIFY(!overrides.customDimText);

    // Clearing a color goes back to following the scheme.
    settings.setTextColor(QString());
    QVERIFY(!settings.textAppearance().customText);
}

void TestSettings::autostartEntryPointsAtThisBinary()
{
    SettingsManager settings;
    const QString path = SettingsManager::autostartDesktopFilePath();

    // Nothing is written while autostart is off.
    settings.ensureAutostartEntry();
    QVERIFY(!QFile::exists(path));

    const QString executable = SettingsManager::autostartExecutablePath();
    QVERIFY(!executable.isEmpty());
    QVERIFY(QFileInfo(executable).isAbsolute());

    settings.setAutostartEnabled(true);
    QVERIFY(QFile::exists(path));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray entry = file.readAll();
    file.close();

    // The entry names the binary itself. A bare "egoboard" is resolved against
    // the reader's PATH, and the systemd xdg-autostart generator Plasma 6 uses
    // runs with a minimal environment: it drops such entries ("executable
    // specified in Exec= does not exist") and the app never starts at login.
    QVERIFY(entry.contains("[Desktop Entry]"));
    QVERIFY(entry.contains("Type=Application"));
    QVERIFY(entry.contains(QStringLiteral("Exec=\"%1\"").arg(executable).toUtf8()));
    QVERIFY(!entry.contains("\nExec=egoboard\n"));

    // An entry left behind by an older build is repaired without touching the
    // setting, so login keeps working after a rebuild, install or move.
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("[Desktop Entry]\nType=Application\nExec=egoboard\n");
    file.close();

    settings.ensureAutostartEntry();
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray repaired = file.readAll();
    file.close();
    QVERIFY(repaired.contains(QStringLiteral("Exec=\"%1\"").arg(executable).toUtf8()));
    // Running it again is a no-op, not a rewrite loop.
    settings.ensureAutostartEntry();
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), repaired);
    file.close();

    settings.setAutostartEnabled(false);
    QVERIFY(!QFile::exists(path));
}

void TestSettings::autostartCommandCanPointAtAnAppImage()
{
    SettingsManager settings;

    // A stand-in for the .AppImage the user starts from.
    const QString appImage = m_tempDir.path() + QStringLiteral("/Egoboard.AppImage");
    QFile standIn(appImage);
    QVERIFY(standIn.open(QIODevice::WriteOnly));
    standIn.write("#!/bin/sh\n");
    standIn.close();
    QVERIFY(QFile::setPermissions(appImage, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

    // With no choice stored, the entry follows whatever binary is running.
    QCOMPARE(settings.autostartCommand(), QString());
    QCOMPARE(settings.effectiveAutostartCommand(), SettingsManager::autostartExecutablePath());

    settings.setAutostartEnabled(true);

    // A chosen command is what the entry runs - that is how "always start from
    // the AppImage" works.
    settings.setAutostartCommand(appImage);
    QCOMPARE(settings.autostartCommand(), appImage);
    QCOMPARE(settings.effectiveAutostartCommand(), appImage);

    QFile entry(SettingsManager::autostartDesktopFilePath());
    QVERIFY(entry.open(QIODevice::ReadOnly));
    QVERIFY(entry.readAll().contains(QStringLiteral("Exec=\"%1\"").arg(appImage).toUtf8()));
    entry.close();

    // If the chosen file moves away, fall back to the running binary instead of
    // writing an entry login would silently skip.
    const QString goneImage = m_tempDir.path() + QStringLiteral("/Moved.AppImage");
    settings.setAutostartCommand(goneImage);
    QCOMPARE(settings.autostartCommand(), goneImage);
    QCOMPARE(settings.effectiveAutostartCommand(), SettingsManager::autostartExecutablePath());
    QVERIFY(entry.open(QIODevice::ReadOnly));
    QVERIFY(entry.readAll().contains(
        QStringLiteral("Exec=\"%1\"").arg(SettingsManager::autostartExecutablePath()).toUtf8()));
    entry.close();

    // ...and the choice can be cleared again.
    settings.setAutostartCommand(QString());
    QCOMPARE(settings.autostartCommand(), QString());
    QCOMPARE(settings.effectiveAutostartCommand(), SettingsManager::autostartExecutablePath());

    settings.setAutostartEnabled(false);
}

void TestSettings::persistsAcrossInstances()
{
    {
        SettingsManager settings;
        settings.setStartVisible(true);
        settings.setOcrEnabled(false);
        settings.setNotificationsEnabled(false);
        settings.setDisabledScripts({QStringLiteral("script-a")});
        settings.setHiddenTransforms({QStringLiteral("json-pretty")});
        settings.setCaptureImages(false);
        settings.setMaxEntries(1000);
        settings.setTimelineEnabled(false);
        settings.setCloseAfterPaste(false);
        settings.setBumpOnPaste(false);
        settings.setPasteAsPlainText(true);
        settings.setListDensity(QStringLiteral("compact"));
        settings.setSortMode(1);
        settings.setTimestampStyle(QStringLiteral("absolute"));
        settings.setClock24h(false);
        settings.setRememberWindowGeometry(false);
        settings.setRestoreLastFilter(true);
    }

    SettingsManager loaded;
    QCOMPARE(loaded.startVisible(), true);
    QCOMPARE(loaded.ocrEnabled(), false);
    QCOMPARE(loaded.notificationsEnabled(), false);
    QVERIFY(loaded.isScriptDisabled(QStringLiteral("script-a")));
    QVERIFY(loaded.isTransformHidden(QStringLiteral("JSON-PRETTY")));
    QCOMPARE(loaded.captureImages(), false);
    QCOMPARE(loaded.captureText(), true);
    QCOMPARE(loaded.maxEntries(), 1000);
    QCOMPARE(loaded.timelineEnabled(), false);
    QCOMPARE(loaded.closeAfterPaste(), false);
    QCOMPARE(loaded.bumpOnPaste(), false);
    QCOMPARE(loaded.pasteAsPlainText(), true);
    QCOMPARE(loaded.listDensity(), QStringLiteral("compact"));
    QCOMPARE(loaded.sortMode(), 1);
    QCOMPARE(loaded.timestampStyle(), QStringLiteral("absolute"));
    QCOMPARE(loaded.clock24h(), false);
    QCOMPARE(loaded.rememberWindowGeometry(), false);
    QCOMPARE(loaded.restoreLastFilter(), true);
}

void TestSettings::searchScopeAndRecentsPersist()
{
    {
        SettingsManager settings;
        QCOMPARE(settings.searchScope(), 0);
        settings.setSearchScope(3);
        for (int i = 1; i <= 12; ++i)
            settings.addRecentSearch(QStringLiteral("query %1").arg(i));
        settings.addRecentSearch(QStringLiteral("query 5")); // moves to front, no duplicate
        settings.addRecentSearch(QStringLiteral("   ")); // ignored
    }
    {
        SettingsManager loaded;
        QCOMPARE(loaded.searchScope(), 3);
        const QStringList recents = loaded.recentSearches();
        QCOMPARE(recents.size(), 10); // capped at 10
        QCOMPARE(recents.first(), QStringLiteral("query 5"));
        QCOMPARE(recents.count(QStringLiteral("query 5")), 1);
        QVERIFY(!recents.contains(QStringLiteral("query 1")));
        QVERIFY(!recents.contains(QStringLiteral("query 2")));

        loaded.clearRecentSearches();
        QVERIFY(loaded.recentSearches().isEmpty());
    }

    // Out-of-range scope values normalize to "all text".
    {
        SettingsManager settings;
        settings.setSearchScope(99);
        QCOMPARE(settings.searchScope(), 0);
    }
}

void TestSettings::recentPaletteCommandsPersist()
{
    {
        SettingsManager settings;
        QVERIFY(settings.recentPaletteCommands().isEmpty());
        settings.addRecentPaletteCommand(QStringLiteral("tag"));
        settings.addRecentPaletteCommand(QStringLiteral("export"));
        settings.addRecentPaletteCommand(QStringLiteral("tag")); // moves to front
        settings.addRecentPaletteCommand(QStringLiteral("   ")); // ignored
        for (int i = 0; i < 12; ++i)
            settings.addRecentPaletteCommand(QStringLiteral("cmd%1").arg(i));
    }
    SettingsManager loaded;
    const QStringList recents = loaded.recentPaletteCommands();
    QCOMPARE(recents.size(), 10); // same cap as recent searches
    QCOMPARE(recents.first(), QStringLiteral("cmd11"));
    QCOMPARE(recents.count(QStringLiteral("tag")), 0); // pushed out by 12 newer ones
    QCOMPARE(loaded.recentPaletteCommands().count(QStringLiteral("cmd11")), 1);

    loaded.addRecentPaletteCommand(QStringLiteral("tag"));
    QCOMPARE(loaded.recentPaletteCommands().first(), QStringLiteral("tag"));
    QCOMPARE(loaded.recentPaletteCommands().size(), 10);
}

void TestSettings::trayBehaviourPersists()
{
    {
        SettingsManager settings;
        // Defaults match what the tray did before these settings existed.
        QCOMPARE(settings.trayPrimaryClick(), SettingsManager::TrayClick::ShowWindow);
        QCOMPARE(settings.traySecondaryClick(), SettingsManager::TrayClick::QuickPaste);
        QCOMPARE(settings.trayWheelCycles(), true);

        settings.setTrayPrimaryClick(SettingsManager::TrayClick::TogglePause);
        settings.setTraySecondaryClick(SettingsManager::TrayClick::Nothing);
        settings.setTrayWheelCycles(false);
    }
    SettingsManager loaded;
    QCOMPARE(loaded.trayPrimaryClick(), SettingsManager::TrayClick::TogglePause);
    QCOMPARE(loaded.traySecondaryClick(), SettingsManager::TrayClick::Nothing);
    QCOMPARE(loaded.trayWheelCycles(), false);

    // Every value survives, and a hand-edited out-of-range value falls back to
    // that key's default instead of becoming an undefined enum.
    for (int value = 0; value <= int(SettingsManager::TrayClick::Nothing); ++value)
        loaded.setTrayPrimaryClick(static_cast<SettingsManager::TrayClick>(value));
    loaded.setTrayPrimaryClick(static_cast<SettingsManager::TrayClick>(99));
    QCOMPARE(loaded.trayPrimaryClick(), SettingsManager::TrayClick::ShowWindow);
    loaded.setTraySecondaryClick(static_cast<SettingsManager::TrayClick>(-1));
    QCOMPARE(loaded.traySecondaryClick(), SettingsManager::TrayClick::QuickPaste);
}

void TestSettings::pauseOnLockSettingPersists()
{
    {
        SettingsManager settings;
        QCOMPARE(settings.pauseOnLock(), true); // on by default
        settings.setPauseOnLock(false);
    }
    SettingsManager loaded;
    QCOMPARE(loaded.pauseOnLock(), false);
    loaded.setPauseOnLock(true);
    QCOMPARE(loaded.pauseOnLock(), true);
}

void TestSettings::backupSettingsPersist()
{
    {
        SettingsManager settings;
        // Defaults: off, default folder, a week of daily files, never run.
        QCOMPARE(settings.backupsEnabled(), false);
        QCOMPARE(settings.backupFolder(), QString());
        QCOMPARE(settings.backupKeep(), 7);
        QCOMPARE(settings.lastBackupMs(), qint64(0));
        QVERIFY(!settings.defaultBackupFolder().isEmpty());

        settings.setBackupsEnabled(true);
        settings.setBackupFolder(QStringLiteral("/tmp/egoboard-backups"));
        settings.setBackupKeep(3);
        settings.setLastBackupMs(123456789);
    }
    {
        SettingsManager loaded;
        QCOMPARE(loaded.backupsEnabled(), true);
        QCOMPARE(loaded.backupFolder(), QStringLiteral("/tmp/egoboard-backups"));
        QCOMPARE(loaded.backupKeep(), 3);
        QCOMPARE(loaded.lastBackupMs(), qint64(123456789));
    }
    // The setter clamps to the supported range...
    {
        SettingsManager settings;
        settings.setBackupKeep(0);
        QCOMPARE(settings.backupKeep(), 1);
        settings.setBackupKeep(1000);
        QCOMPARE(settings.backupKeep(), 100);
    }
    // ...while a hand-edited out-of-range value reads back as the default.
    {
        KConfig config(QStringLiteral("egoboardrc"), KConfig::NoGlobals);
        config.group(QStringLiteral("Backups")).writeEntry(QStringLiteral("Keep"), 1000);
        config.sync();
        SettingsManager settings;
        QCOMPARE(settings.backupKeep(), 7);
    }
}

void TestSettings::listDisplayOptionsPersist()
{
    // R2: all default off.
    {
        SettingsManager settings;
        QCOMPARE(settings.groupByDay(), false);
        QCOMPARE(settings.showEntryIndex(), false);
        QCOMPARE(settings.showUseCountBadge(), false);
        QCOMPARE(settings.privacyBlur(), false);

        settings.setGroupByDay(true);
        settings.setShowEntryIndex(true);
        settings.setShowUseCountBadge(true);
        settings.setPrivacyBlur(true);
    }
    {
        SettingsManager loaded;
        QCOMPARE(loaded.groupByDay(), true);
        QCOMPARE(loaded.showEntryIndex(), true);
        QCOMPARE(loaded.showUseCountBadge(), true);
        QCOMPARE(loaded.privacyBlur(), true);
    }
}

void TestSettings::quickPasteOptionsPersist()
{
    // R3: two-line rows default off; per-screen positions round-trip.
    {
        SettingsManager settings;
        QCOMPARE(settings.quickPasteTwoLine(), false);
        QVERIFY(settings.quickPastePos(QStringLiteral("HDMI-1")).isNull());

        settings.setQuickPasteTwoLine(true);
        settings.setQuickPastePos(QStringLiteral("HDMI-1"), QPoint(120, 340));
        settings.setQuickPastePos(QStringLiteral("eDP-1"), QPoint(10, 20));
    }
    {
        SettingsManager loaded;
        QCOMPARE(loaded.quickPasteTwoLine(), true);
        QCOMPARE(loaded.quickPastePos(QStringLiteral("HDMI-1")), QPoint(120, 340));
        QCOMPARE(loaded.quickPastePos(QStringLiteral("eDP-1")), QPoint(10, 20));
        loaded.clearQuickPastePos(QStringLiteral("HDMI-1"));
        QVERIFY(loaded.quickPastePos(QStringLiteral("HDMI-1")).isNull());
        QCOMPARE(loaded.quickPastePos(QStringLiteral("eDP-1")), QPoint(10, 20));
    }
}

void TestSettings::configMigrationsAreForwardOnly()
{
    const QString path = m_tempDir.path() + QStringLiteral("/egoboardrc");

    // A config from before versioning: no ConfigVersion, unchecked values.
    QFile::remove(path);
    {
        KConfig legacy(QStringLiteral("egoboardrc"), KConfig::NoGlobals);
        legacy.group(QStringLiteral("General")).writeEntry(QStringLiteral("QuickPasteCount"), 42);
        legacy.group(QStringLiteral("History")).writeEntry(QStringLiteral("DebounceMs"), 1);
        legacy.group(QStringLiteral("History")).writeEntry(QStringLiteral("MaxItemBytes"), qint64(-5));
        legacy.group(QStringLiteral("History")).writeEntry(QStringLiteral("DiskCapBytes"), qint64(-1));
        legacy.group(QStringLiteral("History")).writeEntry(QStringLiteral("MaxEntries"), -9);
        legacy.sync();
    }

    {
        SettingsManager settings;
        QCOMPARE(settings.configVersion(), SettingsManager::currentConfigVersion());
        QCOMPARE(settings.quickPasteCount(), 9);
        QCOMPARE(settings.debounceMs(), 50);
        QCOMPARE(settings.maxItemBytes(), qint64(0)); // negative cap normalized
        QCOMPARE(settings.diskCapBytes(), qint64(0));
        QCOMPARE(settings.maxEntries(), 0);
    }

    // The migration wrote the normalized values and the version back.
    {
        KConfig check(QStringLiteral("egoboardrc"), KConfig::NoGlobals);
        QCOMPARE(check.group(QStringLiteral("General")).readEntry("ConfigVersion", 0),
                 SettingsManager::currentConfigVersion());
        QCOMPARE(check.group(QStringLiteral("History")).readEntry("DebounceMs", 0), 50);
        QCOMPARE(check.group(QStringLiteral("General")).readEntry("QuickPasteCount", 0), 9);
        QCOMPARE(check.group(QStringLiteral("History")).readEntry<qint64>("MaxItemBytes", qint64(1)),
                 qint64(0));
    }

    // A file written by a newer build is left alone (never downgraded).
    QFile::remove(path);
    {
        KConfig newer(QStringLiteral("egoboardrc"), KConfig::NoGlobals);
        newer.group(QStringLiteral("General")).writeEntry(QStringLiteral("ConfigVersion"), 999);
        newer.group(QStringLiteral("History")).writeEntry(QStringLiteral("MaxItemBytes"), qint64(-5));
        newer.sync();
    }
    SettingsManager future;
    QCOMPARE(future.configVersion(), 999);
    QCOMPARE(future.maxItemBytes(), qint64(-5)); // untouched
}

void TestSettings::settingsJsonRoundTrip()
{
    // U14 portability: every preference survives export → fresh home → import.
    SettingsManager settings;
    settings.setTrayPrimaryClick(SettingsManager::TrayClick::TogglePause);
    settings.setTraySecondaryClick(SettingsManager::TrayClick::Nothing);
    settings.setTrayWheelCycles(false);
    settings.setTrayMode(QStringLiteral("always"));
    settings.setNotificationsEnabled(false);
    settings.setCaptureSoundEnabled(false);
    settings.setCaptureNotificationEnabled(false);
    settings.setStartVisible(true);
    settings.setHideOnFocusOut(true);
    settings.setMonitorPrimarySelection(true);
    settings.setQuickPasteCount(5);
    settings.setQuickPasteTwoLine(true);
    settings.setQuickPastePos(QStringLiteral("HDMI-1"), QPoint(120, 340));
    settings.setAutostartEnabled(true);
    settings.setAutostartCommand(QStringLiteral("/tmp/egoboard-test"));
    settings.setCaptureText(false);
    settings.setCaptureImages(false);
    settings.setDebounceMs(500);
    settings.setSensitiveMode(SettingsManager::SensitiveMode::Redact);
    settings.setRedactKinds(QStringList{QStringLiteral("creditcard")});
    ExpireRule rule;
    rule.contentType = -1;
    rule.sourceAppWildcard = QStringLiteral("firefox*");
    rule.ageSeconds = 86400;
    rule.keepPinned = true;
    settings.setExpireRules(QList<ExpireRule>{rule});
    settings.setMaxItemBytes(1024);
    settings.setMaxImageBytes(2048);
    settings.setDiskCapBytes(1 << 20);
    settings.setMaxEntries(100);
    settings.setIgnoredSourceApps(QStringList{QStringLiteral("game")});
    settings.setCustomSensitivePatterns(QStringList{QStringLiteral("secret-.*")});
    settings.setEncryptionEnabled(true);
    settings.setOcrEnabled(false);
    settings.setOcrLanguage(QStringLiteral("deu"));
    settings.setOcrMaxChars(1024);
    settings.setPreviewCodeHighlight(false);
    settings.setPreviewLinkify(false);
    settings.setPreviewColorSwatches(false);
    settings.setDisabledScripts(QStringList{QStringLiteral("x")});
    settings.setHiddenTransforms(QStringList{QStringLiteral("uppercase")});
    settings.setFontPointDelta(2);
    settings.setTextColor(QStringLiteral("#ff0000"));
    settings.setDimTextColor(QStringLiteral("#00ff00"));
    settings.setToolbarIconOnly(true);
    settings.setReduceMotion(true);
    settings.setTimelineEnabled(false);
    settings.setGroupByDay(true);
    settings.setShowEntryIndex(true);
    settings.setShowUseCountBadge(true);
    settings.setPrivacyBlur(true);
    settings.setCloseAfterPaste(false);
    settings.setBumpOnPaste(false);
    settings.setPasteAsPlainText(true);
    settings.setListDensity(QStringLiteral("spacious"));
    settings.setSortMode(2);
    settings.setSearchScope(3);
    settings.addRecentSearch(QStringLiteral("foo"));
    settings.addRecentSearch(QStringLiteral("bar"));
    settings.addRecentPaletteCommand(QStringLiteral("pin"));
    settings.setTimestampStyle(QStringLiteral("absolute"));
    settings.setClock24h(false);
    settings.setRememberWindowGeometry(false);
    settings.setRestoreLastFilter(true);
    settings.setWindowGeometry(QByteArray("geom-bytes"));
    settings.setSplitterState(QByteArray("split-bytes"));
    settings.setSplitterStateForMode(1, QByteArray("mode1-bytes"));
    settings.setLastFilter(QStringLiteral("{\"x\":1}"));
    settings.setBackupsEnabled(true);
    settings.setBackupFolder(QStringLiteral("/tmp/egoback"));
    settings.setBackupKeep(3);
    const QJsonObject snapshot = settings.exportToJson();
    QCOMPARE(snapshot.value(QStringLiteral("format")).toString(),
             SettingsManager::settingsFormatTag());
    QCOMPARE(snapshot.value(QStringLiteral("version")).toInt(),
             SettingsManager::settingsFormatVersion());

    QTemporaryDir other;
    QVERIFY(other.isValid());
    XdgConfigHomeRestore homeGuard(other.path());
    SettingsManager imported;
    QString error;
    QVERIFY2(imported.importFromJson(snapshot, &error), qPrintable(error));
    QCOMPARE(imported.trayPrimaryClick(), SettingsManager::TrayClick::TogglePause);
    QCOMPARE(imported.traySecondaryClick(), SettingsManager::TrayClick::Nothing);
    QVERIFY(!imported.trayWheelCycles());
    QCOMPARE(imported.trayMode(), QStringLiteral("always"));
    QVERIFY(!imported.notificationsEnabled());
    QVERIFY(!imported.captureSoundEnabled());
    QVERIFY(!imported.captureNotificationEnabled());
    QVERIFY(imported.startVisible());
    QVERIFY(imported.hideOnFocusOut());
    QVERIFY(imported.monitorPrimarySelection());
    QCOMPARE(imported.quickPasteCount(), 5);
    QVERIFY(imported.quickPasteTwoLine());
    QCOMPARE(imported.quickPastePos(QStringLiteral("HDMI-1")), QPoint(120, 340));
    QVERIFY(imported.autostartEnabled());
    QCOMPARE(imported.autostartCommand(), settings.autostartCommand());
    QVERIFY(!imported.captureText());
    QVERIFY(!imported.captureImages());
    QCOMPARE(imported.debounceMs(), 500);
    QCOMPARE(imported.sensitiveMode(), SettingsManager::SensitiveMode::Redact);
    QCOMPARE(imported.redactKinds(), QStringList({QStringLiteral("creditcard")}));
    const QList<ExpireRule> rules = imported.expireRules();
    QCOMPARE(rules.size(), 1);
    QCOMPARE(rules.first().sourceAppWildcard, QStringLiteral("firefox*"));
    QCOMPARE(rules.first().ageSeconds, qint64(86400));
    QCOMPARE(imported.maxItemBytes(), qint64(1024));
    QCOMPARE(imported.maxImageBytes(), qint64(2048));
    QCOMPARE(imported.diskCapBytes(), qint64(1 << 20));
    QCOMPARE(imported.maxEntries(), 100);
    QCOMPARE(imported.ignoredSourceApps(), QStringList({QStringLiteral("game")}));
    QCOMPARE(imported.customSensitivePatterns(), QStringList({QStringLiteral("secret-.*")}));
    QVERIFY(imported.encryptionEnabled());
    QVERIFY(!imported.ocrEnabled());
    QCOMPARE(imported.ocrLanguage(), QStringLiteral("deu"));
    QCOMPARE(imported.ocrMaxChars(), 1024);
    QVERIFY(!imported.previewCodeHighlight());
    QVERIFY(!imported.previewLinkify());
    QVERIFY(!imported.previewColorSwatches());
    QCOMPARE(imported.disabledScripts(), QStringList({QStringLiteral("x")}));
    QCOMPARE(imported.hiddenTransforms(), QStringList({QStringLiteral("uppercase")}));
    QCOMPARE(imported.fontPointDelta(), 2);
    QCOMPARE(imported.textColor(), QStringLiteral("#ff0000"));
    QCOMPARE(imported.dimTextColor(), QStringLiteral("#00ff00"));
    QVERIFY(imported.toolbarIconOnly());
    QVERIFY(imported.reduceMotion());
    QVERIFY(!imported.timelineEnabled());
    QVERIFY(imported.groupByDay());
    QVERIFY(imported.showEntryIndex());
    QVERIFY(imported.showUseCountBadge());
    QVERIFY(imported.privacyBlur());
    QVERIFY(!imported.closeAfterPaste());
    QVERIFY(!imported.bumpOnPaste());
    QVERIFY(imported.pasteAsPlainText());
    QCOMPARE(imported.listDensity(), QStringLiteral("spacious"));
    QCOMPARE(imported.sortMode(), 2);
    QCOMPARE(imported.searchScope(), 3);
    QCOMPARE(imported.recentSearches(),
             QStringList({QStringLiteral("bar"), QStringLiteral("foo")}));
    QCOMPARE(imported.recentPaletteCommands(), QStringList({QStringLiteral("pin")}));
    QCOMPARE(imported.timestampStyle(), QStringLiteral("absolute"));
    QVERIFY(!imported.clock24h());
    QVERIFY(!imported.rememberWindowGeometry());
    QVERIFY(imported.restoreLastFilter());
    QCOMPARE(imported.windowGeometry(), QByteArray("geom-bytes"));
    QCOMPARE(imported.splitterState(), QByteArray("split-bytes"));
    QCOMPARE(imported.splitterStateForMode(1), QByteArray("mode1-bytes"));
    QCOMPARE(imported.lastFilter(), QStringLiteral("{\"x\":1}"));
    QVERIFY(imported.backupsEnabled());
    QCOMPARE(imported.backupFolder(), QStringLiteral("/tmp/egoback"));
    QCOMPARE(imported.backupKeep(), 3);
}

void TestSettings::settingsJsonIgnoresUnknownAndKeepsMissing()
{
    // Forward compatible: unknown keys are ignored, absent keys keep current.
    // Starts from a blank file: earlier slots leave non-defaults behind.
    QFile::remove(m_tempDir.path() + QStringLiteral("/egoboardrc"));
    SettingsManager settings;
    settings.setTrayMode(QStringLiteral("always"));
    QJsonObject root;
    root.insert(QStringLiteral("format"), SettingsManager::settingsFormatTag());
    root.insert(QStringLiteral("version"), SettingsManager::settingsFormatVersion());
    root.insert(QStringLiteral("futureKnob"), 42);
    root.insert(QStringLiteral("captureText"), false);
    QString error;
    QVERIFY2(settings.importFromJson(root, &error), qPrintable(error));
    QVERIFY(error.isEmpty());
    QVERIFY(!settings.captureText());
    QCOMPARE(settings.trayMode(), QStringLiteral("always")); // untouched
    QVERIFY(settings.captureImages()); // untouched default
}

void TestSettings::settingsJsonRejectsBadFiles()
{
    SettingsManager settings;
    QString error;
    QVERIFY(!settings.importFromJson(QJsonObject(), &error));
    QVERIFY(!error.isEmpty());
    QJsonObject wrongTag;
    wrongTag.insert(QStringLiteral("format"), QStringLiteral("egoboard-export"));
    wrongTag.insert(QStringLiteral("version"), 1);
    QVERIFY(!settings.importFromJson(wrongTag, &error));
    QJsonObject newer;
    newer.insert(QStringLiteral("format"), SettingsManager::settingsFormatTag());
    newer.insert(QStringLiteral("version"), SettingsManager::settingsFormatVersion() + 1);
    QVERIFY(!settings.importFromJson(newer, &error));
    QVERIFY(error.contains(QStringLiteral("newer")));
}

void TestSettings::settingsJsonExcludesBackupSchedule()
{
    // lastBackupMs is machine-local schedule state: it must neither travel
    // in the file nor be clobbered by an import.
    SettingsManager settings;
    settings.setLastBackupMs(123456);
    const QJsonObject snapshot = settings.exportToJson();
    QVERIFY(!snapshot.contains(QStringLiteral("lastBackupMs")));

    QTemporaryDir other;
    QVERIFY(other.isValid());
    XdgConfigHomeRestore homeGuard(other.path());
    SettingsManager imported;
    QCOMPARE(imported.lastBackupMs(), qint64(0));
    QString error;
    QVERIFY2(imported.importFromJson(snapshot, &error), qPrintable(error));
    QCOMPARE(imported.lastBackupMs(), qint64(0));
}

void TestSettings::settingsJsonPositionsRoundTrip()
{
    SettingsManager settings;
    settings.setQuickPastePos(QStringLiteral("HDMI-1"), QPoint(120, 340));
    settings.setQuickPastePos(QStringLiteral("eDP-1"), QPoint(10, 20));
    const QJsonObject snapshot = settings.exportToJson();
    const QJsonValue positions = snapshot.value(QStringLiteral("quickPastePositions"));
    QVERIFY(positions.isObject());
    QCOMPARE(positions.toObject().size(), 2);

    QTemporaryDir other;
    QVERIFY(other.isValid());
    XdgConfigHomeRestore homeGuard(other.path());
    SettingsManager imported;
    imported.setQuickPastePos(QStringLiteral("Old-Screen"), QPoint(1, 1)); // stale: replaced
    QString error;
    QVERIFY2(imported.importFromJson(snapshot, &error), qPrintable(error));
    QCOMPARE(imported.quickPastePos(QStringLiteral("HDMI-1")), QPoint(120, 340));
    QCOMPARE(imported.quickPastePos(QStringLiteral("eDP-1")), QPoint(10, 20));
    QVERIFY(imported.quickPastePos(QStringLiteral("Old-Screen")).isNull());
}

void TestSettings::settingsJsonImportEmitsSingleChanged()
{
    // One import batches dozens of setters into a single changed().
    SettingsManager settings;
    QJsonObject root;
    root.insert(QStringLiteral("format"), SettingsManager::settingsFormatTag());
    root.insert(QStringLiteral("version"), SettingsManager::settingsFormatVersion());
    root.insert(QStringLiteral("captureText"), false);
    root.insert(QStringLiteral("quickPasteCount"), 3);
    root.insert(QStringLiteral("trayMode"), QStringLiteral("hidden"));
    QSignalSpy spy(&settings, &SettingsManager::changed);
    QString error;
    QVERIFY2(settings.importFromJson(root, &error), qPrintable(error));
    QCOMPARE(spy.count(), 1);
    QVERIFY(!settings.captureText());
    QCOMPARE(settings.quickPasteCount(), 3);
    QCOMPARE(settings.trayMode(), QStringLiteral("hidden"));
}

void TestSettings::resetGeneralRestoresDefaults()
{
    // U14 per-page reset: General returns to defaults, other pages untouched.
    QFile::remove(m_tempDir.path() + QStringLiteral("/egoboardrc"));
    SettingsManager settings;
    settings.setStartVisible(true);
    settings.setHideOnFocusOut(true);
    settings.setAutostartEnabled(true);
    settings.setTrayMode(QStringLiteral("hidden"));
    settings.setTrayPrimaryClick(SettingsManager::TrayClick::Nothing);
    settings.setNotificationsEnabled(false);
    settings.setCaptureSoundEnabled(false);
    settings.setTheme(QStringLiteral("light"));
    settings.setFontPointDelta(4);
    settings.setTextColor(QStringLiteral("#123456"));
    settings.setListDensity(QStringLiteral("compact"));
    settings.setTimestampStyle(QStringLiteral("absolute"));
    settings.setClock24h(false);
    settings.setToolbarIconOnly(true);
    settings.setReduceMotion(true);
    settings.setGroupByDay(true);
    settings.setShowEntryIndex(true);
    settings.setShowUseCountBadge(true);
    settings.setPrivacyBlur(true);
    settings.setCloseAfterPaste(false);
    settings.setBumpOnPaste(false);
    settings.setPasteAsPlainText(true);
    // A Capture knob to prove the reset is page-scoped.
    settings.setCaptureText(false);

    QSignalSpy spy(&settings, &SettingsManager::changed);
    settings.resetPageToDefaults(SettingsManager::SettingsPage::General);
    QCOMPARE(spy.count(), 1);

    QVERIFY(!settings.startVisible());
    QVERIFY(!settings.hideOnFocusOut());
    QVERIFY(!settings.autostartEnabled());
    QCOMPARE(settings.autostartCommand(), QString());
    QVERIFY(settings.rememberWindowGeometry());
    QVERIFY(!settings.restoreLastFilter());
    QCOMPARE(settings.trayMode(), QStringLiteral("auto"));
    QCOMPARE(settings.trayPrimaryClick(), SettingsManager::TrayClick::ShowWindow);
    QCOMPARE(settings.traySecondaryClick(), SettingsManager::TrayClick::QuickPaste);
    QVERIFY(settings.trayWheelCycles());
    QVERIFY(settings.notificationsEnabled());
    QVERIFY(settings.captureSoundEnabled());
    QVERIFY(settings.captureNotificationEnabled());
    QCOMPARE(settings.theme(), QStringLiteral("system"));
    QCOMPARE(settings.iconTheme(), QStringLiteral("system"));
    QCOMPARE(settings.fontPointDelta(), 0);
    QCOMPARE(settings.textColor(), QString());
    QCOMPARE(settings.dimTextColor(), QString());
    QCOMPARE(settings.listDensity(), QStringLiteral("comfortable"));
    QCOMPARE(settings.timestampStyle(), QStringLiteral("relative"));
    QVERIFY(settings.clock24h());
    QVERIFY(!settings.toolbarIconOnly());
    QVERIFY(!settings.reduceMotion());
    QVERIFY(!settings.groupByDay());
    QVERIFY(!settings.showEntryIndex());
    QVERIFY(!settings.showUseCountBadge());
    QVERIFY(!settings.privacyBlur());
    QVERIFY(settings.closeAfterPaste());
    QVERIFY(settings.bumpOnPaste());
    QVERIFY(!settings.pasteAsPlainText());
    // Other pages are untouched.
    QVERIFY(!settings.captureText());
    settings.setAutostartEnabled(false);
}

void TestSettings::resetCaptureRestoresDefaults()
{
    // U14 per-page reset: Capture returns to defaults, General untouched.
    QFile::remove(m_tempDir.path() + QStringLiteral("/egoboardrc"));
    SettingsManager settings;
    settings.setMonitorPrimarySelection(true);
    settings.setCaptureText(false);
    settings.setCaptureRichText(false);
    settings.setCaptureImages(false);
    settings.setCaptureFiles(false);
    settings.setPauseOnLock(false);
    settings.setDebounceMs(1000);
    settings.setMaxItemBytes(1024);
    settings.setMaxImageBytes(2048);
    settings.setQuickPasteCount(3);
    settings.setQuickPasteTwoLine(true);
    settings.setIgnoredSourceApps(QStringList{QStringLiteral("game")});
    settings.setStartVisible(true); // General knob: must survive

    QSignalSpy spy(&settings, &SettingsManager::changed);
    settings.resetPageToDefaults(SettingsManager::SettingsPage::Capture);
    QCOMPARE(spy.count(), 1);

    QVERIFY(!settings.monitorPrimarySelection());
    QVERIFY(settings.captureText());
    QVERIFY(settings.captureRichText());
    QVERIFY(settings.captureImages());
    QVERIFY(settings.captureFiles());
    QVERIFY(settings.pauseOnLock());
    QCOMPARE(settings.debounceMs(), 250);
    QCOMPARE(settings.maxItemBytes(), qint64(5 * 1024 * 1024));
    QCOMPARE(settings.maxImageBytes(), qint64(8 * 1024 * 1024));
    QCOMPARE(settings.quickPasteCount(), 9);
    QVERIFY(!settings.quickPasteTwoLine());
    QVERIFY(settings.ignoredSourceApps().isEmpty());
    QVERIFY(settings.startVisible());
}

void TestSettings::resetPrivacyHistorySearchAutomationStorage()
{
    // U14 per-page reset: the remaining pages restore defaults independently.
    // Encryption is security-sensitive (needs the confirm + rekey flow), so a
    // Privacy reset leaves it alone.
    QFile::remove(m_tempDir.path() + QStringLiteral("/egoboardrc"));
    SettingsManager settings;
    settings.setSensitiveMode(SettingsManager::SensitiveMode::Off);
    settings.setRedactKinds(QStringList{QStringLiteral("creditcard")});
    settings.setCustomSensitivePatterns(QStringList{QStringLiteral("secret-.*")});
    settings.setEncryptionEnabled(true);
    settings.setMaxEntries(100);
    settings.setDiskCapBytes(1 << 20);
    ExpireRule rule;
    rule.contentType = -1;
    rule.sourceAppWildcard = QStringLiteral("firefox*");
    rule.ageSeconds = 86400;
    rule.keepPinned = true;
    settings.setExpireRules(QList<ExpireRule>{rule});
    settings.setPreviewCodeHighlight(false);
    settings.setPreviewLinkify(false);
    settings.setPreviewColorSwatches(false);
    settings.setTimelineEnabled(false);
    settings.setOcrEnabled(false);
    settings.setOcrLanguage(QStringLiteral("deu"));
    settings.setOcrMaxChars(1024);
    settings.setDisabledScripts(QStringList{QStringLiteral("x")});
    settings.setHiddenTransforms(QStringList{QStringLiteral("uppercase")});
    settings.setBackupsEnabled(true);
    settings.setBackupFolder(QStringLiteral("/tmp/egoback"));
    settings.setBackupKeep(3);

    settings.resetPageToDefaults(SettingsManager::SettingsPage::Privacy);
    QCOMPARE(settings.sensitiveMode(), SettingsManager::SensitiveMode::Exclude);
    QCOMPARE(settings.redactKinds(), SensitiveDataDetector::allKinds());
    QVERIFY(settings.customSensitivePatterns().isEmpty());
    QVERIFY(settings.encryptionEnabled()); // untouched
    // Other pages still hold their non-defaults.
    QCOMPARE(settings.maxEntries(), 100);
    QVERIFY(!settings.previewCodeHighlight());

    settings.resetPageToDefaults(SettingsManager::SettingsPage::History);
    QCOMPARE(settings.maxEntries(), 0);
    QCOMPARE(settings.diskCapBytes(), qint64(0));
    QVERIFY(settings.expireRules().isEmpty());

    settings.resetPageToDefaults(SettingsManager::SettingsPage::SearchPreview);
    QVERIFY(settings.previewCodeHighlight());
    QVERIFY(settings.previewLinkify());
    QVERIFY(settings.previewColorSwatches());
    QVERIFY(settings.timelineEnabled());
    QVERIFY(settings.ocrEnabled());
    QCOMPARE(settings.ocrLanguage(), QStringLiteral("eng"));
    QCOMPARE(settings.ocrMaxChars(), 8192);

    settings.resetPageToDefaults(SettingsManager::SettingsPage::Automation);
    QVERIFY(settings.disabledScripts().isEmpty());
    QVERIFY(settings.hiddenTransforms().isEmpty());

    settings.resetPageToDefaults(SettingsManager::SettingsPage::Storage);
    QVERIFY(!settings.backupsEnabled());
    QCOMPARE(settings.backupFolder(), QString());
    QCOMPARE(settings.backupKeep(), 7);
    settings.setEncryptionEnabled(false);
}

void TestSettings::resetPageEmitsSingleChangedAndPreservesSessionState()
{
    // Session/placement/geometry state is not knobs: a page reset batches its
    // own setters into one changed() and leaves that state alone.
    QFile::remove(m_tempDir.path() + QStringLiteral("/egoboardrc"));
    SettingsManager settings;
    settings.setCaptureText(false);
    settings.setQuickPastePos(QStringLiteral("HDMI-1"), QPoint(120, 340));
    settings.setWindowGeometry(QByteArray("geom-bytes"));
    settings.setSplitterState(QByteArray("split-bytes"));
    settings.setLastFilter(QStringLiteral("{\"x\":1}"));
    settings.addRecentSearch(QStringLiteral("foo"));
    settings.setLastBackupMs(123456);
    settings.setSortMode(2);

    QSignalSpy spy(&settings, &SettingsManager::changed);
    settings.resetPageToDefaults(SettingsManager::SettingsPage::Capture);
    QCOMPARE(spy.count(), 1);
    QVERIFY(settings.captureText());
    QCOMPARE(settings.quickPastePos(QStringLiteral("HDMI-1")), QPoint(120, 340));
    QCOMPARE(settings.windowGeometry(), QByteArray("geom-bytes"));
    QCOMPARE(settings.splitterState(), QByteArray("split-bytes"));
    QCOMPARE(settings.lastFilter(), QStringLiteral("{\"x\":1}"));
    QCOMPARE(settings.recentSearches(), QStringList({QStringLiteral("foo")}));
    QCOMPARE(settings.lastBackupMs(), qint64(123456));
    QCOMPARE(settings.sortMode(), 2);
}

void TestSettings::profileSaveApplyDeleteRoundTrip()
{
    // U14 profiles: a saved profile round-trips the whole setting set through
    // its own KConfig group and can be re-applied after local edits.
    QFile::remove(m_tempDir.path() + QStringLiteral("/egoboardrc"));
    SettingsManager settings;
    QVERIFY(settings.profileNames().isEmpty());
    QVERIFY(settings.activeProfile().isEmpty());

    settings.setQuickPasteCount(3);
    settings.setTrayMode(QStringLiteral("hidden"));
    settings.setSensitiveMode(SettingsManager::SensitiveMode::Mark);
    QString error;
    QVERIFY2(settings.saveProfile(QStringLiteral("Work"), &error), qPrintable(error));
    QCOMPARE(settings.profileNames(), QStringList({QStringLiteral("Work")}));
    QVERIFY(settings.hasProfile(QStringLiteral("Work")));
    QCOMPARE(settings.activeProfile(), QStringLiteral("Work"));

    // Local edits diverge from the snapshot...
    settings.setQuickPasteCount(7);
    settings.setTrayMode(QStringLiteral("always"));
    settings.setSensitiveMode(SettingsManager::SensitiveMode::Off);

    QVERIFY2(settings.applyProfile(QStringLiteral("Work"), &error), qPrintable(error));
    QCOMPARE(settings.quickPasteCount(), 3);
    QCOMPARE(settings.trayMode(), QStringLiteral("hidden"));
    QCOMPARE(settings.sensitiveMode(), SettingsManager::SensitiveMode::Mark);
    QCOMPARE(settings.activeProfile(), QStringLiteral("Work"));

    // A second profile coexists; names sort case-insensitively.
    settings.setQuickPasteCount(5);
    QVERIFY2(settings.saveProfile(QStringLiteral("personal"), &error), qPrintable(error));
    QCOMPARE(settings.profileNames(),
             QStringList({QStringLiteral("personal"), QStringLiteral("Work")}));
    QCOMPARE(settings.activeProfile(), QStringLiteral("personal"));

    QVERIFY2(settings.deleteProfile(QStringLiteral("Work"), &error), qPrintable(error));
    QCOMPARE(settings.profileNames(), QStringList({QStringLiteral("personal")}));
    QVERIFY(!settings.hasProfile(QStringLiteral("Work")));
    // Deleting the active profile clears it; other live settings are kept.
    QVERIFY2(settings.deleteProfile(QStringLiteral("personal"), &error), qPrintable(error));
    QVERIFY(settings.profileNames().isEmpty());
    QVERIFY(settings.activeProfile().isEmpty());
    QCOMPARE(settings.quickPasteCount(), 5);

    // Profiles survive a fresh instance (they live in egoboardrc groups).
    QVERIFY2(settings.saveProfile(QStringLiteral("Work"), &error), qPrintable(error));
    SettingsManager reloaded;
    QCOMPARE(reloaded.profileNames(), QStringList({QStringLiteral("Work")}));
    QVERIFY(reloaded.hasProfile(QStringLiteral("Work")));
}

void TestSettings::profileRejectsBadNamesAndUnknownApply()
{
    // Profile names stay INI-safe and bounded; unknown profiles fail loudly.
    QFile::remove(m_tempDir.path() + QStringLiteral("/egoboardrc"));
    SettingsManager settings;
    QString error;
    QVERIFY(!settings.saveProfile(QString(), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!settings.saveProfile(QStringLiteral("   "), &error));
    QVERIFY(!settings.saveProfile(QStringLiteral("a/b"), &error));
    QVERIFY(!settings.saveProfile(QStringLiteral("a\\b"), &error));
    QVERIFY(!settings.saveProfile(QStringLiteral("a[b]"), &error));
    QVERIFY(!settings.saveProfile(QString(41, QLatin1Char('x')), &error));
    QVERIFY(settings.profileNames().isEmpty());

    QVERIFY(!settings.applyProfile(QStringLiteral("Nobody"), &error));
    QVERIFY(error.contains(QStringLiteral("Nobody")));
    QVERIFY(!settings.deleteProfile(QStringLiteral("Nobody"), &error));
    QVERIFY(!error.isEmpty());

    // Case-insensitive apply resolves the stored spelling...
    QVERIFY2(settings.saveProfile(QStringLiteral("Work"), &error), qPrintable(error));
    QVERIFY2(settings.applyProfile(QStringLiteral("work"), &error), qPrintable(error));
    QCOMPARE(settings.activeProfile(), QStringLiteral("Work"));

    // ...while surrounding whitespace is trimmed everywhere.
    QVERIFY(settings.hasProfile(QStringLiteral("  Work  ")));
    QVERIFY(!settings.hasProfile(QString()));
}

void TestSettings::profileApplyEmitsSingleChanged()
{
    // Applying routes through the validating import: one changed(), and the
    // machine-local backup schedule still does not travel with the profile.
    QFile::remove(m_tempDir.path() + QStringLiteral("/egoboardrc"));
    SettingsManager settings;
    settings.setCaptureText(false);
    settings.setQuickPasteCount(4);
    settings.setLastBackupMs(999);
    QString error;
    QVERIFY2(settings.saveProfile(QStringLiteral("Work"), &error), qPrintable(error));

    settings.setCaptureText(true);
    settings.setQuickPasteCount(9);
    settings.setLastBackupMs(123456);
    QSignalSpy spy(&settings, &SettingsManager::changed);
    QVERIFY2(settings.applyProfile(QStringLiteral("Work"), &error), qPrintable(error));
    QCOMPARE(spy.count(), 1);
    QVERIFY(!settings.captureText());
    QCOMPARE(settings.quickPasteCount(), 4);
    QCOMPARE(settings.lastBackupMs(), qint64(123456)); // schedule state stays local
}

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
