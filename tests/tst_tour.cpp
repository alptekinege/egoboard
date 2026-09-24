#include <QtTest>

#include "FirstRunTour.h"

#include <QLabel>
#include <QPushButton>

// U15 first-run tour: four data-only steps (hotkeys, palette, privacy,
// settings search), step navigation, and the show-once decision.
class TestTour : public QObject
{
    Q_OBJECT

private slots:
    void defaultStepsAreFourCompleteAreas();
    void dialogNavigatesAndFinishes();
    void shouldShowHonorsSeenFlag();
};

void TestTour::defaultStepsAreFourCompleteAreas()
{
    const QList<FirstRunTour::Step> steps = FirstRunTour::defaultSteps();
    QCOMPARE(steps.size(), 4);
    bool hasHotkeys = false;
    bool hasPalette = false;
    bool hasPrivacy = false;
    bool hasSearch = false;
    for (const auto &step : steps) {
        QVERIFY(!step.iconName.isEmpty());
        QVERIFY(!step.title.isEmpty());
        QVERIFY(!step.body.isEmpty());
        QVERIFY(!step.hint.isEmpty());
        hasHotkeys = hasHotkeys || step.body.contains(QStringLiteral("Meta+V"));
        hasPalette = hasPalette || step.body.contains(QStringLiteral("Ctrl+K"));
        hasPrivacy = hasPrivacy || step.title.contains(tr("Private"));
        hasSearch = hasSearch || step.body.contains(tr("search box"));
    }
    QVERIFY(hasHotkeys);
    QVERIFY(hasPalette);
    QVERIFY(hasPrivacy);
    QVERIFY(hasSearch);
}

void TestTour::dialogNavigatesAndFinishes()
{
    FirstRunTour dialog(FirstRunTour::defaultSteps());
    QCOMPARE(dialog.stepCount(), 4);
    QCOMPARE(dialog.stepIndex(), 0);
    dialog.show();
    QTest::qWait(20);

    // Named controls, progress labelling, Back disabled on the first step.
    QVERIFY(!dialog.accessibleName().isEmpty());
    const QList<QLabel *> labels = dialog.findChildren<QLabel *>();
    bool hasCounter = false;
    for (const QLabel *label : labels) {
        if (label->text() == tr("Step 1 of 4"))
            hasCounter = true;
    }
    QVERIFY(hasCounter);
    const QList<QPushButton *> buttons = dialog.findChildren<QPushButton *>();
    QPushButton *back = nullptr;
    QPushButton *next = nullptr;
    QPushButton *skip = nullptr;
    for (QPushButton *button : buttons) {
        if (button->accessibleName() == tr("Previous tour step"))
            back = button;
        else if (button->accessibleName() == tr("Next tour step"))
            next = button;
        else if (button->accessibleName() == tr("Skip tour"))
            skip = button;
    }
    QVERIFY(back != nullptr);
    QVERIFY(next != nullptr);
    QVERIFY(skip != nullptr);
    QVERIFY(!skip->accessibleDescription().isEmpty());
    QVERIFY(!back->isEnabled());

    // Forward to the last step: Back enables, Next becomes Finish.
    dialog.goToNext();
    QCOMPARE(dialog.stepIndex(), 1);
    dialog.goToNext();
    dialog.goToNext();
    QCOMPARE(dialog.stepIndex(), 3);
    QVERIFY(back->isEnabled());
    QCOMPARE(next->text(), tr("Finish"));
    QCOMPARE(next->accessibleName(), tr("Finish tour"));

    // Steps clamp into range instead of leaving the content behind.
    dialog.goToNext();
    QCOMPARE(dialog.stepIndex(), 3);
    dialog.goToStep(99);
    QCOMPARE(dialog.stepIndex(), 3);
    dialog.goToStep(-4);
    QCOMPARE(dialog.stepIndex(), 0);
    dialog.goToPrevious();
    QCOMPARE(dialog.stepIndex(), 0);
    dialog.close();
}

void TestTour::shouldShowHonorsSeenFlag()
{
    // First launch only: unseen shows, seen (finished, skipped or closed)
    // never re-shows without asking.
    QVERIFY(FirstRunTour::shouldShow(false));
    QVERIFY(!FirstRunTour::shouldShow(true));
}

QTEST_MAIN(TestTour)
#include "tst_tour.moc"
