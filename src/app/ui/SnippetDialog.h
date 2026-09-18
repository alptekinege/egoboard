#pragma once

#include <QDialog>

class SnippetManager;
class KKeySequenceWidget;
class QLabel;
class QListWidget;
class QLineEdit;
class QPlainTextEdit;

/**
 * @brief Simple snippet CRUD dialog (list on left, editor on right).
 *
 * Placeholders: {{clipboard}}, {{date}}, {{time}}, {{datetime}} — local only.
 * An optional global shortcut (KGlobalAccel) pastes the expanded snippet.
 */
class SnippetDialog : public QDialog {
    Q_OBJECT
public:
    explicit SnippetDialog(SnippetManager *manager, const QString &clipboardText = {}, QWidget *parent = nullptr);

signals:
    void insertRequested(const QString &expandedText);

private:
    void reload();
    void onSelectionChanged();
    void onCreate();
    void onUpdate();
    void onDelete();
    void onInsert();
    void updatePreview();
    // Reflects whether the sequence currently in the editor can be bound.
    void updateShortcutNote();
    // The editor's sequence as stored in the database.
    QString shortcutText() const;

    SnippetManager *m_manager = nullptr;
    QString m_clipboard;
    QListWidget *m_list = nullptr;
    QLineEdit *m_name = nullptr;
    QPlainTextEdit *m_template = nullptr;
    KKeySequenceWidget *m_shortcut = nullptr;
    QLabel *m_shortcutNote = nullptr;
    QPlainTextEdit *m_preview = nullptr;
    qint64 m_currentId = 0;
};
