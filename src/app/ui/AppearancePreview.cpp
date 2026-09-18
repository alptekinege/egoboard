#include "AppearancePreview.h"

#include "DesignTokens.h"
#include "../TextAppearance.h"

#include <QEvent>
#include <QIcon>
#include <QPainter>

namespace {
// Icons shown at the bottom: whatever the active icon theme provides for them.
const QVector<QString> &sampleIcons()
{
    static const QVector<QString> icons = {
        QStringLiteral("edit-copy"),
        QStringLiteral("edit-delete"),
        QStringLiteral("bookmarks"),
        QStringLiteral("configure"),
    };
    return icons;
}

QFont hintFont(const QFont &uiFont)
{
    return TextAppearance::withFontPointDelta(uiFont, -1);
}
} // namespace

AppearancePreview::AppearancePreview(QWidget *parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::NoFrame);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize AppearancePreview::sizeHint() const
{
    // Drawn from the delegate's own row metrics (comfortable density), so the
    // sample keeps the list's proportions whatever the density scale becomes.
    const DesignTokens::RowMetrics metrics =
        DesignTokens::rowMetrics(DesignTokens::RowPaddingComfortable);
    const int rowHeight = metrics.sizeHint(QFontMetrics(font()), 0).height();
    const int hintHeight = QFontMetrics(hintFont(font())).height();
    return QSize(420, DesignTokens::SpaceM * 2 + rowHeight * 2 + DesignTokens::SpaceXs * 2
                          + hintHeight * 2);
}

void AppearancePreview::changeEvent(QEvent *event)
{
    QFrame::changeEvent(event);
    switch (event->type()) {
    case QEvent::PaletteChange:
    case QEvent::FontChange:
    case QEvent::StyleChange:
        update();
        break;
    default:
        break;
    }
}

void AppearancePreview::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QPalette pal = palette();

    // The list surface, drawn the way QListView paints it.
    painter.setPen(pal.color(QPalette::Mid));
    painter.setBrush(pal.color(QPalette::Base));
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), DesignTokens::RadiusL,
                            DesignTokens::RadiusL);

    const DesignTokens::RowMetrics metrics =
        DesignTokens::rowMetrics(DesignTokens::RowPaddingComfortable);
    const QFontMetrics rowMetrics(font());
    const int rowHeight = metrics.sizeHint(rowMetrics, 0).height();

    const auto drawRow = [&](int top, bool selected, bool hovered, const QString &title,
                             const QString &meta, const QString &iconName) {
        if (selected) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(pal.color(QPalette::Highlight));
            painter.drawRect(QRect(1, top, width() - 2, rowHeight));
        } else if (hovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(DesignTokens::hoverBackground(pal));
            painter.drawRect(QRect(1, top, width() - 2, rowHeight));
        }
        const QColor titleColor = DesignTokens::previewTextColor(pal, selected, true);
        const QColor metaColor = DesignTokens::metaTextColor(pal, selected, true);

        const int left = 1 + metrics.margin;
        const QIcon icon = QIcon::fromTheme(iconName);
        icon.paint(&painter,
                   QRect(left, top + (rowHeight - metrics.iconSize) / 2, metrics.iconSize,
                         metrics.iconSize));

        const int textLeft = metrics.contentLeft(1);
        QFont titleFont = font();
        titleFont.setWeight(QFont::DemiBold);
        painter.setFont(titleFont);
        painter.setPen(titleColor);
        const QRect titleRect(textLeft, top + metrics.padding, width() - textLeft - metrics.margin,
                              rowMetrics.height());
        painter.drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft,
                         painter.fontMetrics().elidedText(title, Qt::ElideRight, titleRect.width()));

        painter.setFont(font());
        painter.setPen(metaColor);
        painter.drawText(QRect(textLeft, top + metrics.padding + rowMetrics.height() + 2,
                               width() - textLeft - metrics.margin, rowMetrics.height()),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         painter.fontMetrics().elidedText(meta, Qt::ElideRight,
                                                          width() - textLeft - metrics.margin));
    };

    int top = DesignTokens::SpaceM;
    drawRow(top, false, true, tr("Hovered entry"), tr("3 min ago"), QStringLiteral("edit-copy"));
    top += rowHeight + DesignTokens::SpaceXs;
    drawRow(top, true, false, tr("Selected entry"), tr("1 h ago"), QStringLiteral("edit-paste"));
    top += rowHeight + DesignTokens::SpaceXs;

    // Hint/small print, the role the muted labels use.
    const QFont small = hintFont(font());
    painter.setFont(small);
    painter.setPen(pal.color(QPalette::Mid));

    const int iconStripWidth = sampleIcons().size() * (metrics.iconSize + DesignTokens::SpaceXs);
    const int hintHeight = QFontMetrics(small).height();
    painter.drawText(QRect(DesignTokens::SpaceM, top,
                           width() - DesignTokens::SpaceM * 2 - iconStripWidth, hintHeight),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     painter.fontMetrics().elidedText(
                         tr("Hint text — timestamps, source apps, small print"), Qt::ElideRight,
                         width() - DesignTokens::SpaceM * 2 - iconStripWidth));
    top += hintHeight;

    // Placeholder and a strip of icons from the active icon theme.
    painter.setPen(pal.color(QPalette::PlaceholderText));
    painter.drawText(QRect(DesignTokens::SpaceM, top,
                           width() - DesignTokens::SpaceM * 2 - iconStripWidth, hintHeight),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     painter.fontMetrics().elidedText(tr("Type to search… (placeholder)"),
                                                      Qt::ElideRight,
                                                      width() - DesignTokens::SpaceM * 2
                                                          - iconStripWidth));

    int iconLeft = width() - DesignTokens::SpaceM - metrics.iconSize;
    for (const QString &name : sampleIcons()) {
        const QIcon icon = QIcon::fromTheme(name);
        if (!icon.isNull())
            icon.paint(&painter, QRect(iconLeft, top + (hintHeight - metrics.iconSize) / 2,
                                       metrics.iconSize, metrics.iconSize));
        iconLeft -= metrics.iconSize + DesignTokens::SpaceXs;
    }
}
