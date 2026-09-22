#pragma once

#include "ClipboardRecord.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QLocale>
#include <QString>
#include <QVector>

// Pure tray logic (U18): visibility policy, header/tooltip wording and recent
// menu rows. QtCore-only on purpose, so the offscreen suite can pin the whole
// matrix without a StatusNotifier host. TrayController renders whatever this
// namespace computes, for both the SNI item and the QSystemTrayIcon fallback.
namespace TrayMenuModel {

enum class Mode {
    Auto, // icon available while there is history to show
    Always,
    Hidden, // no tray surface; hotkeys and the process keep running
};

// SettingsManager already normalizes stored values; unknown input (direct
// callers, older configs) falls back to Auto.
inline Mode parseMode(const QString &mode)
{
    if (mode == QLatin1String("always"))
        return Mode::Always;
    if (mode == QLatin1String("hidden"))
        return Mode::Hidden;
    return Mode::Auto;
}

inline bool isVisible(Mode mode, qint64 entryCount)
{
    switch (mode) {
    case Mode::Always:
        return true;
    case Mode::Hidden:
        return false;
    case Mode::Auto:
        break;
    }
    return entryCount > 0;
}

inline QString tr(const char *text)
{
    return QCoreApplication::translate("TrayController", text);
}

// "1 entry" vs "%n entries" — Qt keeps the source form for n == 1, so the
// singular is spelled out instead of relying on plural rules.
inline QString countText(qint64 entryCount)
{
    if (entryCount == 1)
        return tr("1 entry");
    return QCoreApplication::translate("TrayController", "%n entries", nullptr, int(entryCount));
}

// Menu section + tooltip subtitle: capture state first, then the count.
inline QString headerText(bool paused, qint64 entryCount)
{
    const QString state = paused ? tr("Paused") : tr("Capturing");
    return QStringLiteral("%1 · %2").arg(state, countText(entryCount));
}

// "2 min ago" style age for the last capture; deterministic in nowMs so tests
// can pin it without sleeping.
inline QString describeCaptureAge(qint64 captureMs, qint64 nowMs)
{
    if (captureMs <= 0)
        return tr("never");
    const qint64 seconds = qMax<qint64>(0, (nowMs - captureMs) / 1000);
    if (seconds < 60)
        return tr("just now");
    const qint64 minutes = seconds / 60;
    if (minutes < 60)
        return QCoreApplication::translate("TrayController", "%n min ago", nullptr, int(minutes));
    const qint64 hours = minutes / 60;
    if (hours < 24)
        return QCoreApplication::translate("TrayController", "%n h ago", nullptr, int(hours));
    return QLocale::system().toString(QDateTime::fromMSecsSinceEpoch(captureMs),
                                      QLocale::ShortFormat);
}

// Tooltip body: what state the capture is in and how fresh the history is.
inline QString tooltipText(bool paused, qint64 entryCount, qint64 lastCaptureMs, qint64 nowMs)
{
    if (entryCount <= 0)
        return paused ? tr("Capture paused · no entries yet")
                      : tr("No entries yet — copy something first");
    const QString age = describeCaptureAge(lastCaptureMs, nowMs);
    if (paused)
        return QStringLiteral("%1 · paused · last capture %2").arg(countText(entryCount), age);
    return QStringLiteral("%1 · last capture %2").arg(countText(entryCount), age);
}

inline QString iconNameForType(ContentType type)
{
    switch (type) {
    case ContentType::RichText:
        return QStringLiteral("text-html");
    case ContentType::Image:
        return QStringLiteral("image-x-generic");
    case ContentType::Files:
        return QStringLiteral("folder");
    case ContentType::Text:
        break;
    }
    return QStringLiteral("text-plain");
}

inline QString typeDisplayName(ContentType type)
{
    switch (type) {
    case ContentType::RichText:
        return tr("Rich text");
    case ContentType::Image:
        return tr("Image");
    case ContentType::Files:
        return tr("Files");
    case ContentType::Text:
        break;
    }
    return tr("Text");
}

struct RecentRow {
    qint64 id = 0;
    QString iconName; // freedesktop name for QIcon::fromTheme
    QString label; // short, menu-ready
    QString toolTip; // full meta line
    bool enabled = true;
};

// Menus elide, but never feed them paragraphs: labels are capped here.
constexpr int kRecentLabelMax = 80;

inline QVector<RecentRow> buildRecentRows(const QVector<ClipboardRecord> &recents, qint64 nowMs)
{
    QVector<RecentRow> rows;
    rows.reserve(recents.size());
    for (const ClipboardRecord &record : recents) {
        RecentRow row;
        row.id = record.id;
        row.iconName = iconNameForType(record.type);
        QString base = record.preview.trimmed();
        if (base.isEmpty()) {
            if (record.type == ContentType::Image)
                base = tr("Image");
            else if (record.type == ContentType::Files)
                base = tr("Files");
            else
                base = tr("(empty)");
        }
        if (base.size() > kRecentLabelMax)
            base = base.left(kRecentLabelMax - 1) + QChar(0x2026);
        const QString app = record.sourceApp.trimmed();
        if (!app.isEmpty())
            base += QStringLiteral(" · ") + app;
        row.label = base;

        QStringList meta;
        meta << typeDisplayName(record.type);
        if (!app.isEmpty())
            meta << app;
        meta << describeCaptureAge(record.timestamp, nowMs);
        meta << QLocale::system().formattedDataSize(record.sizeBytes);
        if (record.pinned)
            meta << tr("pinned");
        row.toolTip = meta.join(QStringLiteral(" · "));
        rows.append(row);
    }
    return rows;
}

// Explicit empty state instead of a bare menu: disabled, never pastes.
inline QVector<RecentRow> emptyRows()
{
    RecentRow row;
    row.id = 0;
    row.iconName = QStringLiteral("edit-copy");
    row.label = tr("(no history yet — copy something first)");
    row.enabled = false;
    return {row};
}

} // namespace TrayMenuModel
