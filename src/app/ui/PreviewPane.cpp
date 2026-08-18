#include "PreviewPane.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

QString humanSize(qint64 bytes)
{
    if (bytes < 1024)
        return PreviewPane::tr("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return PreviewPane::tr("%1 kB").arg(bytes / 1024.0, 0, 'f', 1);
    return PreviewPane::tr("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
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
    setMeta(meta.join(QStringLiteral(" · ")));

    switch (record.type) {
    case ContentType::Text:
        m_textEdit->setPlainText(record.textData);
        m_stack->setCurrentWidget(m_textEdit->parentWidget());
        break;
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
        m_stack->setCurrentIndex(3); // image scroll page
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
}
