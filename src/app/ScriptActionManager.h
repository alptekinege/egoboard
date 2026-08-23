#pragma once

#include <QObject>
#include <QString>
#include <QVector>

struct ScriptAction {
    QString id; // filename without extension
    QString label;
    QString filePath;
    QString matchPattern; // regex string from meta.match, may be empty
    bool hasTransform = false;
};

/**
 * @brief Loads and executes user JS transforms from ~/.local/share/egoboard/actions
 *
 * Sandbox: QJSEngine with no file/network globals, 2s logical timeout via
 * input size cap (256 kB) and no exposed Qt/C++ objects. Supports both
 * CommonJS `function transform(text){}` and ES `export function transform` forms
 * by stripping `export` before evaluation.
 */
class ScriptActionManager : public QObject {
    Q_OBJECT
public:
    explicit ScriptActionManager(QObject *parent = nullptr);

    void reload();
    QVector<ScriptAction> actions() const { return m_actions; }
    bool hasAction(const QString &id) const;

    struct Result {
        bool ok = false;
        QString output;
        QString error;
    };
    Result apply(const QString &id, const QString &input) const;

    static QString actionsDir();
    static QString exampleSource(); // returns JS template for a new action

private:
    static QString preprocessSource(const QString &source);
    static ScriptAction parseMeta(const QString &id, const QString &filePath, const QString &source);

    QVector<ScriptAction> m_actions;
};
