#include <QtTest>

#include "SettingsManager.h"

#include <QSignalSpy>
#include <QTemporaryDir>

class TestSettings : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void defaultValues();
    void mutateGeneralSettingsAndSignal();
    void sensitiveModeAndRedactKinds();
    void expireRulesPersistence();
    void ignoredSourceAppsWildcardMatching();
    void boundsAndLimits();

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
    QCOMPARE(settings.theme(), QStringLiteral("system"));
    QCOMPARE(settings.toolbarIconOnly(), false);
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

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
