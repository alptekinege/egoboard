#pragma once

#include <QString>
#include <QStringList>

// Detects likely-sensitive text in clipboard content (credit card numbers,
// passwords/API keys/secrets). Used to optionally exclude such captures from
// history.
class SensitiveDataDetector {
public:
    struct Finding {
        int offset = 0;
        int length = 0;
        QString kind; // "creditcard", "credential", "token", ...
    };

    static QList<Finding> scan(const QString &text);
    static bool isSensitive(const QString &text);
    static QStringList kinds(const QString &text);

private:
    static bool luhnValid(const QString &digits);
};
