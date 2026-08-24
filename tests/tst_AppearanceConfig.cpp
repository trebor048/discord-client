#include <QtTest>
#include <QSettings>

#include "Core/Appearance/AppearanceConfig.hpp"

using namespace Acheron;
using Core::Appearance::AppearanceConfig;
using Core::Appearance::MemberListMode;

// QSettings() uses the app/org name; isolate the test under a dedicated name.
class TestAppearanceConfig : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("AcheronTests"));
        QCoreApplication::setApplicationName(QStringLiteral("AppearanceConfig"));
    }

    void cleanup()
    {
        QSettings().clear();
    }

    void defaults()
    {
        AppearanceConfig config;
        QCOMPARE(config.memberListMode(), MemberListMode::ResizeHandle);
        QCOMPARE(config.memberCardScale(), 1.0f);
        QCOMPARE(config.guildIconScale(), 1.0f);
        QCOMPARE(config.channelScale(), 1.0f);
    }

    void clamping()
    {
        AppearanceConfig config;
        config.setMemberCardScale(2.0f);
        QCOMPARE(config.memberCardScale(), 1.5f);
        config.setGuildIconScale(0.5f);
        QCOMPARE(config.guildIconScale(), 0.8f);
        config.setChannelScale(0.0f);
        QCOMPARE(config.channelScale(), 0.8f);
    }

    void stepMath()
    {
        QVERIFY(qAbs(AppearanceConfig::stepScale(1.0f, 1) - 1.05f) < 1e-6f);
        QVERIFY(qAbs(AppearanceConfig::stepScale(1.0f, -1) - 0.95f) < 1e-6f);
        QCOMPARE(AppearanceConfig::stepScale(1.5f, 1), 1.5f);   // clamped at max
        QCOMPARE(AppearanceConfig::stepScale(0.8f, -1), 0.8f);  // clamped at min
    }

    void scaledIntRounding()
    {
        QCOMPARE(AppearanceConfig::scaledInt(28, 1.5f), 42);
        QCOMPARE(AppearanceConfig::scaledInt(28, 0.8f), 22);
        QCOMPARE(AppearanceConfig::scaledInt(38, 1.0f), 38);
    }

    void persistenceRoundTrip()
    {
        {
            AppearanceConfig config;
            config.setMemberListMode(MemberListMode::SlideOut);
            config.setMemberCardScale(1.25f);
            config.setGuildIconScale(1.10f);
            config.setChannelScale(0.90f);
        }
        AppearanceConfig reloaded;
        QCOMPARE(reloaded.memberListMode(), MemberListMode::SlideOut);
        QCOMPARE(reloaded.memberCardScale(), 1.25f);
        QCOMPARE(reloaded.guildIconScale(), 1.10f);
        QCOMPARE(reloaded.channelScale(), 0.90f);
    }

    void signalsEmitted()
    {
        AppearanceConfig config;
        QSignalSpy configSpy(&config, &AppearanceConfig::configChanged);
        QSignalSpy modeSpy(&config, &AppearanceConfig::memberListModeChanged);

        config.setMemberCardScale(1.2f);
        QCOMPARE(configSpy.count(), 1);
        QCOMPARE(modeSpy.count(), 0);

        config.setMemberListMode(MemberListMode::SlideOut);
        QCOMPARE(configSpy.count(), 2);
        QCOMPARE(modeSpy.count(), 1);

        config.setMemberListMode(MemberListMode::SlideOut); // no-op
        QCOMPARE(configSpy.count(), 2);
        QCOMPARE(modeSpy.count(), 1);
    }
};

QTEST_MAIN(TestAppearanceConfig)
#include "tst_AppearanceConfig.moc"
