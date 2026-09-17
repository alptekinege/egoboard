#include <QtTest>

#include "CodePreviewHighlighter.h"
#include "ColorSchemeIndex.h"
#include "IconThemeIndex.h"
#include "IconThemeManager.h"
#include "TextAppearance.h"
#include "ThemeManager.h"

#include <KColorScheme>
#include <KConfigGroup>
#include <KSharedConfig>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QPalette>
#include <QSet>
#include <QStyle>
#include <QTextDocument>
#include <QVector>

namespace {
// Mirrors IconThemeIndex's rule: only a directory whose index.theme declares
// icon directories is a usable theme (cursor themes declare none).
bool declaresIconDirectories(const QString &directory)
{
    const QString indexFile = QDir(directory).filePath(QStringLiteral("index.theme"));
    if (!QFileInfo::exists(indexFile))
        return false;
    return !KSharedConfig::openConfig(indexFile)
                ->group(QStringLiteral("Icon Theme"))
                .readEntry(QStringLiteral("Directories"), QString())
                .isEmpty();
}

// Directory backing an installed icon theme, empty when it is not installed.
QString iconThemeDirectory(const QString &id)
{
    const QStringList roots = IconThemeIndex::iconDirectories();
    for (const QString &root : roots) {
        const QString candidate = QDir(root).filePath(id);
        if (declaresIconDirectories(candidate))
            return candidate;
    }
    return QString();
}

// True when the theme itself ships the icon. QIcon::hasThemeIcon() would also
// answer yes when only the fallback theme has it, which is exactly the case the
// pixmap comparison below must avoid.
bool themeShipsIcon(const QString &id, const QString &iconName)
{
    const QString directory = iconThemeDirectory(id);
    if (directory.isEmpty())
        return false;
    const QStringList iconDirectories =
        KSharedConfig::openConfig(QDir(directory).filePath(QStringLiteral("index.theme")))
            ->group(QStringLiteral("Icon Theme"))
            .readEntry(QStringLiteral("Directories"), QString())
            .split(QLatin1Char(','));
    for (const QString &iconDirectory : iconDirectories) {
        const QDir dir(QDir(directory).filePath(iconDirectory));
        if (!dir.entryList({iconName + QStringLiteral(".*")}, QDir::Files).isEmpty())
            return true;
    }
    return false;
}
} // namespace

class TestUiLogic : public QObject
{
    Q_OBJECT

private slots:
    void detectsPreviewModes();
    void plainModeLeavesTextUnformatted();
    void jsonModeFormatsKeysStringsNumbersAndBooleans();
    void xmlModeFormatsTagsAndAttributes();
    void codeModeFormatsKeywordsCommentsAndStrings();
    void themePaletteSwitchesAndRestores();
    void discoversInstalledColorSchemes();
    void appliesSchemeFromDisk();
    void legacyPresetsResolveToBreezeSchemes();
    void themeFilesAreNeverModified();
    void discoversInstalledIconThemes();
    void appliesIconThemeFromDisk();
    void iconThemeSwitchUpdatesExistingIcons();
    void computesContrastRatios();
    void raisesUnreadableTextToTheContrastFloor();
    void keepsEveryInstalledSchemeReadable();
    void appliesTextOverridesAndFontSize();
};

void TestUiLogic::detectsPreviewModes()
{
    QCOMPARE(CodePreviewHighlighter::detect(QString()), CodePreviewHighlighter::Mode::Plain);
    QCOMPARE(CodePreviewHighlighter::detect(QStringLiteral("ordinary clipboard text")),
             CodePreviewHighlighter::Mode::Plain);
    QCOMPARE(CodePreviewHighlighter::detect(QStringLiteral(R"({"name":"egoboard","count":2})")),
             CodePreviewHighlighter::Mode::Json);
    QCOMPARE(CodePreviewHighlighter::detect(QStringLiteral("<root><item>value</item></root>")),
             CodePreviewHighlighter::Mode::Xml);
    QCOMPARE(CodePreviewHighlighter::detect(QStringLiteral("function run() { return 'ok'; }")),
             CodePreviewHighlighter::Mode::Code);
    QCOMPARE(CodePreviewHighlighter::detect(QStringLiteral("def run():\n    return 1")),
             CodePreviewHighlighter::Mode::Code);
    QCOMPARE(CodePreviewHighlighter::detect(QStringLiteral("{ not actually structured }")),
             CodePreviewHighlighter::Mode::Plain);
}

void TestUiLogic::plainModeLeavesTextUnformatted()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("plain text 42"));
    CodePreviewHighlighter highlighter(&document);
    highlighter.setMode(CodePreviewHighlighter::Mode::Plain);
    const QTextBlock block = document.begin();
    QVERIFY(block.isValid());
    QCOMPARE(block.layout()->formats().size(), 0);
}

void TestUiLogic::jsonModeFormatsKeysStringsNumbersAndBooleans()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral(R"({"name":"egoboard","count":42,"enabled":true})"));
    CodePreviewHighlighter highlighter(&document);
    highlighter.setMode(CodePreviewHighlighter::Mode::Json);

    const QList<QTextLayout::FormatRange> formats = document.begin().layout()->formats();
    QVERIFY(formats.size() >= 4);
    const QString text = document.toPlainText();
    const auto hasFormatAt = [&formats](int position) {
        for (const auto &range : formats) {
            if (position >= range.start && position < range.start + range.length)
                return true;
        }
        return false;
    };
    QVERIFY(hasFormatAt(text.indexOf(QStringLiteral("name"))));
    QVERIFY(hasFormatAt(text.indexOf(QStringLiteral("egoboard"))));
    QVERIFY(hasFormatAt(text.indexOf(QStringLiteral("42"))));
    QVERIFY(hasFormatAt(text.indexOf(QStringLiteral("true"))));
}

void TestUiLogic::xmlModeFormatsTagsAndAttributes()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("<item id=\"7\">value</item>"));
    CodePreviewHighlighter highlighter(&document);
    highlighter.setMode(CodePreviewHighlighter::Mode::Xml);

    const auto formats = document.begin().layout()->formats();
    QVERIFY(formats.size() >= 2);
    const QString text = document.toPlainText();
    QVERIFY(std::any_of(formats.cbegin(), formats.cend(), [&text](const QTextLayout::FormatRange &range) {
        return range.start == text.indexOf(QLatin1Char('<'));
    }));
    QVERIFY(std::any_of(formats.cbegin(), formats.cend(), [&text](const QTextLayout::FormatRange &range) {
        return range.start == text.indexOf(QStringLiteral("id"));
    }));
}

void TestUiLogic::codeModeFormatsKeywordsCommentsAndStrings()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("return \"value\"; // comment"));
    CodePreviewHighlighter highlighter(&document);
    highlighter.setMode(CodePreviewHighlighter::Mode::Code);

    const auto formats = document.begin().layout()->formats();
    QVERIFY(formats.size() >= 3);
    const QString text = document.toPlainText();
    QVERIFY(std::any_of(formats.cbegin(), formats.cend(), [&text](const QTextLayout::FormatRange &range) {
        return range.start == text.indexOf(QStringLiteral("return"));
    }));
    QVERIFY(std::any_of(formats.cbegin(), formats.cend(), [&text](const QTextLayout::FormatRange &range) {
        return range.start == text.indexOf(QStringLiteral("\"value\""));
    }));
    QVERIFY(std::any_of(formats.cbegin(), formats.cend(), [&text](const QTextLayout::FormatRange &range) {
        return range.start == text.indexOf(QStringLiteral("// comment"));
    }));
}

void TestUiLogic::themePaletteSwitchesAndRestores()
{
    const QPalette original = qApp->palette();

    // Themes are not defined in code any more: apply() installs the palette of
    // the *.colors file ColorSchemeIndex points at, so the expectation is
    // derived from that same file instead of hardcoded hex values.
    for (const QString &id : {QStringLiteral("light"), QStringLiteral("dark")}) {
        const QString path = ColorSchemeIndex::filePath(id);
        QVERIFY2(!path.isEmpty(), qPrintable(QStringLiteral("scheme not installed: %1").arg(id)));

        ThemeManager::apply(id);
        // Installed palettes always go through the readability layer, so that is
        // what the result has to match.
        const QPalette expected = TextAppearance::applyOverrides(
            KColorScheme::createApplicationPalette(KSharedConfig::openConfig(path)), {});
        QCOMPARE(qApp->palette().color(QPalette::Window), expected.color(QPalette::Window));
        QCOMPARE(qApp->palette().color(QPalette::Base), expected.color(QPalette::Base));
        QCOMPARE(qApp->palette().color(QPalette::Highlight), expected.color(QPalette::Highlight));
    }

    // An id with no scheme behind it restores the style's own palette.
    ThemeManager::apply(QStringLiteral("invalid"));
    QCOMPARE(qApp->palette(),
             TextAppearance::applyOverrides(QApplication::style()->standardPalette(), {}));
    qApp->setPalette(original);
}

void TestUiLogic::discoversInstalledColorSchemes()
{
    const QVector<ColorSchemeIndex::Entry> schemes = ColorSchemeIndex::scan();
    QVERIFY2(!schemes.isEmpty(), "no KDE color schemes installed to discover");

    QSet<QString> ids;
    QString previousName;
    for (const ColorSchemeIndex::Entry &scheme : schemes) {
        QVERIFY(!scheme.id.isEmpty());
        QVERIFY(!scheme.name.isEmpty());
        QVERIFY(scheme.path.endsWith(QStringLiteral(".colors")));
        QVERIFY(QFileInfo::exists(scheme.path));
        // One entry per id even when several XDG directories provide it.
        QVERIFY(!ids.contains(scheme.id));
        ids.insert(scheme.id);
        // Sorted by display name, and every id resolves back to its own file.
        QVERIFY(previousName.isEmpty()
                || QString::compare(previousName, scheme.name, Qt::CaseInsensitive) <= 0);
        previousName = scheme.name;
        QVERIFY(ColorSchemeIndex::isValid(scheme.id));
        QCOMPARE(ColorSchemeIndex::filePath(scheme.id), scheme.path);
    }
}

void TestUiLogic::appliesSchemeFromDisk()
{
    const QVector<ColorSchemeIndex::Entry> schemes = ColorSchemeIndex::scan();
    QVERIFY2(!schemes.isEmpty(), "no KDE color schemes installed to discover");

    const QPalette original = qApp->palette();
    for (const ColorSchemeIndex::Entry &scheme : schemes) {
        ThemeManager::apply(scheme.id);
        // Whatever the scheme says wins - no built-in palette gets in the way,
        // and the text roles come out of the readability layer.
        const QPalette expected = TextAppearance::applyOverrides(
            KColorScheme::createApplicationPalette(KSharedConfig::openConfig(scheme.path)), {});
        QCOMPARE(qApp->palette().color(QPalette::Window), expected.color(QPalette::Window));
        QCOMPARE(qApp->palette().color(QPalette::Base), expected.color(QPalette::Base));
        QCOMPARE(qApp->palette().color(QPalette::Text), expected.color(QPalette::Text));
        QCOMPARE(qApp->palette().color(QPalette::Highlight), expected.color(QPalette::Highlight));
    }
    qApp->setPalette(original);
}

void TestUiLogic::legacyPresetsResolveToBreezeSchemes()
{
    // Ids stored by older releases keep working: they map onto the Breeze
    // schemes, and stay valid even where that scheme is not installed.
    QCOMPARE(ColorSchemeIndex::resolvedId(QStringLiteral("light")), QStringLiteral("BreezeLight"));
    QCOMPARE(ColorSchemeIndex::resolvedId(QStringLiteral("dark")), QStringLiteral("BreezeDark"));
    QCOMPARE(ColorSchemeIndex::resolvedId(QStringLiteral("Dracula")), QStringLiteral("Dracula"));

    QVERIFY(ColorSchemeIndex::isValid(QStringLiteral("system")));
    QVERIFY(ColorSchemeIndex::isValid(QStringLiteral("light")));
    QVERIFY(ColorSchemeIndex::isValid(QStringLiteral("dark")));
    QVERIFY(!ColorSchemeIndex::isValid(QStringLiteral("no-such-scheme")));
    QVERIFY(!ColorSchemeIndex::isValid(QString()));

    // Ids build file paths, so path-like input must never resolve.
    QVERIFY(!ColorSchemeIndex::isValid(QStringLiteral("../../etc/passwd")));
    QVERIFY(ColorSchemeIndex::filePath(QStringLiteral("../../etc/passwd")).isEmpty());
}

void TestUiLogic::themeFilesAreNeverModified()
{
    const QVector<ColorSchemeIndex::Entry> schemes = ColorSchemeIndex::scan();
    QVERIFY2(!schemes.isEmpty(), "no KDE color schemes installed to discover");

    QFile file(schemes.first().path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray before = file.readAll();
    file.close();

    const QPalette original = qApp->palette();
    ThemeManager::apply(schemes.first().id);
    qApp->setPalette(original);

    // Loading a theme is read-only: the asset on disk is byte-identical.
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), before);
    file.close();
}

void TestUiLogic::discoversInstalledIconThemes()
{
    const QVector<IconThemeIndex::Entry> themes = IconThemeIndex::scan();
    QVERIFY2(!themes.isEmpty(), "no KDE icon themes installed to discover");

    QSet<QString> ids;
    QString previousName;
    for (const IconThemeIndex::Entry &theme : themes) {
        QVERIFY(!theme.id.isEmpty());
        QVERIFY(!theme.name.isEmpty());
        // One entry per id even when several XDG dirs provide it.
        QVERIFY(!ids.contains(theme.id));
        ids.insert(theme.id);
        // Sorted by display name.
        QVERIFY(previousName.isEmpty()
                || QString::compare(previousName, theme.name, Qt::CaseInsensitive) <= 0);
        previousName = theme.name;
        QVERIFY(IconThemeIndex::isValid(theme.id));

        // Every listed theme really is an icon theme: a directory whose
        // index.theme declares icon directories. Cursor themes carry an
        // index.theme too but no icon directories, and must not be offered.
        const QString directory = iconThemeDirectory(theme.id);
        QVERIFY2(!directory.isEmpty(), qPrintable(theme.id));
        QVERIFY(declaresIconDirectories(directory));
    }
    QVERIFY(IconThemeIndex::isValid(QStringLiteral("system")));
    QVERIFY(!IconThemeIndex::isValid(QStringLiteral("no-such-icon-theme")));
}

void TestUiLogic::appliesIconThemeFromDisk()
{
    const QVector<IconThemeIndex::Entry> themes = IconThemeIndex::scan();
    QVERIFY2(!themes.isEmpty(), "no KDE icon themes installed to discover");

    const QString previousTheme = QIcon::themeName();
    const QString previousFallback = QIcon::fallbackThemeName();

    // Qt only searches the XDG icon dirs when a platform theme hands them over,
    // so apply() has to wire them up or every fromTheme() lookup comes back empty.
    IconThemeManager::apply(QStringLiteral("system"));
    for (const QString &directory : IconThemeIndex::iconDirectories())
        QVERIFY(QIcon::themeSearchPaths().contains(directory));

    // "system" follows the desktop, and falls back to the Plasma default when
    // the desktop has no icon theme configured - never an empty theme.
    const QString systemTheme = IconThemeIndex::resolvedId(QStringLiteral("system"));
    QCOMPARE(QIcon::themeName(),
             systemTheme.isEmpty() ? IconThemeIndex::fallbackId() : systemTheme);
    if (!IconThemeIndex::fallbackId().isEmpty())
        QCOMPARE(QIcon::fallbackThemeName(), IconThemeIndex::fallbackId());

    // Every discovered id installs exactly itself, and icons keep resolving.
    for (const IconThemeIndex::Entry &theme : themes) {
        IconThemeManager::apply(theme.id);
        QCOMPARE(QIcon::themeName(), theme.id);
    }
    QVERIFY(QIcon::hasThemeIcon(QStringLiteral("edit-copy")));
    QVERIFY(!QIcon::fromTheme(QStringLiteral("edit-copy")).isNull());

    QIcon::setThemeName(previousTheme);
    QIcon::setFallbackThemeName(previousFallback);
}

void TestUiLogic::iconThemeSwitchUpdatesExistingIcons()
{
    const QVector<IconThemeIndex::Entry> themes = IconThemeIndex::scan();
    QVERIFY2(!themes.isEmpty(), "no KDE icon themes installed to discover");

    const QString probe = QStringLiteral("edit-copy");
    QString first;
    QString second;
    for (const IconThemeIndex::Entry &theme : themes) {
        if (!themeShipsIcon(theme.id, probe))
            continue;
        if (first.isEmpty()) {
            first = theme.id;
        } else {
            second = theme.id;
            break;
        }
    }
    if (second.isEmpty())
        QSKIP("fewer than two icon themes ship the probe icon");

    const QString previousTheme = QIcon::themeName();

    IconThemeManager::apply(first);
    QIcon pinned = QIcon::fromTheme(probe);
    const QImage beforeSwitch = pinned.pixmap(22, 22).toImage();

    IconThemeManager::apply(second);
    // The icon handed out before the switch re-resolves against the new theme on
    // the next paint - that is what keeps toolbar, menus and list entries in
    // sync without restarting the app or re-walking the widget tree.
    QCOMPARE(pinned.pixmap(22, 22).toImage(), QIcon::fromTheme(probe).pixmap(22, 22).toImage());
    // The theme name survives, so an explicit rebuild stays possible.
    QCOMPARE(pinned.name(), probe);
    // The two themes ship their own artwork for the same icon name.
    QVERIFY(pinned.pixmap(22, 22).toImage() != beforeSwitch);

    QIcon::setThemeName(previousTheme);
}

void TestUiLogic::computesContrastRatios()
{
    // WCAG 2.1 reference values.
    QCOMPARE(TextAppearance::contrastRatio(Qt::black, Qt::white), 21.0);
    QCOMPARE(TextAppearance::contrastRatio(Qt::white, Qt::black), 21.0);
    QCOMPARE(TextAppearance::contrastRatio(Qt::black, Qt::black), 1.0);
    QVERIFY(TextAppearance::contrastRatio(QColor(0x77, 0x77, 0x77), Qt::white) > 4.4);
    QVERIFY(TextAppearance::contrastRatio(QColor(0x77, 0x77, 0x77), Qt::white) < 4.6);
    // Invalid colors cannot be measured, so they must not look "readable".
    QCOMPARE(TextAppearance::contrastRatio(QColor(), Qt::white), 1.0);
}

void TestUiLogic::raisesUnreadableTextToTheContrastFloor()
{
    // Dark gray on a dark surface: the classic "cannot read this" case.
    const QColor background(0x2a, 0x2e, 0x32);
    const QColor unreadable(0x33, 0x36, 0x3a);
    QVERIFY(TextAppearance::contrastRatio(unreadable, background)
            < TextAppearance::kTextContrastRatio);

    const QColor fixed =
        TextAppearance::ensureContrast(unreadable, background, TextAppearance::kTextContrastRatio);
    QVERIFY(TextAppearance::contrastRatio(fixed, background) >= TextAppearance::kTextContrastRatio);
    // Pushed toward the lighter end of the scale (stays light-on-dark), not inverted.
    QVERIFY(fixed.lightness() > background.lightness());

    // A color that already reads well is returned untouched.
    const QColor readable(0xcf, 0xd7, 0xe1);
    QVERIFY(TextAppearance::contrastRatio(readable, background)
            >= TextAppearance::kTextContrastRatio);
    QCOMPARE(TextAppearance::ensureContrast(readable, background,
                                            TextAppearance::kTextContrastRatio),
             readable);

    // Palettes get the same treatment: a near-invisible one becomes usable.
    QPalette hostile;
    hostile.setColor(QPalette::Window, background);
    hostile.setColor(QPalette::Base, QColor(0x25, 0x28, 0x2c));
    hostile.setColor(QPalette::AlternateBase, QColor(0x28, 0x2b, 0x2f));
    hostile.setColor(QPalette::Button, background);
    hostile.setColor(QPalette::WindowText, unreadable);
    hostile.setColor(QPalette::Text, unreadable);
    hostile.setColor(QPalette::ButtonText, unreadable);
    hostile.setColor(QPalette::Mid, unreadable);
    hostile.setColor(QPalette::PlaceholderText, unreadable);

    const QPalette fixedPalette = TextAppearance::applyOverrides(hostile, {});
    QVERIFY(TextAppearance::contrastRatio(fixedPalette.color(QPalette::Text),
                                          fixedPalette.color(QPalette::Base))
            >= TextAppearance::kTextContrastRatio);
    QVERIFY(TextAppearance::contrastRatio(fixedPalette.color(QPalette::WindowText),
                                          fixedPalette.color(QPalette::Window))
            >= TextAppearance::kTextContrastRatio);
    QVERIFY(TextAppearance::contrastRatio(fixedPalette.color(QPalette::Mid),
                                          fixedPalette.color(QPalette::Window))
            >= TextAppearance::kDimTextContrastRatio);
    QVERIFY(TextAppearance::contrastRatio(fixedPalette.color(QPalette::PlaceholderText),
                                          fixedPalette.color(QPalette::Base))
            >= TextAppearance::kDimTextContrastRatio);

    // Custom colors are honored, and still raised to the floor.
    TextAppearance::Overrides overrides;
    overrides.customText = true;
    overrides.textColor = QColor(0x10, 0x12, 0x14);
    overrides.customDimText = true;
    overrides.dimTextColor = QColor(0x60, 0x64, 0x6a);
    const QPalette custom = TextAppearance::applyOverrides(hostile, overrides);
    QVERIFY(TextAppearance::contrastRatio(custom.color(QPalette::Text), custom.color(QPalette::Base))
            >= TextAppearance::kTextContrastRatio);
    QVERIFY(TextAppearance::contrastRatio(custom.color(QPalette::Mid), custom.color(QPalette::Window))
            >= TextAppearance::kDimTextContrastRatio);
    QVERIFY(TextAppearance::contrastRatio(custom.color(QPalette::Mid), custom.color(QPalette::Base))
            >= TextAppearance::kDimTextContrastRatio);
}

void TestUiLogic::keepsEveryInstalledSchemeReadable()
{
    const QVector<ColorSchemeIndex::Entry> schemes = ColorSchemeIndex::scan();
    QVERIFY2(!schemes.isEmpty(), "no KDE color schemes installed to discover");

    for (const ColorSchemeIndex::Entry &scheme : schemes) {
        const QPalette schemePalette =
            KColorScheme::createApplicationPalette(KSharedConfig::openConfig(scheme.path));
        const QPalette fixed = TextAppearance::applyOverrides(schemePalette, {});

        const QColor window = fixed.color(QPalette::Window);
        const QColor base = fixed.color(QPalette::Base);
        const QColor alternateBase = fixed.color(QPalette::AlternateBase);

        // Body text and the subdued small print must read on every surface the
        // app paints them on, whatever the scheme says.
        QVERIFY2(TextAppearance::contrastRatio(fixed.color(QPalette::Text), base)
                     >= TextAppearance::kTextContrastRatio, qPrintable(scheme.id));
        QVERIFY2(TextAppearance::contrastRatio(fixed.color(QPalette::Text), alternateBase)
                     >= TextAppearance::kTextContrastRatio, qPrintable(scheme.id));
        QVERIFY2(TextAppearance::contrastRatio(fixed.color(QPalette::WindowText), window)
                     >= TextAppearance::kTextContrastRatio, qPrintable(scheme.id));
        QVERIFY2(TextAppearance::contrastRatio(fixed.color(QPalette::Mid), window)
                     >= TextAppearance::kDimTextContrastRatio, qPrintable(scheme.id));
        QVERIFY2(TextAppearance::contrastRatio(fixed.color(QPalette::Mid), base)
                     >= TextAppearance::kDimTextContrastRatio, qPrintable(scheme.id));
        QVERIFY2(TextAppearance::contrastRatio(fixed.color(QPalette::PlaceholderText), base)
                     >= TextAppearance::kDimTextContrastRatio, qPrintable(scheme.id));

        // Colors that were already readable stay exactly as the theme defined them.
        const auto untouched = [&](QPalette::ColorRole role, const QColor &surface) {
            const QColor original = schemePalette.color(role);
            if (TextAppearance::contrastRatio(original, surface)
                < TextAppearance::kTextContrastRatio) {
                return;
            }
            QCOMPARE(fixed.color(role), original);
        };
        untouched(QPalette::Text, base);
        untouched(QPalette::WindowText, window);
    }
}

void TestUiLogic::appliesTextOverridesAndFontSize()
{
    const QFont originalFont = QApplication::font();
    const int originalSize = originalFont.pointSize();

    TextAppearance::Overrides overrides;
    overrides.fontPointDelta = 3;
    overrides.customText = true;
    overrides.textColor = QColor(0x11, 0x22, 0x33);
    overrides.customDimText = true;
    overrides.dimTextColor = QColor(0x44, 0x55, 0x66);

    ThemeManager::apply(QStringLiteral("dark"), overrides);
    // The installed text color reads on the surface it is drawn on, whether it
    // matched the requested color or had to be raised to the floor.
    const QColor applied = qApp->palette().color(QPalette::Text);
    QVERIFY(TextAppearance::contrastRatio(applied, qApp->palette().color(QPalette::Base))
            >= TextAppearance::kTextContrastRatio);
    if (applied != QColor(0x11, 0x22, 0x33)) {
        // Raised, so the requested color must have been unreadable to begin with.
        QVERIFY(TextAppearance::contrastRatio(QColor(0x11, 0x22, 0x33),
                                              qApp->palette().color(QPalette::Base))
                < TextAppearance::kTextContrastRatio);
    }
    if (originalSize > 0)
        QCOMPARE(QApplication::font().pointSize(), originalSize + 3);

    // A custom color that already reads well is installed exactly as picked.
    overrides.textColor = QColor(0xff, 0xff, 0xff);
    ThemeManager::apply(QStringLiteral("dark"), overrides);
    if (TextAppearance::contrastRatio(overrides.textColor, qApp->palette().color(QPalette::Base))
        >= TextAppearance::kTextContrastRatio) {
        QCOMPARE(qApp->palette().color(QPalette::Text), overrides.textColor);
    }

    // Back to the theme's own colors and size.
    ThemeManager::apply(QStringLiteral("dark"), {});
    QCOMPARE(QApplication::font(), originalFont);
    QCOMPARE(qApp->palette().color(QPalette::Text),
             TextAppearance::applyOverrides(
                 KColorScheme::createApplicationPalette(KSharedConfig::openConfig(
                     ColorSchemeIndex::filePath(QStringLiteral("dark")))),
                 {})
                 .color(QPalette::Text));
}

QTEST_MAIN(TestUiLogic)
#include "tst_ui.moc"
