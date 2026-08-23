#pragma once

#include <QDialog>

#include "TransformEngine.h"

class ScriptActionManager;
class QLabel;
class QListWidget;
class QPlainTextEdit;

/**
 * @brief Chainable transform dialog: pick N transforms, live preview, copy result.
 */
class TransformChainDialog : public QDialog {
    Q_OBJECT
public:
    explicit TransformChainDialog(const QString &inputText, ScriptActionManager *scripts, QWidget *parent = nullptr);

    QString resultText() const;

private:
    void rebuildPreview();
    void moveUp();
    void moveDown();

    QString m_input;
    ScriptActionManager *m_scripts = nullptr;
    QListWidget *m_available = nullptr;
    QListWidget *m_chain = nullptr;
    QPlainTextEdit *m_preview = nullptr;
    QLabel *m_error = nullptr;
};
