#include "PreviewPane.h"

#include "CodePreviewHighlighter.h"
#include "DesignTokens.h"
#include "UiHelpers.h"
#include "../OcrWorker.h"
#include "../ScriptActionManager.h"
#include "../SettingsManager.h"
#include "TransformChainDialog.h"
#include "TransformEngine.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTimer>
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
    // U15/§7 focus area: Alt+3 (and Tab) lands here, then moves inside.
    setFocusPolicy(Qt::StrongFocus);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    buildHeaderBar();
    layout->addWidget(m_headerBar);

    buildTransformBar();
    layout->addWidget(m_transformBar);

    m_stack = new QStackedWidget(this);

    auto *emptyPage = new QWidget(this);
    auto *emptyLayout = new QVBoxLayout(emptyPage);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    m_emptyState = UiHelpers::makeEmptyState(
        QStringLiteral("view-preview"), tr("Select an entry to preview"), {}, emptyPage);
    m_emptyState->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    emptyLayout->addWidget(m_emptyState);
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

    // U11 skeleton overlay: covers the stack while content loads.
    m_skeletonOverlay = new QWidget(m_stack);
    m_skeletonOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    auto *skelLayout = new QVBoxLayout(m_skeletonOverlay);
    skelLayout->setContentsMargins(DesignTokens::SpaceM, DesignTokens::SpaceM,
                                   DesignTokens::SpaceM, DesignTokens::SpaceM);
    skelLayout->setSpacing(DesignTokens::SpaceS);
    for (int i = 0; i < 4; ++i) {
        auto *row = UiHelpers::makeSkeleton(m_skeletonOverlay);
        row->setFixedHeight(DesignTokens::IconL);
        skelLayout->addWidget(row);
    }
    skelLayout->addStretch(1);
    m_skeletonOverlay->hide();

    m_skeletonTimer = new QTimer(this);
    m_skeletonTimer->setSingleShot(true);
    m_skeletonTimer->setInterval(80);

    setMinimumWidth(260);
}

void PreviewPane::buildHeaderBar()
{
    // R2: header with copy/pin/source/close + text tools (wrap/edit). Close is
    // shown by the owner (drawer mode); inline here it stays hidden.
    m_headerBar = new QWidget(this);
    auto *bar = new QHBoxLayout(m_headerBar);
    bar->setContentsMargins(6, 4, 6, 4);
    bar->setSpacing(6);

    m_copyBtn = new QToolButton(m_headerBar);
    m_copyBtn->setText(tr("Copy"));
    m_copyBtn->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
    m_copyBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_copyBtn->setToolTip(tr("Copy the full payload to the clipboard"));
    connect(m_copyBtn, &QToolButton::clicked, this, [this] {
        if (!m_current.isValid())
            return;
        QString text = m_current.textData.isEmpty() ? m_current.preview : m_current.textData;
        if (m_editing && m_textEdit)
            text = m_textEdit->toPlainText();
        if (!text.isEmpty())
            emit copyToClipboardRequested(text);
    });
    bar->addWidget(m_copyBtn);

    m_pinBtn = new QToolButton(m_headerBar);
    m_pinBtn->setText(tr("Pin"));
    m_pinBtn->setIcon(QIcon::fromTheme(QStringLiteral("bookmarks")));
    m_pinBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_pinBtn->setCheckable(true);
    m_pinBtn->setToolTip(tr("Pin / unpin this entry"));
    connect(m_pinBtn, &QToolButton::clicked, this, [this] {
        if (m_current.isValid())
            emit pinRequested(m_current.id);
    });
    bar->addWidget(m_pinBtn);

    m_sourceLabel = UiHelpers::makeHint(QString(), m_headerBar, /*richText=*/false);
    bar->addWidget(m_sourceLabel, 1);

    m_wrapBtn = new QToolButton(m_headerBar);
    m_wrapBtn->setText(tr("Wrap"));
    m_wrapBtn->setCheckable(true);
    m_wrapBtn->setChecked(true);
    m_wrapBtn->setToolTip(tr("Toggle line wrap in the text preview"));
    connect(m_wrapBtn, &QToolButton::toggled, this, [this](bool on) {
        if (m_textEdit)
            m_textEdit->setWordWrapMode(on ? QTextOption::WrapAnywhere : QTextOption::NoWrap);
    });
    bar->addWidget(m_wrapBtn);

    m_editBtn = new QToolButton(m_headerBar);
    m_editBtn->setText(tr("Edit"));
    m_editBtn->setToolTip(tr("Fix typos before pasting (re-hashes on save)"));
    connect(m_editBtn, &QToolButton::clicked, this, [this] { setEditing(true); });
    bar->addWidget(m_editBtn);

    m_saveEditBtn = new QToolButton(m_headerBar);
    m_saveEditBtn->setText(tr("Save"));
    m_saveEditBtn->setVisible(false);
    connect(m_saveEditBtn, &QToolButton::clicked, this, [this] { saveEdit(); });
    bar->addWidget(m_saveEditBtn);

    m_cancelEditBtn = new QToolButton(m_headerBar);
    m_cancelEditBtn->setText(tr("Cancel"));
    m_cancelEditBtn->setVisible(false);
    connect(m_cancelEditBtn, &QToolButton::clicked, this, [this] { setEditing(false); });
    bar->addWidget(m_cancelEditBtn);

    m_closeBtn = new QToolButton(m_headerBar);
    m_closeBtn->setText(tr("Close"));
    m_closeBtn->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
    m_closeBtn->setVisible(false); // drawer mode enables it
    connect(m_closeBtn, &QToolButton::clicked, this, [this] { emit closeRequested(); });
    bar->addWidget(m_closeBtn);

    m_headerBar->setVisible(false);
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
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // R2 image bar: zoom slider + fit/100% toggle.
    m_imageBar = new QWidget(page);
    auto *bar = new QHBoxLayout(m_imageBar);
    bar->setContentsMargins(6, 4, 6, 4);
    bar->setSpacing(6);
    auto *zoomOut = new QToolButton(m_imageBar);
    zoomOut->setText(QStringLiteral("−"));
    zoomOut->setToolTip(tr("Zoom out"));
    connect(zoomOut, &QToolButton::clicked, this, [this] {
        m_zoom = m_zoom <= 0.0 ? 0.75 : qMax(0.25, m_zoom - 0.25);
        m_zoomSlider->setValue(qRound(m_zoom * 100));
        updateImageView();
    });
    bar->addWidget(zoomOut);
    m_zoomSlider = new QSlider(Qt::Horizontal, m_imageBar);
    m_zoomSlider->setRange(25, 400);
    m_zoomSlider->setValue(0); // 0 = fit (special-cased)
    m_zoomSlider->setAccessibleName(tr("Image zoom"));
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int value) {
        m_zoom = value <= 0 ? 0.0 : value / 100.0;
        updateImageView();
    });
    bar->addWidget(m_zoomSlider, 1);
    auto *zoomIn = new QToolButton(m_imageBar);
    zoomIn->setText(QStringLiteral("+"));
    zoomIn->setToolTip(tr("Zoom in"));
    connect(zoomIn, &QToolButton::clicked, this, [this] {
        m_zoom = m_zoom <= 0.0 ? 1.25 : qMin(4.0, m_zoom + 0.25);
        m_zoomSlider->setValue(qRound(m_zoom * 100));
        updateImageView();
    });
    bar->addWidget(zoomIn);
    m_zoomFitBtn = new QToolButton(m_imageBar);
    m_zoomFitBtn->setText(tr("Fit"));
    m_zoomFitBtn->setCheckable(true);
    m_zoomFitBtn->setChecked(true);
    m_zoomFitBtn->setToolTip(tr("Fit to width / 100%"));
    connect(m_zoomFitBtn, &QToolButton::toggled, this, [this](bool fit) {
        m_zoomFitBtn->setText(fit ? tr("Fit") : tr("100%"));
        m_zoom = fit ? 0.0 : 1.0;
        m_zoomSlider->setValue(fit ? 0 : 100);
        updateImageView();
    });
    bar->addWidget(m_zoomFitBtn);
    m_zoomLabel = UiHelpers::makeHint(QStringLiteral("100%"), m_imageBar, /*richText=*/false);
    bar->addWidget(m_zoomLabel);
    layout->addWidget(m_imageBar);

    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setAlignment(Qt::AlignCenter);
    m_imageScroll = scroll;
    m_imageLabel = new QLabel(scroll);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    scroll->setWidget(m_imageLabel);
    layout->addWidget(scroll, 1);

    // R2 privacy blur overlay: covers the payload until hover/focus.
    m_blurLabel = new QLabel(page);
    m_blurLabel->setAlignment(Qt::AlignCenter);
    m_blurLabel->setWordWrap(true);
    m_blurLabel->setText(tr("Blurred — hover or focus to reveal"));
    m_blurLabel->setAccessibleName(tr("Blurred preview"));
    m_blurLabel->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_blurLabel->hide();
    m_blurLabel->installEventFilter(this);
    return page;
}

void PreviewPane::updateImageView()
{
    if (m_image.isNull() || !m_imageLabel)
        return;
    QPixmap pixmap;
    if (m_zoom <= 0.0) {
        const int maxWidth = m_imageScroll ? qMax(200, m_imageScroll->viewport()->width() - 16)
                                           : qMax(200, width() - 32);
        pixmap = QPixmap::fromImage(
            m_image.scaled(maxWidth, 4096, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if (m_zoomLabel)
            m_zoomLabel->setText(tr("Fit"));
    } else {
        const QSize scaled = (m_image.size() * m_zoom).boundedTo(QSize(8192, 8192));
        pixmap = QPixmap::fromImage(
            m_image.scaled(scaled, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if (m_zoomLabel)
            m_zoomLabel->setText(tr("%1%").arg(qRound(m_zoom * 100)));
    }
    m_imageLabel->setPixmap(pixmap);
}

void PreviewPane::setScreencastActive(bool active)
{
    if (active == m_screencastActive)
        return;
    m_screencastActive = active;
    updateBlurOverlay();
}

void PreviewPane::updateBlurOverlay()
{    const bool blur = UiHelpers::shouldBlurPreview(
        m_current.isValid(), m_settings ? m_settings->privacyBlur() : false,
        m_current.sensitive, m_screencastActive);
    if (!m_blurLabel)
        return;
    if (!blur) {
        m_blurLabel->hide();
        return;
    }
    // Overlay the stack area; hover/focus hides it (eventFilter below).
    m_blurLabel->setParent(m_stack);
    m_blurLabel->setGeometry(m_stack->rect().adjusted(8, 8, -8, -8));
    m_blurLabel->setStyleSheet(QStringLiteral("background: palette(window); "
                                              "border: 1px solid palette(mid); border-radius: %1px;")
                                   .arg(DesignTokens::RadiusL));
    m_blurLabel->raise();
    m_blurLabel->show();
}

void PreviewPane::showSkeleton()
{
    if (!m_skeletonOverlay || !m_stack)
        return;
    m_skeletonOverlay->setGeometry(m_stack->rect());
    m_skeletonOverlay->raise();
    m_skeletonOverlay->show();
}

void PreviewPane::hideSkeleton()
{
    if (m_skeletonOverlay)
        m_skeletonOverlay->hide();
}

void PreviewPane::setEditing(bool editing)
{
    const bool isText = m_current.type == ContentType::Text;
    if (!isText || !m_textEdit)
        return;
    m_editing = editing;
    m_textEdit->setReadOnly(!editing);
    m_textEdit->setFocusPolicy(editing ? Qt::StrongFocus : Qt::NoFocus);
    if (m_saveEditBtn)
        m_saveEditBtn->setVisible(editing);
    if (m_cancelEditBtn)
        m_cancelEditBtn->setVisible(editing);
    if (m_editBtn)
        m_editBtn->setVisible(!editing);
    if (editing) {
        m_textEdit->setFocus();
    } else if (m_current.isValid()) {
        // Cancel: restore the stored text.
        m_textEdit->setPlainText(m_originalText);
        applySearchHighlights();
    }
}

void PreviewPane::saveEdit()
{
    if (!m_editing || !m_textEdit || !m_current.isValid())
        return;
    const QString edited = m_textEdit->toPlainText();
    if (edited == m_originalText) {
        setEditing(false);
        return;
    }
    // Write-back goes through the owner (MainWindow) via copy signal is not
    // enough — store the edit flag and let the window re-hash. The pane itself
    // stays storage-free: emit the edited text for the window to persist.
    m_originalText = edited;
    m_isTransformed = false;
    setEditing(false);
    applySearchHighlights();
    emit copyToClipboardRequested(edited);
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

bool PreviewPane::eventFilter(QObject *watched, QEvent *event)
{
    // R2 privacy blur: hover or focus over the overlay reveals the payload.
    if (watched == m_blurLabel && m_blurLabel) {
        if (event->type() == QEvent::Enter || event->type() == QEvent::FocusIn
            || event->type() == QEvent::MouseButtonPress)
            m_blurLabel->hide();
    }
    return QWidget::eventFilter(watched, event);
}

void PreviewPane::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_zoom <= 0.0 && m_stack && m_stack->currentWidget() == m_imagePage)
        updateImageView();
    if (m_blurLabel && m_blurLabel->isVisible() && m_stack)
        m_blurLabel->setGeometry(m_stack->rect().adjusted(8, 8, -8, -8));
    if (m_skeletonOverlay && m_skeletonOverlay->isVisible() && m_stack)
        m_skeletonOverlay->setGeometry(m_stack->rect());
}

void PreviewPane::setCloseVisible(bool visible)
{
    if (m_closeBtn)
        m_closeBtn->setVisible(visible);
}

void PreviewPane::showEmpty(const QString &message)
{
    if (m_editing)
        setEditing(false);
    auto *emptyPage = m_emptyState->parentWidget();
    if (!message.isEmpty()) {
        delete m_emptyState;
        m_emptyState = UiHelpers::makeEmptyState(
            QStringLiteral("image-missing"), message, {}, emptyPage);
        m_emptyState->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        emptyPage->layout()->addWidget(m_emptyState);
    }
    m_stack->setCurrentWidget(emptyPage);
    m_transformBar->setVisible(false);
    if (m_headerBar)
        m_headerBar->setVisible(false);
    m_isTransformed = false;
    m_copyResultBtn->setVisible(false);
    m_revertBtn->setVisible(false);
    m_transformStatus->clear();
    m_current = {};
    setMeta({});
    updateBlurOverlay();
    hideSkeleton();
}

void PreviewPane::showRecord(const ClipboardRecord &record)
{
    // Showing another entry abandons any transformed view of the previous one.
    if (m_editing)
        setEditing(false);
    m_isTransformed = false;
    m_current = record;
    m_originalText = record.textData.isEmpty() ? record.preview : record.textData;
    m_copyResultBtn->setVisible(false);
    m_revertBtn->setVisible(false);
    m_transformStatus->clear();
    showSkeleton();
    QTimer::singleShot(0, this, &PreviewPane::hideSkeleton);

    // R2 header: source label + pin state, visible for any record.
    if (m_headerBar)
        m_headerBar->setVisible(true);
    if (m_sourceLabel)
        m_sourceLabel->setText(record.sourceApp.isEmpty()
                                   ? tr("Unknown source")
                                   : tr("from %1").arg(record.sourceApp));
    if (m_pinBtn) {
        QSignalBlocker blocker(m_pinBtn);
        m_pinBtn->setChecked(record.pinned);
    }
    const bool isTextEntry = record.type == ContentType::Text;
    if (m_editBtn)
        m_editBtn->setVisible(isTextEntry);
    if (m_wrapBtn)
        m_wrapBtn->setVisible(isTextEntry);

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
        m_image = image;
        if (m_zoomSlider)
            m_zoomSlider->setValue(0);
        m_zoom = 0.0;
        if (m_zoomFitBtn) {
            QSignalBlocker blocker(m_zoomFitBtn);
            m_zoomFitBtn->setChecked(true);
            m_zoomFitBtn->setText(tr("Fit"));
        }
        updateImageView();
        // U13: OCR footer via the pure helper — stored text wins, otherwise the
        // line names the state (processing vs. tesseract missing with install hint).
        extraMeta += UiHelpers::ocrMetaSuffix(record.hasBlob, record.ocrText,
                                              OcrWorker::isAvailable());
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
    updateBlurOverlay();
}
