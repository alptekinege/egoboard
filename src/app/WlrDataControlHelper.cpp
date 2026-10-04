#include "WlrDataControlHelper.h"
#include "ActiveWindowTracker.h"
#include "SettingsManager.h"
#include "SensitiveDataDetector.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QTextDocument>
#include <QUrl>

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
    const auto pats = settings->customSensitivePatterns();
    for (const QString &pat : pats) {
        QRegularExpression re(pat, QRegularExpression::CaseInsensitiveOption);
        if (re.isValid() && re.match(text).hasMatch()) return true;
    }
    return false;
}

QStringList customKinds(const QString &text, SettingsManager *settings) {
    QStringList out = SensitiveDataDetector::kinds(text);
    if (!settings) return out;
    const auto pats = settings->customSensitivePatterns();
    for (const QString &pat : pats) {
        QRegularExpression re(pat, QRegularExpression::CaseInsensitiveOption);
        if (re.isValid() && re.match(text).hasMatch())
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
    if (m_device) { delete m_device; m_device = nullptr; }
    qDeleteAll(m_offerObjects); m_offerObjects.clear();
    m_offers.clear();
    m_pendingOffer = nullptr;
}

void WlrDataControlHelper::destroyExtDevice() {
    if (m_extDevice) { delete m_extDevice; m_extDevice = nullptr; }
    qDeleteAll(m_extOfferObjects); m_extOfferObjects.clear();
    m_extOffers.clear();
    m_extPendingOffer = nullptr;
}

void WlrDataControlHelper::onDeviceSelection(void *offerId, bool primary) {
    if (primary && !(m_settings && m_settings->monitorPrimarySelection())) return;

    // A new selection invalidates every earlier offer; destroying them here
    // (and dropping their wrappers) keeps the offer maps from growing on
    // every copy.
    for (auto it = m_offerObjects.begin(); it != m_offerObjects.end();) {
        if (it.key() != offerId) {
            delete it.value();
            m_offers.remove(it.key());
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
            delete it.value();
            m_extOffers.remove(it.key());
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
            QByteArray png; QBuffer buf(&png); buf.open(QIODevice::WriteOnly); img.save(&buf, "PNG");
            record.type = ContentType::Image;
            record.sizeBytes = png.size();
            if (maxStore <= 0 || png.size() <= maxStore) { record.blobData = png; record.hasBlob = true; record.preview = QStringLiteral("Image %1×%2 · %3").arg(img.width()).arg(img.height()).arg(png.size() < 1024 ? QStringLiteral("%1 B").arg(png.size()) : QStringLiteral("%1 kB").arg(png.size()/1024.0,0,'f',1)); }
            else record.preview = QStringLiteral("Image %1×%2 · not stored").arg(img.width()).arg(img.height());
            record.hash = hashPayloadLocal(ContentType::Image, png);
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
        if (maxStore > 0 && htmlStr.size() > maxStore) htmlStr.truncate(int(maxStore));
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
                if (maxStore > 0 && textLocal.size() > maxStore) { textLocal.truncate(int(maxStore)); record.preview = singleLineLocal(textLocal)+QStringLiteral(" …"); } else record.preview = singleLineLocal(textLocal);
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

    const auto typeEnabled = [this](ContentType type) {
        return !m_settings || m_settings->captureTypeEnabled(type);
    };
    // The record this selection would produce follows the same priority as the
    // build below; if the user disabled that type, stop before reading any
    // payload (pipe polls block the GUI thread and the record would be dropped).
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
        if (!typeEnabled(primaryType))
            return;
    }

    // All mime reads of one selection share a single budget: the pipe poll
    // blocks the GUI thread, so it must not be multiplied by the number of
    // advertised mime types.
    const qint64 readDeadlineMs = QDateTime::currentMSecsSinceEpoch() + kReadBudgetMs;
    auto readMimeSync = [&](const QString &mimeStr) -> QByteArray {
        if (QDateTime::currentMSecsSinceEpoch() >= readDeadlineMs) return {};
        if (!mimes.isEmpty()) {
            bool found = false;
            for (const QString &m : std::as_const(mimes))
                if (m == mimeStr || m.startsWith(mimeStr)) { found = true; break; }
            if (!found) return {};
        }
        int pipefd[2];
        if (pipe(pipefd) != 0) return {};
        fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
        offerWrap->receive(mimeStr, pipefd[1]);
        close(pipefd[1]);
        wl_display_flush(display);
        QByteArray out; out.reserve(4096);
        while (out.size() < cap) {
            struct pollfd pfd; pfd.fd = pipefd[0]; pfd.events = POLLIN; pfd.revents = 0;
            const int ret = poll(&pfd, 1, 80);
            if (ret > 0) {
                if (pfd.revents & POLLIN) {
                    char buf[8192];
                    ssize_t n = read(pipefd[0], buf, sizeof(buf));
                    if (n > 0) {
                        const qint64 rem = cap - out.size();
                        if (n > rem) n = rem;
                        out.append(buf, n);
                        if (out.size() >= cap) break;
                        continue;
                    }
                    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        // Spurious wakeup on a non-blocking pipe; keep waiting.
                    } else {
                        break; // EOF (0) or hard read error: the writer is done.
                    }
                } else if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
                    break; // writer closed and nothing left to read
                }
            } else if (ret < 0 && errno != EINTR) {
                break; // poll error
            }
            if (display) wl_display_dispatch_pending(display);
            if (QDateTime::currentMSecsSinceEpoch() >= readDeadlineMs) break;
        }
        close(pipefd[0]);
        return out;
    };

    QMimeData *mimeData = new QMimeData;
    bool hasData = false;

    QByteArray uriData = typeEnabled(ContentType::Files) ? readMimeSync(QStringLiteral("text/uri-list")) : QByteArray();
    if (!uriData.isEmpty()) {
        QList<QUrl> urls;
        const QString text = QString::fromUtf8(uriData);
        for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QString t = line.trimmed();
            if (t.startsWith(QLatin1Char('#'))) continue;
            QUrl u(t);
            if (u.isValid()) urls.append(u);
        }
        if (!urls.isEmpty()) { mimeData->setUrls(urls); hasData = true; }
    }

    QByteArray imgPng = typeEnabled(ContentType::Image) ? readMimeSync(QStringLiteral("image/png")) : QByteArray();
    if (!imgPng.isEmpty()) {
        QImage img = QImage::fromData(imgPng, "PNG");
        if (!img.isNull()) { mimeData->setImageData(img); hasData = true; }
    }
    if (!hasData && typeEnabled(ContentType::Image)) {
        QByteArray imgJpeg = readMimeSync(QStringLiteral("image/jpeg"));
        if (!imgJpeg.isEmpty()) {
            QImage img = QImage::fromData(imgJpeg, "JPEG");
            if (!img.isNull()) { mimeData->setImageData(img); hasData = true; }
        }
    }

    // Plain text is read before HTML: it feeds the preview and the
    // sensitive-data policy, so a slow/large HTML payload must never starve
    // it under the shared read budget.
    QByteArray textData;
    if (typeEnabled(ContentType::Text)) {
        textData = readMimeSync(QStringLiteral("text/plain;charset=utf-8"));
        if (textData.isEmpty()) textData = readMimeSync(QStringLiteral("text/plain"));
        if (textData.isEmpty()) textData = readMimeSync(QStringLiteral("UTF8_STRING"));
        if (textData.isEmpty()) textData = readMimeSync(QStringLiteral("TEXT"));
        if (textData.isEmpty()) textData = readMimeSync(QStringLiteral("STRING"));
    }
    if (!textData.isEmpty()) { mimeData->setText(QString::fromUtf8(textData)); hasData = true; }

    QByteArray html = typeEnabled(ContentType::RichText) ? readMimeSync(QStringLiteral("text/html")) : QByteArray();
    if (!html.isEmpty()) { mimeData->setHtml(QString::fromUtf8(html)); hasData = true; }

    if (!hasData) { delete mimeData; return; }
    emitRecordFromMimeData(mimeData);
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

    const auto typeEnabled = [this](ContentType type) {
        return !m_settings || m_settings->captureTypeEnabled(type);
    };
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
        if (!typeEnabled(primaryType))
            return;
    }

    const qint64 readDeadlineMs = QDateTime::currentMSecsSinceEpoch() + kReadBudgetMs;
    auto readMimeSync = [&](const QString &mimeStr) -> QByteArray {
        if (QDateTime::currentMSecsSinceEpoch() >= readDeadlineMs) return {};
        if (!mimes.isEmpty()) {
            bool found = false;
            for (const QString &m : std::as_const(mimes))
                if (m == mimeStr || m.startsWith(mimeStr)) { found = true; break; }
            if (!found) return {};
        }
        int pipefd[2];
        if (pipe(pipefd) != 0) return {};
        fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
        offerWrap->receive(mimeStr, pipefd[1]);
        close(pipefd[1]);
        wl_display_flush(display);
        QByteArray out; out.reserve(4096);
        while (out.size() < cap) {
            struct pollfd pfd; pfd.fd = pipefd[0]; pfd.events = POLLIN; pfd.revents = 0;
            const int ret = poll(&pfd, 1, 80);
            if (ret > 0) {
                if (pfd.revents & POLLIN) {
                    char buf[8192];
                    ssize_t n = read(pipefd[0], buf, sizeof(buf));
                    if (n > 0) {
                        const qint64 rem = cap - out.size();
                        if (n > rem) n = rem;
                        out.append(buf, n);
                        if (out.size() >= cap) break;
                        continue;
                    }
                    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        // Spurious wakeup on a non-blocking pipe; keep waiting.
                    } else {
                        break; // EOF (0) or hard read error: the writer is done.
                    }
                } else if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
                    break; // writer closed and nothing left to read
                }
            } else if (ret < 0 && errno != EINTR) {
                break; // poll error
            }
            if (display) wl_display_dispatch_pending(display);
            if (QDateTime::currentMSecsSinceEpoch() >= readDeadlineMs) break;
        }
        close(pipefd[0]);
        return out;
    };

    QMimeData *mimeData = new QMimeData;
    bool hasData = false;

    QByteArray uriData = typeEnabled(ContentType::Files) ? readMimeSync(QStringLiteral("text/uri-list")) : QByteArray();
    if (!uriData.isEmpty()) {
        QList<QUrl> urls;
        const QString text = QString::fromUtf8(uriData);
        for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QString t = line.trimmed();
            if (t.startsWith(QLatin1Char('#'))) continue;
            QUrl u(t);
            if (u.isValid()) urls.append(u);
        }
        if (!urls.isEmpty()) { mimeData->setUrls(urls); hasData = true; }
    }

    QByteArray imgPng = typeEnabled(ContentType::Image) ? readMimeSync(QStringLiteral("image/png")) : QByteArray();
    if (!imgPng.isEmpty()) {
        QImage img = QImage::fromData(imgPng, "PNG");
        if (!img.isNull()) { mimeData->setImageData(img); hasData = true; }
    }
    if (!hasData && typeEnabled(ContentType::Image)) {
        QByteArray imgJpeg = readMimeSync(QStringLiteral("image/jpeg"));
        if (!imgJpeg.isEmpty()) {
            QImage img = QImage::fromData(imgJpeg, "JPEG");
            if (!img.isNull()) { mimeData->setImageData(img); hasData = true; }
        }
    }

    // Plain text is read before HTML: it feeds the preview and the
    // sensitive-data policy, so a slow/large HTML payload must never starve
    // it under the shared read budget.
    QByteArray textData;
    if (typeEnabled(ContentType::Text)) {
        textData = readMimeSync(QStringLiteral("text/plain;charset=utf-8"));
        if (textData.isEmpty()) textData = readMimeSync(QStringLiteral("text/plain"));
        if (textData.isEmpty()) textData = readMimeSync(QStringLiteral("UTF8_STRING"));
        if (textData.isEmpty()) textData = readMimeSync(QStringLiteral("TEXT"));
        if (textData.isEmpty()) textData = readMimeSync(QStringLiteral("STRING"));
    }
    if (!textData.isEmpty()) { mimeData->setText(QString::fromUtf8(textData)); hasData = true; }

    QByteArray html = typeEnabled(ContentType::RichText) ? readMimeSync(QStringLiteral("text/html")) : QByteArray();
    if (!html.isEmpty()) { mimeData->setHtml(QString::fromUtf8(html)); hasData = true; }

    if (!hasData) { delete mimeData; return; }
    emitRecordFromMimeData(mimeData);
}

#include "WlrDataControlHelper.moc"
