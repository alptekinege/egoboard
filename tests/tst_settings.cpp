#include <QtTest>

#include "ColorSchemeIndex.h"
#include "IconThemeIndex.h"
#include "SettingsManager.h"
#include "SystemThemeWatcher.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVector>

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
    void configMigrationsAreForwardOnly();

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

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
