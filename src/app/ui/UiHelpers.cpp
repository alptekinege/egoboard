#include "UiHelpers.h"

#include "DesignTokens.h"
#include "../TextAppearance.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAccessible>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QGroupBox>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QPalette>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>
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

QString UiHelpers::ocrMetaSuffix(bool hasBlob, const QString &ocrText, bool tesseractAvailable)
{
    if (!ocrText.isEmpty())
        return QStringLiteral("<br/>🔍 OCR: ")
            + ocrText.left(500).toHtmlEscaped().replace(QStringLiteral("\n"),
                                                        QStringLiteral("<br/>"));
    if (!hasBlob)
        return {};
    if (!tesseractAvailable)
        return QCoreApplication::translate(
            "UiHelpers", "<br/><i>OCR unavailable — install <code>tesseract</code> to make "
                         "images searchable</i>");
    return QCoreApplication::translate("UiHelpers",
                                       "<br/><i>OCR: processing… or no text found</i>");
}

namespace {
// Rich-text labels carry tags; matching runs on the visible words.
QString strippedVisibleText(const QString &text)
{
    static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
    QString out = text;
    out.remove(tags);
    return out;
}
} // namespace

QStringList UiHelpers::collectSettingTexts(const QWidget *page)
{
    QStringList out;
    if (!page)
        return out;
    const QList<QWidget *> widgets = page->findChildren<QWidget *>();
    out.reserve(widgets.size() * 2);
    for (QWidget *widget : widgets) {
        if (auto *label = qobject_cast<QLabel *>(widget)) {
            const QString text = strippedVisibleText(label->text());
            if (!text.isEmpty())
                out.append(text);
        } else if (auto *button = qobject_cast<QAbstractButton *>(widget)) {
            if (!button->text().isEmpty())
                out.append(strippedVisibleText(button->text()));
        } else if (auto *box = qobject_cast<QGroupBox *>(widget)) {
            if (!box->title().isEmpty())
                out.append(strippedVisibleText(box->title()));
        } else if (auto *edit = qobject_cast<QLineEdit *>(widget)) {
            if (!edit->placeholderText().isEmpty())
                out.append(edit->placeholderText());
        }
        if (!widget->toolTip().isEmpty())
            out.append(widget->toolTip());
        if (!widget->accessibleName().isEmpty())
            out.append(widget->accessibleName());
    }
    return out;
}

bool UiHelpers::settingQueryMatches(const QStringList &texts, const QString &query)
{
    const QStringList tokens =
        query.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (tokens.isEmpty())
        return true;
    const QString joined = texts.join(QLatin1Char('\n'));
    for (const QString &token : tokens) {
        if (!joined.contains(token, Qt::CaseInsensitive))
            return false;
    }
    return true;
}

int UiHelpers::firstSettingMatchRow(const QList<QStringList> &pages, const QString &query)
{
    if (query.trimmed().isEmpty())
        return -1;
    for (int i = 0; i < pages.size(); ++i) {
        if (settingQueryMatches(pages.at(i), query))
            return i;
    }
    return -1;
}

void UiHelpers::styleSearchField(QLineEdit *field)
{
    if (!field)
        return;
    const QFontMetrics metrics(field->font());
    field->setMinimumHeight(
        qMax(DesignTokens::TouchTargetCompact,
             qMax(DesignTokens::IconL + 2 * DesignTokens::SpaceXs,
                  metrics.height() + 2 * DesignTokens::SpaceXs)));
    field->setTextMargins(DesignTokens::SpaceS, 0, DesignTokens::SpaceS, 0);
}

QFrame *UiHelpers::makeCard(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setStyleSheet(QStringLiteral("QFrame { background: palette(window); "
                                       "border: 1px solid palette(mid); border-radius: %1px; }")
                            .arg(DesignTokens::RadiusL));
    card->setGraphicsEffect(cardShadow(card, false));
    return card;
}

QFrame *UiHelpers::makePopupPanel(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setStyleSheet(QStringLiteral("QFrame { background: palette(window); "
                                       "border: 1px solid palette(mid); border-radius: %1px; }")
                            .arg(DesignTokens::RadiusL));
    card->setGraphicsEffect(cardShadow(card, true));
    return card;
}

QGraphicsDropShadowEffect *UiHelpers::cardShadow(QObject *parent, bool elevated)
{
    auto *shadow = new QGraphicsDropShadowEffect(parent);
    shadow->setBlurRadius(elevated ? DesignTokens::ElevationPopupBlur
                                   : DesignTokens::ElevationCardBlur);
    shadow->setOffset(0, elevated ? DesignTokens::ElevationPopupOffsetY
                                  : DesignTokens::ElevationCardOffsetY);
    shadow->setColor(QColor(0, 0, 0, DesignTokens::ShadowAlpha));
    return shadow;
}

namespace {
QString itemListStyle()
{
    return QStringLiteral("QListWidget, QListView { border: none; background: transparent; }"
                          "QListWidget::item, QListView::item { padding: %1px 2px; border-radius: %2px; "
                          "min-height: %3px; }"
                          "QListWidget::item:selected, QListView::item:selected { "
                          "background: palette(highlight); color: palette(highlighted-text); }")
        .arg(DesignTokens::SpaceXs)
        .arg(DesignTokens::RadiusL)
        .arg(DesignTokens::TouchTargetCompact - 2 * DesignTokens::SpaceXs);
}
} // namespace

void UiHelpers::styleItemList(QListWidget *list)
{
    if (list)
        list->setStyleSheet(itemListStyle());
}

void UiHelpers::styleItemList(QListView *list)
{
    if (list)
        list->setStyleSheet(itemListStyle());
}

QWidget *UiHelpers::makeChip(const QString &label, const QString &accessibleName, QWidget *parent,
                             const std::function<void()> &onClose)
{
    auto *chip = new QWidget(parent);
    chip->setAccessibleName(accessibleName.isEmpty() ? label : accessibleName);
    // Not a full accessible button container: the close button carries the
    // action, the label carries the reading.
    auto *layout = new QHBoxLayout(chip);
    layout->setContentsMargins(DesignTokens::ChipPaddingH, DesignTokens::ChipPaddingV,
                               DesignTokens::SpaceXs, DesignTokens::ChipPaddingV);
    layout->setSpacing(DesignTokens::SpaceXs);

    auto *text = new QLabel(label, chip);
    text->setTextFormat(Qt::PlainText);
    text->setAccessibleName(chip->accessibleName());
    layout->addWidget(text);

    auto *close = new QPushButton(QStringLiteral("\u00d7"), chip);
    close->setFlat(true);
    close->setFixedSize(DesignTokens::ChipCloseSize, DesignTokens::ChipCloseSize);
    close->setAccessibleName(QObject::tr("Remove filter %1").arg(label));
    close->setToolTip(close->accessibleName());
    close->setCursor(Qt::PointingHandCursor);
    QObject::connect(close, &QPushButton::clicked, chip, [onClose] {
        if (onClose)
            onClose();
    });
    layout->addWidget(close);

    // Pill outline, palette-relative so every scheme keeps the border visible.
    chip->setStyleSheet(QStringLiteral("QWidget { background: palette(button); "
                                       "border: 1px solid palette(mid); border-radius: %1px; }")
                            .arg(DesignTokens::RadiusL));
    chip->setMinimumHeight(DesignTokens::TouchTargetCompact);
    return chip;
}

QWidget *UiHelpers::makeEmptyState(const QString &iconName, const QString &title,
                                   const QString &subtitle, QWidget *parent,
                                   const QString &actionText,
                                   const std::function<void()> &onAction)
{
    auto *box = new QWidget(parent);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(DesignTokens::SpaceL, DesignTokens::SpaceL, DesignTokens::SpaceL,
                               DesignTokens::SpaceL);
    layout->setSpacing(DesignTokens::SpaceS);
    layout->setAlignment(Qt::AlignCenter);

    if (!iconName.isEmpty()) {
        auto *icon = new QLabel(box);
        const QIcon theme = QIcon::fromTheme(iconName);
        if (!theme.isNull())
            icon->setPixmap(theme.pixmap(DesignTokens::IconL, DesignTokens::IconL));
        icon->setAlignment(Qt::AlignCenter);
        icon->setAccessibleName(title);
        layout->addWidget(icon);
    }
    auto *titleLabel = new QLabel(title, box);
    QFont titleFont = titleLabel->font();
    titleFont.setWeight(QFont::DemiBold);
    titleLabel->setFont(titleFont);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setTextFormat(Qt::PlainText);
    layout->addWidget(titleLabel);

    if (!subtitle.isEmpty()) {
        QLabel *hint = makeHint(subtitle, box, /*richText=*/false);
        hint->setAlignment(Qt::AlignCenter);
        layout->addWidget(hint);
    }
    if (!actionText.isEmpty()) {
        auto *button = new QPushButton(actionText, box);
        button->setMinimumHeight(DesignTokens::TouchTargetCompact);
        QObject::connect(button, &QPushButton::clicked, box, [onAction] {
            if (onAction)
                onAction();
        });
        layout->addWidget(button, 0, Qt::AlignCenter);
    }
    return box;
}

QWidget *UiHelpers::makeToast(const QString &message, QWidget *parent,
                              const QString &actionText,
                              const std::function<void()> &onAction)
{
    auto *toast = new QWidget(parent, Qt::ToolTip);
    toast->setAttribute(Qt::WA_ShowWithoutActivating, true);
    toast->setAccessibleName(message);
    QAccessibleEvent alert(toast, QAccessible::Alert);
    QAccessible::updateAccessibility(&alert);

    auto *layout = new QHBoxLayout(toast);
    layout->setContentsMargins(DesignTokens::SpaceM, DesignTokens::SpaceS, DesignTokens::SpaceM,
                               DesignTokens::SpaceS);
    layout->setSpacing(DesignTokens::SpaceM);

    auto *text = new QLabel(message, toast);
    text->setTextFormat(Qt::PlainText);
    text->setWordWrap(true);
    layout->addWidget(text, 1);

    if (!actionText.isEmpty()) {
        auto *button = new QPushButton(actionText, toast);
        button->setFlat(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(DesignTokens::TouchTargetCompact);
        QObject::connect(button, &QPushButton::clicked, toast, [toast, onAction] {
            if (onAction)
                onAction();
            toast->deleteLater();
        });
        layout->addWidget(button);
    }

    toast->setStyleSheet(QStringLiteral("QWidget { background: palette(window); "
                                        "border: 1px solid palette(mid); border-radius: %1px; }")
                             .arg(DesignTokens::RadiusL));
    toast->setGraphicsEffect(cardShadow(toast, true));
    toast->setMaximumWidth(DesignTokens::ToastMaxWidth);

    // Auto-dismiss; hovering keeps it alive is a later R4 concern.
    QTimer::singleShot(DesignTokens::ToastDurationMs, toast, &QObject::deleteLater);
    return toast;
}

void UiHelpers::ensureTouchTarget(QWidget *widget, const QString &density)
{
    if (!widget)
        return;
    const int floor = DesignTokens::rowMinHeightForDensity(density);
    if (widget->minimumHeight() < floor)
        widget->setMinimumHeight(floor);
}

QWidget *UiHelpers::makeSkeleton(QWidget *parent)
{
    auto *widget = new QWidget(parent);
    const QColor color = DesignTokens::skeletonBase(QApplication::palette());
    widget->setStyleSheet(
        QStringLiteral("QWidget { background: rgba(%1, %2, %3, %4); border-radius: %5px; }")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue())
            .arg(QString::number(color.alphaF(), 'f', 2))
            .arg(DesignTokens::RadiusS));
    if (!reduceMotion()) {
        auto *effect = new QGraphicsOpacityEffect(widget);
        widget->setGraphicsEffect(effect);
        auto *animation = new QPropertyAnimation(effect, "opacity", widget);
        animation->setDuration(1200);
        animation->setStartValue(0.4);
        animation->setEndValue(1.0);
        animation->setEasingCurve(QEasingCurve::InOutSine);
        animation->setLoopCount(-1);
        animation->start();
    }
    return widget;
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
    animate(window, MotionKind::Fade);
}

void UiHelpers::animate(QWidget *widget, MotionKind kind)
{
    if (!widget)
        return;
    if (g_reduceMotion) {
        // Skip motion entirely: leave the final state, never a mid-fade.
        // Preserve drop shadows (toast/popup cards) — only clear our own
        // opacity effect back to opaque.
        widget->setWindowOpacity(1.0);
        if (auto *opacity = qobject_cast<QGraphicsOpacityEffect *>(widget->graphicsEffect()))
            opacity->setOpacity(1.0);
        return;
    }
    int duration = DesignTokens::MotionDurationMs;
    if (kind == MotionKind::SlideSide)
        duration = DesignTokens::MotionDrawerMs;
    else if (kind == MotionKind::Chip)
        duration = DesignTokens::MotionChipMs;
    else if (kind == MotionKind::SlideUp)
        duration = DesignTokens::MotionToastMs;
    const bool isWindow = widget->isWindow();

    if (kind == MotionKind::Fade) {
        if (isWindow) {
            // Top-level popups (quick-paste, palette): window opacity keeps the
            // card shadow intact while the fade runs.
            widget->setWindowOpacity(0.0);
            auto *animation = new QPropertyAnimation(widget, "windowOpacity", widget);
            animation->setDuration(duration);
            animation->setStartValue(0.0);
            animation->setEndValue(1.0);
            animation->setEasingCurve(QEasingCurve::OutCubic);
            animation->start(QAbstractAnimation::DeleteWhenStopped);
        } else {
            // Layout-managed children: windowOpacity is a no-op for them, so
            // fade the graphics opacity instead. Never replace a shadow.
            if (widget->graphicsEffect() != nullptr
                && qobject_cast<QGraphicsOpacityEffect *>(widget->graphicsEffect()) == nullptr)
                return;
            auto *effect = qobject_cast<QGraphicsOpacityEffect *>(widget->graphicsEffect());
            if (!effect) {
                effect = new QGraphicsOpacityEffect(widget);
                widget->setGraphicsEffect(effect);
            }
            effect->setOpacity(0.0);
            auto *animation = new QPropertyAnimation(effect, "opacity", widget);
            animation->setDuration(duration);
            animation->setStartValue(0.0);
            animation->setEndValue(1.0);
            animation->setEasingCurve(QEasingCurve::OutCubic);
            animation->start(QAbstractAnimation::DeleteWhenStopped);
        }
        return;
    }

    if (kind == MotionKind::Chip) {
        // Filter chips (U6): 80 ms fade + layout-safe scale. The opacity effect
        // survives the layout pass, so chips created before layout still fade
        // in; the geometry pulse only runs when the chip already has a valid
        // rect and ends back on it, so the row is stable afterwards.
        QGraphicsOpacityEffect *effect =
            qobject_cast<QGraphicsOpacityEffect *>(widget->graphicsEffect());
        if (!effect && widget->graphicsEffect() == nullptr) {
            effect = new QGraphicsOpacityEffect(widget);
            widget->setGraphicsEffect(effect);
        }
        auto *group = new QParallelAnimationGroup(widget);
        if (effect) {
            effect->setOpacity(0.0);
            auto *fade = new QPropertyAnimation(effect, "opacity", group);
            fade->setDuration(duration);
            fade->setStartValue(0.0);
            fade->setEndValue(1.0);
            fade->setEasingCurve(QEasingCurve::OutCubic);
            group->addAnimation(fade);
        }
        const QRect endGeom = widget->geometry();
        if (widget->isVisible() && endGeom.width() > 0 && endGeom.height() > 0) {
            const int dx = qMax(1, endGeom.width() / 20);
            const int dy = qMax(1, endGeom.height() / 20);
            const QRect startGeom = endGeom.adjusted(dx, dy, -dx, -dy);
            widget->setGeometry(startGeom);
            auto *scale = new QPropertyAnimation(widget, "geometry", group);
            scale->setDuration(duration);
            scale->setStartValue(startGeom);
            scale->setEndValue(endGeom);
            scale->setEasingCurve(QEasingCurve::OutCubic);
            group->addAnimation(scale);
        }
        if (group->animationCount() > 0)
            group->start(QAbstractAnimation::DeleteWhenStopped);
        else
            group->deleteLater();
        return;
    }

    // SlideUp (toast, 120 ms) and SlideSide (drawer, 80 ms): opacity plus a
    // short positional slide that ends back on the layout position, so the
    // geometry is stable afterwards. Offsets reuse the spacing tokens.
    const QPoint endPos = widget->pos();
    const QPoint offset = kind == MotionKind::SlideUp
        ? QPoint(0, 2 * DesignTokens::SpaceM)
        : QPoint(2 * DesignTokens::SpaceL, 0);
    const QPoint startPos = endPos + offset;

    auto *group = new QParallelAnimationGroup(widget);
    if (isWindow) {
        widget->setWindowOpacity(0.0);
        auto *fade = new QPropertyAnimation(widget, "windowOpacity", group);
        fade->setDuration(duration);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        fade->setEasingCurve(QEasingCurve::OutCubic);
        group->addAnimation(fade);
    } else {
        // Docked drawer: fade via an opacity effect when there is no shadow
        // to preserve; the slide below runs regardless.
        auto *effect = qobject_cast<QGraphicsOpacityEffect *>(widget->graphicsEffect());
        if (!effect && widget->graphicsEffect() == nullptr) {
            effect = new QGraphicsOpacityEffect(widget);
            widget->setGraphicsEffect(effect);
        }
        if (effect) {
            effect->setOpacity(0.0);
            auto *fade = new QPropertyAnimation(effect, "opacity", group);
            fade->setDuration(duration);
            fade->setStartValue(0.0);
            fade->setEndValue(1.0);
            fade->setEasingCurve(QEasingCurve::OutCubic);
            group->addAnimation(fade);
        }
    }
    widget->move(startPos);
    auto *slide = new QPropertyAnimation(widget, "pos", group);
    slide->setDuration(duration);
    slide->setStartValue(startPos);
    slide->setEndValue(endPos);
    slide->setEasingCurve(QEasingCurve::OutCubic);
    group->addAnimation(slide);
    group->start(QAbstractAnimation::DeleteWhenStopped);
}
