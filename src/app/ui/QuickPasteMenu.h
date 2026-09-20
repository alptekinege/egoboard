#pragma once

#include "ClipboardRecord.h"

#include <QPoint>
#include <QVector>
#include <QWidget>

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QTimer;
class QHideEvent;
class QShowEvent;
class QKeyEvent;
class QFocusEvent;
class SettingsManager;

// Frameless popup listing the most recent entries with numeric badges.
// Keys 1-9 paste the corresponding item, Enter pastes the highlighted one,
// Esc closes. Auto-hides on focus loss or after a timeout.
//
// On Wayland + KWin the popup is promoted to a layer-shell overlay
// (via LayerShellHelper + LayerShellQt) for exclusive keyboard grab and
// cursor-anchored placement through margins. On X11/offscreen it is a
// regular Tool window positioned with move() — see popupAtCursor().
class QuickPasteMenu : public QWidget {
    Q_OBJECT
public:
    explicit QuickPasteMenu(class StorageManager *storage, int itemCount, QWidget *parent = nullptr);

    void popupAtCursor();
    void hide(); // shadows QWidget::hide() to stop the auto-hide timer
    void setItemCount(int count); // 1..9, applied without a restart
    void setSettings(SettingsManager *settings) { m_settings = settings; }
    void setTwoLine(bool twoLine);

signals:
    void pasteRequested(qint64 entryId);
    void hidden();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refresh();
    void refreshList(); // re-filter without touching the search text
    void activateRow(int row);
    void moveSelection(int delta);
    static QString metaLine(const ClipboardRecord &record);
    static QString screenNameFor(const QPoint &cursorPos);

    class StorageManager *m_storage = nullptr;
    SettingsManager *m_settings = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QTimer *m_autoHide = nullptr;
    int m_itemCount;
    bool m_layerShellConfigured = false;
    bool m_twoLine = false;
    QString m_lastScreen;
    QPoint m_lastCursor;
    QVector<ClipboardRecord> m_rows; // filtered rows behind the list widget
};
