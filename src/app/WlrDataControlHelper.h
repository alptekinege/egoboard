#pragma once
#include <QObject>
#include <QTimer>
#include <QHash>
#include <QStringList>

#include "ClipboardRecord.h"

class IActiveWindowTracker;
class SettingsManager;
class QMimeData;

// Privileged Wayland helper for focus-free clipboard observation.
//
// Two protocols, same role: wlr-data-control-unstable-v1 (wlroots: Sway,
// Hyprland, ...) and its standardized successor ext-data-control-v1 (KDE
// KWin >= 6, which dropped the wlr variant in Plasma 6.5). Both are tried;
// whichever the compositor exposes is used. When neither is available the
// helper stays inactive and capture falls back to QClipboard (which on
// Wayland only sees the clipboard while our window is focused).
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

    // Paused capture: new selections are ignored until resumed.
    void setPaused(bool paused) { m_paused = paused; }
    bool isPaused() const { return m_paused; }

signals:
    void captured(const ClipboardRecord &record);
    void excludedSensitive(const QString &reason);
    void redactedSensitive(const QString &kinds); // Redact mode: secrets replaced
    void activeChanged(bool active);

private:
    void onManagerActiveChanged();
    void onExtManagerActiveChanged();
    void onDeviceSelection(void *offerId, bool primary);
    void onExtDeviceSelection(void *offerId, bool primary);
    void onOfferMime(void *offerId, const QString &mime);
    void onExtOfferMime(void *offerId, const QString &mime);
    void handleSelection(void *offerId, bool primary);
    void handleExtSelection(void *offerId, bool primary);
    // Shared second half of both selection handlers: builds the record from
    // an already-filled QMimeData, applies the sensitive-data policy and
    // emits captured(). Takes ownership of mimeData.
    void emitRecordFromMimeData(QMimeData *mimeData);
    void tryCreateDevice();
    void tryCreateExtDevice();
    void destroyDevice();
    void destroyExtDevice();

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

    class ExtManager;
    class ExtDevice;
    class ExtOffer;
    ExtManager *m_extManager = nullptr;
    ExtDevice *m_extDevice = nullptr;
    QHash<void*, OfferState> m_extOffers;
    QHash<void*, ExtOffer*> m_extOfferObjects;

    QTimer m_readDebounce;
    void *m_pendingOffer = nullptr;
    bool m_pendingPrimary = false;

    QTimer m_extReadDebounce;
    void *m_extPendingOffer = nullptr;
    bool m_extPendingPrimary = false;

    qint64 m_suppressUntilMs = 0;
    bool m_started = false;
    bool m_paused = false;
    friend class Device;
    friend class Offer;
    friend class ExtDevice;
    friend class ExtOffer;
};
