#include <QtTest>

#include "ActiveWindowTracker.h"
#include "ClipboardWatcher.h"
#include "SettingsManager.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>

class TestClipboardWatcher : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void capturesTextAndSourceMetadata();
    void ignoresConfiguredSourceApp();
    void excludesSensitiveText();
    void marksSensitiveText();
    void redactsSensitiveText();
    void appliesCustomSensitivePattern();
    void truncatesOversizedText();
    void skipsDisabledCaptureType();
    void clipboardChangeDoesNotProcessSynchronously();
    void suppressesOwnClipboardWrites();
    void ignoresPrimarySelectionWhenDisabled();
    void pausedWatcherSkipsCapturesUntilResumed();

private:
    class Tracker final : public IActiveWindowTracker
    {
    public:
        ActiveWindowInfo activeWindow() const override { return {QStringLiteral("test-app"), QStringLiteral("Test Window")}; }
    };

    void setClipboardText(ClipboardWatcher &watcher, const QString &text);
    static int capturedWithText(const QSignalSpy &spy, const QString &text,
                                ClipboardRecord *record = nullptr);

    QTemporaryDir m_configDir;
};

void TestClipboardWatcher::initTestCase()
{
    QVERIFY(m_configDir.isValid());
    qputenv("XDG_CONFIG_HOME", m_configDir.path().toUtf8());
}

void TestClipboardWatcher::setClipboardText(ClipboardWatcher &watcher, const QString &text)
{
    QGuiApplication::clipboard()->setText(text, QClipboard::Clipboard);
    QVERIFY(QMetaObject::invokeMethod(&watcher, "processPending", Qt::DirectConnection));
}

int TestClipboardWatcher::capturedWithText(const QSignalSpy &spy, const QString &text,
                                           ClipboardRecord *record)
{
    for (int i = 0; i < spy.size(); ++i) {
        const ClipboardRecord candidate = spy.at(i).at(0).value<ClipboardRecord>();
        if (candidate.textData == text) {
            if (record)
                *record = candidate;
            return i;
        }
    }
    return -1;
}

void TestClipboardWatcher::capturesTextAndSourceMetadata()
{
    SettingsManager settings;
    settings.setSensitiveMode(SettingsManager::SensitiveMode::Off);
    settings.setDebounceMs(50);
    Tracker tracker;
    ClipboardWatcher watcher(QGuiApplication::clipboard(), &settings, &tracker);
    QSignalSpy capturedSpy(&watcher, &ClipboardWatcher::captured);
    setClipboardText(watcher, QStringLiteral("hello watcher"));
    ClipboardRecord record;
    QVERIFY(capturedWithText(capturedSpy, QStringLiteral("hello watcher"), &record) >= 0);
    QCOMPARE(record.type, ContentType::Text);
    QCOMPARE(record.preview, QStringLiteral("hello watcher"));
    QCOMPARE(record.sourceApp, QStringLiteral("test-app"));
    QCOMPARE(record.sourceWindow, QStringLiteral("Test Window"));
    QVERIFY(!record.hash.isEmpty());
    QCOMPARE(record.sizeBytes, qint64(QStringLiteral("hello watcher").toUtf8().size()));
}

void TestClipboardWatcher::ignoresConfiguredSourceApp()
{
    SettingsManager settings;
    settings.setSensitiveMode(SettingsManager::SensitiveMode::Off);
    settings.setIgnoredSourceApps({QStringLiteral("test-*"), QStringLiteral("other-app")});
    settings.setDebounceMs(50);
    Tracker tracker;
    ClipboardWatcher watcher(QGuiApplication::clipboard(), &settings, &tracker);
    QSignalSpy capturedSpy(&watcher, &ClipboardWatcher::captured);
    setClipboardText(watcher, QStringLiteral("ignored clipboard"));
    QVERIFY(capturedWithText(capturedSpy, QStringLiteral("ignored clipboard")) < 0);
}

void TestClipboardWatcher::excludesSensitiveText()
{
    SettingsManager settings;
    settings.setSensitiveMode(SettingsManager::SensitiveMode::Exclude);
    settings.setIgnoredSourceApps({});
    settings.setDebounceMs(50);
    Tracker tracker;
    ClipboardWatcher watcher(QGuiApplication::clipboard(), &settings, &tracker);
    QSignalSpy capturedSpy(&watcher, &ClipboardWatcher::captured);
    QSignalSpy excludedSpy(&watcher, &ClipboardWatcher::excludedSensitive);
    setClipboardText(watcher, QStringLiteral("password=hunter2"));
    QVERIFY(capturedWithText(capturedSpy, QStringLiteral("password=hunter2")) < 0);
    QVERIFY(excludedSpy.count() >= 1);
    QVERIFY(excludedSpy.last().first().toString().contains(QStringLiteral("credential")));
}

void TestClipboardWatcher::marksSensitiveText()
{
    SettingsManager settings;
    settings.setSensitiveMode(SettingsManager::SensitiveMode::Mark);
    settings.setIgnoredSourceApps({});
    settings.setDebounceMs(50);
    Tracker tracker;
    ClipboardWatcher watcher(QGuiApplication::clipboard(), &settings, &tracker);
    QSignalSpy capturedSpy(&watcher, &ClipboardWatcher::captured);
    const QString text = QStringLiteral("password=hunter3");
    setClipboardText(watcher, text);
    ClipboardRecord record;
    QVERIFY(capturedWithText(capturedSpy, text, &record) >= 0);
    QVERIFY(record.sensitive);
    QCOMPARE(record.textData, text);
}

void TestClipboardWatcher::redactsSensitiveText()
{
    SettingsManager settings;
    settings.setSensitiveMode(SettingsManager::SensitiveMode::Redact);
    settings.setRedactKinds({});
    settings.setIgnoredSourceApps({});
    settings.setDebounceMs(50);
    Tracker tracker;
    ClipboardWatcher watcher(QGuiApplication::clipboard(), &settings, &tracker);
    QSignalSpy capturedSpy(&watcher, &ClipboardWatcher::captured);
    QSignalSpy redactedSpy(&watcher, &ClipboardWatcher::redactedSensitive);

    setClipboardText(watcher, QStringLiteral("password=hunter4"));
    ClipboardRecord record;
    QVERIFY(capturedWithText(capturedSpy, QStringLiteral("••••"), &record) >= 0);
    QVERIFY(record.sensitive);
    QVERIFY(!record.textData.contains(QStringLiteral("hunter4")));
    QVERIFY(redactedSpy.count() >= 1);
}

void TestClipboardWatcher::appliesCustomSensitivePattern()
{
    SettingsManager settings;
    settings.setSensitiveMode(SettingsManager::SensitiveMode::Exclude);
    settings.setIgnoredSourceApps({});
    settings.setCustomSensitivePatterns({QStringLiteral("internal-[A-Z]+")});
    settings.setDebounceMs(50);
    Tracker tracker;
    ClipboardWatcher watcher(QGuiApplication::clipboard(), &settings, &tracker);
    QSignalSpy capturedSpy(&watcher, &ClipboardWatcher::captured);
    QSignalSpy excludedSpy(&watcher, &ClipboardWatcher::excludedSensitive);

    setClipboardText(watcher, QStringLiteral("internal-SECRET"));
    QVERIFY(capturedWithText(capturedSpy, QStringLiteral("internal-SECRET")) < 0);
    QVERIFY(excludedSpy.count() >= 1);
    QVERIFY(excludedSpy.last().first().toString().contains(QStringLiteral("custom:")));
}

void TestClipboardWatcher::truncatesOversizedText()
{
    SettingsManager settings;
    settings.setSensitiveMode(SettingsManager::SensitiveMode::Off);
    settings.setIgnoredSourceApps({});
    settings.setMaxItemBytes(8);
    settings.setDebounceMs(50);
    Tracker tracker;
    ClipboardWatcher watcher(QGuiApplication::clipboard(), &settings, &tracker);
    QSignalSpy capturedSpy(&watcher, &ClipboardWatcher::captured);

    const QString input = QStringLiteral("1234567890abcdef");
    setClipboardText(watcher, input);
    ClipboardRecord record;
    QVERIFY(capturedSpy.count() > 0);
    record = capturedSpy.last().at(0).value<ClipboardRecord>();
    QCOMPARE(record.textData, QStringLiteral("12345678"));
    QCOMPARE(record.sizeBytes, qint64(8));
    QVERIFY(record.preview.endsWith(QChar(0x2026)));
}

void TestClipboardWatcher::skipsDisabledCaptureType()
{
    SettingsManager settings;
    settings.setSensitiveMode(SettingsManager::SensitiveMode::Off);
    settings.setIgnoredSourceApps({});
    settings.setCaptureText(false);
    // The shared config dir may carry a small item cap from other tests.
    settings.setMaxItemBytes(1024 * 1024);
    settings.setDebounceMs(50);
    Tracker tracker;
    ClipboardWatcher watcher(QGuiApplication::clipboard(), &settings, &tracker);
    QSignalSpy capturedSpy(&watcher, &ClipboardWatcher::captured);

    setClipboardText(watcher, QStringLiteral("no plain text please"));
    QVERIFY(capturedWithText(capturedSpy, QStringLiteral("no plain text please")) < 0);
    QCOMPARE(capturedSpy.count(), 0);

    // Re-enabling the type resumes capture.
    settings.setCaptureText(true);
    setClipboardText(watcher, QStringLiteral("plain text again"));
    QVERIFY(capturedWithText(capturedSpy, QStringLiteral("plain text again")) >= 0);
}

void TestClipboardWatcher::clipboardChangeDoesNotProcessSynchronously()
{
    SettingsManager settings;
    settings.setSensitiveMode(SettingsManager::SensitiveMode::Off);
    settings.setIgnoredSourceApps({});
    settings.setDebounceMs(120);
    Tracker tracker;
    ClipboardWatcher watcher(QGuiApplication::clipboard(), &settings, &tracker);
    QSignalSpy capturedSpy(&watcher, &ClipboardWatcher::captured);

    QGuiApplication::clipboard()->setText(QStringLiteral("debounced"), QClipboard::Clipboard);
    QVERIFY(QMetaObject::invokeMethod(&watcher, "onClipboardChanged", Qt::DirectConnection,
                                      Q_ARG(QClipboard::Mode, QClipboard::Clipboard)));
    QCOMPARE(capturedSpy.count(), 0);

    // Processing is deferred to the timer in normal operation; no capture may
    // be emitted synchronously from the change notification.
    QCOMPARE(capturedSpy.count(), 0);
}

void TestClipboardWatcher::suppressesOwnClipboardWrites()
{
    SettingsManager settings;
    settings.setSensitiveMode(SettingsManager::SensitiveMode::Off);
    settings.setIgnoredSourceApps({});
    Tracker tracker;
    ClipboardWatcher watcher(QGuiApplication::clipboard(), &settings, &tracker);
    QSignalSpy capturedSpy(&watcher, &ClipboardWatcher::captured);

    watcher.suppressOwnSets();
    setClipboardText(watcher, QStringLiteral("own clipboard write"));
    QCOMPARE(capturedSpy.count(), 0);
}

void TestClipboardWatcher::ignoresPrimarySelectionWhenDisabled()
{
    SettingsManager settings;
    settings.setSensitiveMode(SettingsManager::SensitiveMode::Off);
    settings.setIgnoredSourceApps({});
    settings.setMonitorPrimarySelection(false);
    Tracker tracker;
    ClipboardWatcher watcher(QGuiApplication::clipboard(), &settings, &tracker);
    QSignalSpy capturedSpy(&watcher, &ClipboardWatcher::captured);
    QGuiApplication::clipboard()->setText(QStringLiteral("primary selection"), QClipboard::Selection);

    QVERIFY(QMetaObject::invokeMethod(&watcher, "onClipboardChanged", Qt::DirectConnection,
                                      Q_ARG(QClipboard::Mode, QClipboard::Selection)));
    QTest::qWait(200);
    QCOMPARE(capturedSpy.count(), 0);
}

void TestClipboardWatcher::pausedWatcherSkipsCapturesUntilResumed()
{
    SettingsManager settings;
    settings.setSensitiveMode(SettingsManager::SensitiveMode::Off);
    settings.setIgnoredSourceApps({});
    Tracker tracker;
    ClipboardWatcher watcher(QGuiApplication::clipboard(), &settings, &tracker);
    QSignalSpy capturedSpy(&watcher, &ClipboardWatcher::captured);

    // Paused: copies are ignored entirely (no record, no signal).
    watcher.setPaused(true);
    QVERIFY(watcher.isPaused());
    setClipboardText(watcher, QStringLiteral("while paused"));
    QCOMPARE(capturedSpy.count(), 0);
    QCOMPARE(capturedWithText(capturedSpy, QStringLiteral("while paused")), -1);

    // Resuming captures again, including content copied while paused as soon as
    // something new arrives.
    watcher.setPaused(false);
    QVERIFY(!watcher.isPaused());
    setClipboardText(watcher, QStringLiteral("after resume"));
    QCOMPARE(capturedWithText(capturedSpy, QStringLiteral("after resume")), 0);
    QCOMPARE(capturedSpy.count(), 1);

    // Pausing after a capture does not "un-capture" it.
    watcher.setPaused(true);
    setClipboardText(watcher, QStringLiteral("paused again"));
    QCOMPARE(capturedSpy.count(), 1);
}

QTEST_MAIN(TestClipboardWatcher)
#include "tst_clipboardwatcher.moc"
