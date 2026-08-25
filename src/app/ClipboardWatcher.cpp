#include "ClipboardWatcher.h"

#include "ActiveWindowTracker.h"
#include "SettingsManager.h"
#include "SensitiveDataDetector.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QRegularExpression>

namespace {

constexpr int kMaxPathCount = 64; // beyond this, treat as plain text
constexpr qint64 kSuppressOwnSetMs = 2000;

QByteArray hashPayload(ContentType type, const QByteArray &payload)
{
    const QByteArray seed = QByteArray(contentTypeTag(type)) + '\0';
    return QCryptographicHash::hash(seed + payload, QCryptographicHash::Sha256).toHex();
}

QString singleLine(const QString &text, int maxLength = 180)
{
    QString line = text.simplified();
    if (line.size() > maxLength)
        line = line.left(maxLength - 1) + QChar(0x2026);
    return line;
}

QString humanSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 kB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

// True when every non-empty line is an absolute path to an existing local file.
bool isFilePathList(const QString &text, QStringList *pathsOut)
{
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.isEmpty() || lines.size() > kMaxPathCount)
        return false;
    QStringList paths;
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith(QLatin1Char('/')))
            return false;
        if (!QFileInfo::exists(trimmed))
            return false;
        paths.append(trimmed);
    }
    if (pathsOut)
        *pathsOut = paths;
    return true;
}

bool isSensitiveWithCustom(const QString &text, SettingsManager *settings)
{
    if (SensitiveDataDetector::isSensitive(text)) return true;
    if (!settings) return false;
    const auto pats = settings->customSensitivePatterns();
    for (const QString &pat : pats) {
        QRegularExpression re(pat, QRegularExpression::CaseInsensitiveOption);
        if (re.isValid() && re.match(text).hasMatch()) return true;
    }
    return false;
}

QStringList customKinds(const QString &text, SettingsManager *settings)
{
    QStringList out = SensitiveDataDetector::kinds(text);
    if (!settings) return out;
    const auto pats = settings->customSensitivePatterns();
    for (const QString &pat : pats) {
        QRegularExpression re(pat, QRegularExpression::CaseInsensitiveOption);
        if (re.isValid() && re.match(text).hasMatch()) out << QStringLiteral("custom:%1").arg(pat.left(16));
    }
    return out;
}

// Replaces every match of the user's custom patterns with "••••" (returns the
// custom kinds that fired; ex. "custom:secret-\\w+").
QStringList redactCustomPatterns(QString *text, SettingsManager *settings)
{
    QStringList kinds;
    if (!settings || !text || text->isEmpty())
        return kinds;
    const auto pats = settings->customSensitivePatterns();
    for (const QString &pat : pats) {
        QRegularExpression re(pat, QRegularExpression::CaseInsensitiveOption);
        if (!re.isValid())
            continue;
        QString replaced = *text;
        replaced.replace(re, QStringLiteral("••••"));
        if (replaced != *text) {
            *text = replaced;
            kinds << QStringLiteral("custom:%1").arg(pat.left(16));
        }
    }
    return kinds;
}

// Redact mode: replaces detected secrets in a Text/RichText record payload
// with "••••", re-hashing from the redacted content so dedup and FTS stay
// consistent. Returns the kinds actually redacted (empty when nothing was).
QStringList applyRedaction(ClipboardRecord *record, const QString &plainSource,
                           SettingsManager *settings)
{
    if (!record || (record->type != ContentType::Text && record->type != ContentType::RichText))
        return {};
    const QStringList enabledKinds = settings ? settings->redactKinds() : QStringList();
    QStringList redactedKinds;

    if (record->type == ContentType::Text) {
        SensitiveDataDetector::RedactionResult result =
            SensitiveDataDetector::redact(record->textData, enabledKinds);
        const QStringList custom = redactCustomPatterns(&result.text, settings);
        if (result.redactedCount == 0 && custom.isEmpty())
            return {};
        const bool truncated = record->preview.endsWith(QChar(0x2026));
        record->textData = result.text;
        record->sizeBytes = result.text.toUtf8().size();
        record->preview = singleLine(result.text);
        if (truncated)
            record->preview += QStringLiteral(" …");
        record->hash = hashPayload(ContentType::Text, result.text.toUtf8());
        record->sensitive = true;
        redactedKinds = result.redactedKinds;
        for (const QString &kind : custom)
            if (!redactedKinds.contains(kind))
                redactedKinds.append(kind);
        return redactedKinds;
    }

    // RichText: the HTML payload is the source of truth (what gets stored).
    SensitiveDataDetector::RedactionResult html =
        SensitiveDataDetector::redact(record->textData, enabledKinds);
    const QStringList htmlCustom = redactCustomPatterns(&html.text, settings);
    if (html.redactedCount == 0 && htmlCustom.isEmpty())
        return {};
    SensitiveDataDetector::RedactionResult plain =
        SensitiveDataDetector::redact(plainSource, enabledKinds);
    (void)redactCustomPatterns(&plain.text, settings);
    const bool truncated = record->preview.endsWith(QChar(0x2026));
    record->textData = html.text;
    record->sizeBytes = html.text.toUtf8().size();
    record->preview = singleLine(plain.text);
    if (truncated)
        record->preview += QStringLiteral(" …");
    record->hash = hashPayload(ContentType::RichText, html.text.toUtf8());
    record->sensitive = true;
    redactedKinds = html.redactedKinds;
    for (const QString &kind : htmlCustom)
        if (!redactedKinds.contains(kind))
            redactedKinds.append(kind);
    return redactedKinds;
}

} // namespace

ClipboardWatcher::ClipboardWatcher(QClipboard *clipboard, SettingsManager *settings,
                                   IActiveWindowTracker *activeWindow, QObject *parent)
    : QObject(parent)
    , m_clipboard(clipboard)
    , m_settings(settings)
    , m_activeWindow(activeWindow)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(settings ? settings->debounceMs() : 250);
    connect(&m_debounce, &QTimer::timeout, this, &ClipboardWatcher::processPending);
}

void ClipboardWatcher::start()
{
    connect(m_clipboard, &QClipboard::dataChanged, this,
            [this] { onClipboardChanged(QClipboard::Clipboard); });
    if (m_settings && m_settings->monitorPrimarySelection()) {
        connect(m_clipboard, &QClipboard::selectionChanged, this,
                [this] { onClipboardChanged(QClipboard::Selection); });
    }
    // Capture whatever is on the clipboard right now so a fresh install has
    // a sensible first entry.
    QTimer::singleShot(500, this, [this] {
        m_pendingMode = QClipboard::Clipboard;
        processPending();
    });
}

void ClipboardWatcher::suppressOwnSets()
{
    m_suppressUntilEpochMs = QDateTime::currentMSecsSinceEpoch() + kSuppressOwnSetMs;
}

void ClipboardWatcher::onClipboardChanged(QClipboard::Mode mode)
{
    if (mode == QClipboard::Selection
        && !(m_settings && m_settings->monitorPrimarySelection()))
        return;
    m_pendingMode = mode;
    m_debounce.start();
}

void ClipboardWatcher::processPending()
{
    if (QDateTime::currentMSecsSinceEpoch() < m_suppressUntilEpochMs)
        return; // our own paste-back; not a user capture

    const QMimeData *mimeData = m_clipboard->mimeData(m_pendingMode);
    if (!mimeData)
        return;

    ClipboardRecord record = buildRecord(mimeData);
    if (record.hash.isEmpty())
        return; // nothing we care about (e.g. unknown formats)

    record.timestamp = QDateTime::currentMSecsSinceEpoch();
    if (m_activeWindow) {
        const ActiveWindowInfo source = m_activeWindow->activeWindow();
        record.sourceApp = source.appIdentifier;
        record.sourceWindow = source.windowTitle;
    }
    if (m_settings && !record.sourceApp.isEmpty() && m_settings->isSourceIgnored(record.sourceApp))
        return; // per-app ignore rule (runs before any sensitive-data policy)

    // Sensitive-data policy applies to anything that carries text.
    const QString text = mimeData->text();
    if (!text.isEmpty()) {
        const auto mode = m_settings ? m_settings->sensitiveMode()
                                     : SettingsManager::SensitiveMode::Off;
        if (mode == SettingsManager::SensitiveMode::Exclude
            && isSensitiveWithCustom(text, m_settings)) {
            emit excludedSensitive(customKinds(text, m_settings).join(QStringLiteral(", ")));
            return;
        }
        if (mode == SettingsManager::SensitiveMode::Mark)
            record.sensitive = isSensitiveWithCustom(text, m_settings);
        if (mode == SettingsManager::SensitiveMode::Redact) {
            const QStringList kinds = applyRedaction(&record, text, m_settings);
            if (!kinds.isEmpty())
                emit redactedSensitive(kinds.join(QStringLiteral(", ")));
        }
    }
    emit captured(record);
}

ClipboardRecord ClipboardWatcher::buildRecord(const QMimeData *mimeData) const
{
    // On Wayland, touching formats() first makes Qt fetch the offer details
    // before any typed accessors are queried.
    mimeData->formats();

    const qint64 maxBytes = m_settings ? m_settings->maxItemBytes() : 5 * 1024 * 1024;
    const qint64 maxImageBytes = m_settings ? m_settings->maxImageBytes() : maxBytes;
    ClipboardRecord record;

    if (mimeData->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mimeData->imageData());
        if (image.isNull())
            return record;
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        record.type = ContentType::Image;
        record.sizeBytes = png.size();
        const qint64 imgCap = maxImageBytes > 0 ? maxImageBytes : maxBytes;
        if (imgCap <= 0 || png.size() <= imgCap) {
            record.blobData = png;
            record.hasBlob = true;
            record.preview = QStringLiteral("Image %1×%2 · %3")
                                 .arg(image.width())
                                 .arg(image.height())
                                 .arg(humanSize(png.size()));
        } else {
            record.preview = QStringLiteral("Image %1×%2 · not stored (exceeds %3)")
                                 .arg(image.width())
                                 .arg(image.height())
                                 .arg(humanSize(imgCap));
        }
        record.hash = hashPayload(ContentType::Image, png);
        return record;
    }

    if (mimeData->hasUrls()) {
        const QList<QUrl> urls = mimeData->urls();
        QStringList paths;
        bool allLocal = !urls.isEmpty();
        for (const QUrl &url : urls) {
            if (!url.isLocalFile()) {
                allLocal = false;
                break;
            }
            paths.append(url.toLocalFile());
        }
        if (allLocal) {
            record.type = ContentType::Files;
            QJsonArray array;
            for (const QString &path : paths)
                array.append(path);
            record.textData = QString::fromUtf8(
                QJsonDocument(array).toJson(QJsonDocument::Compact));
            record.sizeBytes = record.textData.toUtf8().size();
            QString names;
            for (const QString &path : paths) {
                if (!names.isEmpty())
                    names += QStringLiteral(", ");
                names += QFileInfo(path).fileName();
            }
            record.preview = QStringLiteral("%1 %2: %3")
                                 .arg(paths.size())
                                 .arg(paths.size() == 1 ? QStringLiteral("file")
                                                        : QStringLiteral("files"))
                                 .arg(singleLine(names));
            record.hash = hashPayload(ContentType::Files, record.textData.toUtf8());
            return record;
        }
        // non-local URLs fall through to text handling below
    }

    if (mimeData->hasHtml()) {
        record.type = ContentType::RichText;
        QString html = mimeData->html();
        bool truncated = false;
        if (maxBytes > 0 && qint64(html.size()) > maxBytes) {
            html.truncate(int(maxBytes));
            truncated = true;
        }
        record.textData = html;
        record.sizeBytes = html.toUtf8().size();
        const QString plain = mimeData->text();
        record.preview = singleLine(plain);
        if (truncated)
            record.preview += QStringLiteral(" …");
        record.hash = hashPayload(ContentType::RichText, html.toUtf8());
        return record;
    }

    if (mimeData->hasText()) {
        QString text = mimeData->text();
        if (text.isEmpty())
            return record;

        QStringList paths;
        if (isFilePathList(text, &paths)) {
            record.type = ContentType::Files;
            QJsonArray array;
            for (const QString &path : paths)
                array.append(path);
            record.textData = QString::fromUtf8(
                QJsonDocument(array).toJson(QJsonDocument::Compact));
            record.sizeBytes = record.textData.toUtf8().size();
            QString names;
            for (const QString &path : paths) {
                if (!names.isEmpty())
                    names += QStringLiteral(", ");
                names += QFileInfo(path).fileName();
            }
            record.preview = QStringLiteral("%1 %2: %3")
                                 .arg(paths.size())
                                 .arg(paths.size() == 1 ? QStringLiteral("file")
                                                        : QStringLiteral("files"))
                                 .arg(singleLine(names));
            record.hash = hashPayload(ContentType::Files, record.textData.toUtf8());
            return record;
        }

        record.type = ContentType::Text;
        if (maxBytes > 0 && qint64(text.size()) > maxBytes) {
            text.truncate(int(maxBytes));
            record.preview = singleLine(text) + QStringLiteral(" …");
        } else {
            record.preview = singleLine(text);
        }
        record.textData = text;
        record.sizeBytes = text.toUtf8().size();
        record.hash = hashPayload(ContentType::Text, text.toUtf8());
        return record;
    }

    return record;
}
