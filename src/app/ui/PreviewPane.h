#pragma once

#include "ClipboardRecord.h"

#include <QWidget>

class QLabel;
class CodePreviewHighlighter;
class QListWidget;
class QPlainTextEdit;
class QStackedWidget;
class QTextBrowser;
class QToolButton;
class ScriptActionManager;
class SettingsManager;

// Right-hand side of the main window: renders the full payload of the
// selected entry (text / HTML / image / file list) plus a metadata footer.
// Phase 3: adds local Transform menu (built-ins + QJSEngine scripts) and
// live chain preview.
class PreviewPane : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPane(QWidget *parent = nullptr);

    void showRecord(const ClipboardRecord &record);
    void showEmpty(const QString &message = {});
    void setScriptManager(ScriptActionManager *mgr) { m_scripts = mgr; }
    void setSettingsManager(SettingsManager *mgr) { m_settings = mgr; }

signals:
    void copyToClipboardRequested(const QString &text);

private:
    QWidget *pageText();
    QWidget *pageHtml();
    QWidget *pageImage();
    QWidget *pageFiles();
    void setMeta(const QString &text);
    void buildTransformBar();
    void refreshTransformMenu();
    void applyBuiltin(int transformIndex);
    void applyScript(const QString &id);
    void openChainDialog();

    QStackedWidget *m_stack = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QPlainTextEdit *m_textEdit = nullptr;
    QTextBrowser *m_htmlView = nullptr;
    QWidget *m_imagePage = nullptr;
    QLabel *m_imageLabel = nullptr;
    QListWidget *m_filesList = nullptr;
    QLabel *m_metaLabel = nullptr;
    CodePreviewHighlighter *m_highlighter = nullptr;

    // Phase 3 transform bar
    QWidget *m_transformBar = nullptr;
    QToolButton *m_transformBtn = nullptr;
    QToolButton *m_copyResultBtn = nullptr;
    QToolButton *m_revertBtn = nullptr;
    QLabel *m_transformStatus = nullptr;

    ClipboardRecord m_current;
    QString m_originalText;
    bool m_isTransformed = false;
    ScriptActionManager *m_scripts = nullptr;
    SettingsManager *m_settings = nullptr;
};
