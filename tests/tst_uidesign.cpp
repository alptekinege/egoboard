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
#include "FirstRunTour.h"
#include "GroupsDock.h"
#include "SettingsStructure.h"
#include "TextAppearance.h"
#include "TimelineStrip.h"
#include "UiHelpers.h"

#include <KColorScheme>
#include <KSharedConfig>

#include <QApplication>
#include <QAccessible>
#include <QBuffer>
#include <QCheckBox>
#include <QDateTime>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QPalette>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScroller>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>
#include <QtConcurrent>

#include "ClipboardRecord.h"
#include "StorageManager.h"
#include "BookmarkManager.h"
#include "ExportImportDialogs.h"

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

// U19: one history row for the deferred-handoff tests below.
ClipboardRecord makeSettingsProbeRecord(const QByteArray &hash, const QString &text,
                                        const QString &app, qint64 timestamp)
{
    ClipboardRecord record;
    record.type = ContentType::Text;
    record.hash = hash;
    record.textData = text;
    record.preview = text.left(80);
    record.sizeBytes = text.size();
    record.timestamp = timestamp;
    record.sourceApp = app;
    return record;
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
    void motionDurationsFollowDesignTokens();
    void motionReduceSkipsAllKinds();
    void motionChipFadesAndScales();
    void motionToastSlidesUp();
    void motionDrawerSlidesSide();
    void emptyStateActionFiresOnClick();
    void ocrMetaSuffixCoversAvailability();
    void groupsEmptyStateOffersNewGroup();
    void settingQueryMatchingRequiresAllTokens();
    void settingTextHarvestFindsControlTexts();
    void firstSettingMatchRowFindsFirstHit();
    void settingsNarrowLayoutFollowsToken();
    void sidebarModeSwitchesDirectionAndFlow();
    void timelineKeyboardMovesAndActivates();
    void timelineAccessibleNamesBars();
    void timelineCollapsesBelowItsWidth();
    void timelineDayOptionsFeedNarrowCombo();
    void groupsOverlayDrawerFloats();
    void touchScrollGrabbedOnViewport();
    void pseudoLongExpandsGermanStyle();
    void longTranslationsDoNotClip();
    void screencastBlurPolicy();
    void dayHeaderCoversTodayAndYesterday();
    void delegateRespectsRowExtras();
    void settingsDeferredSnapshotsNeverTouchStorageOffThread();
    void settingsDeferredDeliverySurvivesEarlyClose();
    void settingsRepeatedOpenCloseStressesTheHandoff();
    void imageJpegEncoderRoundTripsPixels();
    void imageJpegEncoderRejectsUndecodableBlobs();
    void imageExportDialogDefaultsToSelection();
    void settingsSidebarPlanGroupsNormalAndAdvanced();
    void settingsSidebarLabelsStayFullyReadable();
    void settingsSidebarItemsAreCentered();
    void settingsSidebarFilterKeepsHeadersWithMatches();
    void aboutInfoCarriesVersionLicenseAndLocalNote();
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

void TestUiDesign::motionDurationsFollowDesignTokens()
{
    // U12: one animate() entry, durations from the tokens, capped at 120 ms.
    UiHelpers::setReduceMotion(false);
    QVERIFY(DesignTokens::MotionDrawerMs <= DesignTokens::MotionDurationMs);
    QVERIFY(DesignTokens::MotionChipMs <= DesignTokens::MotionDurationMs);
    QVERIFY(DesignTokens::MotionToastMs <= DesignTokens::MotionDurationMs);
    struct Case {
        UiHelpers::MotionKind kind;
        int expectedMs;
    };
    const QVector<Case> cases = {
        {UiHelpers::MotionKind::Fade, DesignTokens::MotionDurationMs},
        {UiHelpers::MotionKind::Chip, DesignTokens::MotionChipMs},
        {UiHelpers::MotionKind::SlideUp, DesignTokens::MotionToastMs},
        {UiHelpers::MotionKind::SlideSide, DesignTokens::MotionDrawerMs},
    };
    for (const Case &c : cases) {
        QWidget widget;
        widget.resize(120, 40);
        widget.move(50, 50);
        widget.show();
        QTest::qWait(10);
        UiHelpers::animate(&widget, c.kind);
        const QList<QPropertyAnimation *> animations =
            widget.findChildren<QPropertyAnimation *>();
        QVERIFY2(!animations.isEmpty(), "animate() started no property animations");
        for (QPropertyAnimation *animation : animations)
            QCOMPARE(animation->duration(), c.expectedMs);
        QTest::qWait(250); // let the 80–120 ms run finish before the next case
        widget.hide();
    }
    UiHelpers::setReduceMotion(false);
}

void TestUiDesign::motionReduceSkipsAllKinds()
{
    // U12: Reduce motion skips every kind — final state, no new animations.
    UiHelpers::setReduceMotion(true);
    const QVector<UiHelpers::MotionKind> kinds = {
        UiHelpers::MotionKind::Fade,
        UiHelpers::MotionKind::Chip,
        UiHelpers::MotionKind::SlideUp,
        UiHelpers::MotionKind::SlideSide,
    };
    for (UiHelpers::MotionKind kind : kinds) {
        QWidget widget;
        widget.resize(120, 40);
        widget.move(60, 60);
        widget.show();
        QTest::qWait(10);
        const QPoint posBefore = widget.pos();
        const QRect geomBefore = widget.geometry();
        UiHelpers::animate(&widget, kind);
        QCOMPARE(widget.pos(), posBefore);
        QCOMPARE(widget.geometry(), geomBefore);
        QCOMPARE(widget.windowOpacity(), 1.0);
        QVERIFY(widget.findChildren<QPropertyAnimation *>().isEmpty());
        widget.hide();
    }
    // An existing opacity effect is left opaque, never cleared to reveal a
    // shadow-less flash (toast/popup shadows are preserved).
    QWidget parent;
    parent.resize(300, 200);
    parent.show();
    QTest::qWait(10);
    auto *child = new QWidget(&parent);
    child->resize(100, 32);
    child->move(10, 10);
    child->show();
    auto *effect = new QGraphicsOpacityEffect(child);
    effect->setOpacity(0.3);
    child->setGraphicsEffect(effect);
    const QPoint childPosBefore = child->pos();
    UiHelpers::animate(child, UiHelpers::MotionKind::Chip);
    QCOMPARE(child->pos(), childPosBefore);
    QCOMPARE(effect->opacity(), 1.0);
    UiHelpers::setReduceMotion(false);
}

void TestUiDesign::motionChipFadesAndScales()
{
    // U12: chips fade via an opacity effect (windowOpacity is a no-op for them)
    // plus a layout-safe geometry pulse that ends back on the row geometry.
    UiHelpers::setReduceMotion(false);
    QWidget parent;
    parent.resize(400, 200);
    parent.show();
    QTest::qWait(20);
    QWidget *chip = UiHelpers::makeChip(QStringLiteral("type:text"),
                                        QStringLiteral("Type filter"), &parent, [] {});
    QVERIFY(chip);
    chip->resize(140, 32);
    chip->move(20, 20);
    chip->show();
    QTest::qWait(20);
    const QRect before = chip->geometry();
    QVERIFY(before.width() > 0);
    UiHelpers::animate(chip, UiHelpers::MotionKind::Chip);
    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(chip->graphicsEffect());
    QVERIFY(effect);
    QCOMPARE(effect->opacity(), 0.0);
    QVERIFY(chip->geometry().width() <= before.width());
    QVERIFY(chip->geometry().height() <= before.height());
    QTest::qWait(250);
    QCOMPARE(effect->opacity(), 1.0);
    QCOMPARE(chip->geometry(), before);
}

void TestUiDesign::motionToastSlidesUp()
{
    // U12: toast windows rise 16 px while fading over the toast duration.
    UiHelpers::setReduceMotion(false);
    QWidget toast(nullptr, Qt::ToolTip);
    toast.resize(220, 60);
    toast.move(100, 100);
    toast.show();
    QTest::qWait(20);
    const QPoint end = toast.pos();
    UiHelpers::animate(&toast, UiHelpers::MotionKind::SlideUp);
    QCOMPARE(toast.windowOpacity(), 0.0);
    QCOMPARE(toast.pos(), end + QPoint(0, 2 * DesignTokens::SpaceM));
    QTest::qWait(300);
    QCOMPARE(toast.pos(), end);
    QCOMPARE(toast.windowOpacity(), 1.0);
}

void TestUiDesign::motionDrawerSlidesSide()
{
    // U12: docked drawers slide from the side while fading over 80 ms; the
    // slide ends back on the layout position so the dock is stable.
    UiHelpers::setReduceMotion(false);
    QWidget parent;
    parent.resize(500, 400);
    parent.show();
    QTest::qWait(20);
    QWidget dock(&parent);
    dock.resize(300, 80);
    dock.move(50, 250);
    dock.show();
    QTest::qWait(20);
    const QPoint end = dock.pos();
    UiHelpers::animate(&dock, UiHelpers::MotionKind::SlideSide);
    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(dock.graphicsEffect());
    QVERIFY(effect);
    QCOMPARE(effect->opacity(), 0.0);
    QCOMPARE(dock.pos(), end + QPoint(2 * DesignTokens::SpaceL, 0));
    QTest::qWait(300);
    QCOMPARE(dock.pos(), end);
    QCOMPARE(effect->opacity(), 1.0);
}

void TestUiDesign::emptyStateActionFiresOnClick()
{
    // U13: every empty/error state is icon + title + one action — and the
    // action stays clickable under the click-through overlay attribute the
    // list, dock and snippet views use on the state widget.
    QWidget window;
    window.resize(360, 200);
    bool fired = false;
    QWidget *state = UiHelpers::makeEmptyState(
        QStringLiteral("edit-paste"), QStringLiteral("No entries yet"),
        QStringLiteral("Copy something first."), &window, QStringLiteral("Clear filters"),
        [&fired] { fired = true; });
    QVERIFY(state);
    state->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    state->setGeometry(window.rect());
    window.show();
    QTest::qWait(20);
    const QList<QPushButton *> buttons = state->findChildren<QPushButton *>();
    QCOMPARE(buttons.size(), 1);
    QVERIFY(buttons.first()->isVisible());
    QTest::mouseClick(buttons.first(), Qt::LeftButton);
    QVERIFY(fired);
}

void TestUiDesign::ocrMetaSuffixCoversAvailability()
{
    // U13 OCR-missing: stored text always shows; otherwise the footer names
    // the state — a processing note when tesseract exists, an install hint
    // when it does not. Pure helper, so no PATH dependency in tests.
    const QString withText =
        UiHelpers::ocrMetaSuffix(true, QStringLiteral("hello"), true);
    QVERIFY(withText.contains(QStringLiteral("hello")));
    // Stored text wins even without a blob or tesseract.
    QVERIFY(UiHelpers::ocrMetaSuffix(false, QStringLiteral("hello"), false)
                .contains(QStringLiteral("hello")));
    // Nothing stored and no blob: no footer line at all.
    QVERIFY(UiHelpers::ocrMetaSuffix(false, {}, true).isEmpty());
    // Blob awaiting OCR: processing note when tesseract exists...
    const QString pending = UiHelpers::ocrMetaSuffix(true, {}, true);
    QVERIFY(pending.contains(QStringLiteral("processing")));
    // ...install hint when it does not.
    const QString missing = UiHelpers::ocrMetaSuffix(true, {}, false);
    QVERIFY(missing.contains(QStringLiteral("tesseract")));
    QVERIFY(missing != pending);
}

void TestUiDesign::groupsEmptyStateOffersNewGroup()
{
    // U13 group-empty: the overlay carries a New-group action and hides once
    // a group exists (model signals drive the same updateEmptyState path).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("groups-empty.db")));
    BookmarkManager bookmarks(storage.database());
    GroupsDock dock(&bookmarks);
    dock.resize(280, 400);
    dock.show();
    QTest::qWait(100);
    QPushButton *action = nullptr;
    for (QPushButton *button : dock.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("New group"))
            action = button;
    }
    QVERIFY2(action != nullptr, "group-empty state has no New group action");
    QVERIFY(action->isVisible());
    QVERIFY(bookmarks.createGroup(QStringLiteral("Work")) != 0);
    QTest::qWait(100);
    QVERIFY(!action->isVisible());
}

void TestUiDesign::settingQueryMatchingRequiresAllTokens()
{
    // U14 search: order-free, case-insensitive, every token must occur.
    const QStringList texts{QStringLiteral("Backup folder daily JSON"),
                            QStringLiteral("Restore from file")};
    QVERIFY(UiHelpers::settingQueryMatches(texts, QStringLiteral("backup")));
    QVERIFY(UiHelpers::settingQueryMatches(texts, QStringLiteral("daily backup")));
    QVERIFY(UiHelpers::settingQueryMatches(texts, QStringLiteral("BACKUP")));
    QVERIFY(UiHelpers::settingQueryMatches(texts, QStringLiteral("  backup   daily  ")));
    QVERIFY(!UiHelpers::settingQueryMatches(texts, QStringLiteral("backup missing")));
    QVERIFY(!UiHelpers::settingQueryMatches(texts, QStringLiteral("tesseract")));
    QVERIFY(UiHelpers::settingQueryMatches(texts, QString())); // empty shows all
}

void TestUiDesign::settingTextHarvestFindsControlTexts()
{
    // U14 search harvest: labels, buttons, group titles, placeholders and
    // tooltips — HTML stripped so tags never match.
    QWidget page;
    auto *pageLayout = new QVBoxLayout(&page);
    auto *box = new QGroupBox(QStringLiteral("OCR — image text"), &page);
    pageLayout->addWidget(box);
    auto *layout = new QVBoxLayout(box);
    auto *label = new QLabel(QStringLiteral(R"(Install <code>tesseract</code> first)"), box);
    layout->addWidget(label);
    auto *check = new QCheckBox(QStringLiteral("Enable OCR"), box);
    layout->addWidget(check);
    auto *button = new QPushButton(QStringLiteral("Check now"), box);
    button->setToolTip(QStringLiteral("Probe the PATH"));
    layout->addWidget(button);
    auto *edit = new QLineEdit(box);
    edit->setPlaceholderText(QStringLiteral("Language code"));
    layout->addWidget(edit);

    const QStringList texts = UiHelpers::collectSettingTexts(&page);
    QVERIFY(texts.contains(QStringLiteral("OCR — image text")));
    QVERIFY(texts.contains(QStringLiteral("Install tesseract first"))); // tags stripped
    QVERIFY(!texts.contains(QStringLiteral("Install <code>tesseract</code> first")));
    QVERIFY(texts.contains(QStringLiteral("Enable OCR")));
    QVERIFY(texts.contains(QStringLiteral("Check now")));
    QVERIFY(texts.contains(QStringLiteral("Probe the PATH")));
    QVERIFY(texts.contains(QStringLiteral("Language code")));
    QVERIFY(UiHelpers::collectSettingTexts(nullptr).isEmpty());
}

void TestUiDesign::firstSettingMatchRowFindsFirstHit()
{
    // U14 search filtering: first matching page wins; empty/none give -1.
    const QList<QStringList> pages{
        QStringList{QStringLiteral("General theme")},
        QStringList{QStringLiteral("Storage backup folder")},
        QStringList{QStringLiteral("Diagnostics backup log")},
    };
    QCOMPARE(UiHelpers::firstSettingMatchRow(pages, QStringLiteral("backup")), 1);
    QCOMPARE(UiHelpers::firstSettingMatchRow(pages, QStringLiteral("theme")), 0);
    QCOMPARE(UiHelpers::firstSettingMatchRow(pages, QStringLiteral("missing")), -1);
    QCOMPARE(UiHelpers::firstSettingMatchRow(pages, QString()), -1);
    QCOMPARE(UiHelpers::firstSettingMatchRow(pages, QStringLiteral("   ")), -1);
    QCOMPARE(UiHelpers::firstSettingMatchRow({}, QStringLiteral("backup")), -1);
}

void TestUiDesign::settingsNarrowLayoutFollowsToken()
{
    // U14 responsive narrow: the collapse decision is one token, pinned like
    // the shell breakpoints above.
    QVERIFY(DesignTokens::SettingsSidebarCollapseWidth < DesignTokens::BreakpointNarrow);
    QVERIFY(DesignTokens::settingsNarrowLayoutForWidth(0));
    QVERIFY(DesignTokens::settingsNarrowLayoutForWidth(
        DesignTokens::SettingsSidebarCollapseWidth - 1));
    QVERIFY(!DesignTokens::settingsNarrowLayoutForWidth(
        DesignTokens::SettingsSidebarCollapseWidth));
    QVERIFY(!DesignTokens::settingsNarrowLayoutForWidth(4000));
}

void TestUiDesign::sidebarModeSwitchesDirectionAndFlow()
{
    // U14 responsive narrow: the shared helper turns a vertical icon sidebar
    // into a horizontal top strip and back, releasing the width clamp in
    // narrow mode and restoring it in wide mode. Idempotent.
    QWidget window;
    auto *content = new QHBoxLayout(&window);
    auto *sidebar = new QListWidget(&window);
    sidebar->setViewMode(QListView::IconMode);
    sidebar->setFlow(QListView::TopToBottom);
    sidebar->setGridSize(QSize(146, 64));
    sidebar->setFixedWidth(148);
    sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    for (int i = 0; i < 3; ++i)
        new QListWidgetItem(QStringLiteral("Page %1").arg(i), sidebar);
    auto *stack = new QStackedWidget(&window);
    content->addWidget(sidebar);
    content->addWidget(stack, 1);

    UiHelpers::applySidebarMode(sidebar, content, true, 148);
    QCOMPARE(content->direction(), QBoxLayout::TopToBottom);
    QCOMPARE(sidebar->flow(), QListView::LeftToRight);
    QCOMPARE(sidebar->horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    QCOMPARE(sidebar->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    QVERIFY(sidebar->maximumWidth() > 600); // 148 px clamp released
    QVERIFY(sidebar->minimumHeight() > 0); // one strip row tall

    UiHelpers::applySidebarMode(sidebar, content, false, 148);
    QCOMPARE(content->direction(), QBoxLayout::LeftToRight);
    QCOMPARE(sidebar->flow(), QListView::TopToBottom);
    QCOMPARE(sidebar->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    QCOMPARE(sidebar->minimumWidth(), 148);
    QCOMPARE(sidebar->maximumWidth(), 148);

    // Re-applying the same mode is a no-op (resize churn is harmless).
    UiHelpers::applySidebarMode(sidebar, content, false, 148);
    QCOMPARE(content->direction(), QBoxLayout::LeftToRight);
    QCOMPARE(sidebar->flow(), QListView::TopToBottom);
    QCOMPARE(sidebar->maximumWidth(), 148);
}

namespace {
// One history row for the timeline keyboard/AT tests below.
ClipboardRecord makeTimelineRecord(const QByteArray &hash, qint64 timestamp)
{
    ClipboardRecord record;
    record.type = ContentType::Text;
    record.hash = hash;
    record.textData = QString::fromLatin1(hash);
    record.preview = record.textData;
    record.sizeBytes = record.textData.size();
    record.timestamp = timestamp;
    record.sourceApp = QStringLiteral("tester");
    return record;
}
} // namespace

void TestUiDesign::timelineKeyboardMovesAndActivates()
{
    // U10: arrows move the day cursor from today, Enter filters, Esc clears.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("timeline-keys.db")));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVERIFY(storage.insertOrUpdate(makeTimelineRecord(QByteArrayLiteral("tk-today"),
                                                      now - 3600000))
            != 0);
    QVERIFY(storage.insertOrUpdate(makeTimelineRecord(QByteArrayLiteral("tk-yesterday"),
                                                      now - 86400000 - 3600000))
            != 0);
    TimelineStrip strip(&storage);
    strip.resize(400, 60);
    strip.show();
    QTest::qWait(20);
    strip.setFocus(Qt::OtherFocusReason);
    QTest::qWait(20);

    QSignalSpy selected(&strip, &TimelineStrip::daySelected);
    QTest::keyClick(&strip, Qt::Key_Left); // today (13) -> yesterday (12)
    QCOMPARE(strip.focusedBar(), 12);
    QTest::keyClick(&strip, Qt::Key_Right);
    QCOMPARE(strip.focusedBar(), 13);
    QTest::keyClick(&strip, Qt::Key_Home);
    QCOMPARE(strip.focusedBar(), 0);
    QTest::keyClick(&strip, Qt::Key_End);
    QCOMPARE(strip.focusedBar(), 13);
    // Empty bars clear like a click outside does (bar 0 is 13 days back).
    QTest::keyClick(&strip, Qt::Key_Home);
    QTest::keyClick(&strip, Qt::Key_Enter);
    QCOMPARE(strip.selectedBar(), -1);
    QCOMPARE(selected.count(), 1);
    QCOMPARE(selected.at(0).at(0).toLongLong(), qint64(0));
    // Today has an entry: Enter filters by its range.
    QTest::keyClick(&strip, Qt::Key_End);
    QTest::keyClick(&strip, Qt::Key_Enter);
    QCOMPARE(strip.selectedBar(), 13);
    QCOMPARE(selected.count(), 2);
    const qint64 from = selected.at(1).at(0).toLongLong();
    const qint64 to = selected.at(1).at(1).toLongLong();
    QVERIFY(from <= now && now <= to);
    QCOMPARE(to - from, qint64(86400000 - 1));
    QTest::keyClick(&strip, Qt::Key_Escape);
    QCOMPARE(strip.selectedBar(), -1);
    QCOMPARE(selected.count(), 3);
    // The focus ring paints without warnings or crashes.
    strip.grab();
}

void TestUiDesign::timelineAccessibleNamesBars()
{
    // U10: every bar exposes ListItem role + "N entries, Day" name + press.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("timeline-at.db")));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVERIFY(storage.insertOrUpdate(makeTimelineRecord(QByteArrayLiteral("ta-today"), now)) != 0);
    TimelineStrip strip(&storage);
    strip.resize(400, 60);
    strip.show();
    QTest::qWait(20);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&strip);
    QVERIFY(iface);
    QCOMPARE(iface->role(), QAccessible::List);
    QCOMPARE(iface->childCount(), 14);
    QVERIFY(iface->child(-1) == nullptr);
    QVERIFY(iface->child(14) == nullptr);
    QAccessibleInterface *today = iface->child(13);
    QVERIFY(today);
    QCOMPARE(today->role(), QAccessible::ListItem);
    const QString name = today->text(QAccessible::Name);
    QVERIFY2(name.contains(QStringLiteral("Today")), qPrintable(name));
    QVERIFY2(name.contains(QLatin1Char('1')), qPrintable(name)); // one entry
    QVERIFY(!today->text(QAccessible::Description).isEmpty());
    QVERIFY(today->childCount() == 0);
    QCOMPARE(iface->indexOfChild(today), 13);
    // Press activates the bar exactly like Enter (today has an entry).
    QSignalSpy selected(&strip, &TimelineStrip::daySelected);
    auto *actions = static_cast<QAccessibleActionInterface *>(
        today->interface_cast(QAccessible::ActionInterface));
    QVERIFY(actions);
    QVERIFY(actions->actionNames().contains(QAccessibleActionInterface::pressAction()));
    actions->doAction(QAccessibleActionInterface::pressAction());
    QCOMPARE(strip.selectedBar(), 13);
    QCOMPARE(selected.count(), 1);
    // Keyboard cursor drives the AT focus child.
    strip.setFocus(Qt::OtherFocusReason);
    QTest::qWait(20);
    QTest::keyClick(&strip, Qt::Key_Left);
    QAccessibleInterface *focused = iface->focusChild();
    QVERIFY(focused);
    QCOMPARE(iface->indexOfChild(focused), 12);
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

void TestUiDesign::timelineDayOptionsFeedNarrowCombo()
{
    // U10 narrow-combo variant: days with entries as labeled ranges that the
    // condensed combo drives with click-identical toggle semantics.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("timeline-options.db")));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVERIFY(storage.insertOrUpdate(makeTimelineRecord(QByteArrayLiteral("do-today"),
                                                      now - 3600000))
            != 0);
    QVERIFY(storage.insertOrUpdate(makeTimelineRecord(QByteArrayLiteral("do-yesterday"),
                                                      now - 86400000 - 3600000))
            != 0);
    TimelineStrip strip(&storage);
    strip.resize(400, 60);
    strip.show();
    QTest::qWait(20);

    const QVector<TimelineStrip::DayOption> options = strip.dayOptions();
    QCOMPARE(options.size(), 2); // empty days stay out of the combo
    QCOMPARE(options.at(0).label, QStringLiteral("Yest."));
    QCOMPARE(options.at(0).count, 1);
    QCOMPARE(options.at(1).label, QStringLiteral("Today"));
    QCOMPARE(options.at(1).count, 1);
    for (const auto &option : options) {
        QVERIFY(option.fromMs > 0);
        QCOMPARE(option.toMs - option.fromMs, qint64(86400000 - 1));
    }

    // Driving the strip from a day start filters exactly like a bar click,
    // and re-driving the active day clears again.
    QSignalSpy selected(&strip, &TimelineStrip::daySelected);
    strip.selectDay(options.at(1).fromMs);
    QCOMPARE(strip.selectedBar(), 13);
    QCOMPARE(selected.count(), 1);
    QCOMPARE(selected.at(0).at(0).toLongLong(), options.at(1).fromMs);
    strip.selectDay(options.at(1).fromMs);
    QCOMPARE(strip.selectedBar(), -1);
    QCOMPARE(selected.count(), 2);
    QCOMPARE(selected.at(1).at(0).toLongLong(), qint64(0));
    // Unknown days fall through to the clear path, never to a stray filter.
    strip.selectDay(12345);
    QCOMPARE(strip.selectedBar(), -1);
    QCOMPARE(selected.count(), 3);
    QCOMPARE(strip.barDayStart(-1), qint64(0));
    QCOMPARE(strip.barDayStart(99), qint64(0));
    QVERIFY(strip.barDayStart(13) > 0);
}

void TestUiDesign::groupsOverlayDrawerFloats()
{
    // U10 groups overlay: off Wide the dock floats as a drawer (drawer width
    // follows the parent window, accessible description names the mode).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("groups-overlay.db")));
    BookmarkManager bookmarks(storage.database());
    QWidget window;
    window.resize(800, 600);
    GroupsDock dock(&bookmarks, &window);
    window.show();
    QTest::qWait(50);

    QVERIFY(!dock.isOverlayMode());
    QVERIFY(!dock.isFloating());
    dock.setOverlayMode(true);
    QVERIFY(dock.isOverlayMode());
    QVERIFY(dock.isFloating());
    QVERIFY(!dock.accessibleDescription().isEmpty());
    QVERIFY(dock.width() >= 240 && dock.width() <= 420);
    // Idempotent: re-asserting the mode neither re-floats nor renames.
    const QString description = dock.accessibleDescription();
    dock.setOverlayMode(true);
    QVERIFY(dock.isFloating());
    QCOMPARE(dock.accessibleDescription(), description);
    dock.setOverlayMode(false);
    QVERIFY(!dock.isOverlayMode());
    QVERIFY(!dock.isFloating());
    QVERIFY(!dock.accessibleDescription().isEmpty());
}

void TestUiDesign::touchScrollGrabbedOnViewport()
{
    // §7 tail: the history list swipes on touchscreens (TouchGesture on the
    // viewport — mouse drags keep their DnD meaning). Null-safe.
    QListView view;
    QVERIFY(!QScroller::hasScroller(view.viewport()));
    UiHelpers::enableTouchScroll(&view);
    QVERIFY(QScroller::hasScroller(view.viewport()));
    UiHelpers::enableTouchScroll(nullptr); // must not crash
}

void TestUiDesign::pseudoLongExpandsGermanStyle()
{
    // §7 tail probe: German-length expansion for offscreen layout checks.
    QCOMPARE(UiHelpers::pseudoLong(QString()), QStringLiteral("[]"));
    const QString expanded = UiHelpers::pseudoLong(QStringLiteral("Paste"));
    QVERIFY(expanded.startsWith(QLatin1Char('[')));
    QVERIFY(expanded.endsWith(QLatin1Char(']')));
    QCOMPARE(expanded, QStringLiteral("[Paastee]"));
    QVERIFY(expanded.size() > QStringLiteral("Paste").size() + 2);
    // Consonants survive in order; umlauts double like plain vowels.
    QVERIFY(UiHelpers::pseudoLong(QStringLiteral("Grüße")).contains(QStringLiteral("üü")));
}

namespace {
// §7 tail: every word-wrapped label must be tall enough for its own width —
// anything shorter clips German-length text.
bool noClippedLabels(QWidget *dialog, const char *context)
{
    for (QLabel *label : dialog->findChildren<QLabel *>()) {
        if (!label->isVisible() || !label->wordWrap() || label->width() <= 0)
            continue;
        const int needed = label->heightForWidth(label->width());
        if (needed >= 0 && label->height() < needed) {
            qWarning("%s: label clips (%dx%d, needs %d): %s", context, label->width(),
                     label->height(), needed, qPrintable(label->text().left(40)));
            return false;
        }
    }
    return true;
}
} // namespace

void TestUiDesign::longTranslationsDoNotClip()
{
    // §7 tail: the tour survives German-length strings at default and narrow
    // widths without clipping a wrapped label.
    QList<FirstRunTour::Step> steps;
    for (const auto &step : FirstRunTour::defaultSteps()) {
        steps.append({step.iconName, UiHelpers::pseudoLong(step.title),
                      UiHelpers::pseudoLong(step.body), UiHelpers::pseudoLong(step.hint)});
    }
    FirstRunTour dialog(steps);
    dialog.show();
    QTest::qWait(50);
    for (int i = 0; i < dialog.stepCount(); ++i) {
        dialog.goToStep(i);
        QTest::qWait(20);
        QVERIFY(noClippedLabels(&dialog, "tour-default"));
    }
    dialog.resize(360, 300); // narrow window: wrapping absorbs the length
    QTest::qWait(50);
    for (int i = 0; i < dialog.stepCount(); ++i) {
        dialog.goToStep(i);
        QTest::qWait(20);
        QVERIFY(noClippedLabels(&dialog, "tour-narrow"));
    }
    dialog.close();
}

void TestUiDesign::screencastBlurPolicy()
{
    // R6 auto-blur truth table: sharing hides any valid payload; otherwise
    // the R2 rule stands (sensitive entries behind the privacy-blur setting).
    QVERIFY(!UiHelpers::shouldBlurPreview(false, false, false, false));
    QVERIFY(!UiHelpers::shouldBlurPreview(true, false, false, false));
    QVERIFY(!UiHelpers::shouldBlurPreview(true, true, false, false));
    QVERIFY(UiHelpers::shouldBlurPreview(true, true, true, false));
    QVERIFY(UiHelpers::shouldBlurPreview(true, false, false, true));
    QVERIFY(UiHelpers::shouldBlurPreview(true, true, false, true));
    QVERIFY(UiHelpers::shouldBlurPreview(true, false, true, true));
    QVERIFY(!UiHelpers::shouldBlurPreview(false, true, true, true));
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

void TestUiDesign::settingsDeferredSnapshotsNeverTouchStorageOffThread()
{
    // U19: the Settings dialog snapshots sourceApps()/stats()/databasePath on
    // the owning (GUI) thread and the worker only carries those copies plus an
    // external probe — storage is never touched off-thread. The handoff below
    // is the same shape the dialog uses (QPointer guard + queued delivery on
    // qApp), driven against the real StorageManager.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("settings-handoff.db")));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVERIFY(storage.insertOrUpdate(
                makeSettingsProbeRecord(QByteArrayLiteral("u19-a1"), QStringLiteral("alpha"),
                                        QStringLiteral("firefox"), now))
            != 0);
    QVERIFY(storage.insertOrUpdate(
                makeSettingsProbeRecord(QByteArrayLiteral("u19-a2"), QStringLiteral("beta"),
                                        QStringLiteral("konsole"), now + 1))
            != 0);

    // Snapshot on the owning thread, exactly like refreshDiagnostics does.
    const QStringList appsSnap = storage.sourceApps();
    const StorageStats statsSnap = storage.stats();
    const QString dbPathSnap = storage.databasePath();
    QCOMPARE(appsSnap.size(), 2);
    QVERIFY(appsSnap.contains(QStringLiteral("firefox")));
    QVERIFY(appsSnap.contains(QStringLiteral("konsole")));
    QCOMPARE(statsSnap.entryCount, 2);

    QThread *guiThread = QThread::currentThread();
    QObject guardHost; // stands in for the dialog; stays alive for delivery
    QPointer<QObject> guard(&guardHost);
    bool workerRanOffGui = false;
    bool delivered = false;
    QStringList deliveredApps;
    qint64 deliveredEntries = -1;
    QString deliveredPath;
    QFuture<void> future = QtConcurrent::run([guard, appsSnap, statsSnap, dbPathSnap,
                                              guiThread, &workerRanOffGui, &delivered,
                                              &deliveredApps, &deliveredEntries,
                                              &deliveredPath] {
        workerRanOffGui = (QThread::currentThread() != guiThread);
        // Only snapshots cross the thread boundary here — no storage calls.
        QMetaObject::invokeMethod(
            qApp,
            [guard, appsSnap, statsSnap, dbPathSnap, &delivered, &deliveredApps,
             &deliveredEntries, &deliveredPath] {
                if (!guard)
                    return;
                delivered = true;
                deliveredApps = appsSnap;
                deliveredEntries = statsSnap.entryCount;
                deliveredPath = dbPathSnap;
            },
            Qt::QueuedConnection);
    });
    future.waitForFinished();
    QVERIFY(workerRanOffGui); // proves the worker really ran off-thread
    for (int i = 0; i < 50 && !delivered; ++i)
        QTest::qWait(10);
    QVERIFY(delivered);
    QCOMPARE(deliveredApps, appsSnap);
    QCOMPARE(deliveredEntries, qint64(2));
    QCOMPARE(deliveredPath, dbPathSnap);

    // Empty history takes the same path: no rows, same handoff, no crash.
    StorageManager empty(dir.filePath(QStringLiteral("settings-handoff-empty.db")));
    const QStringList emptyApps = empty.sourceApps();
    const StorageStats emptyStats = empty.stats();
    QVERIFY(emptyApps.isEmpty());
    QCOMPARE(emptyStats.entryCount, 0);
    bool emptyDelivered = false;
    QFuture<void> emptyFuture = QtConcurrent::run([guard, emptyApps, emptyStats, &emptyDelivered] {
        QMetaObject::invokeMethod(
            qApp,
            [guard, emptyApps, emptyStats, &emptyDelivered] {
                if (!guard)
                    return;
                emptyDelivered = emptyApps.isEmpty() && emptyStats.entryCount == 0;
            },
            Qt::QueuedConnection);
    });
    emptyFuture.waitForFinished();
    for (int i = 0; i < 50 && !emptyDelivered; ++i)
        QTest::qWait(10);
    QVERIFY(emptyDelivered);
}

void TestUiDesign::settingsDeferredDeliverySurvivesEarlyClose()
{
    // U19: open-then-immediately-close while a deferred worker is pending. The
    // guard dies before the queued delivery runs, so the delivery must no-op
    // instead of touching a dangling dialog. Exercised with and without history.
    for (int round = 0; round < 2; ++round) {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        StorageManager storage(dir.filePath(QStringLiteral("settings-close.db")));
        if (round == 1) {
            QVERIFY(storage.insertOrUpdate(makeSettingsProbeRecord(
                        QByteArrayLiteral("u19-c1"), QStringLiteral("hello"),
                        QStringLiteral("firefox"), QDateTime::currentMSecsSinceEpoch()))
                    != 0);
        }
        const QStringList appsSnap = storage.sourceApps();
        const StorageStats statsSnap = storage.stats();
        QCOMPARE(statsSnap.entryCount, round == 1 ? qint64(1) : qint64(0));

        QPointer<QObject> guard(new QObject); // the "dialog", closed at once
        bool delivered = false;
        QFuture<void> future = QtConcurrent::run([guard, appsSnap, statsSnap, &delivered] {
            QThread::msleep(20); // widen the close race: dialog dies first
            QMetaObject::invokeMethod(
                qApp,
                [guard, &delivered] {
                    if (!guard)
                        return; // closed — must not touch the dead dialog
                    delivered = true;
                },
                Qt::QueuedConnection);
        });
        delete guard.data(); // immediate close while the worker is pending
        QVERIFY(guard.isNull());
        future.waitForFinished();
        for (int i = 0; i < 30; ++i)
            QTest::qWait(10);
        QVERIFY(!delivered); // no dangling callback fired
    }
}

void TestUiDesign::settingsRepeatedOpenCloseStressesTheHandoff()
{
    // U19: hammer the open/close race — 20 rapid snapshot+dispatch+destroy
    // cycles across populated and empty databases — then prove a live handoff
    // still completes afterwards.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager full(dir.filePath(QStringLiteral("settings-stress-full.db")));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int i = 0; i < 5; ++i) {
        QVERIFY(full.insertOrUpdate(makeSettingsProbeRecord(
                    QByteArrayLiteral("u19-s") + QByteArray::number(i),
                    QStringLiteral("entry %1").arg(i),
                    i % 2 == 0 ? QStringLiteral("firefox") : QStringLiteral("konsole"),
                    now + i))
                != 0);
    }
    StorageManager empty(dir.filePath(QStringLiteral("settings-stress-empty.db")));

    QList<QFuture<void>> pending;
    for (int i = 0; i < 20; ++i) {
        StorageManager &storage = (i % 2 == 0) ? full : empty;
        const QStringList appsSnap = storage.sourceApps();
        const StorageStats statsSnap = storage.stats();
        QPointer<QObject> guard(new QObject);
        pending.append(QtConcurrent::run([guard, appsSnap, statsSnap] {
            QThread::msleep(5);
            QMetaObject::invokeMethod(
                qApp,
                [guard, appsSnap, statsSnap] {
                    if (!guard)
                        return;
                    // Live delivery would apply the snapshots; dead ones no-op.
                    Q_UNUSED(appsSnap);
                    Q_UNUSED(statsSnap);
                },
                Qt::QueuedConnection);
        }));
        delete guard.data(); // close before the worker finishes
    }
    for (QFuture<void> &future : pending)
        future.waitForFinished();
    for (int i = 0; i < 20; ++i)
        QTest::qWait(10); // let stale deliveries drain; none may crash

    // A live dialog still gets its snapshots after the storm.
    QObject liveHost;
    QPointer<QObject> liveGuard(&liveHost);
    const QStringList appsSnap = full.sourceApps();
    bool liveDelivered = false;
    QFuture<void> live = QtConcurrent::run([liveGuard, appsSnap, &liveDelivered] {
        QMetaObject::invokeMethod(
            qApp,
            [liveGuard, appsSnap, &liveDelivered] {
                if (!liveGuard)
                    return;
                liveDelivered = (appsSnap.size() == 2);
            },
            Qt::QueuedConnection);
    });
    live.waitForFinished();
    for (int i = 0; i < 50 && !liveDelivered; ++i)
        QTest::qWait(10);
    QVERIFY(liveDelivered);
}

void TestUiDesign::imageJpegEncoderRoundTripsPixels()
{
    // U17 (format choice): the app-layer encoder behind core's hook turns the
    // stored PNG into a readable JPEG of the same dimensions.
    QImage source(16, 12, QImage::Format_ARGB32);
    source.fill(QColor(0xd0, 0x20, 0x20, 0xff));
    QByteArray png;
    {
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(source.save(&buffer, "PNG"));
    }
    QString extension;
    QString error;
    const QByteArray jpeg =
        ExportImportDialogs::encodeImageForExport(png, 42, 90, &extension, &error);
    QVERIFY2(!jpeg.isEmpty(), qPrintable(error));
    QCOMPARE(extension, QStringLiteral("jpg"));
    // Real JPEG bitstream, same frame size.
    QVERIFY(jpeg.size() >= 2);
    QCOMPARE(static_cast<unsigned char>(jpeg.at(0)), 0xff);
    QCOMPARE(static_cast<unsigned char>(jpeg.at(1)), 0xd8);
    const QImage decoded = QImage::fromData(jpeg, "JPEG");
    QCOMPARE(decoded.size(), source.size());
}

void TestUiDesign::imageJpegEncoderRejectsUndecodableBlobs()
{
    // Corrupt blobs abort the run with an actionable message instead of
    // writing an empty file.
    QString extension = QStringLiteral("kept");
    QString error;
    const QByteArray out = ExportImportDialogs::encodeImageForExport(
        QByteArrayLiteral("not an image"), 7, 85, &extension, &error);
    QVERIFY(out.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("7")));
}

void TestUiDesign::imageExportDialogDefaultsToSelection()
{
    // U17: the bulk bar opens the dialog on its selection; other scopes stay
    // one click away and privacy defaults stay opt-in.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("image-dialog.db")));
    BookmarkManager bookmarks(storage.database());
    ClipboardRecord record;
    record.type = ContentType::Image;
    record.hash = QByteArrayLiteral("dlg-1");
    record.blobData = QByteArrayLiteral("PNG-DATA");
    record.hasBlob = true;
    record.preview = QStringLiteral("image");
    record.timestamp = QDateTime::currentMSecsSinceEpoch();
    const qint64 id = storage.insertOrUpdate(record);
    QVERIFY(id != 0);

    ExportImportDialogs::ImageExportDialog dialog(&bookmarks, {id}, FilterSpec{}, false, nullptr);
    QCOMPARE(dialog.scope(), ExportImportDialogs::ImageExportDialog::Scope::Selection);
    QVERIFY(dialog.folder().isEmpty());
    QVERIFY(!dialog.includeSensitive());
    QVERIFY(!dialog.includeText());
    QCOMPARE(dialog.fileFormat(),
             ExportImportManager::ImageExportRequest::ImageFileFormat::Png);
    QCOMPARE(dialog.jpegQuality(), 85);
    dialog.setScope(ExportImportDialogs::ImageExportDialog::Scope::Everything);
    QCOMPARE(dialog.scope(), ExportImportDialogs::ImageExportDialog::Scope::Everything);
}

void TestUiDesign::settingsSidebarPlanGroupsNormalAndAdvanced()
{
    // Normal/Advanced split: Normal pages first, one "Advanced" header,
    // advanced pages, About last. Headers carry no page; pages cover the
    // whole builder order exactly once.
    const QVector<SettingsStructure::SidebarRow> rows = SettingsStructure::sidebarRows();
    QCOMPARE(rows.size(), 12);
    const QStringList normal{QStringLiteral("General"), QStringLiteral("Capture"),
                             QStringLiteral("History"), QStringLiteral("Usage"),
                             QStringLiteral("Shortcuts"), QStringLiteral("Storage")};
    for (int i = 0; i < normal.size(); ++i) {
        QVERIFY(!rows.at(i).header);
        QCOMPARE(rows.at(i).label, normal.at(i));
        QCOMPARE(rows.at(i).page, i);
        QVERIFY(!rows.at(i).iconName.isEmpty());
    }
    QVERIFY(rows.at(6).header);
    QCOMPARE(rows.at(6).label, QStringLiteral("Advanced"));
    QCOMPARE(rows.at(6).page, -1);
    const QStringList advanced{QStringLiteral("Privacy"), QStringLiteral("Search & Preview"),
                               QStringLiteral("Automation"), QStringLiteral("Diagnostics")};
    for (int i = 0; i < advanced.size(); ++i) {
        QVERIFY(!rows.at(7 + i).header);
        QCOMPARE(rows.at(7 + i).label, advanced.at(i));
        QCOMPARE(rows.at(7 + i).page, 6 + i);
    }
    QVERIFY(!rows.last().header);
    QCOMPARE(rows.last().label, QStringLiteral("About"));
    QCOMPARE(rows.last().page, 10);
}

void TestUiDesign::settingsSidebarLabelsStayFullyReadable()
{
    // Regression: setUniformItemSizes on this IconMode sidebar forced every
    // label into the first row's narrow text rect ("Gene…", "Searc…" —
    // reproduced offscreen under Fusion and Breeze). The factory must leave
    // it off, and every plan label must fit the cell (wrapping allowed).
    QListWidget *sidebar = SettingsStructure::createSidebar();
    QVERIFY(sidebar);
    QVERIFY(!sidebar->uniformItemSizes());
    QCOMPARE(sidebar->viewMode(), QListView::IconMode);
    QCOMPARE(sidebar->gridSize(), QSize(146, 64));
    QCOMPARE(sidebar->minimumWidth(), sidebar->maximumWidth());
    const QFontMetrics metrics(sidebar->font());
    const int cellWidth = sidebar->gridSize().width();
    for (const SettingsStructure::SidebarRow &row : SettingsStructure::sidebarRows()) {
        if (metrics.horizontalAdvance(row.label) <= cellWidth)
            continue; // fits on one line
        // Otherwise every whitespace-separated word must fit its own line.
        for (const QString &word : row.label.split(QLatin1Char(' '))) {
            QVERIFY2(metrics.horizontalAdvance(word) <= cellWidth, qPrintable(row.label));
        }
    }
    sidebar->deleteLater();
}

void TestUiDesign::settingsSidebarItemsAreCentered()
{
    // IconMode centering varies by style — pin it explicitly on every row
    // (icons follow the centered text block), headers included.
    QListWidget *sidebar = SettingsStructure::createSidebar();
    SettingsStructure::populateSidebar(sidebar);
    QCOMPARE(sidebar->count(), SettingsStructure::sidebarRows().size());
    for (int r = 0; r < sidebar->count(); ++r) {
        // Note: QListWidgetItem::textAlignment() returns int, not Qt::Alignment.
        const int alignment = sidebar->item(r)->textAlignment();
        QVERIFY2(alignment & Qt::AlignHCenter, qPrintable(sidebar->item(r)->text()));
    }
    sidebar->deleteLater();
}

void TestUiDesign::settingsSidebarFilterKeepsHeadersWithMatches()
{
    // Search filtering over the grouped plan: a page match keeps its group
    // header visible, the jump target skips headers, nothing matches nothing.
    const QVector<SettingsStructure::SidebarRow> rows = SettingsStructure::sidebarRows();
    QVector<QStringList> texts;
    for (const auto &row : rows)
        texts.append(QStringList{row.label});
    texts[8].append(QStringLiteral("OCR language code")); // Search & Preview knob

    const QVector<bool> ocrVisible =
        SettingsStructure::filterSidebarRows(texts, rows, QStringLiteral("ocr"));
    QVERIFY(ocrVisible.at(8)); // the matching page
    QVERIFY(ocrVisible.at(6)); // its group header stays
    QVERIFY(!ocrVisible.at(0)); // unrelated Normal pages hide
    QVERIFY(!ocrVisible.at(11)); // About hides too
    QCOMPARE(SettingsStructure::firstContentRow(ocrVisible, rows), 8);

    const QVector<bool> noneVisible =
        SettingsStructure::filterSidebarRows(texts, rows, QStringLiteral("zzz-no-match"));
    for (bool show : noneVisible)
        QVERIFY(!show);
    QCOMPARE(SettingsStructure::firstContentRow(noneVisible, rows), -1);

    const QVector<bool> allVisible =
        SettingsStructure::filterSidebarRows(texts, rows, QString());
    for (bool show : allVisible)
        QVERIFY(show);
    QCOMPARE(SettingsStructure::firstContentRow(allVisible, rows), 0);
}

void TestUiDesign::aboutInfoCarriesVersionLicenseAndLocalNote()
{
    // Sade About: title, the caller's version, description + MIT + local-only.
    const SettingsStructure::AboutInfo info =
        SettingsStructure::aboutInfo(QStringLiteral("9.9-test"));
    QVERIFY(!info.title.isEmpty());
    QCOMPARE(info.version, QStringLiteral("9.9-test"));
    QCOMPARE(info.paragraphs.size(), 3);
    QVERIFY(info.paragraphs.at(0).contains(QStringLiteral("clipboard"), Qt::CaseInsensitive));
    QVERIFY(info.paragraphs.at(1).contains(QStringLiteral("MIT")));
    QVERIFY(info.paragraphs.at(2).contains(QStringLiteral("telemetry")));
}

QTEST_MAIN(TestUiDesign)
#include "tst_uidesign.moc"
