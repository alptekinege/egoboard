#pragma once

#include "ClipboardRecord.h"

#include <QImage>
#include <QWidget>

class QCheckBox;
class QLabel;
class QScrollArea;
class QSlider;
class CodePreviewHighlighter;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
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
    // Drawer mode shows the header close button; inline keeps it hidden.
    void setCloseVisible(bool visible);
    // Words from the active search, marked in the current text preview.
    void setSearchTerms(const QStringList &terms);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

signals:
    void copyToClipboardRequested(const QString &text);
    void pinRequested(qint64 entryId);
    void closeRequested();

private:
    QWidget *pageText();
    QWidget *pageHtml();
    QWidget *pageImage();
    QWidget *pageFiles();
    void setMeta(const QString &text);
    void buildHeaderBar();
    void buildTransformBar();
    void refreshTransformMenu();
    void applyBuiltin(int transformIndex);
    void applyScript(const QString &id);
    void openChainDialog();
    void applySearchHighlights();
    void updateImageView();
    void updateBlurOverlay();
    void setEditing(bool editing);
    void saveEdit();

    QStackedWidget *m_stack = nullptr;
    QLabel *m_emptyLabel = nullptr;
    // R2 header: copy, pin, source-app label, close (drawer only).
    QWidget *m_headerBar = nullptr;
    QToolButton *m_copyBtn = nullptr;
    QToolButton *m_pinBtn = nullptr;
    QLabel *m_sourceLabel = nullptr;
    QToolButton *m_closeBtn = nullptr;
    // R2 text tools: wrap toggle + inline edit.
    QToolButton *m_wrapBtn = nullptr;
    QToolButton *m_editBtn = nullptr;
    QToolButton *m_saveEditBtn = nullptr;
    QToolButton *m_cancelEditBtn = nullptr;
    bool m_editing = false;
    // R2 image tools: zoom slider + fit/100% toggle.
    QWidget *m_imageBar = nullptr;
    QSlider *m_zoomSlider = nullptr;
    QToolButton *m_zoomFitBtn = nullptr;
    QLabel *m_zoomLabel = nullptr;
    qreal m_zoom = 0.0; // 0 = fit-to-width, else scale factor
    QImage m_image;
    QScrollArea *m_imageScroll = nullptr;
    QLabel *m_blurLabel = nullptr; // R2 privacy blur overlay
    QPlainTextEdit *m_textEdit = nullptr;
    QTextBrowser *m_htmlView = nullptr;
    QWidget *m_imagePage = nullptr;
    QLabel *m_imageLabel = nullptr;
    QListWidget *m_filesList = nullptr;
    QLabel *m_metaLabel = nullptr;
    CodePreviewHighlighter *m_highlighter = nullptr;
    QStringList m_searchTerms;

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

    // U11 shimmer overlay while content loads
    QWidget *m_skeletonOverlay = nullptr;
    QTimer *m_skeletonTimer = nullptr;
    void showSkeleton();
    void hideSkeleton();
};
