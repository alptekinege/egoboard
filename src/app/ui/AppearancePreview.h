#pragma once

#include <QFrame>
#include <QSize>

// Miniature of the real UI, painted with the live application palette and font.
//
// Readability is the point: the sample uses the exact roles Egoboard draws with -
// list text on Base, the subdued meta line, a selected row on Highlight, a hint
// in the "palette(mid)" style and a placeholder - plus a few icons from the
// active icon theme. Any theme or text change repaints it, so an unreadable
// combination shows up before it reaches the window.
class AppearancePreview : public QFrame {
    Q_OBJECT
public:
    explicit AppearancePreview(QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override; // repaint on palette/font changes
};
