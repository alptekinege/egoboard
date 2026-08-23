#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include <optional>

struct Snippet {
    qint64 id = 0;
    QString name;
    QString templateText;
    QString shortcut;
    qint64 createdMs = 0;

    bool isValid() const { return id != 0; }
};

/**
 * @brief DB-backed snippet templates with placeholder expansion.
 *
 * Placeholders (case-sensitive):
 *  {{clipboard}} / {{text}} / {{selection}} — replaced with provided clipboard text
 *  {{date}}      — YYYY-MM-DD
 *  {{time}}      — HH:mm
 *  {{datetime}}  — YYYY-MM-DD HH:mm:ss
 *  {{timestamp}} — milliseconds since epoch
 *
 * Lives in egoboard_core (QtCore+Sql only) and is headless-testable.
 */
class SnippetManager : public QObject {
    Q_OBJECT
public:
    explicit SnippetManager(QSqlDatabase db, QObject *parent = nullptr);

    qint64 createSnippet(const QString &name, const QString &templateText,
                         const QString &shortcut = QString());
    bool updateSnippet(qint64 id, const QString &name, const QString &templateText,
                       const QString &shortcut);
    bool deleteSnippet(qint64 id);

    QVector<Snippet> snippets() const;
    std::optional<Snippet> snippet(qint64 id) const;

    // Pure expansion (no DB access) — also used for preview.
    static QString expand(const QString &tmpl, const QString &clipboardText);
    QString expandSnippet(qint64 id, const QString &clipboardText) const;

signals:
    void snippetsChanged();

private:
    QSqlDatabase m_db;
};
