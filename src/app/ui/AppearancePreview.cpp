#include "AppearancePreview.h"

#include <QEvent>
#include <QIcon>
#include <QPainter>

namespace {
constexpr int kPadding = 10;
constexpr int kIconSize = 18;
constexpr int kRowSpacing = 4;
constexpr int kSmallLineHeight = 18;

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
} // namespace

AppearancePreview::AppearancePreview(QWidget *parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::NoFrame);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize AppearancePreview::sizeHint() const
{
    const int rowHeight = qMax(kIconSize + 8, fontMetrics().height() + 10);
    return QSize(420, kPadding * 2 + rowHeight * 2 + kRowSpacing * 2 + kSmallLineHeight * 2);
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
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);

    const int rowHeight = qMax(kIconSize + 8, fontMetrics().height() + 10);
    const int metaWidth = qMax(70, fontMetrics().horizontalAdvance(QStringLiteral("2 min ago")) + 10);

    const auto drawRow = [&](int top, bool selected, const QString &title, const QString &meta,
                             const QString &iconName) {
        if (selected) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(pal.color(QPalette::Highlight));
            painter.drawRect(QRect(1, top, width() - 2, rowHeight));
        }
        const QColor titleColor = pal.color(selected ? QPalette::HighlightedText : QPalette::Text);
        const QColor metaColor = pal.color(selected ? QPalette::HighlightedText : QPalette::Mid);

        int left = kPadding;
        const QIcon icon = QIcon::fromTheme(iconName);
        if (!icon.isNull())
            icon.paint(&painter, QRect(left, top + (rowHeight - kIconSize) / 2, kIconSize, kIconSize));
        left += kIconSize + 8;

        QFont titleFont = font();
        titleFont.setWeight(QFont::DemiBold);
        painter.setFont(titleFont);
        painter.setPen(titleColor);
        const QRect titleRect(left, top, width() - left - metaWidth - kPadding, rowHeight);
        painter.drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft,
                         painter.fontMetrics().elidedText(title, Qt::ElideRight, titleRect.width()));

        painter.setFont(font());
        painter.setPen(metaColor);
        painter.drawText(QRect(width() - metaWidth - kPadding, top, metaWidth, rowHeight),
                         Qt::AlignVCenter | Qt::AlignRight, meta);
    };

    int top = kPadding;
    drawRow(top, false, tr("Copied from Kate"), tr("2 min ago"), QStringLiteral("edit-copy"));
    top += rowHeight + kRowSpacing;
    drawRow(top, true, tr("Selected entry"), tr("1 h ago"), QStringLiteral("edit-paste"));
    top += rowHeight + kRowSpacing;

    // Hint/small print, the role the "palette(mid)" stylesheets resolve to.
    QFont small = font();
    if (small.pointSizeF() > 0)
        small.setPointSizeF(qMax(1.0, small.pointSizeF() - 1.0));
    painter.setFont(small);
    painter.setPen(pal.color(QPalette::Mid));

    const int iconStripWidth = sampleIcons().size() * (kIconSize + 4);
    painter.drawText(QRect(kPadding, top, width() - kPadding * 2 - iconStripWidth, kSmallLineHeight),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     painter.fontMetrics().elidedText(
                         tr("Hint text — timestamps, source apps, small print"),
                         Qt::ElideRight, width() - kPadding * 2 - iconStripWidth));
    top += kSmallLineHeight;

    // Placeholder and a strip of icons from the active icon theme.
    painter.setPen(pal.color(QPalette::PlaceholderText));
    painter.drawText(QRect(kPadding, top, width() - kPadding * 2 - iconStripWidth, kSmallLineHeight),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     painter.fontMetrics().elidedText(tr("Type to search… (placeholder)"),
                                                      Qt::ElideRight,
                                                      width() - kPadding * 2 - iconStripWidth));

    int iconLeft = width() - kPadding - kIconSize;
    for (const QString &name : sampleIcons()) {
        const QIcon icon = QIcon::fromTheme(name);
        if (!icon.isNull())
            icon.paint(&painter, QRect(iconLeft, top + (kSmallLineHeight - kIconSize) / 2, kIconSize,
                                       kIconSize));
        iconLeft -= kIconSize + 4;
    }
}
