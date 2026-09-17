#pragma once

#include "ContentType.h"

#include <QList>
#include <QString>
#include <QStringList>

// One auto-expire rule ("delete unpinned Terminal copies after 24h").
// Entries older than ageSeconds, matching the optional content type and
// source-app wildcard, are removed — pinned entries survive when keepPinned.
struct ExpireRule {
    int contentType = -1; // -1 = any, else ContentType value; below -1 = invalid
    QString sourceAppWildcard; // empty = any ("firefox*" style)
    qint64 ageSeconds = 0; // entries strictly older than this are candidates
    bool keepPinned = true;

    bool isValid() const { return ageSeconds > 0 && contentType >= -1; }

    // "text|firefox*|86400|1" — type name, wildcard, age, keepPinned.
    // Age supports m/h/d suffixes (e.g. "7d"). Invalid input yields
    // an invalid rule (isValid() == false).
    QString toString() const;
    static ExpireRule fromString(const QString &encoded);
};

// List codecs for KConfig QStringList storage (one string per rule).
QStringList encodeRules(const QList<ExpireRule> &rules);
QList<ExpireRule> decodeRules(const QStringList &encoded);