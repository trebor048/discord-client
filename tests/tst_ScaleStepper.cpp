#include <QtTest>
#include <QToolButton>

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
};

QTEST_MAIN(TestScaleStepper)
#include "tst_ScaleStepper.moc"
