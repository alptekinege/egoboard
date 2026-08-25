#include "ExpireScheduler.h"

#include "SettingsManager.h"
#include "StorageManager.h"

#include <QDateTime>

namespace {
constexpr int kIntervalMs = 15 * 60 * 1000; // 15 minutes
constexpr int kCaptureDebounceMs = 30 * 1000; // idle 30s after the last capture
} // namespace

ExpireScheduler::ExpireScheduler(StorageManager *storage, SettingsManager *settings,
                                 QObject *parent)
    : QObject(parent)
    , m_storage(storage)
    , m_settings(settings)
{
    m_timer.setInterval(kIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &ExpireScheduler::applyRules);
    m_captureDebounce.setSingleShot(true);
    m_captureDebounce.setInterval(kCaptureDebounceMs);
    connect(&m_captureDebounce, &QTimer::timeout, this, &ExpireScheduler::applyRules);
}

void ExpireScheduler::start()
{
    m_timer.start();
    applyRules(); // catch anything that aged out while egoboard wasn't running
}

void ExpireScheduler::applyRules()
{
    if (!m_storage || !m_settings)
        return;
    const QList<ExpireRule> rules = m_settings->expireRules();
    if (rules.isEmpty())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int removed = 0;
    for (const ExpireRule &rule : rules) {
        if (!rule.isValid())
            continue;
        const qint64 cutoff = now - rule.ageSeconds * 1000;
        removed += m_storage->expireEntries(cutoff, rule.contentType,
                                            rule.sourceAppWildcard, rule.keepPinned);
    }
    if (removed > 0)
        emit expired(removed);
}

void ExpireScheduler::scheduleAfterCapture()
{
    m_captureDebounce.start();
}