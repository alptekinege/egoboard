#include "WlrDataControlHelper.h"
#include "ActiveWindowTracker.h"
#include "SettingsManager.h"
#include "SensitiveDataDetector.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QPointer>
#include <QTextDocument>
#include <QUrl>

#include <QtConcurrent>

#include <QtWaylandClient/QWaylandClientExtension>
#include <qwayland-wlr-data-control-unstable-v1.h>
#include <qwayland-ext-data-control-v1.h>
#include <wayland-client.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>

namespace {

constexpr qint64 kSuppressMs = 2000;
constexpr qint64 kMaxOfferBytes = 5 * 1024 * 1024;
constexpr qint64 kReadBudgetMs = 1200; // total mime-read budget per selection
constexpr qint64 kMaxImagePixels = 16 * 1024 * 1024;

// Decompression guard: pipe bytes are capped, but the decoded bitmap can be
// orders of magnitude larger (decompression bomb). Drop oversized images
// before they reach QMimeData/storage.
bool imageWithinBudget(const QImage &img)
{
    if (img.isNull())
        return false;
    return qint64(img.width()) * qint64(img.height()) <= kMaxImagePixels;
}

// Ordered mime candidates for one selection, mirroring the old priority:
// uri-list, then images, then plain-text variants (first hit wins), then
// HTML last so a slow/large HTML payload never starves the policy input.
// Unknown advertisements (empty list) try everything gated by type flags.
QStringList orderedMimeCandidates(bool wantFiles, bool wantImage, bool wantText,
                                  bool wantRich, const QStringList &advertised)
{
    const auto advertises = [&advertised](const QString &mime) {
        if (advertised.isEmpty())
            return true;
        for (const QString &m : advertised) {
            if (m == mime || m.startsWith(mime))
                return true;
        }
        return false;
    };
    QStringList out;
    if (wantFiles && advertises(QStringLiteral("text/uri-list")))
        out << QStringLiteral("text/uri-list");
    if (wantImage) {
        if (advertises(QStringLiteral("image/png")))
            out << QStringLiteral("image/png");
        if (advertises(QStringLiteral("image/jpeg")))
            out << QStringLiteral("image/jpeg");
    }
    if (wantText) {
        static const QStringList textMimes{
            QStringLiteral("text/plain;charset=utf-8"),
            QStringLiteral("text/plain"),
            QStringLiteral("UTF8_STRING"),
            QStringLiteral("TEXT"),
            QStringLiteral("STRING"),
        };
        for (const QString &mime : textMimes) {
            if (advertises(mime))
                out << mime;
        }
    }
    if (wantRich && advertises(QStringLiteral("text/html")))
        out << QStringLiteral("text/html");
    return out;
}

// Worker-thread pipe reader: pure POSIX poll/read, no Wayland calls, so a
// slow-loris source blocks a pool thread instead of the GUI thread. The
// shared budget uses a monotonic clock (immune to wall-clock jumps). Each
// fd is closed here; the caller must not touch them after dispatch.
QHash<QString, QByteArray> readMimeFdsOffThread(QVector<QPair<QString, int>> jobs,
                                                qint64 cap, qint64 budgetMs)
{
    QHash<QString, QByteArray> out;
    QElapsedTimer budget;
    budget.start();
    for (const auto &job : jobs) {
        if (budget.elapsed() >= budgetMs)
            break;
        const QString mime = job.first;
        const int fd = job.second;
        QByteArray data;
        data.reserve(4096);
        while (data.size() < cap && budget.elapsed() < budgetMs) {
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLIN;
            pfd.revents = 0;
            const int ret = poll(&pfd, 1, 80);
            if (ret > 0) {
                if (pfd.revents & POLLIN) {
                    char buf[8192];
                    const ssize_t n = read(fd, buf, sizeof(buf));
                    if (n > 0) {
                        const qint64 rem = cap - data.size();
                        data.append(buf, n > rem ? rem : n);
                        if (data.size() >= cap)
                            break;
                        continue;
                    }
                    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                        continue; // spurious wakeup on a non-blocking pipe
                    break; // EOF (0) or hard read error: the writer is done
                }
                if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL))
                    break; // writer closed and nothing left to read
            } else if (ret < 0 && errno != EINTR) {
                break; // poll error
            }
        }
        close(fd);
        if (!data.isEmpty())
            out.insert(mime, data);
    }
    return out;
}

QString singleLineLocal(const QString &s) {
    QString line = s.simplified();
    if (line.size() > 180) line = line.left(179) + QChar(0x2026);
    return line;
}

bool isFilePathListLocal(const QString &text, QStringList *out) {
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.isEmpty() || lines.size() > 64) return false;
    QStringList paths;
    for (const QString &l : lines) {
        const QString t = l.trimmed();
        if (!t.startsWith(QLatin1Char('/'))) return false;
        if (!QFileInfo::exists(t)) return false;
        paths.append(t);
    }
    if (out) *out = paths;
    return true;
}

QByteArray hashPayloadLocal(ContentType t, const QByteArray &p) {
    const QByteArray seed = QByteArray(contentTypeTag(t)) + '\0';
    return QCryptographicHash::hash(seed + p, QCryptographicHash::Sha256).toHex();
}

// Sensitive detection incl. user-defined patterns — must match ClipboardWatcher
// so the data-control paths enforce the same privacy rules on Wayland.
bool isSensitiveWithCustom(const QString &text, SettingsManager *settings) {
    if (SensitiveDataDetector::isSensitive(text)) return true;
    if (!settings) return false;
    const QString subject = text.size() > 20000 ? text.left(20000) : text;
    const auto pats = settings->customSensitivePatterns();
    for (const QString &pat : pats) {
        if (pat.size() > 200)
            continue;
        QRegularExpression re(pat, QRegularExpression::CaseInsensitiveOption);
        if (re.isValid() && re.match(subject).hasMatch()) return true;
    }
    return false;
}

QStringList customKinds(const QString &text, SettingsManager *settings) {
    QStringList out = SensitiveDataDetector::kinds(text);
    if (!settings) return out;
    const QString subject = text.size() > 20000 ? text.left(20000) : text;
    const auto pats = settings->customSensitivePatterns();
    for (const QString &pat : pats) {
        if (pat.size() > 200)
            continue;
        QRegularExpression re(pat, QRegularExpression::CaseInsensitiveOption);
        if (re.isValid() && re.match(subject).hasMatch())
            out << QStringLiteral("custom:%1").arg(pat.left(16));
    }
    return out;
}

// Mirrors ClipboardWatcher::applyRedaction so the data-control capture
// paths enforce Redact mode identically on Wayland.
QStringList redactCustomPatterns(QString *text, SettingsManager *settings) {
    QStringList kinds;
    if (!settings || !text || text->isEmpty()) return kinds;
    const auto pats = settings->customSensitivePatterns();
    for (const QString &pat : pats) {
        if (pat.size() > 200)
            continue;
        QRegularExpression re(pat, QRegularExpression::CaseInsensitiveOption);
        if (!re.isValid()) continue;
        QString replaced = *text;
        replaced.replace(re, QStringLiteral("••••"));
        if (replaced != *text) {
            *text = replaced;
            kinds << QStringLiteral("custom:%1").arg(pat.left(16));
        }
    }
    return kinds;
}

QStringList applyRedactionLocal(ClipboardRecord *record, const QString &plainSource,
                                SettingsManager *settings) {
    if (!record || (record->type != ContentType::Text && record->type != ContentType::RichText))
        return {};
    const QStringList enabledKinds = settings ? settings->redactKinds() : QStringList();
    QStringList redactedKinds;

    if (record->type == ContentType::Text) {
        SensitiveDataDetector::RedactionResult result =
            SensitiveDataDetector::redact(record->textData, enabledKinds);
        const QStringList custom = redactCustomPatterns(&result.text, settings);
        if (result.redactedCount == 0 && custom.isEmpty()) return {};
        const bool truncated = record->preview.endsWith(QChar(0x2026));
        record->textData = result.text;
        record->sizeBytes = result.text.toUtf8().size();
        record->preview = singleLineLocal(result.text);
        if (truncated) record->preview += QStringLiteral(" …");
        record->hash = hashPayloadLocal(ContentType::Text, result.text.toUtf8());
        record->sensitive = true;
        redactedKinds = result.redactedKinds;
        for (const QString &kind : custom)
            if (!redactedKinds.contains(kind)) redactedKinds.append(kind);
        return redactedKinds;
    }

    SensitiveDataDetector::RedactionResult html =
        SensitiveDataDetector::redact(record->textData, enabledKinds);
    const QStringList htmlCustom = redactCustomPatterns(&html.text, settings);
    if (html.redactedCount == 0 && htmlCustom.isEmpty()) return {};
    SensitiveDataDetector::RedactionResult plain =
        SensitiveDataDetector::redact(plainSource, enabledKinds);
    (void)redactCustomPatterns(&plain.text, settings);
    const bool truncated = record->preview.endsWith(QChar(0x2026));
    record->textData = html.text;
    record->sizeBytes = html.text.toUtf8().size();
    record->preview = singleLineLocal(plain.text);
    if (truncated) record->preview += QStringLiteral(" …");
    record->hash = hashPayloadLocal(ContentType::RichText, html.text.toUtf8());
    record->sensitive = true;
    redactedKinds = html.redactedKinds;
    for (const QString &kind : htmlCustom)
        if (!redactedKinds.contains(kind)) redactedKinds.append(kind);
    return redactedKinds;
}

} // namespace

// wlr Manager
class WlrDataControlHelper::Manager : public QWaylandClientExtensionTemplate<Manager>, public QtWayland::zwlr_data_control_manager_v1 {
    Q_OBJECT
public:
    explicit Manager() : QWaylandClientExtensionTemplate<Manager>(2) {}
};

// wlr Offer — collects mimes for one data_offer
class WlrDataControlHelper::Offer : public QtWayland::zwlr_data_control_offer_v1 {
public:
    Offer(WlrDataControlHelper *h, struct ::zwlr_data_control_offer_v1 *obj)
        : QtWayland::zwlr_data_control_offer_v1(obj), m_helper(h) {}
    ~Offer() override { if (isInitialized()) destroy(); }
protected:
    void zwlr_data_control_offer_v1_offer(const QString &mime) override {
        m_helper->onOfferMime(object(), mime);
    }
private:
    WlrDataControlHelper *m_helper = nullptr;
};

// wlr Device — forwards selection events to helper
class WlrDataControlHelper::Device : public QtWayland::zwlr_data_control_device_v1 {
public:
    Device(WlrDataControlHelper *h, struct ::zwlr_data_control_device_v1 *dev)
        : QtWayland::zwlr_data_control_device_v1(dev), m_helper(h) {}
    ~Device() override { if (isInitialized()) destroy(); }
protected:
    void zwlr_data_control_device_v1_data_offer(struct ::zwlr_data_control_offer_v1 *id) override {
        if (m_helper->m_offers.contains(id)) return;
        OfferState st; st.id = id;
        m_helper->m_offers.insert(id, st);
        auto *offerWrap = new Offer(m_helper, id);
        m_helper->m_offerObjects.insert(id, offerWrap);
    }
    void zwlr_data_control_device_v1_selection(struct ::zwlr_data_control_offer_v1 *id) override {
        m_helper->onDeviceSelection(id, false);
    }
    void zwlr_data_control_device_v1_finished() override {}
    void zwlr_data_control_device_v1_primary_selection(struct ::zwlr_data_control_offer_v1 *id) override {
        m_helper->onDeviceSelection(id, true);
    }
private:
    WlrDataControlHelper *m_helper = nullptr;
};

// ext Manager (standardized successor; what current KWin exposes)
class WlrDataControlHelper::ExtManager : public QWaylandClientExtensionTemplate<ExtManager>, public QtWayland::ext_data_control_manager_v1 {
    Q_OBJECT
public:
    explicit ExtManager() : QWaylandClientExtensionTemplate<ExtManager>(1) {}
};

// ext Offer
class WlrDataControlHelper::ExtOffer : public QtWayland::ext_data_control_offer_v1 {
public:
    ExtOffer(WlrDataControlHelper *h, struct ::ext_data_control_offer_v1 *obj)
        : QtWayland::ext_data_control_offer_v1(obj), m_helper(h) {}
    ~ExtOffer() override { if (isInitialized()) destroy(); }
protected:
    void ext_data_control_offer_v1_offer(const QString &mime) override {
        m_helper->onExtOfferMime(object(), mime);
    }
private:
    WlrDataControlHelper *m_helper = nullptr;
};

// ext Device
class WlrDataControlHelper::ExtDevice : public QtWayland::ext_data_control_device_v1 {
public:
    ExtDevice(WlrDataControlHelper *h, struct ::ext_data_control_device_v1 *dev)
        : QtWayland::ext_data_control_device_v1(dev), m_helper(h) {}
    ~ExtDevice() override { if (isInitialized()) destroy(); }
protected:
    void ext_data_control_device_v1_data_offer(struct ::ext_data_control_offer_v1 *id) override {
        if (m_helper->m_extOffers.contains(id)) return;
        OfferState st; st.id = id;
        m_helper->m_extOffers.insert(id, st);
        auto *offerWrap = new ExtOffer(m_helper, id);
        m_helper->m_extOfferObjects.insert(id, offerWrap);
    }
    void ext_data_control_device_v1_selection(struct ::ext_data_control_offer_v1 *id) override {
        m_helper->onExtDeviceSelection(id, false);
    }
    void ext_data_control_device_v1_finished() override {}
    void ext_data_control_device_v1_primary_selection(struct ::ext_data_control_offer_v1 *id) override {
        m_helper->onExtDeviceSelection(id, true);
    }
private:
    WlrDataControlHelper *m_helper = nullptr;
};

WlrDataControlHelper::WlrDataControlHelper(SettingsManager *settings, IActiveWindowTracker *tracker, QObject *parent)
    : QObject(parent), m_settings(settings), m_tracker(tracker)
{
    m_readDebounce.setSingleShot(true);
    m_readDebounce.setInterval(120);
    connect(&m_readDebounce, &QTimer::timeout, this, [this]{
        void *offer = m_pendingOffer;
        bool primary = m_pendingPrimary;
        m_pendingOffer = nullptr;
        handleSelection(offer, primary);
    });
    m_extReadDebounce.setSingleShot(true);
    m_extReadDebounce.setInterval(120);
    connect(&m_extReadDebounce, &QTimer::timeout, this, [this]{
        void *offer = m_extPendingOffer;
        bool primary = m_extPendingPrimary;
        m_extPendingOffer = nullptr;
        handleExtSelection(offer, primary);
    });
}

WlrDataControlHelper::~WlrDataControlHelper() { stop(); }

bool WlrDataControlHelper::isWayland() { return QGuiApplication::platformName() == QLatin1String("wayland"); }
QString WlrDataControlHelper::platformName() { return QGuiApplication::platformName(); }
bool WlrDataControlHelper::isSupported() { return isWayland(); }

bool WlrDataControlHelper::isActive() const
{
    const bool wlrActive = m_manager && m_manager->isActive() && m_device;
    const bool extActive = m_extManager && m_extManager->isActive() && m_extDevice;
    return wlrActive || extActive;
}

int WlrDataControlHelper::protocolVersion() const
{
    if (m_extManager && m_extManager->isActive() && m_extDevice)
        return static_cast<int>(m_extManager->QWaylandClientExtension::version());
    if (m_manager)
        return static_cast<int>(m_manager->QWaylandClientExtension::version());
    return 0;
}

QString WlrDataControlHelper::diagnostics() const {
    const QString plat = platformName();
    const bool wayland = isWayland();
    if (!wayland)
        return QStringLiteral("data-control: <b>n/a</b> (Wayland-only, platform <b>%1</b>) · fallback <b>QClipboard</b>.").arg(plat.toHtmlEscaped());
    const bool wlrActive = m_manager && m_manager->isActive() && m_device;
    const bool extActive = m_extManager && m_extManager->isActive() && m_extDevice;
    if (wlrActive || extActive) {
        QStringList backends;
        if (extActive)
            backends << QStringLiteral("ext-data-control <b>active ✓</b> (v%1)").arg(
                m_extManager ? static_cast<int>(m_extManager->QWaylandClientExtension::version()) : 1);
        if (wlrActive)
            backends << QStringLiteral("wlr-data-control <b>active ✓</b> (v%1)").arg(
                m_manager ? static_cast<int>(m_manager->QWaylandClientExtension::version()) : 2);
        return QStringLiteral("data-control: %1 on <b>%2</b> · observes clipboard without focus.")
            .arg(backends.join(QStringLiteral(" + ")), plat.toHtmlEscaped());
    }
    const bool wlrMissing = !m_manager || !m_manager->isActive();
    const bool extMissing = !m_extManager || !m_extManager->isActive();
    if (wlrMissing && extMissing)
        return QStringLiteral("data-control: <b>inactive</b> — compositor exposes neither <code>ext_data_control_manager_v1</code> nor <code>zwlr_data_control_manager_v1</code> (platform <b>%1</b>). Using <b>QClipboard</b> fallback (focused-window only).").arg(plat.toHtmlEscaped());
    return QStringLiteral("data-control: <b>inactive</b> (no seat/device yet) on <b>%1</b> · waiting for Wayland registry.").arg(plat.toHtmlEscaped());
}

void WlrDataControlHelper::start() {
    if (m_started) return;
    m_started = true;
    if (!isSupported()) { qDebug() << "WlrDataControlHelper: not Wayland, inactive"; return; }
    m_manager = new Manager();
    m_manager->setParent(this);
    connect(m_manager, &QWaylandClientExtension::activeChanged, this, &WlrDataControlHelper::onManagerActiveChanged);
    m_extManager = new ExtManager();
    m_extManager->setParent(this);
    connect(m_extManager, &QWaylandClientExtension::activeChanged, this, &WlrDataControlHelper::onExtManagerActiveChanged);
    QTimer::singleShot(500, this, &WlrDataControlHelper::onManagerActiveChanged);
    QTimer::singleShot(500, this, &WlrDataControlHelper::onExtManagerActiveChanged);
}

void WlrDataControlHelper::stop() {
    ++m_readGeneration; // drop any in-flight async read on completion
    destroyDevice();
    destroyExtDevice();
    if (m_manager) { m_manager->deleteLater(); m_manager = nullptr; }
    if (m_extManager) { m_extManager->deleteLater(); m_extManager = nullptr; }
    m_started = false;
}

void WlrDataControlHelper::suppressOwnSets() { m_suppressUntilMs = QDateTime::currentMSecsSinceEpoch() + kSuppressMs; }

void WlrDataControlHelper::onManagerActiveChanged() {
    if (!m_manager || !m_manager->isActive()) { destroyDevice(); emit activeChanged(isActive()); return; }
    tryCreateDevice();
    emit activeChanged(isActive());
}

void WlrDataControlHelper::onExtManagerActiveChanged() {
    if (!m_extManager || !m_extManager->isActive()) { destroyExtDevice(); emit activeChanged(isActive()); return; }
    tryCreateExtDevice();
    emit activeChanged(isActive());
}

static struct wl_seat *waylandSeat()
{
    auto *native = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    struct wl_seat *seat = nullptr;
    if (native) { seat = native->seat(); if (!seat) seat = native->lastInputSeat(); }
    return seat;
}

void WlrDataControlHelper::tryCreateDevice() {
    if (m_device) return;
    if (!m_manager || !m_manager->isActive()) return;
    struct wl_seat *seat = waylandSeat();
    if (!seat) { qDebug() << "WlrDataControlHelper: no wl_seat yet, retry"; QTimer::singleShot(500, this, &WlrDataControlHelper::tryCreateDevice); return; }
    struct ::zwlr_data_control_device_v1 *devRaw = m_manager->get_data_device(seat);
    if (!devRaw) { qWarning() << "WlrDataControlHelper: wlr get_data_device failed"; return; }
    m_device = new Device(this, devRaw);
    qDebug() << "WlrDataControlHelper: wlr data device created";
}

void WlrDataControlHelper::tryCreateExtDevice() {
    if (m_extDevice) return;
    if (!m_extManager || !m_extManager->isActive()) return;
    struct wl_seat *seat = waylandSeat();
    if (!seat) { qDebug() << "WlrDataControlHelper: no wl_seat yet, retry (ext)"; QTimer::singleShot(500, this, &WlrDataControlHelper::tryCreateExtDevice); return; }
    struct ::ext_data_control_device_v1 *devRaw = m_extManager->get_data_device(seat);
    if (!devRaw) { qWarning() << "WlrDataControlHelper: ext get_data_device failed"; return; }
    m_extDevice = new ExtDevice(this, devRaw);
    qDebug() << "WlrDataControlHelper: ext data device created";
}

void WlrDataControlHelper::destroyDevice() {
    // Never delete protocol objects synchronously inside a Wayland dispatch:
    // defer to the next event-loop turn so in-flight offer/finished events
    // cannot hit a freed wrapper.
    if (m_device) {
        auto *dead = m_device;
        m_device = nullptr;
        QTimer::singleShot(0, [dead] { delete dead; });
    }
    for (auto *offer : std::as_const(m_offerObjects))
        QTimer::singleShot(0, [offer] { delete offer; });
    m_offerObjects.clear();
    m_offers.clear();
    m_pendingOffer = nullptr;
}

void WlrDataControlHelper::destroyExtDevice() {
    if (m_extDevice) {
        auto *dead = m_extDevice;
        m_extDevice = nullptr;
        QTimer::singleShot(0, [dead] { delete dead; });
    }
    for (auto *offer : std::as_const(m_extOfferObjects))
        QTimer::singleShot(0, [offer] { delete offer; });
    m_extOfferObjects.clear();
    m_extOffers.clear();
    m_extPendingOffer = nullptr;
}

void WlrDataControlHelper::onDeviceSelection(void *offerId, bool primary) {
    if (primary && !(m_settings && m_settings->monitorPrimarySelection())) return;

    // A new selection invalidates every earlier offer; defer-destroy them
    // (never delete inside the protocol callback: the compositor may still
    // deliver events for them) and drop their wrappers from the maps.
    for (auto it = m_offerObjects.begin(); it != m_offerObjects.end();) {
        if (it.key() != offerId) {
            auto *dead = it.value();
            QTimer::singleShot(0, [dead] { delete dead; });
            m_offers.remove(it.key());
            if (m_pendingOffer == it.key())
                m_pendingOffer = nullptr;
            it = m_offerObjects.erase(it);
        } else {
            ++it;
        }
    }

    m_pendingOffer = offerId;
    m_pendingPrimary = primary;
    m_readDebounce.start();
}

void WlrDataControlHelper::onExtDeviceSelection(void *offerId, bool primary) {
    if (primary && !(m_settings && m_settings->monitorPrimarySelection())) return;

    for (auto it = m_extOfferObjects.begin(); it != m_extOfferObjects.end();) {
        if (it.key() != offerId) {
            auto *dead = it.value();
            QTimer::singleShot(0, [dead] { delete dead; });
            m_extOffers.remove(it.key());
            if (m_extPendingOffer == it.key())
                m_extPendingOffer = nullptr;
            it = m_extOfferObjects.erase(it);
        } else {
            ++it;
        }
    }

    m_extPendingOffer = offerId;
    m_extPendingPrimary = primary;
    m_extReadDebounce.start();
}

void WlrDataControlHelper::onOfferMime(void *offerId, const QString &mime) {
    auto it = m_offers.find(offerId);
    if (it == m_offers.end()) { OfferState st; st.id = offerId; st.mimes.append(mime); m_offers.insert(offerId, st); }
    else it->mimes.append(mime);
}

void WlrDataControlHelper::onExtOfferMime(void *offerId, const QString &mime) {
    auto it = m_extOffers.find(offerId);
    if (it == m_extOffers.end()) { OfferState st; st.id = offerId; st.mimes.append(mime); m_extOffers.insert(offerId, st); }
    else it->mimes.append(mime);
}

void WlrDataControlHelper::emitRecordFromMimeData(QMimeData *mimeData)
{
    // Privacy check + record build; the capture-type filter runs at the end.
    if (!mimeData) return;
    const QString text = mimeData->text();
    ClipboardRecord record;
    const qint64 maxStore = m_settings ? m_settings->maxItemBytes() : kMaxOfferBytes;

    if (mimeData->hasImage()) {
        QImage img = qvariant_cast<QImage>(mimeData->imageData());
        if (!img.isNull()) {
            constexpr qint64 kMaxPixels = 16 * 1024 * 1024;
            const qint64 pixels = qint64(img.width()) * qint64(img.height());
            if (pixels <= 0 || pixels > kMaxPixels) {
                record.type = ContentType::Image;
                record.preview = QStringLiteral("Image %1×%2 · not stored").arg(img.width()).arg(img.height());
                record.hash = hashPayloadLocal(ContentType::Image, QByteArray::number(pixels));
            } else {
                QByteArray png; QBuffer buf(&png); buf.open(QIODevice::WriteOnly); img.save(&buf, "PNG");
                record.type = ContentType::Image;
                record.sizeBytes = png.size();
                if (maxStore <= 0 || png.size() <= maxStore) { record.blobData = png; record.hasBlob = true; record.preview = QStringLiteral("Image %1×%2 · %3").arg(img.width()).arg(img.height()).arg(png.size() < 1024 ? QStringLiteral("%1 B").arg(png.size()) : QStringLiteral("%1 kB").arg(png.size()/1024.0,0,'f',1)); }
                else record.preview = QStringLiteral("Image %1×%2 · not stored").arg(img.width()).arg(img.height());
                record.hash = hashPayloadLocal(ContentType::Image, png);
            }
        }
    } else if (mimeData->hasUrls()) {
        const auto urls = mimeData->urls();
        QStringList paths; bool allLocal = !urls.isEmpty();
        for (auto &u: urls) { if (!u.isLocalFile()) allLocal=false; else paths.append(u.toLocalFile()); }
        if (allLocal) {
            record.type = ContentType::Files;
            QJsonArray a; for (auto &p: paths) a.append(p);
            record.textData = QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
            record.sizeBytes = record.textData.toUtf8().size();
            QString names; for (auto &p: paths) { if (!names.isEmpty()) names+=QStringLiteral(", "); names+=QFileInfo(p).fileName(); }
            record.preview = QStringLiteral("%1 %2: %3").arg(paths.size()).arg(paths.size()==1?QStringLiteral("file"):QStringLiteral("files")).arg(singleLineLocal(names));
            record.hash = hashPayloadLocal(ContentType::Files, record.textData.toUtf8());
        }
    }
    if (record.hash.isEmpty() && mimeData->hasHtml()) {
        QString htmlStr = mimeData->html();
        const qint64 safeStore = qBound<qint64>(0, maxStore, kMaxOfferBytes);
        if (safeStore > 0 && qint64(htmlStr.size()) > safeStore)
            htmlStr.truncate(qsizetype(safeStore));
        record.type = ContentType::RichText;
        record.textData = htmlStr; record.sizeBytes = htmlStr.toUtf8().size();
        QString plainForPreview = mimeData->text();
        if (plainForPreview.isEmpty()) {
            // Plain flavor missing (disabled or unreadable): derive the
            // preview from the HTML so the entry never shows up blank.
            QTextDocument document;
            document.setHtml(htmlStr);
            plainForPreview = document.toPlainText();
        }
        record.preview = singleLineLocal(plainForPreview);
        record.hash = hashPayloadLocal(ContentType::RichText, htmlStr.toUtf8());
    }
    if (record.hash.isEmpty() && mimeData->hasText()) {
        QString textLocal = mimeData->text();
        if (!textLocal.isEmpty()) {
            QStringList paths;
            if (isFilePathListLocal(textLocal, &paths)) {
                record.type = ContentType::Files;
                QJsonArray a; for (auto &p: paths) a.append(p);
                record.textData = QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
                record.sizeBytes = record.textData.toUtf8().size();
                QString names; for (auto &p: paths) { if (!names.isEmpty()) names+=QStringLiteral(", "); names+=QFileInfo(p).fileName(); }
                record.preview = QStringLiteral("%1 %2: %3").arg(paths.size()).arg(paths.size()==1?QStringLiteral("file"):QStringLiteral("files")).arg(singleLineLocal(names));
                record.hash = hashPayloadLocal(ContentType::Files, record.textData.toUtf8());
            } else {
                record.type = ContentType::Text;
                const qint64 safeStore = qBound<qint64>(0, maxStore, kMaxOfferBytes);
                if (safeStore > 0 && qint64(textLocal.size()) > safeStore) { textLocal.truncate(qsizetype(safeStore)); record.preview = singleLineLocal(textLocal)+QStringLiteral(" …"); } else record.preview = singleLineLocal(textLocal);
                record.textData = textLocal; record.sizeBytes = textLocal.toUtf8().size();
                record.hash = hashPayloadLocal(ContentType::Text, textLocal.toUtf8());
            }
        }
    }
    delete mimeData;
    if (record.hash.isEmpty()) return;
    // Same capture-type filter the QClipboard watcher applies.
    if (m_settings && !m_settings->captureTypeEnabled(record.type))
        return;
    if (!text.isEmpty() && m_settings) {
        switch (m_settings->sensitiveMode()) {
        case SettingsManager::SensitiveMode::Exclude:
            if (isSensitiveWithCustom(text, m_settings)) {
                emit excludedSensitive(customKinds(text, m_settings).join(QStringLiteral(", ")));
                return;
            }
            break;
        case SettingsManager::SensitiveMode::Mark:
            record.sensitive = isSensitiveWithCustom(text, m_settings);
            break;
        case SettingsManager::SensitiveMode::Redact: {
            const QStringList kinds = applyRedactionLocal(&record, text, m_settings);
            if (!kinds.isEmpty())
                emit redactedSensitive(kinds.join(QStringLiteral(", ")));
            break;
        }
        case SettingsManager::SensitiveMode::Off:
            break;
        }
    }
    record.timestamp = QDateTime::currentMSecsSinceEpoch();
    ActiveWindowInfo src = m_tracker ? m_tracker->activeWindow() : ActiveWindowInfo{};
    record.sourceApp = src.appIdentifier; record.sourceWindow = src.windowTitle;
    if (m_settings && !record.sourceApp.isEmpty() && m_settings->isSourceIgnored(record.sourceApp)) return;
    emit captured(record);
}

void WlrDataControlHelper::fetchMimesAsync(
    const QStringList &candidates, const QStringList &advertised, qint64 cap,
    bool wantFiles, bool wantImage, bool wantText, bool wantRich,
    const std::function<void(const QString &, int)> &receive)
{
    Q_UNUSED(advertised);
    // Pipes + receive() stay on the GUI thread (Wayland objects are not
    // thread-safe); only the blocking poll/read loop leaves it.
    QVector<QPair<QString, int>> jobs;
    jobs.reserve(candidates.size());
    for (const QString &mime : candidates) {
        int pipefd[2];
        if (pipe(pipefd) != 0)
            continue;
        fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
        receive(mime, pipefd[1]);
        close(pipefd[1]);
        jobs.append({mime, pipefd[0]});
    }
    if (jobs.isEmpty())
        return;
    const int generation = ++m_readGeneration;
    QPointer<WlrDataControlHelper> guard(this);
    auto *watcher = new QFutureWatcher<QHash<QString, QByteArray>>(this);
    connect(watcher, &QFutureWatcher<QHash<QString, QByteArray>>::finished, this,
            [this, guard, watcher, cap, wantFiles, wantImage,
             wantText, wantRich, generation]() {
                watcher->deleteLater();
                if (!guard || generation != m_readGeneration)
                    return; // superseded by a newer selection, or stopped
                if (m_paused)
                    return;
                if (QDateTime::currentMSecsSinceEpoch() < m_suppressUntilMs)
                    return; // a paste-back landed while reading
                buildMimeFromBlobs(watcher->result(), wantFiles, wantImage, wantText,
                                   wantRich, generation);
            });
    watcher->setFuture(QtConcurrent::run(readMimeFdsOffThread, std::move(jobs), cap,
                                         kReadBudgetMs));
}

void WlrDataControlHelper::buildMimeFromBlobs(const QHash<QString, QByteArray> &blobs,
                                              bool wantFiles, bool wantImage,
                                              bool wantText, bool wantRich,
                                              int generation)
{
    if (generation != m_readGeneration)
        return;
    auto *mimeData = new QMimeData;
    bool hasData = false;

    if (wantFiles) {
        const QByteArray uriData = blobs.value(QStringLiteral("text/uri-list"));
        if (!uriData.isEmpty()) {
            QList<QUrl> urls;
            const QString text = QString::fromUtf8(uriData);
            for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                const QString t = line.trimmed();
                if (t.startsWith(QLatin1Char('#')))
                    continue;
                QUrl u(t);
                if (u.isValid())
                    urls.append(u);
            }
            if (!urls.isEmpty()) {
                mimeData->setUrls(urls);
                hasData = true;
            }
        }
    }

    if (wantImage) {
        const QByteArray imgPng = blobs.value(QStringLiteral("image/png"));
        if (!imgPng.isEmpty()) {
            const QImage img = QImage::fromData(imgPng, "PNG");
            if (imageWithinBudget(img)) {
                mimeData->setImageData(img);
                hasData = true;
            }
        }
        if (!hasData) {
            const QByteArray imgJpeg = blobs.value(QStringLiteral("image/jpeg"));
            if (!imgJpeg.isEmpty()) {
                const QImage img = QImage::fromData(imgJpeg, "JPEG");
                if (imageWithinBudget(img)) {
                    mimeData->setImageData(img);
                    hasData = true;
                }
            }
        }
    }

    // Plain text before HTML: it feeds the preview and the sensitive-data
    // policy, so a slow/large HTML payload must never starve it.
    if (wantText) {
        static const QStringList textMimes{
            QStringLiteral("text/plain;charset=utf-8"),
            QStringLiteral("text/plain"),
            QStringLiteral("UTF8_STRING"),
            QStringLiteral("TEXT"),
            QStringLiteral("STRING"),
        };
        for (const QString &mime : textMimes) {
            const QByteArray raw = blobs.value(mime);
            if (!raw.isEmpty()) {
                mimeData->setText(QString::fromUtf8(raw));
                hasData = true;
                break;
            }
        }
    }

    if (wantRich) {
        const QByteArray html = blobs.value(QStringLiteral("text/html"));
        if (!html.isEmpty()) {
            mimeData->setHtml(QString::fromUtf8(html));
            hasData = true;
        }
    }

    if (!hasData) {
        delete mimeData;
        return;
    }
    emitRecordFromMimeData(mimeData);
}

void WlrDataControlHelper::handleSelection(void *offerId, bool primary) {
    Q_UNUSED(primary)
    if (!offerId) return;
    if (m_paused) return; // capture paused (manual or session locked)
    if (QDateTime::currentMSecsSinceEpoch() < m_suppressUntilMs) return;

    auto it = m_offers.find(offerId);
    QStringList mimes = (it != m_offers.end()) ? it->mimes : QStringList{};
    auto offerIt = m_offerObjects.find(offerId);
    if (offerIt == m_offerObjects.end()) return;
    Offer *offerWrap = offerIt.value();
    if (!offerWrap) return;

    auto *native = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    struct wl_display *display = native ? native->display() : nullptr;
    if (!display) return;

    const qint64 maxBytes = m_settings ? m_settings->maxItemBytes() : kMaxOfferBytes;
    const qint64 cap = (maxBytes > 0) ? qMin(maxBytes, kMaxOfferBytes) : kMaxOfferBytes;

    // Snapshot type flags on the GUI thread: SettingsManager is only ever
    // touched here, never on the reader worker.
    const bool wantFiles = !m_settings || m_settings->captureTypeEnabled(ContentType::Files);
    const bool wantImage = !m_settings || m_settings->captureTypeEnabled(ContentType::Image);
    const bool wantText = !m_settings || m_settings->captureTypeEnabled(ContentType::Text);
    const bool wantRich =
        !m_settings || m_settings->captureTypeEnabled(ContentType::RichText);
    // The record this selection would produce follows the same priority as
    // the build below; if the user disabled that type, stop before issuing
    // any receive() (the record would be dropped anyway).
    if (!mimes.isEmpty()) {
        ContentType primaryType = ContentType::Text;
        const auto advertises = [&mimes](const char *mime) {
            for (const QString &m : mimes)
                if (m == QLatin1String(mime))
                    return true;
            return false;
        };
        if (advertises("image/png") || advertises("image/jpeg"))
            primaryType = ContentType::Image;
        else if (advertises("text/uri-list"))
            primaryType = ContentType::Files;
        else if (advertises("text/html"))
            primaryType = ContentType::RichText;
        const bool primaryEnabled = !m_settings || m_settings->captureTypeEnabled(primaryType);
        if (!primaryEnabled)
            return;
    }

    const QStringList candidates =
        orderedMimeCandidates(wantFiles, wantImage, wantText, wantRich, mimes);
    if (candidates.isEmpty())
        return;
    // Pipes + receive() run here on the GUI thread; the blocking poll/read
    // loop runs on a worker thread, so a slow-loris source cannot freeze
    // the UI for the whole read budget.
    fetchMimesAsync(candidates, mimes, cap, wantFiles, wantImage, wantText, wantRich,
                    [offerWrap](const QString &mime, int writeFd) {
                        offerWrap->receive(mime, writeFd);
                    });
    wl_display_flush(display);
}

void WlrDataControlHelper::handleExtSelection(void *offerId, bool primary) {
    Q_UNUSED(primary)
    if (!offerId) return;
    if (m_paused) return; // capture paused (manual or session locked)
    if (QDateTime::currentMSecsSinceEpoch() < m_suppressUntilMs) return;

    auto it = m_extOffers.find(offerId);
    QStringList mimes = (it != m_extOffers.end()) ? it->mimes : QStringList{};
    auto offerIt = m_extOfferObjects.find(offerId);
    if (offerIt == m_extOfferObjects.end()) return;
    ExtOffer *offerWrap = offerIt.value();
    if (!offerWrap) return;

    auto *native = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    struct wl_display *display = native ? native->display() : nullptr;
    if (!display) return;

    const qint64 maxBytes = m_settings ? m_settings->maxItemBytes() : kMaxOfferBytes;
    const qint64 cap = (maxBytes > 0) ? qMin(maxBytes, kMaxOfferBytes) : kMaxOfferBytes;

    // Snapshot type flags on the GUI thread: SettingsManager is only ever
    // touched here, never on the reader worker.
    const bool wantFiles = !m_settings || m_settings->captureTypeEnabled(ContentType::Files);
    const bool wantImage = !m_settings || m_settings->captureTypeEnabled(ContentType::Image);
    const bool wantText = !m_settings || m_settings->captureTypeEnabled(ContentType::Text);
    const bool wantRich =
        !m_settings || m_settings->captureTypeEnabled(ContentType::RichText);
    if (!mimes.isEmpty()) {
        ContentType primaryType = ContentType::Text;
        const auto advertises = [&mimes](const char *mime) {
            for (const QString &m : mimes)
                if (m == QLatin1String(mime))
                    return true;
            return false;
        };
        if (advertises("image/png") || advertises("image/jpeg"))
            primaryType = ContentType::Image;
        else if (advertises("text/uri-list"))
            primaryType = ContentType::Files;
        else if (advertises("text/html"))
            primaryType = ContentType::RichText;
        const bool primaryEnabled = !m_settings || m_settings->captureTypeEnabled(primaryType);
        if (!primaryEnabled)
            return;
    }

    const QStringList candidates =
        orderedMimeCandidates(wantFiles, wantImage, wantText, wantRich, mimes);
    if (candidates.isEmpty())
        return;
    // Pipes + receive() run here on the GUI thread; the blocking poll/read
    // loop runs on a worker thread, so a slow-loris source cannot freeze
    // the UI for the whole read budget.
    fetchMimesAsync(candidates, mimes, cap, wantFiles, wantImage, wantText, wantRich,
                    [offerWrap](const QString &mime, int writeFd) {
                        offerWrap->receive(mime, writeFd);
                    });
    wl_display_flush(display);
}

#include "WlrDataControlHelper.moc"
