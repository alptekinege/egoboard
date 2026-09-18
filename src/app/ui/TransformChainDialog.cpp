#include "TransformChainDialog.h"

#include "ScriptActionManager.h"
#include "UiHelpers.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

TransformChainDialog::TransformChainDialog(const QString &inputText, ScriptActionManager *scripts, QWidget *parent)
    : QDialog(parent)
    , m_input(inputText)
    , m_scripts(scripts)
{
    setWindowTitle(tr("Transform Chain — live preview"));
    resize(860, 520);

    auto *layout = new QVBoxLayout(this);
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    // Available
    auto *left = new QWidget(splitter);
    auto *leftLay = new QVBoxLayout(left);
    leftLay->addWidget(new QLabel(tr("Available transforms (double-click to add):"), left));
    m_available = new QListWidget(left);
    leftLay->addWidget(m_available, 1);

    // Chain
    auto *mid = new QWidget(splitter);
    auto *midLay = new QVBoxLayout(mid);
    midLay->addWidget(new QLabel(tr("Chain (in order):"), mid));
    m_chain = new QListWidget(mid);
    m_chain->setDragDropMode(QAbstractItemView::InternalMove);
    midLay->addWidget(m_chain, 1);
    auto *upDown = new QHBoxLayout();
    auto *upBtn = new QPushButton(tr("↑ Up"), mid);
    auto *downBtn = new QPushButton(tr("↓ Down"), mid);
    auto *removeBtn = new QPushButton(tr("Remove"), mid);
    auto *clearBtn = new QPushButton(tr("Clear"), mid);
    upDown->addWidget(upBtn);
    upDown->addWidget(downBtn);
    upDown->addWidget(removeBtn);
    upDown->addWidget(clearBtn);
    upDown->addStretch(1);
    midLay->addLayout(upDown);

    splitter->addWidget(left);
    splitter->addWidget(mid);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    // Preview
    layout->addWidget(new QLabel(tr("Live preview:"), this));
    m_preview = new QPlainTextEdit(this);
    m_preview->setReadOnly(true);
    m_preview->setMinimumHeight(140);
    m_preview->setPlainText(m_input.left(8000));
    layout->addWidget(m_preview);

    m_error = new QLabel(this);
    m_error->setStyleSheet(UiHelpers::warningStyle());
    m_error->setWordWrap(true);
    layout->addWidget(m_error);

    auto *hint = UiHelpers::makeHint(
        tr("Chain is applied left→right. JSON transforms, Base64, URL encode etc. Scripts from "
           "<code>~/.local/share/egoboard/actions/*.js</code> are listed under <i>Scripts</i> if present."),
        this);
    layout->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Copy Result"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Populate available
    for (const auto &d : TransformEngine::allDescriptors()) {
        auto *it = new QListWidgetItem(QStringLiteral("%1 — %2").arg(d.label, d.description), m_available);
        it->setData(Qt::UserRole, d.name);
        it->setData(Qt::UserRole + 1, QStringLiteral("builtin"));
        it->setToolTip(d.name);
    }
    // Scripts
    if (m_scripts) {
        for (const auto &a : m_scripts->actions()) {
            auto *it = new QListWidgetItem(QStringLiteral("[JS] %1").arg(a.label), m_available);
            it->setData(Qt::UserRole, a.id);
            it->setData(Qt::UserRole + 1, QStringLiteral("script"));
            it->setToolTip(a.filePath + (a.matchPattern.isEmpty() ? QString() : QStringLiteral(" — match: ") + a.matchPattern));
        }
        if (m_scripts->actions().isEmpty()) {
            auto *it = new QListWidgetItem(tr("(no JS actions — add .js files to ~/.local/share/egoboard/actions/)"), m_available);
            it->setFlags(it->flags() & ~Qt::ItemIsEnabled);
        }
    }

    connect(m_available, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it){
        if (!it) return;
        const QString name = it->data(Qt::UserRole).toString();
        const QString kind = it->data(Qt::UserRole + 1).toString();
        auto *ni = new QListWidgetItem(it->text(), m_chain);
        ni->setData(Qt::UserRole, name);
        ni->setData(Qt::UserRole + 1, kind);
        rebuildPreview();
    });
    connect(m_chain, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it){
        delete it;
        rebuildPreview();
    });
    connect(m_chain->model(), &QAbstractItemModel::rowsMoved, this, [this](){ rebuildPreview(); });
    connect(upBtn, &QPushButton::clicked, this, &TransformChainDialog::moveUp);
    connect(downBtn, &QPushButton::clicked, this, &TransformChainDialog::moveDown);
    connect(removeBtn, &QPushButton::clicked, this, [this]{
        delete m_chain->currentItem();
        rebuildPreview();
    });
    connect(clearBtn, &QPushButton::clicked, this, [this]{
        m_chain->clear();
        rebuildPreview();
    });

    rebuildPreview();
}

void TransformChainDialog::moveUp()
{
    const int row = m_chain->currentRow();
    if (row <= 0) return;
    auto *it = m_chain->takeItem(row);
    m_chain->insertItem(row - 1, it);
    m_chain->setCurrentRow(row - 1);
    rebuildPreview();
}

void TransformChainDialog::moveDown()
{
    const int row = m_chain->currentRow();
    if (row < 0 || row >= m_chain->count() - 1) return;
    auto *it = m_chain->takeItem(row);
    m_chain->insertItem(row + 1, it);
    m_chain->setCurrentRow(row + 1);
    rebuildPreview();
}

void TransformChainDialog::rebuildPreview()
{
    QString cur = m_input;
    QString error;
    for (int i = 0; i < m_chain->count(); ++i) {
        auto *it = m_chain->item(i);
        const QString name = it->data(Qt::UserRole).toString();
        const QString kind = it->data(Qt::UserRole + 1).toString();
        TransformEngine::Result r;
        if (kind == QLatin1String("script") && m_scripts) {
            auto sr = m_scripts->apply(name, cur);
            r.ok = sr.ok;
            r.output = sr.output;
            r.error = sr.error;
        } else {
            auto id = TransformEngine::idForName(name);
            if (!id.has_value()) {
                error = tr("Unknown transform: %1").arg(name);
                break;
            }
            r = TransformEngine::apply(*id, cur);
        }
        if (!r.ok) {
            error = r.error;
            // Keep partial preview up to failure point.
            // `cur` already holds the last successful output here.
            break;
        }
        cur = r.output;
    }
    m_preview->setPlainText(cur.left(200000)); // cap preview
    m_error->setText(error);
    m_error->setVisible(!error.isEmpty());
}

QString TransformChainDialog::resultText() const
{
    return m_preview->toPlainText();
}
