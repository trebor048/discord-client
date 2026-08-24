#include "App.hpp"
#include "UI/MainWindow.hpp"
#include "Storage/DatabaseManager.hpp"
#include "Core/Session.hpp"
#include "Core/Logging.hpp"
#include "Core/Theme/Manager.hpp"
#include "Core/ScrollBarStyle.hpp"
#include "Core/Animation/HoverAnimator.hpp"
#include "Discord/CurlUtils.hpp"

#include <curl/curl.h>

#include <QtGlobal>
#include <QGuiApplication>
#include <QNetworkAccessManager>
#include <QFontDatabase>
#include <QStyleFactory>
#include <QTimer>

#include <cstdio>
#include <cstring>

#ifndef ACHERON_NO_VOICE
#include <dave/dave.h>
#include <dave/logger.h>
#endif

// potentially named after that river
// or the honkai star rail character
// who knows

void registerMetatypes()
{
    qRegisterMetaType<Acheron::Core::Snowflake>("Snowflake");

    QMetaType::registerConverter<Acheron::Core::Snowflake, QString>(
            [](const Acheron::Core::Snowflake &s) { return s.toString(); });
}

static bool isRuntimeSmokeInvocation(int argc, char *argv[])
{
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--runtime-smoke") == 0)
            return true;
    }

    return false;
}

static int runRuntimeSmoke(int argc, char *argv[])
{
    if (argc != 2 || std::strcmp(argv[1], "--runtime-smoke") != 0) {
        std::fputs("error: --runtime-smoke cannot be combined with other arguments\n", stderr);
        return 2;
    }

    QGuiApplication app(argc, argv);
    std::fputs("ACHERON_RUNTIME_SMOKE_READY\n", stdout);
    std::fflush(stdout);

    QTimer::singleShot(0, &app, &QCoreApplication::quit);
    return app.exec();
}

#ifndef ACHERON_NO_VOICE
static void DaveLogSink(discord::dave::LoggingSeverity severity, const char *file, int line, const std::string &message)
{
    switch (severity) {
    case discord::dave::LoggingSeverity::LS_ERROR:
        qCCritical(LogDave) << file << ":" << line << ": " << message.c_str();
        break;
    case discord::dave::LoggingSeverity::LS_WARNING:
        qCWarning(LogDave) << file << ":" << line << ": " << message.c_str();
        break;
    case discord::dave::LoggingSeverity::LS_INFO:
        qCInfo(LogDave) << file << ":" << line << ": " << message.c_str();
        break;
    case discord::dave::LoggingSeverity::LS_VERBOSE:
        qCDebug(LogDave) << file << ":" << line << ": " << message.c_str();
        break;
    case discord::dave::LoggingSeverity::LS_NONE:
        break;
    }
}
#endif

int main(int argc, char *argv[])
{
    using namespace Acheron;

    if (isRuntimeSmokeInvocation(argc, argv))
        return runRuntimeSmoke(argc, argv);

    curl_global_init(CURL_GLOBAL_DEFAULT);

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    App app(argc, argv);
    app.setOrganizationName("ouwou");
    app.setApplicationName("Acheron");
    app.setStyle(new Core::ScrollBarStyle(QStyleFactory::create("Fusion")));

    Core::Theme::Manager::instance().load();
    Core::Theme::Manager::instance().apply();
    Core::Theme::Manager::instance().applyFonts();

    // App-wide animated hover (wash layers on interactive widgets).
    Core::HoverAnimator::instance().install();

    registerMetatypes();

    QNetworkAccessManager buildNumberNam;
    Discord::CurlUtils::fetchBuildNumber(&buildNumberNam);

    Acheron::Core::Logger::init();

#ifndef ACHERON_NO_VOICE
    discord::dave::SetLogSink(DaveLogSink);
#endif

    qCInfo(LogCore) << "Starting Acheron...";

#ifdef Q_OS_WINDOWS
    int emojiFontId = QFontDatabase::addApplicationFont(
            QCoreApplication::applicationDirPath() + "/fonts/TwemojiCOLRv0.ttf");
    if (emojiFontId != -1) {
        QStringList families = QFontDatabase::applicationFontFamilies(emojiFontId);
        qCInfo(LogCore) << "Loaded emoji font:" << families;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        if (!families.isEmpty())
            QFontDatabase::addApplicationEmojiFontFamily(families.first());
#endif
    } else {
        qCWarning(LogCore) << "Failed to load TwemojiCOLRv0.ttf";
    }
#endif

    if (!Storage::DatabaseManager::instance().init()) {
        QMessageBox::critical(nullptr, "Fatal error",
                              "Could not initialize the database. Acheron will now close.");
        return -1;
    }

    int exitCode = 0;

    {
        Core::Session session;
        UI::MainWindow window(&session);
        window.show();

        session.autoConnectAccounts();

        exitCode = app.exec();
    }

    Storage::DatabaseManager::instance().shutdown();

    curl_global_cleanup();

    Core::Logger::cleanup();

    return exitCode;
}
