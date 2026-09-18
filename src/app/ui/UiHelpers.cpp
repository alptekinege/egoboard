#include "UiHelpers.h"

#include "DesignTokens.h"
#include "../TextAppearance.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPropertyAnimation>
#include <QWidget>

namespace {

// Hint label: subdued color role and one point below the application font.
// The font is re-derived on every application-font change, so the "Text size"
// accessibility setting moves hints too (a stylesheet's `font-size: 11px`
// could not).
class HintLabel : public QLabel {
public:
    explicit HintLabel(QWidget *parent = nullptr)
        : QLabel(parent)
    {
        setWordWrap(true);
        setForegroundRole(QPalette::Mid);
        applyFont();
    }

protected:
    void changeEvent(QEvent *event) override
    {
        QLabel::changeEvent(event);
        if (event->type() == QEvent::ApplicationFontChange || event->type() == QEvent::FontChange)
            applyFont();
    }

private:
    void applyFont()
    {
        const QFont target = TextAppearance::withFontPointDelta(QApplication::font(), -1);
        if (font() != target)
            setFont(target);
    }
};

bool g_reduceMotion = false;

// Contrast-checked against the surface the label is drawn on.
QColor checked(const QColor &color, const QColor &surface)
{
    return TextAppearance::ensureContrast(color, surface, TextAppearance::kTextContrastRatio);
}

} // namespace

QLabel *UiHelpers::makeHint(const QString &text, QWidget *parent, bool richText)
{
    auto *label = new HintLabel(parent);
    label->setTextFormat(richText ? Qt::RichText : Qt::PlainText);
    label->setText(text);
    return label;
}

QLabel *UiHelpers::makeStatusPanel(const QString &text, QWidget *parent)
{
    QLabel *label = makeHint(text, parent);
    label->setStyleSheet(QStringLiteral("border: 1px solid palette(mid); border-radius: %1px; "
                                        "padding: %2px;")
                             .arg(DesignTokens::RadiusL)
                             .arg(DesignTokens::SpaceS));
    return label;
}

QString UiHelpers::humanSize(qint64 bytes)
{
    if (bytes <= 0)
        return {};
    if (bytes < 1024)
        return QCoreApplication::translate("UiHelpers", "%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QCoreApplication::translate("UiHelpers", "%1 kB").arg(bytes / 1024.0, 0, 'f', 1);
    return QCoreApplication::translate("UiHelpers", "%1 MB")
        .arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

void UiHelpers::styleSearchField(QLineEdit *field)
{
    if (!field)
        return;
    const QFontMetrics metrics(field->font());
    field->setMinimumHeight(
        qMax(DesignTokens::IconL + 2 * DesignTokens::SpaceXs,
             metrics.height() + 2 * DesignTokens::SpaceXs));
    field->setTextMargins(DesignTokens::SpaceS, 0, DesignTokens::SpaceS, 0);
}

QString UiHelpers::positiveStyle()
{
    const QPalette palette = QApplication::palette();
    const QColor accent =
        checked(palette.color(QPalette::Highlight), palette.color(QPalette::Window));
    return QStringLiteral("color:%1;").arg(accent.name());
}

QString UiHelpers::warningStyle()
{
    const QPalette palette = QApplication::palette();
    const QColor surface = palette.color(QPalette::Window);
    // A palette has no warning role, so the color is built for the surface it
    // is drawn on: a red lifted for dark schemes and lowered for light ones,
    // then held to the same contrast floor as every other text color.
    const bool dark = surface.lightness() < 128;
    const QColor red =
        checked(QColor::fromHsv(4, dark ? 170 : 210, dark ? 235 : 155), surface);
    return QStringLiteral("color:%1;").arg(red.name());
}

QColor UiHelpers::mutedColor()
{
    const QPalette palette = QApplication::palette();
    return checked(palette.color(QPalette::Mid), palette.color(QPalette::Window));
}

bool UiHelpers::reduceMotion()
{
    return g_reduceMotion;
}

void UiHelpers::setReduceMotion(bool reduce)
{
    g_reduceMotion = reduce;
}

void UiHelpers::fadeIn(QWidget *window)
{
    if (!window)
        return;
    if (g_reduceMotion) {
        window->setWindowOpacity(1.0);
        return;
    }
    window->setWindowOpacity(0.0);
    auto *animation = new QPropertyAnimation(window, "windowOpacity", window);
    animation->setDuration(DesignTokens::MotionDurationMs);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}
