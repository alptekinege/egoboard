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

    // Every built-in kind, in rule order (for settings toggles).
    static QStringList allKinds();

    struct RedactionResult {
        QString text;
        QStringList redactedKinds; // kinds actually redacted (enabled + present)
        int redactedCount = 0;
    };

    // Replaces matches whose kind is in enabledKinds with "••••".
    // An empty enabledKinds means "redact every detected kind".
    static RedactionResult redact(const QString &text, const QStringList &enabledKinds = {});

private:
    static bool luhnValid(const QString &digits);
};
