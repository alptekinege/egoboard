#pragma once

#include <QColor>
#include <QObject>
#include <QString>

#include <functional>

class QFrame;
class QGraphicsDropShadowEffect;
class QLineEdit;
class QLabel;
class QListView;
class QListWidget;
class QAbstractScrollArea;
class QBoxLayout;
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

// U13: the preview image footer line for OCR state. Pure (tesseract
// availability is passed in) so the unavailable/missing cases stay testable
// offscreen without depending on the machine's PATH.
QString ocrMetaSuffix(bool hasBlob, const QString &ocrText, bool tesseractAvailable);

// Trailing room and a common height, so the main window's search box and the
// palette's input read as the same control.
void styleSearchField(QLineEdit *field);

// Colors for inline rich text, where QPalette roles cannot be used. All are
// checked against the window color at the same contrast floor as ordinary text.
QString positiveStyle(); // accent (highlight), for success markers
QString warningStyle(); // warning/error marker
QColor mutedColor(); // the subdued role, for borders and secondary lines

// One card frame for popups (palette + palette-dialog border, rounded,
// shadowed) — the same panel the settings sidebar and quick-paste use.
QFrame *makeCard(QWidget *parent);
QFrame *makePopupPanel(QWidget *parent);
QGraphicsDropShadowEffect *cardShadow(QObject *parent, bool elevated);

// One sidebar/item list style (settings sidebar, quick-paste rows, palette
// rows): transparent rows, rounded selection, same padding as the tokens.
void styleItemList(QListWidget *list);
void styleItemList(QListView *list);

// U14 responsive sidebar (Settings dialog): Wide keeps the vertical icon
// sidebar of sidebarWideWidth px; Narrow turns it into a horizontal top strip
// (single row, scrolls when the pages overflow) above the page stack.
// Idempotent: call from resizeEvent, the strip ends stable either way.
void applySidebarMode(QListWidget *sidebar, QBoxLayout *content, bool narrow,
                      int sidebarWideWidth);

// U14 settings search: harvest the visible texts of one settings page
// (labels, buttons, group titles, placeholders, tooltips — HTML stripped) so
// filtering needs no hand-kept keyword table and follows translations.
QStringList collectSettingTexts(const QWidget *page);
// Every whitespace-separated query token must occur somewhere in texts
// (case-insensitive); an empty query matches everything (show all).
bool settingQueryMatches(const QStringList &texts, const QString &query);
// First page index whose texts match, or -1 (empty query: -1, no filtering).
int firstSettingMatchRow(const QList<QStringList> &pages, const QString &query);

// One active-filter chip (U6): horizontal pill with label + close button. The
// close callback fires on click; the whole chip is keyboard-focusable with an
// accessible name. Palette- and font-relative, no fixed point size.
QWidget *makeChip(const QString &label, const QString &accessibleName, QWidget *parent,
                  const std::function<void()> &onClose);

// One empty/error state (U13): icon + title + one optional action button, used
// everywhere a view can be empty instead of bare text.
QWidget *makeEmptyState(const QString &iconName, const QString &title, const QString &subtitle,
                        QWidget *parent, const QString &actionText = {},
                        const std::function<void()> &onAction = {});

// One non-modal bottom toast (U11): message + optional action (Undo/copy).
// Auto-dismisses after ToastDurationMs; accessible via a live region.
QWidget *makeToast(const QString &message, QWidget *parent, const QString &actionText = {},
                   const std::function<void()> &onAction = {});

// Minimum interactive height for a widget under a density (U5): never below
// the touch floor so compact rows stay tappable.
void ensureTouchTarget(QWidget *widget, const QString &density);

// R6 screencast auto-blur: the payload hides while the screen is shared (any
// entry) or, as before, for sensitive entries behind the privacy-blur
// setting. Pure so the truth table stays pinned offscreen.
bool shouldBlurPreview(bool valid, bool privacyBlur, bool sensitive, bool sharingActive);

// Touch scrolling (§7 tail): kinetic swipe on an item-view viewport for
// touchscreens (TouchGesture only — mouse drags keep their DnD meaning).
// Null-safe; harmless without touch hardware.
void enableTouchScroll(QAbstractScrollArea *view);

// Pseudo-long translation probe (§7 tail): German-length expansion of a UI
// string for offscreen layout checks (vowel doubling, ~+30%, bracketed so
// truncation is visible). Test utility; never shown to users.
QString pseudoLong(const QString &text);

// One placeholder row (U11): translucent rounded rect with a pulse shimmer
// when motion is enabled, static when Reduce motion is on. Used for skeleton
// rows in the list and shimmer overlays in the preview while content loads.
QWidget *makeSkeleton(QWidget *parent = nullptr);

// --- motion ------------------------------------------------------------------

// Settings ▸ Appearance ▸ "Reduce motion". Set by ApplicationContext; the
// animations below are skipped while it is on.
bool reduceMotion();
void setReduceMotion(bool reduce);

// 120 ms fade-in for a popup. Call right before show(): the opacity is dropped
// while the window is still hidden, so nothing flashes at full strength.
void fadeIn(QWidget *window);

// Single entry point for widget motion (U12): popup fade, drawer slide,
// chip fade+scale, toast rise. Honors the Reduce motion switch; durations come
// from DesignTokens, so no caller passes its own. Slide/scale end back on the
// layout geometry, so rows and docks are stable afterwards.
enum class MotionKind { Fade, Chip, SlideUp, SlideSide };
void animate(QWidget *widget, MotionKind kind = MotionKind::Fade);

} // namespace UiHelpers
