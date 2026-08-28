#include <QtTest>
#include <QToolButton>

#include <limits>

#include "UI/Settings/ScaleStepper.hpp"

using namespace Acheron;

class TestScaleStepper : public QObject
{
    Q_OBJECT

private slots:
    void initialValueIsDefault()
    {
        UI::ScaleStepper stepper;
        QCOMPARE(stepper.value(), 1.0f);
    }

    void plusStepsUp()
    {
        UI::ScaleStepper stepper;
        auto *plus = stepper.findChild<QToolButton *>(QStringLiteral("scale-plus"));
        QVERIFY(plus);
        QTest::mouseClick(plus, Qt::LeftButton);
        QVERIFY(qAbs(stepper.value() - 1.05f) < 1e-6f);
    }

    void minusStepsDown()
    {
        UI::ScaleStepper stepper;
        auto *minus = stepper.findChild<QToolButton *>(QStringLiteral("scale-minus"));
        QVERIFY(minus);
        QTest::mouseClick(minus, Qt::LeftButton);
        QVERIFY(qAbs(stepper.value() - 0.95f) < 1e-6f);
    }

    void buttonsDisableAtBounds()
    {
        UI::ScaleStepper stepper;
        auto *minus = stepper.findChild<QToolButton *>(QStringLiteral("scale-minus"));
        auto *plus = stepper.findChild<QToolButton *>(QStringLiteral("scale-plus"));
        QVERIFY(minus && plus);

        stepper.setValue(0.80f);
        QVERIFY(!minus->isEnabled());
        QVERIFY(plus->isEnabled());

        stepper.setValue(1.50f);
        QVERIFY(plus->isEnabled() == false);
        QVERIFY(minus->isEnabled());
    }

    void valueChangedEmitted()
    {
        UI::ScaleStepper stepper;
        QSignalSpy spy(&stepper, &UI::ScaleStepper::valueChanged);
        auto *plus = stepper.findChild<QToolButton *>(QStringLiteral("scale-plus"));
        QTest::mouseClick(plus, Qt::LeftButton);
        QCOMPARE(spy.count(), 1);
        QVERIFY(qAbs(spy.takeFirst().at(0).toFloat() - 1.05f) < 1e-6f);
    }

    void setRangeRefreshesButtonState()
    {
        UI::ScaleStepper stepper;
        auto *minus = stepper.findChild<QToolButton *>(QStringLiteral("scale-minus"));
        auto *plus = stepper.findChild<QToolButton *>(QStringLiteral("scale-plus"));
        QVERIFY(minus && plus);

        // The value stays inside the new range, but the buttons' enabled state
        // must still be recomputed from min_/max_.
        stepper.setValue(1.0f);
        stepper.setRange(1.0f, 1.5f);
        QVERIFY(!minus->isEnabled());
        QVERIFY(plus->isEnabled());

        stepper.setRange(0.8f, 1.0f);
        QVERIFY(minus->isEnabled());
        QVERIFY(!plus->isEnabled());
    }

    void nanValueIgnored()
    {
        UI::ScaleStepper stepper;
        QSignalSpy spy(&stepper, &UI::ScaleStepper::valueChanged);
        stepper.setValue(std::numeric_limits<float>::quiet_NaN());
        QCOMPARE(stepper.value(), 1.0f);
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TestScaleStepper)
#include "tst_ScaleStepper.moc"
