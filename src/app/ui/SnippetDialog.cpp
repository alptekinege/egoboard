#include "SnippetDialog.h"

#include "SnippetManager.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

SnippetDialog::SnippetDialog(SnippetManager *manager, const QString &clipboardText, QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
    , m_clipboard(clipboardText)
{
    setWindowTitle(tr("Snippets"));
    resize(780, 480);
    auto *layout = new QVBoxLayout(this);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    m_list = new QListWidget(splitter);
    m_list->setMinimumWidth(200);
    splitter->addWidget(m_list);

    auto *right = new QWidget(splitter);
    auto *form = new QVBoxLayout(right);
    form->addWidget(new QLabel(tr("Name:"), right));
    m_name = new QLineEdit(right);
    m_name->setPlaceholderText(tr("e.g. git commit"));
    form->addWidget(m_name);

    form->addWidget(new QLabel(tr("Shortcut (optional, not yet bound):"), right));
    m_shortcut = new QLineEdit(right);
    m_shortcut->setPlaceholderText(tr("e.g. gcm"));
    form->addWidget(m_shortcut);

    form->addWidget(new QLabel(tr("Template (use {{clipboard}}, {{date}}, {{time}}, {{datetime}}):"), right));
    m_template = new QPlainTextEdit(right);
    m_template->setPlaceholderText(tr("git commit -m \"{{clipboard}}\" [{{date}}]"));
    m_template->setMinimumHeight(100);
    form->addWidget(m_template);

    form->addWidget(new QLabel(tr("Preview (with current clipboard):"), right));
    m_preview = new QPlainTextEdit(right);
    m_preview->setReadOnly(true);
    m_preview->setMinimumHeight(100);
    form->addWidget(m_preview);

    auto *btnRow = new QHBoxLayout();
    auto *createBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), tr("Create"), right);
    auto *updateBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-save")), tr("Save"), right);
    auto *deleteBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-delete")), tr("Delete"), right);
    auto *insertBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-paste")), tr("Copy Expanded"), right);
    insertBtn->setToolTip(tr("Expand with current clipboard and copy to clipboard"));
    // Fix: capture text before emitting
    btnRow->addWidget(createBtn);
    btnRow->addWidget(updateBtn);
    btnRow->addWidget(deleteBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(insertBtn);
    form->addLayout(btnRow);

    splitter->addWidget(right);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    auto *hint = new QLabel(
        tr("Placeholders: <code>{{clipboard}}</code> (current text, also {{text}}/{{selection}}), "
           "<code>{{date}}</code> YYYY-MM-DD, <code>{{time}}</code> HH:mm, "
           "<code>{{datetime}}</code> YYYY-MM-DD HH:mm:ss, <code>{{timestamp}}</code> ms. "
           "All local — no network."),
        this);
    hint->setTextFormat(Qt::RichText);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    layout->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int){ onSelectionChanged(); });
    connect(createBtn, &QPushButton::clicked, this, &SnippetDialog::onCreate);
    connect(updateBtn, &QPushButton::clicked, this, &SnippetDialog::onUpdate);
    connect(deleteBtn, &QPushButton::clicked, this, &SnippetDialog::onDelete);
    connect(insertBtn, &QPushButton::clicked, this, &SnippetDialog::onInsert);
    connect(m_template, &QPlainTextEdit::textChanged, this, &SnippetDialog::updatePreview);
    connect(m_name, &QLineEdit::textChanged, this, &SnippetDialog::updatePreview);

    reload();
    if (m_list->count() > 0) m_list->setCurrentRow(0);
}

void SnippetDialog::reload()
{
    m_list->clear();
    if (!m_manager) return;
    for (const auto &s : m_manager->snippets()) {
        auto *it = new QListWidgetItem(s.name, m_list);
        it->setData(Qt::UserRole, s.id);
        it->setToolTip(s.templateText);
    }
}

void SnippetDialog::onSelectionChanged()
{
    auto *it = m_list->currentItem();
    if (!it) {
        m_currentId = 0;
        m_name->clear();
        m_template->clear();
        m_shortcut->clear();
        updatePreview();
        return;
    }
    const qint64 id = it->data(Qt::UserRole).toLongLong();
    m_currentId = id;
    if (auto s = m_manager->snippet(id)) {
        QSignalBlocker b1(m_name), b2(m_template), b3(m_shortcut);
        m_name->setText(s->name);
        m_template->setPlainText(s->templateText);
        m_shortcut->setText(s->shortcut);
    }
    updatePreview();
}

void SnippetDialog::updatePreview()
{
    const QString tmpl = m_template->toPlainText();
    const QString expanded = SnippetManager::expand(tmpl, m_clipboard);
    m_preview->setPlainText(expanded);
}

void SnippetDialog::onCreate()
{
    const QString name = m_name->text().trimmed();
    const QString tmpl = m_template->toPlainText();
    if (name.isEmpty() || tmpl.isEmpty()) {
        QMessageBox::warning(this, tr("Snippet"), tr("Name and template are required."));
        return;
    }
    const qint64 id = m_manager->createSnippet(name, tmpl, m_shortcut->text().trimmed());
    if (id == 0) {
        QMessageBox::warning(this, tr("Snippet"), tr("Failed to create snippet."));
        return;
    }
    reload();
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).toLongLong() == id) {
            m_list->setCurrentRow(i);
            break;
        }
    }
}

void SnippetDialog::onUpdate()
{
    if (m_currentId == 0) {
        onCreate();
        return;
    }
    const QString name = m_name->text().trimmed();
    const QString tmpl = m_template->toPlainText();
    if (name.isEmpty() || tmpl.isEmpty()) {
        QMessageBox::warning(this, tr("Snippet"), tr("Name and template are required."));
        return;
    }
    if (!m_manager->updateSnippet(m_currentId, name, tmpl, m_shortcut->text().trimmed())) {
        QMessageBox::warning(this, tr("Snippet"), tr("Update failed."));
        return;
    }
    reload();
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).toLongLong() == m_currentId) {
            QSignalBlocker b(m_list);
            m_list->setCurrentRow(i);
            break;
        }
    }
}

void SnippetDialog::onDelete()
{
    if (m_currentId == 0) return;
    if (QMessageBox::question(this, tr("Snippet"), tr("Delete this snippet?")) != QMessageBox::Yes) return;
    if (!m_manager->deleteSnippet(m_currentId)) {
        QMessageBox::warning(this, tr("Snippet"), tr("Delete failed."));
        return;
    }
    m_currentId = 0;
    reload();
    if (m_list->count() > 0) m_list->setCurrentRow(0);
    else { m_name->clear(); m_template->clear(); m_shortcut->clear(); updatePreview(); }
}

void SnippetDialog::onInsert()
{
    const QString expanded = SnippetManager::expand(m_template->toPlainText(), m_clipboard);
    if (expanded.isEmpty()) {
        QMessageBox::information(this, tr("Snippet"), tr("Preview is empty."));
        return;
    }
    emit insertRequested(expanded);
}
