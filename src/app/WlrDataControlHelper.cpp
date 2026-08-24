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
#include <QUrl>

#include <QtWaylandClient/QWaylandClientExtension>
#include <qwayland-wlr-data-control-unstable-v1.h>
#include <wayland-client.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>

namespace {

constexpr qint64 kSuppressMs = 2000;
constexpr qint64 kMaxOfferBytes = 5 * 1024 * 1024;

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
// so the wlr-data-control path enforces the same privacy rules on Wayland.
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

} // namespace

// Manager
class WlrDataControlHelper::Manager : public QWaylandClientExtensionTemplate<Manager>, public QtWayland::zwlr_data_control_manager_v1 {
    Q_OBJECT
public:
    explicit Manager() : QWaylandClientExtensionTemplate<Manager>(2) {}
};

// Offer — collects mimes for one data_offer
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

// Device — forwards selection events to helper
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
}

WlrDataControlHelper::~WlrDataControlHelper() { stop(); }

bool WlrDataControlHelper::isWayland() { return QGuiApplication::platformName() == QLatin1String("wayland"); }
QString WlrDataControlHelper::platformName() { return QGuiApplication::platformName(); }
bool WlrDataControlHelper::isSupported() { return isWayland(); }
bool WlrDataControlHelper::isActive() const { return m_manager && m_manager->isActive() && m_device; }
int WlrDataControlHelper::protocolVersion() const { return m_manager ? static_cast<int>(m_manager->QWaylandClientExtension::version()) : 0; }

QString WlrDataControlHelper::diagnostics() const {
    const QString plat = platformName();
    const bool wayland = isWayland();
    const bool active = isActive();
    if (!wayland)
        return QStringLiteral("wlr-data-control: <b>n/a</b> (Wayland-only, platform <b>%1</b>) · fallback <b>QClipboard</b> polling.").arg(plat.toHtmlEscaped());
    if (active)
        return QStringLiteral("wlr-data-control: <b>active ✓</b> (v%1) on <b>%2</b> · observes clipboard without focus.").arg(protocolVersion()).arg(plat.toHtmlEscaped());
    if (m_manager && !m_manager->isActive())
        return QStringLiteral("wlr-data-control: <b>inactive</b> — compositor does not expose <code>zwlr_data_control_manager_v1</code> or permission denied (platform <b>%1</b>). Using <b>QClipboard</b> fallback.").arg(plat.toHtmlEscaped());
    return QStringLiteral("wlr-data-control: <b>inactive</b> (no seat/device yet) on <b>%1</b> · waiting for Wayland registry.").arg(plat.toHtmlEscaped());
}

void WlrDataControlHelper::start() {
    if (m_started) return;
    m_started = true;
    if (!isSupported()) { qDebug() << "WlrDataControlHelper: not Wayland, inactive"; return; }
    m_manager = new Manager();
    m_manager->setParent(this);
    connect(m_manager, &QWaylandClientExtension::activeChanged, this, &WlrDataControlHelper::onManagerActiveChanged);
    QTimer::singleShot(500, this, &WlrDataControlHelper::onManagerActiveChanged);
}

void WlrDataControlHelper::stop() {
    destroyDevice();
    if (m_manager) { m_manager->deleteLater(); m_manager = nullptr; }
    m_started = false;
}

void WlrDataControlHelper::suppressOwnSets() { m_suppressUntilMs = QDateTime::currentMSecsSinceEpoch() + kSuppressMs; }

void WlrDataControlHelper::onManagerActiveChanged() {
    if (!m_manager || !m_manager->isActive()) { destroyDevice(); emit activeChanged(false); return; }
    tryCreateDevice();
    emit activeChanged(isActive());
}

void WlrDataControlHelper::tryCreateDevice() {
    if (m_device) return;
    if (!m_manager || !m_manager->isActive()) return;
    auto *native = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    struct wl_seat *seat = nullptr;
    if (native) { seat = native->seat(); if (!seat) seat = native->lastInputSeat(); }
    if (!seat) { qDebug() << "WlrDataControlHelper: no wl_seat yet, retry"; QTimer::singleShot(500, this, &WlrDataControlHelper::tryCreateDevice); return; }
    struct ::zwlr_data_control_device_v1 *devRaw = m_manager->get_data_device(seat);
    if (!devRaw) { qWarning() << "WlrDataControlHelper: get_data_device failed"; return; }
    m_device = new Device(this, devRaw);
    qDebug() << "WlrDataControlHelper: data device created";
}

void WlrDataControlHelper::destroyDevice() {
    if (m_device) { delete m_device; m_device = nullptr; }
    qDeleteAll(m_offerObjects); m_offerObjects.clear();
    m_offers.clear();
    m_pendingOffer = nullptr;
}

void WlrDataControlHelper::onDeviceSelection(void *offerId, bool primary) {
    if (primary && !(m_settings && m_settings->monitorPrimarySelection())) return;
    m_pendingOffer = offerId;
    m_pendingPrimary = primary;
    m_readDebounce.start();
}

void WlrDataControlHelper::onOfferMime(void *offerId, const QString &mime) {
    auto it = m_offers.find(offerId);
    if (it == m_offers.end()) { OfferState st; st.id = offerId; st.mimes.append(mime); m_offers.insert(offerId, st); }
    else it->mimes.append(mime);
}

void WlrDataControlHelper::handleSelection(void *offerId, bool primary) {
    Q_UNUSED(primary)
    if (!offerId) return;
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

    auto readMimeSync = [&](const QString &mimeStr) -> QByteArray {
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
        const int timeoutMs = 1200;
        qint64 start = QDateTime::currentMSecsSinceEpoch();
        while (out.size() < cap) {
            struct pollfd pfd; pfd.fd = pipefd[0]; pfd.events = POLLIN; pfd.revents = 0;
            int ret = poll(&pfd, 1, 80);
            if (ret > 0 && (pfd.revents & POLLIN)) {
                char buf[8192];
                ssize_t n = read(pipefd[0], buf, sizeof(buf));
                if (n > 0) {
                    const qint64 rem = cap - out.size();
                    if (n > rem) n = rem;
                    out.append(buf, n);
                    if (out.size() >= cap) break;
                    continue;
                } else if (n == 0) break;
                else if (errno != EAGAIN && errno != EWOULDBLOCK) break;
            }
            if (display) wl_display_dispatch_pending(display);
            if (QDateTime::currentMSecsSinceEpoch() - start > timeoutMs) break;
        }
        close(pipefd[0]);
        return out;
    };

    QMimeData *mimeData = new QMimeData;
    bool hasData = false;

    QByteArray uriData = readMimeSync(QStringLiteral("text/uri-list"));
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

    QByteArray imgPng = readMimeSync(QStringLiteral("image/png"));
    if (!imgPng.isEmpty()) {
        QImage img = QImage::fromData(imgPng, "PNG");
        if (!img.isNull()) { mimeData->setImageData(img); hasData = true; }
    }
    if (!hasData) {
        QByteArray imgJpeg = readMimeSync(QStringLiteral("image/jpeg"));
        if (!imgJpeg.isEmpty()) {
            QImage img = QImage::fromData(imgJpeg, "JPEG");
            if (!img.isNull()) { mimeData->setImageData(img); hasData = true; }
        }
    }

    QByteArray html = readMimeSync(QStringLiteral("text/html"));
    if (!html.isEmpty()) { mimeData->setHtml(QString::fromUtf8(html)); hasData = true; }

    QByteArray textData = readMimeSync(QStringLiteral("text/plain;charset=utf-8"));
    if (textData.isEmpty()) textData = readMimeSync(QStringLiteral("text/plain"));
    if (textData.isEmpty()) textData = readMimeSync(QStringLiteral("UTF8_STRING"));
    if (textData.isEmpty()) textData = readMimeSync(QStringLiteral("TEXT"));
    if (textData.isEmpty()) textData = readMimeSync(QStringLiteral("STRING"));
    if (!textData.isEmpty()) { mimeData->setText(QString::fromUtf8(textData)); hasData = true; }

    if (!hasData) { delete mimeData; return; }

    // Build record — reuse sensitive/ignore logic
    const QString text = mimeData->text();
    if (!text.isEmpty() && m_settings) {
        const auto mode = m_settings->sensitiveMode();
        if (mode == SettingsManager::SensitiveMode::Exclude && isSensitiveWithCustom(text, m_settings)) {
            emit excludedSensitive(customKinds(text, m_settings).join(QStringLiteral(", ")));
            delete mimeData; return;
        }
    }

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
        record.preview = singleLineLocal(mimeData->text());
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
    if (!text.isEmpty() && m_settings && m_settings->sensitiveMode()==SettingsManager::SensitiveMode::Mark)
        record.sensitive = isSensitiveWithCustom(text, m_settings);
    record.timestamp = QDateTime::currentMSecsSinceEpoch();
    ActiveWindowInfo src = m_tracker ? m_tracker->activeWindow() : ActiveWindowInfo{};
    record.sourceApp = src.appIdentifier; record.sourceWindow = src.windowTitle;
    if (m_settings && !record.sourceApp.isEmpty() && m_settings->isSourceIgnored(record.sourceApp)) return;
    emit captured(record);
}

#include "WlrDataControlHelper.moc"
