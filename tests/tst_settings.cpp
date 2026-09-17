#include <QtTest>

#include "ColorSchemeIndex.h"
#include "IconThemeIndex.h"
#include "SettingsManager.h"

#include <QSignalSpy>
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
    void persistsAcrossInstances();

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

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
