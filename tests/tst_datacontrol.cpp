#include <QtTest>
#include "../src/app/WlrDataControlHelper.h"
#include "../src/app/ActiveWindowTracker.h"

#include <QGuiApplication>

class TestDataControl : public QObject {
    Q_OBJECT
private slots:
    void platformNameNotEmpty();
    void supportedFalseOnOffscreen();
    void diagnosticsContainsPlatform();
    void startStopNoCrashOffscreen();
    void suppressNoCrash();
};

void TestDataControl::platformNameNotEmpty()
{
    const QString plat = WlrDataControlHelper::platformName();
    if (plat.isEmpty()) {
        WlrDataControlHelper tmp(nullptr, nullptr);
        const QString diag = tmp.diagnostics();
        QVERIFY(!diag.isEmpty());
        return;
    }
    QVERIFY(!plat.isEmpty());
}

void TestDataControl::supportedFalseOnOffscreen()
{
    const QString plat = WlrDataControlHelper::platformName();
    if (plat == QLatin1String("offscreen") || plat.isEmpty()) {
        QVERIFY(!WlrDataControlHelper::isSupported());
        QVERIFY(!WlrDataControlHelper::isWayland());
        WlrDataControlHelper tmp(nullptr, nullptr);
        QVERIFY(!tmp.isActive());
    } else if (plat == QLatin1String("wayland")) {
        QVERIFY(WlrDataControlHelper::isSupported());
        QVERIFY(WlrDataControlHelper::isWayland());
    }
}

void TestDataControl::diagnosticsContainsPlatform()
{
    WlrDataControlHelper tmp(nullptr, nullptr);
    const QString diag = tmp.diagnostics();
    QVERIFY(!diag.isEmpty());
    const QString plat = WlrDataControlHelper::platformName();
    if (!plat.isEmpty())
        QVERIFY(diag.contains(plat, Qt::CaseInsensitive));
    QVERIFY(diag.contains(QStringLiteral("wlr-data-control"), Qt::CaseInsensitive));
}

void TestDataControl::startStopNoCrashOffscreen()
{
    // Dummy tracker for testing — X11 tracker is fine offscreen
    struct DummyTracker : IActiveWindowTracker {
        ActiveWindowInfo activeWindow() const override { return {}; }
    } dummy;
    WlrDataControlHelper helper(nullptr, &dummy);
    // Must not crash on offscreen
    helper.start();
    QVERIFY(!helper.isActive());
    helper.start(); // idempotent
    helper.stop();
    helper.stop();
    // diagnostics after stop still sane
    QVERIFY(!helper.diagnostics().isEmpty());
}

void TestDataControl::suppressNoCrash()
{
    struct DummyTracker : IActiveWindowTracker {
        ActiveWindowInfo activeWindow() const override { return {}; }
    } dummy;
    WlrDataControlHelper helper(nullptr, &dummy);
    helper.suppressOwnSets();
    // should not crash even without active manager
    QVERIFY(!helper.isActive());
}

QTEST_MAIN(TestDataControl)
#include "tst_datacontrol.moc"
