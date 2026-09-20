// Design polish (Track M): the two guarantees the visual pass must keep.
//
// 1. Every color the UI derives from a scheme stays readable on that scheme —
//    body text at 4.5:1, dimmed text and comments at 3:1 — including the code
//    highlighter colors and the rich-text accent/warning the settings pages use.
// 2. The pixel geometry (row height per density, timeline bar layout) is pinned,
//    so moving a number into DesignTokens.h cannot silently change it.
//
// Runs offscreen against every KDE color scheme installed on the machine, the
// same way tst_ui and tst_paletteui do.

#include <QtTest>

#include "CodePreviewHighlighter.h"
#include "ColorSchemeIndex.h"
#include "DesignTokens.h"
#include "EntryDelegate.h"
#include "TextAppearance.h"
#include "UiHelpers.h"

#include <KColorScheme>
#include <KSharedConfig>

#include <QApplication>
#include <QDateTime>
#include <QFontMetrics>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QVector>
#include <QWidget>

namespace {

struct Scheme {
    QString id;
    QPalette palette; // as the app would install it
};

QVector<Scheme> installedSchemes()
{
    QVector<Scheme> schemes;
    for (const ColorSchemeIndex::Entry &entry : ColorSchemeIndex::scan()) {
        const QPalette raw =
            KColorScheme::createApplicationPalette(KSharedConfig::openConfig(entry.path));
        schemes.append({entry.id, TextAppearance::applyOverrides(raw, {})});
    }
    return schemes;
}

} // namespace

class TestUiDesign : public QObject
{
    Q_OBJECT

private slots:
    void delegateTextRolesKeepTheirContrastFloor();
    void paletteHintStaysReadableOnEveryScheme();
    void timelineCaptionFollowsTheSameFloors();
    void codeHighlighterColorsAreReadableOnEveryScheme();
    void codeHighlighterFollowsTheScheme();
    void richTextAccentAndWarningStayReadable();
    void hoverWashIsDerivedFromTheSelectionColor();
    void rowHeightFollowsTheDensityScale();
    void rowHeightStaysOnTheDelegateFormula();
    void timelineGeometryFitsAndHitTests();
    void shellModesFollowTheBreakpoints();
    void densityFloorsKeepTouchTargets();
    void timelineHeightFollowsTheFont();
    void popupElevationAndShadowAreShared();
    void searchFieldMeetsTheTouchFloor();
    void helpersBuildWithoutFixedPixels();
    void timelineCollapsesBelowItsWidth();
    void dayHeaderCoversTodayAndYesterday();
    void delegateRespectsRowExtras();
};

void TestUiDesign::delegateTextRolesKeepTheirContrastFloor()
{
    const QVector<Scheme> schemes = installedSchemes();
    QVERIFY2(!schemes.isEmpty(), "no KDE color schemes installed to discover");

    for (const Scheme &scheme : schemes) {
        const QColor base = scheme.palette.color(QPalette::Base);
        const QColor alternate = scheme.palette.color(QPalette::AlternateBase);

        const QColor preview = DesignTokens::previewTextColor(scheme.palette, false, true);
        const QColor meta = DesignTokens::metaTextColor(scheme.palette, false, true);
        QVERIFY2(TextAppearance::contrastRatio(preview, base) >= TextAppearance::kTextContrastRatio,
                 qPrintable(scheme.id));
        QVERIFY2(TextAppearance::contrastRatio(preview, alternate)
                     >= TextAppearance::kTextContrastRatio,
                 qPrintable(scheme.id));
        QVERIFY2(TextAppearance::contrastRatio(meta, base) >= TextAppearance::kDimTextContrastRatio,
                 qPrintable(scheme.id));

        // On a selected row the meta line switches to the highlighted text color
        // (the delegate's own rule). The pair itself is the scheme's choice, so
        // only the mapping is pinned here.
        const QColor selectedPreview = DesignTokens::previewTextColor(scheme.palette, true, true);
        const QColor selectedMeta = DesignTokens::metaTextColor(scheme.palette, true, true);
        QCOMPARE(selectedPreview, scheme.palette.color(QPalette::HighlightedText));
        QCOMPARE(selectedMeta, scheme.palette.color(QPalette::HighlightedText));
    }
}

void TestUiDesign::paletteHintStaysReadableOnEveryScheme()
{
    const QVector<Scheme> schemes = installedSchemes();
    for (const Scheme &scheme : schemes) {
        // The hint labels use the subdued role on the window surface.
        const QColor hint = scheme.palette.color(QPalette::Mid);
        QVERIFY2(TextAppearance::contrastRatio(hint, scheme.palette.color(QPalette::Window))
                     >= TextAppearance::kDimTextContrastRatio,
                 qPrintable(scheme.id));
    }
}

void TestUiDesign::timelineCaptionFollowsTheSameFloors()
{
    const QVector<Scheme> schemes = installedSchemes();
    for (const Scheme &scheme : schemes) {
        const QColor base = scheme.palette.color(QPalette::Base);
        const QColor idle = DesignTokens::timelineCaptionColor(scheme.palette, false);
        const QColor selected = DesignTokens::timelineCaptionColor(scheme.palette, true);
        QCOMPARE(idle, scheme.palette.color(QPalette::Mid));
        QCOMPARE(selected, scheme.palette.color(QPalette::Text));
        QVERIFY2(TextAppearance::contrastRatio(idle, base) >= TextAppearance::kDimTextContrastRatio,
                 qPrintable(scheme.id));
        QVERIFY2(TextAppearance::contrastRatio(selected, base) >= TextAppearance::kTextContrastRatio,
                 qPrintable(scheme.id));
    }
}

void TestUiDesign::codeHighlighterColorsAreReadableOnEveryScheme()
{
    const QVector<Scheme> schemes = installedSchemes();
    QVERIFY2(!schemes.isEmpty(), "no KDE color schemes installed to discover");

    for (const Scheme &scheme : schemes) {
        const QColor base = scheme.palette.color(QPalette::Base);
        const CodePreviewHighlighter::Theme theme =
            CodePreviewHighlighter::themeFor(scheme.palette);
        const QVector<QPair<QString, QColor>> roles = {
            {QStringLiteral("string"), theme.string},
            {QStringLiteral("key"), theme.key},
            {QStringLiteral("number"), theme.number},
            {QStringLiteral("keyword"), theme.keyword},
            {QStringLiteral("comment"), theme.comment},
        };
        for (const auto &role : roles) {
            QVERIFY2(role.second.isValid(), qPrintable(scheme.id + QLatin1Char(' ') + role.first));
            const qreal ratio = TextAppearance::contrastRatio(role.second, base);
            const qreal floor = role.first == QLatin1String("comment")
                ? TextAppearance::kDimTextContrastRatio
                : TextAppearance::kTextContrastRatio;
            QVERIFY2(ratio >= floor,
                     qPrintable(QStringLiteral("%1 %2: %3 < %4")
                                    .arg(scheme.id, role.first)
                                    .arg(ratio)
                                    .arg(floor)));
        }
        // The roles have to stay distinguishable from each other, not just
        // readable: a mid-lightness or high-contrast scheme must not collapse
        // everything into one color.
        QVERIFY(theme.keyword != theme.string);
        QVERIFY(theme.keyword != theme.number);
        QVERIFY(theme.key != theme.keyword);
    }
}

void TestUiDesign::codeHighlighterFollowsTheScheme()
{
    const QVector<Scheme> schemes = installedSchemes();
    if (schemes.size() < 2)
        QSKIP("needs two installed color schemes");

    // A light and a dark scheme must not produce the same code colors (the old
    // implementation picked one of two presets by Base.lightness() < 128).
    const Scheme &first = schemes.first();
    const Scheme &last = schemes.last();
    if (first.palette.color(QPalette::Base) == last.palette.color(QPalette::Base))
        QSKIP("the installed schemes share a Base color");

    const CodePreviewHighlighter::Theme a = CodePreviewHighlighter::themeFor(first.palette);
    const CodePreviewHighlighter::Theme b = CodePreviewHighlighter::themeFor(last.palette);
    QVERIFY(a != b);

    // Derived, not literal: the same palette always yields the same theme.
    QCOMPARE(CodePreviewHighlighter::themeFor(first.palette), a);
}

void TestUiDesign::richTextAccentAndWarningStayReadable()
{
    const QVector<Scheme> schemes = installedSchemes();
    QVERIFY2(!schemes.isEmpty(), "no KDE color schemes installed to discover");

    const QPalette original = qApp->palette();
    for (const Scheme &scheme : schemes) {
        qApp->setPalette(scheme.palette);
        const QColor window = scheme.palette.color(QPalette::Window);

        // The helpers return a "color:#rrggbb;" declaration for inline HTML.
        const auto parse = [](const QString &style) {
            const int hash = style.indexOf(QLatin1Char('#'));
            return hash < 0 ? QColor() : QColor::fromString(style.mid(hash, 7));
        };
        const QColor accent = parse(UiHelpers::positiveStyle());
        const QColor warning = parse(UiHelpers::warningStyle());
        QVERIFY2(accent.isValid(), qPrintable(scheme.id));
        QVERIFY2(warning.isValid(), qPrintable(scheme.id));
        QVERIFY2(TextAppearance::contrastRatio(accent, window) >= TextAppearance::kTextContrastRatio,
                 qPrintable(QStringLiteral("%1: accent %2 on window %3 ratio %4 (highlight %5)")
                                .arg(scheme.id, accent.name(), window.name())
                                .arg(TextAppearance::contrastRatio(accent, window))
                                .arg(scheme.palette.color(QPalette::Highlight).name())));
        QVERIFY2(TextAppearance::contrastRatio(warning, window) >= TextAppearance::kTextContrastRatio,
                 qPrintable(scheme.id));
        // ... and the warning is a warning, not the accent again.
        QVERIFY(accent != warning);

        // The muted color used for rich-text borders is readable too.
        QVERIFY(TextAppearance::contrastRatio(UiHelpers::mutedColor(), window)
                >= TextAppearance::kDimTextContrastRatio);
    }
    qApp->setPalette(original);
}

void TestUiDesign::hoverWashIsDerivedFromTheSelectionColor()
{
    const QVector<Scheme> schemes = installedSchemes();
    for (const Scheme &scheme : schemes) {
        const QColor wash = DesignTokens::hoverBackground(scheme.palette);
        QCOMPARE(wash.rgb(), scheme.palette.color(QPalette::Highlight).rgb());
        QCOMPARE(wash.alpha(), DesignTokens::HoverAlpha);
        // Translucent: a hover must tint the row, never replace it.
        QVERIFY(wash.alpha() < DesignTokens::SearchHighlightSelectedAlpha);
    }
}

void TestUiDesign::rowHeightFollowsTheDensityScale()
{
    // The three density names come from the settings file; anything unknown
    // lands on the comfortable default.
    QCOMPARE(DesignTokens::rowPaddingForDensity(QStringLiteral("compact")),
             DesignTokens::RowPaddingCompact);
    QCOMPARE(DesignTokens::rowPaddingForDensity(QStringLiteral("comfortable")),
             DesignTokens::RowPaddingComfortable);
    QCOMPARE(DesignTokens::rowPaddingForDensity(QStringLiteral("spacious")),
             DesignTokens::RowPaddingSpacious);
    QCOMPARE(DesignTokens::rowPaddingForDensity(QStringLiteral("nonsense")),
             DesignTokens::RowPaddingComfortable);
    QCOMPARE(DesignTokens::rowPaddingForDensity(QString()), DesignTokens::RowPaddingComfortable);

    const QFontMetrics metrics(QApplication::font());
    const QSize compact = EntryDelegate::rowSizeHint(metrics, DesignTokens::RowPaddingCompact, 300);
    const QSize comfortable =
        EntryDelegate::rowSizeHint(metrics, DesignTokens::RowPaddingComfortable, 300);
    const QSize spacious =
        EntryDelegate::rowSizeHint(metrics, DesignTokens::RowPaddingSpacious, 300);
    QVERIFY(compact.height() < comfortable.height());
    QVERIFY(comfortable.height() < spacious.height());
    QCOMPARE(compact.height() + 2 * (DesignTokens::RowPaddingComfortable
                                     - DesignTokens::RowPaddingCompact),
             comfortable.height());
    QCOMPARE(comfortable.height() + 2 * (DesignTokens::RowPaddingSpacious
                                         - DesignTokens::RowPaddingComfortable),
             spacious.height());
    QCOMPARE(compact.width(), 300);
}

void TestUiDesign::rowHeightStaysOnTheDelegateFormula()
{
    // The two text lines, the density padding and the small trailing gap: the
    // constants are spelled out here so a change in the delegate shows up.
    const QFontMetrics metrics(QApplication::font());
    for (const int padding : {DesignTokens::RowPaddingCompact, DesignTokens::RowPaddingComfortable,
                              DesignTokens::RowPaddingSpacious}) {
        const int expected = metrics.height() * 2 + 2 * padding + 4;
        QCOMPARE(EntryDelegate::rowSizeHint(metrics, padding, 250).height(), expected);
    }

    // Row metrics the delegate paints with (badge strip, group dots).
    const DesignTokens::RowMetrics row = DesignTokens::rowMetrics(DesignTokens::RowPaddingComfortable);
    QCOMPARE(row.iconSize, 22);
    QCOMPARE(row.badgeSize, 16);
    QCOMPARE(row.margin, 8);
    // Dots are spaced, not overlapped: the step is larger than the dot.
    QVERIFY(row.dotAdvance() > row.dotDiameter);
    // The text block starts after the icon.
    QCOMPARE(row.contentLeft(0), row.margin + row.iconSize + row.margin);
}

void TestUiDesign::timelineGeometryFitsAndHitTests()
{
    const int bars = 14;
    const QSize strip(300, 48);
    const int captionHeight = 12;
    const DesignTokens::TimelineGeometry geometry =
        DesignTokens::timelineGeometry(strip, bars, captionHeight);

    QVERIFY(geometry.barWidth >= DesignTokens::TimelineMinBarWidth);
    QCOMPARE(geometry.stride, geometry.barWidth + DesignTokens::TimelineBarGap);
    QVERIFY(geometry.left >= 0);
    const int right = geometry.left + bars * geometry.barWidth + (bars - 1) * DesignTokens::TimelineBarGap;
    QVERIFY(right <= strip.width());
    QVERIFY(geometry.barHeight > 0);
    QCOMPARE(geometry.barHeight,
             strip.height() - geometry.top - captionHeight - DesignTokens::SpaceXs);

    // Every bar the paint code draws is the bar a click lands on — the layout
    // is shared, so this must hold for each of the 14 days.
    for (int i = 0; i < bars; ++i) {
        const int center = geometry.left + i * geometry.stride + geometry.barWidth / 2;
        QCOMPARE(DesignTokens::timelineBarAt(strip, bars, QPoint(center, strip.height() / 2)), i);
    }
    // Outside the strip: nothing to select.
    QCOMPARE(DesignTokens::timelineBarAt(strip, bars, QPoint(0, 20)), -1);
    QCOMPARE(DesignTokens::timelineBarAt(strip, bars, QPoint(strip.width() - 1, 20)), -1);
    QCOMPARE(DesignTokens::timelineBarAt(strip, 0, QPoint(150, 20)), -1);

    // A narrow strip still lays out (bars clamp to the minimum width).
    const DesignTokens::TimelineGeometry narrow =
        DesignTokens::timelineGeometry(QSize(110, 48), bars, captionHeight);
    QVERIFY(narrow.barWidth >= DesignTokens::TimelineMinBarWidth);
    for (int i = 0; i < bars; ++i) {
        const int center = narrow.left + i * narrow.stride + narrow.barWidth / 2;
        if (center < 0 || center >= 110)
            continue; // bars that do not fit are not clickable either
        QCOMPARE(DesignTokens::timelineBarAt(QSize(110, 48), bars, QPoint(center, 20)), i);
    }
}

void TestUiDesign::shellModesFollowTheBreakpoints()
{
    // U1: the shell mode is a pure function of the window width.
    QCOMPARE(DesignTokens::shellModeForWidth(0), DesignTokens::ShellMode::Narrow);
    QCOMPARE(DesignTokens::shellModeForWidth(DesignTokens::BreakpointNarrow - 1),
             DesignTokens::ShellMode::Narrow);
    QCOMPARE(DesignTokens::shellModeForWidth(DesignTokens::BreakpointNarrow),
             DesignTokens::ShellMode::Medium);
    QCOMPARE(DesignTokens::shellModeForWidth(DesignTokens::BreakpointWide - 1),
             DesignTokens::ShellMode::Medium);
    QCOMPARE(DesignTokens::shellModeForWidth(DesignTokens::BreakpointWide),
             DesignTokens::ShellMode::Wide);
    QCOMPARE(DesignTokens::shellModeForWidth(4000), DesignTokens::ShellMode::Wide);
    // Breakpoints are ordered and the timeline collapses inside Narrow.
    QVERIFY(DesignTokens::BreakpointNarrow < DesignTokens::BreakpointWide);
    QVERIFY(DesignTokens::TimelineCollapseWidth < DesignTokens::BreakpointNarrow);
    QVERIFY(DesignTokens::SettingsSidebarCollapseWidth < DesignTokens::BreakpointNarrow);
}

void TestUiDesign::densityFloorsKeepTouchTargets()
{
    // U5: compact rows stay tappable, spacious rows stay roomy.
    QCOMPARE(DesignTokens::rowMinHeightForDensity(QStringLiteral("compact")),
             DesignTokens::TouchTargetCompact);
    QCOMPARE(DesignTokens::rowMinHeightForDensity(QStringLiteral("comfortable")), 40);
    QCOMPARE(DesignTokens::rowMinHeightForDensity(QStringLiteral("spacious")), 48);
    QCOMPARE(DesignTokens::rowMinHeightForDensity(QStringLiteral("nonsense")), 40);

    const QFontMetrics metrics(QApplication::font());
    // The floor only bites when the font is small: at the default size the
    // delegate formula already clears 40 px, but it must never go below the
    // floor even for a tiny font.
    QVERIFY(DesignTokens::rowHeightForDensity(metrics, QStringLiteral("comfortable")) >= 40);
    QVERIFY(DesignTokens::rowHeightForDensity(metrics, QStringLiteral("compact"))
            >= DesignTokens::TouchTargetCompact);
    QVERIFY(DesignTokens::rowHeightForDensity(metrics, QStringLiteral("spacious")) >= 48);
    const QFont tiny = TextAppearance::withFontPointDelta(QApplication::font(), -2);
    const QFontMetrics tinyMetrics(tiny);
    QVERIFY(DesignTokens::rowHeightForDensity(tinyMetrics, QStringLiteral("compact"))
            >= DesignTokens::TouchTargetCompact);
    // WCAG 2.5.8 target-spacing floor is the loosest bound in the tree.
    QVERIFY(DesignTokens::TouchTargetMin <= DesignTokens::TouchTargetCompact);
    QVERIFY(DesignTokens::TouchTargetCompact <= DesignTokens::TouchTargetFull);
}

void TestUiDesign::timelineHeightFollowsTheFont()
{
    // U5: no fixed 48 px — the strip grows with the UI font, floor at 44 px.
    const QFontMetrics metrics(QApplication::font());
    const int height = DesignTokens::timelineHeightForFont(metrics);
    QVERIFY(height >= 44);
    QCOMPARE(height, qMax(44, qRound(metrics.height() * 2.6)));
    const QFont bigger = TextAppearance::withFontPointDelta(QApplication::font(), 6);
    QVERIFY(DesignTokens::timelineHeightForFont(QFontMetrics(bigger)) >= height);
    const QFont tiny = TextAppearance::withFontPointDelta(QApplication::font(), -2);
    QVERIFY(DesignTokens::timelineHeightForFont(QFontMetrics(tiny)) >= 44);
}

void TestUiDesign::popupElevationAndShadowAreShared()
{
    // U4: one elevation language — popups sit above cards, cards above flat.
    QVERIFY(DesignTokens::ElevationPopupBlur > DesignTokens::ElevationCardBlur);
    QVERIFY(DesignTokens::ElevationPopupOffsetY >= DesignTokens::ElevationCardOffsetY);
    QCOMPARE(DesignTokens::ShadowAlpha, 140);
    QVERIFY(DesignTokens::MotionDrawerMs <= DesignTokens::MotionDurationMs);
    QVERIFY(DesignTokens::MotionChipMs <= DesignTokens::MotionDurationMs);
    QVERIFY(DesignTokens::MotionToastMs <= DesignTokens::MotionDurationMs);
    // Toasts dismiss on their own and never exceed the max width.
    QVERIFY(DesignTokens::ToastDurationMs >= 1000);
    QVERIFY(DesignTokens::ToastMaxWidth >= 320);
}

void TestUiDesign::searchFieldMeetsTheTouchFloor()
{
    // U5: the search field is at least the compact touch target tall.
    QLineEdit field;
    UiHelpers::styleSearchField(&field);
    QVERIFY(field.minimumHeight() >= DesignTokens::TouchTargetCompact);
    // Chips clear the same floor (close button 24 + vertical padding).
    QVERIFY(DesignTokens::ChipCloseSize + 2 * DesignTokens::ChipPaddingV
            >= DesignTokens::TouchTargetCompact);
}

void TestUiDesign::helpersBuildWithoutFixedPixels()
{
    // U4/U13: shared components construct offscreen and stay font-relative.
    QWidget parent;
    bool closed = false;
    QWidget *chip = UiHelpers::makeChip(QStringLiteral("type:text"), QStringLiteral("Type filter"),
                                        &parent, [&closed] { closed = true; });
    QVERIFY(chip);
    QVERIFY(chip->minimumHeight() >= DesignTokens::TouchTargetCompact);
    QVERIFY(!chip->accessibleName().isEmpty());
    QList<QPushButton *> buttons = chip->findChildren<QPushButton *>();
    QVERIFY(!buttons.isEmpty());
    QVERIFY(buttons.first()->minimumHeight() >= 0); // close affordance exists
    QVERIFY(!closed);

    QWidget *empty =
        UiHelpers::makeEmptyState(QStringLiteral("edit-copy"), QStringLiteral("No entries yet"),
                                  QStringLiteral("Copy something first"), &parent,
                                  QStringLiteral("Clear filters"), [] {});
    QVERIFY(empty);
    QVERIFY(!empty->findChildren<QPushButton *>().isEmpty());

    QWidget *toast = UiHelpers::makeToast(QStringLiteral("Entry deleted"), &parent,
                                          QStringLiteral("Undo"), [] {});
    QVERIFY(toast);
    QVERIFY(toast->maximumWidth() <= DesignTokens::ToastMaxWidth
            || toast->maximumWidth() == DesignTokens::ToastMaxWidth);
    QVERIFY(!toast->accessibleName().isEmpty());
    toast->deleteLater();

    // Motion honors the Reduce motion switch.
    UiHelpers::setReduceMotion(true);
    QWidget probe;
    UiHelpers::animate(&probe, UiHelpers::MotionKind::SlideUp);
    QCOMPARE(probe.windowOpacity(), 1.0);
    UiHelpers::setReduceMotion(false);
}

void TestUiDesign::timelineCollapsesBelowItsWidth()
{
    // R1: under TimelineCollapseWidth the strip hides behind a toggle instead
    // of squeezing 14 bars into nothing; the collapse point sits in Narrow.
    QCOMPARE(DesignTokens::shellModeForWidth(DesignTokens::TimelineCollapseWidth - 1),
             DesignTokens::ShellMode::Narrow);
    QCOMPARE(DesignTokens::shellModeForWidth(DesignTokens::TimelineCollapseWidth),
             DesignTokens::ShellMode::Narrow);
    // The collapsed strip still lays out without crashing (bars at min width).
    const DesignTokens::TimelineGeometry geometry = DesignTokens::timelineGeometry(
        QSize(DesignTokens::TimelineCollapseWidth - 100, 48), 14, 12);
    QVERIFY(geometry.barWidth >= DesignTokens::TimelineMinBarWidth);
}

void TestUiDesign::dayHeaderCoversTodayAndYesterday()
{
    // R2: day headers name today/yesterday, older days use the locale format.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QCOMPARE(EntryDelegate::dayHeaderText(now), QStringLiteral("Today"));
    QCOMPARE(EntryDelegate::dayHeaderText(now - 86400000), QStringLiteral("Yesterday"));
    const QString older = EntryDelegate::dayHeaderText(now - 5 * 86400000);
    QVERIFY(!older.isEmpty());
    QVERIFY(older != QLatin1String("Today"));
    QVERIFY(older != QLatin1String("Yesterday"));
}

void TestUiDesign::delegateRespectsRowExtras()
{
    // R2: extras are off by default and cheap to toggle.
    EntryDelegate delegate(nullptr, nullptr);
    QVERIFY(!delegate.showEntryIndex());
    QVERIFY(!delegate.showUseCountBadge());
    delegate.setShowEntryIndex(true);
    delegate.setShowUseCountBadge(true);
    QVERIFY(delegate.showEntryIndex());
    QVERIFY(delegate.showUseCountBadge());
    delegate.setShowEntryIndex(false);
    QVERIFY(!delegate.showEntryIndex());
}

QTEST_MAIN(TestUiDesign)
#include "tst_uidesign.moc"
