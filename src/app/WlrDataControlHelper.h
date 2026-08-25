#pragma once
#include <QObject>
#include <QTimer>
#include <QHash>
#include <QStringList>

#include "ClipboardRecord.h"

class IActiveWindowTracker;
class SettingsManager;

// Privileged Wayland helper for wlr-data-control-unstable-v1.
// Observes clipboard without focus when compositor exposes it; otherwise no-op.
class WlrDataControlHelper : public QObject {
    Q_OBJECT
public:
    explicit WlrDataControlHelper(SettingsManager *settings,
                                  IActiveWindowTracker *tracker,
                                  QObject *parent = nullptr);
    ~WlrDataControlHelper() override;

    static bool isWayland();
    static QString platformName();
    static bool isSupported();

    bool isActive() const;
    int protocolVersion() const;
    QString diagnostics() const;

    void start();
    void stop();
    void suppressOwnSets();

signals:
    void captured(const ClipboardRecord &record);
    void excludedSensitive(const QString &reason);
    void redactedSensitive(const QString &kinds); // Redact mode: secrets replaced
    void activeChanged(bool active);

private:
    void onManagerActiveChanged();
    void onDeviceSelection(void *offerId, bool primary);
    void onOfferMime(void *offerId, const QString &mime);
    void handleSelection(void *offerId, bool primary);
    void tryCreateDevice();
    void destroyDevice();

    struct OfferState {
        void *id = nullptr;
        QStringList mimes;
    };

    SettingsManager *m_settings = nullptr;
    IActiveWindowTracker *m_tracker = nullptr;

    class Manager;
    class Device;
    class Offer;
    Manager *m_manager = nullptr;
    Device *m_device = nullptr;
    QHash<void*, OfferState> m_offers;
    QHash<void*, Offer*> m_offerObjects;

    QTimer m_readDebounce;
    void *m_pendingOffer = nullptr;
    bool m_pendingPrimary = false;

    qint64 m_suppressUntilMs = 0;
    bool m_started = false;
    friend class Device;
    friend class Offer;
};
