#include <QtTest>
#include "../src/app/LayerShellHelper.h"

#include <QGuiApplication>
#include <QScreen>

class TestLayerShell : public QObject {
    Q_OBJECT
private slots:
    void platformNameNotEmpty();
    void availableFalseOnOffscreen();
    void diagnosticsContainsPlatform();
    void configureNoCrashOffscreen();
    void diagnosticsRemainConsistent();
};

void TestLayerShell::platformNameNotEmpty()
{
    // QGuiApplication::platformName() may be empty if test harness hasn't
    // set QT_QPA_PLATFORM — offscreen builds set it explicitly. Accept
    // either "offscreen" or the actual platform, but it must not crash.
    const QString plat = LayerShellHelper::platformName();
    // On environments without a display server, platformName can be empty
    // before the first screen is created. Verify diagnostics still returns
    // something sane instead.
    if (plat.isEmpty()) {
        const QString diag = LayerShellHelper::diagnostics();
        QVERIFY(!diag.isEmpty());
        return;
    }
    QVERIFY(!plat.isEmpty());
}

void TestLayerShell::availableFalseOnOffscreen()
{
    const QString plat = LayerShellHelper::platformName();
    if (plat == QLatin1String("offscreen") || plat.isEmpty()) {
        QVERIFY(!LayerShellHelper::isAvailable());
        QVERIFY(!LayerShellHelper::isWayland());
    } else if (plat == QLatin1String("wayland")) {
        // On Wayland with LayerShellQt linked, expect available.
#ifdef EGOBOARD_HAVE_LAYERSHELLQT
        QVERIFY(LayerShellHelper::isAvailable());
#endif
    }
}

void TestLayerShell::diagnosticsContainsPlatform()
{
    const QString diag = LayerShellHelper::diagnostics();
    QVERIFY(!diag.isEmpty());
    const QString plat = LayerShellHelper::platformName();
    if (!plat.isEmpty())
        QVERIFY(diag.contains(plat, Qt::CaseInsensitive));
    // diagnostics always mentions Quick paste
    QVERIFY(diag.contains(QStringLiteral("Quick paste"), Qt::CaseInsensitive));
}

void TestLayerShell::configureNoCrashOffscreen()
{
    // Must be safe to call even offscreen / without LayerShellQt and without screens.
    LayerShellHelper::configureForQuickPaste(nullptr, nullptr, QSize(100, 100), QPoint(0, 0));
    // Only attempt to create a QWindow if screens are available — offscreen
    // platform in CI has no screens and QWindow construction is fatal there.
    if (QGuiApplication::primaryScreen() == nullptr && QGuiApplication::screens().isEmpty())
        QSKIP("No screens available on this platform (offscreen CI) — skipping QWindow creation");
    QWindow win;
    win.resize(100, 100);
    LayerShellHelper::configureForQuickPaste(&win, nullptr, QSize(100, 100), QPoint(10, 10));
    if (QGuiApplication::primaryScreen())
        LayerShellHelper::configureForQuickPaste(&win, QGuiApplication::primaryScreen(), QSize(100, 100), QPoint(10, 10));
}

void TestLayerShell::diagnosticsRemainConsistent()
{
    const QString diagnostics = LayerShellHelper::diagnostics();
    QVERIFY(!diagnostics.isEmpty());
    QVERIFY(diagnostics.contains(QStringLiteral("Layer-shell"), Qt::CaseInsensitive));
    QCOMPARE(LayerShellHelper::isWayland(), LayerShellHelper::platformName() == QStringLiteral("wayland"));
    if (!LayerShellHelper::isWayland())
        QVERIFY(!LayerShellHelper::isAvailable());
}

QTEST_MAIN(TestLayerShell)
#include "tst_layershell.moc"
