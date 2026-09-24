#include "FirstRunTour.h"

#include "UiHelpers.h"

#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

QList<FirstRunTour::Step> FirstRunTour::defaultSteps()
{
    return {
        {QStringLiteral("input-keyboard"),
         tr("Copy as usual — paste with hotkeys"),
         tr("Egoboard records text, HTML, images and file copies locally. "
            "Press <b>Meta+V</b> for the quick-paste popup or "
            "<b>Meta+Shift+V</b> for the history window. Press <b>?</b> "
            "anywhere in the window for the full shortcut cheatsheet."),
         tr("Meta+V quick paste  •  ? cheatsheet")},
        {QStringLiteral("system-search"),
         tr("Find anything with the palette"),
         tr("Press <b>Ctrl+K</b> and type to search the whole history, or "
            "start with <b>&gt;</b> for commands: <b>&gt;tag</b>, "
            "<b>&gt;group</b>, <b>&gt;export</b>, <b>&gt;profile</b>, "
            "<b>&gt;pause</b>. Tab completes, Enter runs."),
         tr("Ctrl+K palette  •  > for commands")},
        {QStringLiteral("security-medium"),
         tr("Private by default"),
         tr("Sensitive captures (cards, passwords, tokens) are "
            "<b>never stored</b> unless you change it in Settings ▸ Privacy. "
            "Per-app ignore rules keep password managers out, and at-rest "
            "encryption is one opt-in checkbox away."),
         tr("Settings ▸ Privacy  •  nothing leaves this machine")},
        {QStringLiteral("configure"),
         tr("Make it yours with settings search"),
         tr("Settings has a <b>search box on top</b> — type what you want "
            "(“tray”, “backup”, “OCR”) and matching pages and options light "
            "up. Every page can be reset to defaults, and named "
            "<b>profiles</b> (Work, Personal) switch whole setups."),
         tr("Settings search  •  per-page reset  •  >profile")},
    };
}

FirstRunTour::FirstRunTour(const QList<Step> &steps, QWidget *parent)
    : QDialog(parent)
    , m_steps(steps)
{
    setWindowTitle(tr("Welcome to Egoboard"));
    setAccessibleName(tr("Introduction tour"));
    setModal(true);
    auto *layout = new QVBoxLayout(this);

    auto *header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    m_icon = new QLabel(this);
    m_icon->setAccessibleName(tr("Step icon"));
    header->addWidget(m_icon);
    m_title = new QLabel(this);
    QFont titleFont = m_title->font();
    titleFont.setWeight(QFont::DemiBold);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    m_title->setFont(titleFont);
    m_title->setWordWrap(true);
    m_title->setTextFormat(Qt::RichText);
    header->addWidget(m_title, 1);
    layout->addLayout(header);

    m_body = new QLabel(this);
    m_body->setWordWrap(true);
    m_body->setTextFormat(Qt::RichText);
    m_body->setAccessibleName(tr("Step description"));
    layout->addWidget(m_body, 1);

    m_hint = UiHelpers::makeHint(QString(), this);
    layout->addWidget(m_hint);

    m_counter = UiHelpers::makeHint(QString(), this, /*richText=*/false);
    m_counter->setAccessibleName(tr("Tour progress"));
    layout->addWidget(m_counter);

    auto *buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    m_skip = new QPushButton(tr("Skip tour"), this);
    m_skip->setAccessibleName(tr("Skip tour"));
    m_skip->setAccessibleDescription(
        tr("Close the tour. It will not show again on its own."));
    connect(m_skip, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(m_skip);
    buttons->addStretch(1);
    m_back = new QPushButton(tr("Back"), this);
    m_back->setAccessibleName(tr("Previous tour step"));
    connect(m_back, &QPushButton::clicked, this, &FirstRunTour::goToPrevious);
    buttons->addWidget(m_back);
    m_next = new QPushButton(tr("Next"), this);
    m_next->setAccessibleName(tr("Next tour step"));
    m_next->setDefault(true);
    connect(m_next, &QPushButton::clicked, this, [this] {
        if (m_index + 1 >= m_steps.size())
            accept(); // last step: Next finishes the tour
        else
            goToNext();
    });
    buttons->addWidget(m_next);
    layout->addLayout(buttons);

    updateStep();
    resize(460, 300);
}

void FirstRunTour::goToStep(int index)
{
    if (m_steps.isEmpty())
        return;
    m_index = qBound(0, index, m_steps.size() - 1);
    updateStep();
}

void FirstRunTour::goToNext()
{
    goToStep(m_index + 1);
}

void FirstRunTour::goToPrevious()
{
    goToStep(m_index - 1);
}

void FirstRunTour::updateStep()
{
    if (m_steps.isEmpty() || !m_icon || !m_title || !m_body || !m_hint || !m_counter
        || !m_back || !m_next)
        return;
    const Step &step = m_steps.at(m_index);
    m_icon->setPixmap(QIcon::fromTheme(step.iconName).pixmap(32, 32));
    m_title->setText(step.title);
    m_body->setText(step.body);
    m_hint->setText(step.hint);
    m_counter->setText(tr("Step %1 of %2").arg(m_index + 1).arg(m_steps.size()));
    setAccessibleDescription(tr("Step %1 of %2: %3")
                                 .arg(m_index + 1)
                                 .arg(m_steps.size())
                                 .arg(step.title));
    m_back->setEnabled(m_index > 0);
    const bool last = m_index + 1 >= m_steps.size();
    m_next->setText(last ? tr("Finish") : tr("Next"));
    m_next->setAccessibleName(last ? tr("Finish tour") : tr("Next tour step"));
}
