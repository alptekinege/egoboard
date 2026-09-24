#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QLabel;
class QPushButton;
class QStackedWidget;

// U15 first-run tour: 4-step overlay (hotkeys, palette, privacy, settings
// search) shown once on first launch only. Content is plain data (no
// managers), so the dialog constructs anywhere — including offscreen tests.
class FirstRunTour : public QDialog {
    Q_OBJECT
public:
    struct Step {
        QString iconName; // theme icon, e.g. "input-keyboard"
        QString title;
        QString body; // short rich text; palette colors only (theme-safe)
        QString hint; // one-line shortcut/action hint
    };

    // The four documented steps: hotkeys, palette, privacy, settings search.
    static QList<Step> defaultSteps();
    // Pure first-launch decision: the tour shows until it has been seen once
    // (finished, skipped or closed — re-open via More ▸ Tour or `>tour`).
    static bool shouldShow(bool tourSeen) { return !tourSeen; }

    explicit FirstRunTour(const QList<Step> &steps, QWidget *parent = nullptr);

    int stepIndex() const { return m_index; }
    int stepCount() const { return m_steps.size(); }
    void goToStep(int index); // clamped into range
    void goToNext();
    void goToPrevious();

private:
    void updateStep();

    QList<Step> m_steps;
    int m_index = 0;
    QLabel *m_icon = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_body = nullptr;
    QLabel *m_hint = nullptr;
    QLabel *m_counter = nullptr;
    QPushButton *m_back = nullptr;
    QPushButton *m_next = nullptr;
    QPushButton *m_skip = nullptr;
};
