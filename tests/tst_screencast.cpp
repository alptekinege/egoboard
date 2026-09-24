#include <QtTest>

#include "ScreencastWatcher.h"

// R6 screencast awareness: the node heuristic (pure) plus the live probe
// against the local daemon (negative: cameras and audio never count).
class TestScreencast : public QObject
{
    Q_OBJECT

private slots:
    void sharingNodeMatrix();
    void liveProbeReportsInactiveWithoutShare();
    void watcherStartsInactive();
};

void TestScreencast::sharingNodeMatrix()
{
    // A camera is video too, but never screen sharing...
    QMap<QString, QString> webcam{
        {QStringLiteral("media.class"), QStringLiteral("Video/Source")},
        {QStringLiteral("node.name"), QStringLiteral("v4l2_input.pci-0000_00_1a.0")},
        {QStringLiteral("device.api"), QStringLiteral("v4l2")},
    };
    QVERIFY(!ScreencastWatcher::nodeIndicatesSharing(webcam));
    // ...neither is audio, even from an interesting application...
    QMap<QString, QString> audio{
        {QStringLiteral("media.class"), QStringLiteral("Audio/Sink")},
        {QStringLiteral("node.name"), QStringLiteral("alsa_output.pci")},
        {QStringLiteral("application.name"), QStringLiteral("xdg-desktop-portal")},
    };
    QVERIFY(!ScreencastWatcher::nodeIndicatesSharing(audio));
    // ...nor an empty or unrelated node.
    QVERIFY(!ScreencastWatcher::nodeIndicatesSharing({}));
    QMap<QString, QString> browserAudio{
        {QStringLiteral("media.class"), QStringLiteral("Stream/Output/Audio")},
        {QStringLiteral("application.name"), QStringLiteral("Zen")},
    };
    QVERIFY(!ScreencastWatcher::nodeIndicatesSharing(browserAudio));

    // Portal/compositor capture sources do: by application name...
    QMap<QString, QString> portal{
        {QStringLiteral("media.class"), QStringLiteral("Video/Source")},
        {QStringLiteral("node.name"), QStringLiteral("xdg-desktop-portal")},
        {QStringLiteral("application.name"), QStringLiteral("xdg-desktop-portal")},
    };
    QVERIFY(ScreencastWatcher::nodeIndicatesSharing(portal));
    // ...or by an explicit screencast node name.
    QMap<QString, QString> named{
        {QStringLiteral("media.class"), QStringLiteral("Video/Source")},
        {QStringLiteral("node.name"), QStringLiteral("Chrome-ScreenCast")},
    };
    QVERIFY(ScreencastWatcher::nodeIndicatesSharing(named));
}

void TestScreencast::liveProbeReportsInactiveWithoutShare()
{
    // Against the real daemon (or none at all): no active share here means
    // inactive, without hanging or crashing either way.
    ScreencastWatcher watcher;
    QVERIFY(!watcher.sharingActive());
    QVERIFY(!watcher.probeNow());
}

void TestScreencast::watcherStartsInactive()
{
    ScreencastWatcher watcher;
    QVERIFY(!watcher.sharingActive());
    watcher.start(); // slow poll + background first sweep; harmless offscreen
    QVERIFY(!watcher.sharingActive());
}

QTEST_GUILESS_MAIN(TestScreencast)
#include "tst_screencast.moc"
