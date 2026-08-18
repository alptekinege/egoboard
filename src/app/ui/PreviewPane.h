#pragma once

#include "ClipboardRecord.h"

#include <QWidget>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QStackedWidget;
class QTextBrowser;

// Right-hand side of the main window: renders the full payload of the
// selected entry (text / HTML / image / file list) plus a metadata footer.
class PreviewPane : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPane(QWidget *parent = nullptr);

    void showRecord(const ClipboardRecord &record);
    void showEmpty(const QString &message = {});

private:
    QWidget *pageText();
    QWidget *pageHtml();
    QWidget *pageImage();
    QWidget *pageFiles();
    void setMeta(const QString &text);

    QStackedWidget *m_stack = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QPlainTextEdit *m_textEdit = nullptr;
    QTextBrowser *m_htmlView = nullptr;
    QLabel *m_imageLabel = nullptr;
    QListWidget *m_filesList = nullptr;
    QLabel *m_metaLabel = nullptr;
};
