#pragma once

#include <KConfigWatcher>

#include <QObject>
#include <QString>

class QFileSystemWatcher;
class QTimer;

// Watches the desktop's kdeglobals, where Plasma keeps the active color scheme
// ([General] ColorScheme) and the active icon theme ([Icons] Theme). Switching
// either in System Settings then reaches the running app, which re-reads the
// scheme files and repaints instead of waiting for a restart.
//
// Two channels on purpose: KDE's own change notification (what System Settings
// sends) and a filesystem watch for writers that stay silent, such as a config
// file replaced by hand or by a script.
class SystemThemeWatcher : public QObject {
    Q_OBJECT
public:
    explicit SystemThemeWatcher(QObject *parent = nullptr);

    // True when a kdeglobals notification touches a theme key. An empty key list
    // means the writer did not name the keys, so the change is assumed relevant.
    static bool touchesTheme(const QString &group, const QByteArrayList &keys);

signals:
    void changed();

private:
    void armFileWatch();
    void onFileActivity();

    KConfigWatcher::Ptr m_configWatcher;
    QFileSystemWatcher *m_fileWatcher = nullptr;
    QTimer *m_coalesce = nullptr;
    QString m_lastFingerprint; // last seen state of kdeglobals
};
