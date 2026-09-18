#include "PreviewPane.h"

#include "CodePreviewHighlighter.h"
#include "DesignTokens.h"
#include "UiHelpers.h"
#include "../ScriptActionManager.h"
#include "../SettingsManager.h"
#include "TransformChainDialog.h"
#include "TransformEngine.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTextEdit>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace {

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

    buildTransformBar();
    layout->addWidget(m_transformBar);

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
    m_imagePage = pageImage();
    m_stack->addWidget(m_imagePage);
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

void PreviewPane::buildTransformBar()
{
    m_transformBar = new QWidget(this);
    auto *bar = new QHBoxLayout(m_transformBar);
    bar->setContentsMargins(6, 4, 6, 4);
    bar->setSpacing(6);

    m_transformBtn = new QToolButton(m_transformBar);
    m_transformBtn->setText(tr("Transform ▾"));
    m_transformBtn->setPopupMode(QToolButton::InstantPopup);
    m_transformBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_transformBtn->setToolTip(tr("Apply local transforms (built-in + JS scripts in ~/.local/share/egoboard/actions/) — no network"));
    bar->addWidget(m_transformBtn);

    m_copyResultBtn = new QToolButton(m_transformBar);
    m_copyResultBtn->setText(tr("Copy Result"));
    m_copyResultBtn->setToolTip(tr("Copy transformed text to clipboard"));
    m_copyResultBtn->setVisible(false);
    connect(m_copyResultBtn, &QToolButton::clicked, this, [this]{
        const QString t = m_textEdit->toPlainText();
        if (!t.isEmpty()) {
            QGuiApplication::clipboard()->setText(t);
            m_transformStatus->setText(tr("Copied ✓"));
        }
    });
    bar->addWidget(m_copyResultBtn);

    m_revertBtn = new QToolButton(m_transformBar);
    m_revertBtn->setText(tr("Revert"));
    m_revertBtn->setToolTip(tr("Show original text"));
    m_revertBtn->setVisible(false);
    connect(m_revertBtn, &QToolButton::clicked, this, [this]{
        if (!m_current.isValid()) return;
        // Restore original preview logic (re-run showRecord without transform)
        m_isTransformed = false;
        m_copyResultBtn->setVisible(false);
        m_revertBtn->setVisible(false);
        m_transformStatus->clear();
        showRecord(m_current);
    });
    bar->addWidget(m_revertBtn);

    m_transformStatus = UiHelpers::makeHint(QString(), m_transformBar, /*richText=*/false);
    bar->addWidget(m_transformStatus, 1);

    // Build initial menu
    refreshTransformMenu();
    m_transformBar->setVisible(false);
}

void PreviewPane::refreshTransformMenu()
{
    auto *menu = new QMenu(m_transformBtn);
    // Built-ins — filter hidden via SettingsManager
    const QStringList hidden = m_settings ? m_settings->hiddenTransforms() : QStringList();
    for (const auto &d : TransformEngine::allDescriptors()) {
        if (hidden.contains(d.name, Qt::CaseInsensitive)) continue;
        QAction *a = menu->addAction(d.label);
        a->setToolTip(d.description + QStringLiteral("  (") + d.name + QStringLiteral(")"));
        connect(a, &QAction::triggered, this, [this, id = d.id]{ applyBuiltin(static_cast<int>(id)); });
    }
    menu->addSeparator();
    // Scripts — filter disabled
    if (m_scripts) {
        m_scripts->reload();
        const auto acts = m_scripts->actions();
        const QStringList disabled = m_settings ? m_settings->disabledScripts() : QStringList();
        bool anyVisible = false;
        if (!acts.isEmpty()) {
            for (const auto &sa : acts) {
                if (disabled.contains(sa.id)) continue;
                anyVisible = true;
                QAction *a = menu->addAction(QStringLiteral("[JS] %1").arg(sa.label));
                a->setToolTip(sa.filePath);
                connect(a, &QAction::triggered, this, [this, id = sa.id]{ applyScript(id); });
            }
        }
        if (!anyVisible) {
            QAction *a = menu->addAction(tr("(no JS actions)"));
            a->setEnabled(false);
        }
        menu->addSeparator();
    }
    QAction *chain = menu->addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Chain… (combine multiple)"));
    chain->setToolTip(tr("Open chain dialog — combine transforms, live preview, copy result"));
    connect(chain, &QAction::triggered, this, &PreviewPane::openChainDialog);

    QAction *openFolder = menu->addAction(QIcon::fromTheme(QStringLiteral("folder")), tr("Open actions folder…"));
    connect(openFolder, &QAction::triggered, this, []{
        const QString dir = ScriptActionManager::actionsDir();
        QDir().mkpath(dir);
        // Use openUrl for folder
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });

    m_transformBtn->setMenu(menu);
}

void PreviewPane::applyBuiltin(int transformIndex)
{
    auto id = static_cast<TransformEngine::TransformId>(transformIndex);
    QString input;
    if (m_isTransformed) {
        // Chain from current transformed view or original? Use original for single-step clarity
        input = m_originalText;
    } else {
        input = m_current.textData.isEmpty() ? m_current.preview : m_current.textData;
        if (input.isEmpty() && m_textEdit) input = m_textEdit->toPlainText();
    }
    if (input.isEmpty()) {
        m_transformStatus->setText(tr("Nothing to transform"));
        return;
    }
    if (input.size() > 256 * 1024) {
        m_transformStatus->setText(tr("Text too large for transform"));
        return;
    }
    const auto r = TransformEngine::apply(id, input);
    if (!r.ok) {
        m_transformStatus->setText(tr("Error: %1").arg(r.error));
        return;
    }
    m_originalText = input;
    m_isTransformed = true;
    m_textEdit->setPlainText(r.output);
    applySearchHighlights();
    // Highlight as plain if JSON pretty produced JSON, keep highlighter mode
    // Do not change stack — stay on text page
    m_stack->setCurrentWidget(m_textEdit->parentWidget());
    m_copyResultBtn->setVisible(true);
    m_revertBtn->setVisible(true);
    m_transformStatus->setText(tr("Applied: %1").arg(TransformEngine::labelForId(id)));
    emit copyToClipboardRequested(r.output);
}

void PreviewPane::applyScript(const QString &id)
{
    if (!m_scripts) return;
    QString input;
    if (m_isTransformed) input = m_originalText;
    else input = m_current.textData.isEmpty() ? m_current.preview : m_current.textData;
    if (input.isEmpty() && m_textEdit) input = m_textEdit->toPlainText();
    if (input.isEmpty()) {
        m_transformStatus->setText(tr("Nothing to transform"));
        return;
    }
    const auto r = m_scripts->apply(id, input);
    if (!r.ok) {
        m_transformStatus->setText(tr("Script error: %1").arg(r.error));
        return;
    }
    m_originalText = input;
    m_isTransformed = true;
    m_textEdit->setPlainText(r.output);
    applySearchHighlights();
    m_stack->setCurrentWidget(m_textEdit->parentWidget());
    m_copyResultBtn->setVisible(true);
    m_revertBtn->setVisible(true);
    m_transformStatus->setText(tr("Applied script: %1").arg(id));
    emit copyToClipboardRequested(r.output);
}

void PreviewPane::openChainDialog()
{
    QString input = m_current.textData.isEmpty() ? m_current.preview : m_current.textData;
    if (input.isEmpty() && m_textEdit) input = m_textEdit->toPlainText();
    if (input.isEmpty()) {
        m_transformStatus->setText(tr("Nothing to transform"));
        return;
    }
    TransformChainDialog dlg(input, m_scripts, this);
    if (dlg.exec() == QDialog::Accepted) {
        const QString out = dlg.resultText();
        m_originalText = input;
        m_isTransformed = true;
        m_textEdit->setPlainText(out);
        applySearchHighlights();
        m_stack->setCurrentWidget(m_textEdit->parentWidget());
        m_copyResultBtn->setVisible(true);
        m_revertBtn->setVisible(true);
        m_transformStatus->setText(tr("Chain applied — %1 chars").arg(out.size()));
        if (!out.isEmpty()) {
            QGuiApplication::clipboard()->setText(out);
            m_transformStatus->setText(tr("Chain applied — copied ✓"));
        }
    }
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

void PreviewPane::setSearchTerms(const QStringList &terms)
{
    m_searchTerms = terms;
    applySearchHighlights();
}

void PreviewPane::applySearchHighlights()
{
    if (!m_textEdit)
        return;
    m_textEdit->setExtraSelections({});
    if (m_searchTerms.isEmpty())
        return;
    const QString text = m_textEdit->toPlainText();
    if (text.isEmpty())
        return;

    const QString lower = text.toLower();
    QColor fill = m_textEdit->palette().color(QPalette::Highlight);
    fill.setAlpha(80);
    QList<QTextEdit::ExtraSelection> selections;
    for (const QString &term : m_searchTerms) {
        const QString needle = term.toLower();
        if (needle.isEmpty())
            continue;
        int from = 0;
        while (selections.size() < 500) {
            const int at = lower.indexOf(needle, from);
            if (at < 0)
                break;
            QTextEdit::ExtraSelection selection;
            selection.format.setBackground(fill);
            QTextCursor cursor(m_textEdit->document());
            cursor.setPosition(at);
            cursor.setPosition(at + needle.size(), QTextCursor::KeepAnchor);
            selection.cursor = cursor;
            selections.append(selection);
            from = at + needle.size();
        }
    }
    m_textEdit->setExtraSelections(selections);
}

void PreviewPane::showEmpty(const QString &message)
{
    m_emptyLabel->setText(message.isEmpty() ? tr("Select an entry to preview") : message);
    m_stack->setCurrentWidget(m_emptyLabel->parentWidget());
    m_transformBar->setVisible(false);
    m_isTransformed = false;
    m_copyResultBtn->setVisible(false);
    m_revertBtn->setVisible(false);
    m_transformStatus->clear();
    m_current = {};
    setMeta({});
}

void PreviewPane::showRecord(const ClipboardRecord &record)
{
    // Showing another entry abandons any transformed view of the previous one.
    m_isTransformed = false;
    m_current = record;
    m_originalText = record.textData.isEmpty() ? record.preview : record.textData;
    m_copyResultBtn->setVisible(false);
    m_revertBtn->setVisible(false);
    m_transformStatus->clear();

    const QDateTime timestamp = QDateTime::fromMSecsSinceEpoch(record.timestamp);
    QStringList meta;
    meta << QLocale::system().toString(timestamp, QLocale::ShortFormat);
    if (!record.sourceApp.isEmpty())
        meta << record.sourceApp;
    if (!record.sourceWindow.isEmpty())
        meta << tr("from “%1”").arg(record.sourceWindow);
    const QString sizeText = UiHelpers::humanSize(record.sizeBytes);
    if (!sizeText.isEmpty())
        meta << sizeText;
    // use_count counts re-copies/pastes after the initial capture.
    if (record.useCount > 0)
        meta << tr("used %1×").arg(record.useCount);
    QString extraMeta;

    // Transform bar visible only for text-like entries
    const bool isTextLike = (record.type == ContentType::Text || record.type == ContentType::RichText);
    m_transformBar->setVisible(isTextLike);
    if (isTextLike) refreshTransformMenu();

    switch (record.type) {
    case ContentType::Text: {
        QString display = record.textData;
        const bool codeEnabled = !m_settings || m_settings->previewCodeHighlight();
        const auto rawMode = CodePreviewHighlighter::detect(display);
        const auto mode = codeEnabled ? rawMode : CodePreviewHighlighter::Mode::Plain;
        if (mode == CodePreviewHighlighter::Mode::Json) {
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(display.toUtf8(), &err);
            if (err.error == QJsonParseError::NoError && !doc.isNull()) {
                display = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
            }
        }
        m_highlighter->setMode(mode);
        m_textEdit->setPlainText(display);
        applySearchHighlights();
        m_stack->setCurrentWidget(m_textEdit->parentWidget());
        {
            const bool linkify = !m_settings || m_settings->previewLinkify();
            const bool swatches = !m_settings || m_settings->previewColorSwatches();
            if (linkify) {
                const auto urls = extractUrls(display);
                if (!urls.isEmpty()) {
                    QStringList linkHtml;
                    for (const QString &u : urls) linkHtml << QStringLiteral("<a href=\"%1\">%1</a>").arg(u.toHtmlEscaped());
                    extraMeta += QStringLiteral("<br/>🔗 ") + linkHtml.join(QStringLiteral(" · "));
                }
            }
            if (swatches) {
                const auto cols = extractHexColors(display);
                if (!cols.isEmpty()) {
                    QStringList swatchesList;
                    const QString borderColor = UiHelpers::mutedColor().name();
                    for (const QString &c : cols) {
                        // Rich text has no palette() roles, so the outline color
                        // is resolved here instead of silently doing nothing.
                        swatchesList << QStringLiteral("<span style=\"background:%1; border:1px solid %2; padding:0 8px; margin-right:4px; border-radius:3px;\">%1</span>").arg(c, borderColor);
                    }
                    extraMeta += QStringLiteral("<br/>🎨 ") + swatchesList.join(QStringLiteral(" "));
                }
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
        m_stack->setCurrentWidget(m_imagePage);
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
