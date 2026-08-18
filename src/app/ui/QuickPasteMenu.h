#pragma once

#include <QWidget>

class QListWidget;
class QTimer;

// Frameless popup listing the most recent entries with numeric badges.
// Keys 1-9 paste the corresponding item, Enter pastes the highlighted one,
// Esc closes. Auto-hides on focus loss or after a timeout.
class QuickPasteMenu : public QWidget {
    Q_OBJECT
public:
    explicit QuickPasteMenu(class StorageManager *storage, int itemCount, QWidget *parent = nullptr);

    void popupAtCursor();
    void hide(); // shadows QWidget::hide() to stop the auto-hide timer

signals:
    void pasteRequested(qint64 entryId);
    void hidden();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void refresh();
    void activateRow(int row);

    StorageManager *m_storage = nullptr;
    QListWidget *m_list = nullptr;
    QTimer *m_autoHide = nullptr;
    int m_itemCount;
};
