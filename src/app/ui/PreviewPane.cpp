#include "PreviewPane.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
#include "CodePreviewHighlighter.h"

namespace {

QString humanSize(qint64 bytes)
{
    if (bytes < 1024)
        return PreviewPane::tr("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return PreviewPane::tr("%1 kB").arg(bytes / 1024.0, 'f', 1);
    return PreviewPane::tr("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

QStringList extractUrls(const QString &text)
{
    static const QRegularExpression urlRe(QStringLiteral(R"(https?://[^\s"'<>]+)"), QRegularExpression::CaseInsensitiveOption);
    QStringList out;
    auto it = urlRe.globalMatch(text);
    while (it.hasNext()) {
        auto m = it.next();
        QString u = m.captured(0);
        while (!u.isEmpty() && QStringLiteral(".,;!?)").contains(u.back())) u.chop(1);
        if (!out.contains(u)) out << u;
        if (out.size() >= 5) break;
    }
    return out;
}

QStringList extractHexColors(const QString &text)
{
    static const QRegularExpression colRe(QStringLiteral(R"(#(?:[0-9a-fA-F]{8}|[0-9a-fA-F]{6}|[0-9a-fA-F]{3})\b)"));
    QStringList out;
    auto it = colRe.globalMatch(text);
    while (it.hasNext()) {
        auto m = it.next();
        QString c = m.captured(0);
        if (!out.contains(c, Qt::CaseInsensitive)) out << c;
        if (out.size() >= 5) break;
    }
    return out;
}

} // namespace

PreviewPane::PreviewPane(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_stack = new QStackedWidget(this);

    auto *emptyPage = new QWidget(this);
    auto *emptyLayout = new QVBoxLayout(emptyPage);
    m_emptyLabel = new QLabel(tr("Select an entry to preview"), emptyPage);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    emptyLayout->addWidget(m_emptyLabel);
    m_stack->addWidget(emptyPage);

    m_stack->addWidget(pageText());
    m_stack->addWidget(pageHtml());
    m_stack->addWidget(pageImage());
    m_stack->addWidget(pageFiles());
    layout->addWidget(m_stack, 1);

    m_metaLabel = new QLabel(this);
    m_metaLabel->setWordWrap(true);
    m_metaLabel->setTextFormat(Qt::RichText);
    m_metaLabel->setOpenExternalLinks(true);
    m_metaLabel->setContentsMargins(8, 4, 8, 4);
    layout->addWidget(m_metaLabel);

    setMinimumWidth(260);
}

QWidget *PreviewPane::pageText()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    m_textEdit = new QPlainTextEdit(page);
    m_textEdit->setReadOnly(true);
    m_textEdit->setFrameShape(QFrame::NoFrame);
    m_textEdit->setWordWrapMode(QTextOption::WrapAnywhere);
    m_highlighter = new CodePreviewHighlighter(m_textEdit->document());
    layout->addWidget(m_textEdit);
    return page;
}

QWidget *PreviewPane::pageHtml()
{
    m_htmlView = new QTextBrowser(this);
    m_htmlView->setOpenExternalLinks(true);
    return m_htmlView;
}

QWidget *PreviewPane::pageImage()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setAlignment(Qt::AlignCenter);
    m_imageLabel = new QLabel(scroll);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    scroll->setWidget(m_imageLabel);
    return scroll;
}

QWidget *PreviewPane::pageFiles()
{
    m_filesList = new QListWidget(this);
    m_filesList->setSelectionMode(QAbstractItemView::NoSelection);
    m_filesList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    return m_filesList;
}

void PreviewPane::setMeta(const QString &text)
{
    m_metaLabel->setText(text);
    m_metaLabel->setVisible(!text.isEmpty());
}

void PreviewPane::showEmpty(const QString &message)
{
    m_emptyLabel->setText(message.isEmpty() ? tr("Select an entry to preview") : message);
    m_stack->setCurrentWidget(m_emptyLabel->parentWidget());
    setMeta({});
}

void PreviewPane::showRecord(const ClipboardRecord &record)
{
    const QDateTime timestamp = QDateTime::fromMSecsSinceEpoch(record.timestamp);
    QStringList meta;
    meta << QLocale::system().toString(timestamp, QLocale::ShortFormat);
    if (!record.sourceApp.isEmpty())
        meta << record.sourceApp;
    if (!record.sourceWindow.isEmpty())
        meta << tr("from “%1”").arg(record.sourceWindow);
    meta << humanSize(record.sizeBytes);
    if (record.useCount > 0)
        meta << tr("pasted %1×").arg(record.useCount + 1);
    QString extraMeta;

    switch (record.type) {
    case ContentType::Text: {
        QString display = record.textData;
        const auto mode = CodePreviewHighlighter::detect(display);
        if (mode == CodePreviewHighlighter::Mode::Json) {
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(display.toUtf8(), &err);
            if (err.error == QJsonParseError::NoError && !doc.isNull()) {
                display = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
            }
        }
        m_highlighter->setMode(mode);
        m_textEdit->setPlainText(display);
        m_stack->setCurrentWidget(m_textEdit->parentWidget());
        {
            const auto urls = extractUrls(display);
            const auto cols = extractHexColors(display);
            if (!urls.isEmpty()) {
                QStringList linkHtml;
                for (const QString &u : urls) linkHtml << QStringLiteral("<a href=\"%1\">%1</a>").arg(u.toHtmlEscaped());
                extraMeta += QStringLiteral("<br/>🔗 ") + linkHtml.join(QStringLiteral(" · "));
            }
            if (!cols.isEmpty()) {
                QStringList swatches;
                for (const QString &c : cols) {
                    swatches << QStringLiteral("<span style=\"background:%1; border:1px solid palette(mid); padding:0 8px; margin-right:4px; border-radius:3px;\">%1</span>").arg(c);
                }
                extraMeta += QStringLiteral("<br/>🎨 ") + swatches.join(QStringLiteral(" "));
            }
        }
        break;
    }
    case ContentType::RichText:
        m_htmlView->setHtml(record.textData);
        m_stack->setCurrentWidget(m_htmlView);
        break;
    case ContentType::Image: {
        QImage image;
        if (record.hasBlob)
            image.loadFromData(record.blobData, "PNG");
        if (image.isNull()) {
            showEmpty(tr("Image data not stored\n(it exceeded the configured size limit)"));
            return;
        }
        const int maxWidth = qMax(200, width() - 32);
        m_imageLabel->setPixmap(QPixmap::fromImage(
            image.scaled(maxWidth, 4096, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        if (!record.ocrText.isEmpty()) {
            extraMeta += QStringLiteral("<br/>🔍 OCR: ") + record.ocrText.left(500).toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br/>"));
        } else if (record.hasBlob) {
            extraMeta += QStringLiteral("<br/><i>OCR: processing… or no text found</i>");
        }
        m_stack->setCurrentIndex(3);
        break;
    }
    case ContentType::Files: {
        m_filesList->clear();
        const QJsonArray array = QJsonDocument::fromJson(record.textData.toUtf8()).array();
        for (const auto &value : array) {
            const QString path = value.toString();
            const QFileInfo info(path);
            auto *item = new QListWidgetItem(
                QIcon::fromTheme(info.isDir() ? QStringLiteral("folder")
                                              : QStringLiteral("text-x-generic")),
                path, m_filesList);
            item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        }
        m_stack->setCurrentWidget(m_filesList);
        break;
    }
    }
    {
        QString base = meta.join(QStringLiteral(" · "));
        if (!extraMeta.isEmpty()) base += extraMeta;
        setMeta(base);
    }
}
