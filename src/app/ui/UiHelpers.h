#pragma once

#include <QColor>
#include <QString>

class QLineEdit;
class QLabel;
class QWidget;

// Small shared building blocks for the widgets: one look for the muted hint
// labels, one human-readable size, one search-field style, the rich-text colors
// and the capped motion. Everything here is palette- and font-relative, so a
// theme switch or the "Text size" setting moves it along with the rest.
namespace UiHelpers {

// Muted small print (hint under a control, status line, timestamp). With
// richText the argument is HTML, otherwise it is plain text — use plain for
// anything that may contain user or error text.
QLabel *makeHint(const QString &text, QWidget *parent, bool richText = true);

// Muted small print in a bordered box (platform diagnostics, test results).
QLabel *makeStatusPanel(const QString &text, QWidget *parent);

// One human-readable byte size ("512 B", "1.5 kB", "2.0 MB"). Empty for 0.
QString humanSize(qint64 bytes);

// Trailing room and a common height, so the main window's search box and the
// palette's input read as the same control.
void styleSearchField(QLineEdit *field);

// Colors for inline rich text, where QPalette roles cannot be used. All are
// checked against the window color at the same contrast floor as ordinary text.
QString positiveStyle(); // accent (highlight), for success markers
QString warningStyle(); // warning/error marker
QColor mutedColor(); // the subdued role, for borders and secondary lines

// --- motion ------------------------------------------------------------------

// Settings ▸ Appearance ▸ "Reduce motion". Set by ApplicationContext; the
// animations below are skipped while it is on.
bool reduceMotion();
void setReduceMotion(bool reduce);

// 120 ms fade-in for a popup. Call right before show(): the opacity is dropped
// while the window is still hidden, so nothing flashes at full strength.
void fadeIn(QWidget *window);

} // namespace UiHelpers
