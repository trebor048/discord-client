#include "App.hpp"
#include "UI/MainWindow.hpp"
#include "UI/Dialogs/EmojiPickerDialog.hpp"
#include "Storage/DatabaseManager.hpp"
#include "Core/Session.hpp"
#include "Core/Logging.hpp"
#include "Core/EmojiCatalog.hpp"
#include "Core/Theme/Manager.hpp"
#include "Core/ScrollBarStyle.hpp"
#include "Core/Animation/HoverAnimator.hpp"
#include "UI/Dialogs/DialogAnimator.hpp"
#include "Discord/CurlUtils.hpp"

#include <curl/curl.h>

#include <QtGlobal>
#include <QApplication>
#include <QNetworkAccessManager>
#include <QFontDatabase>
#include <QStyleFactory>
#include <QTabWidget>
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
    using namespace Acheron;

    if (argc != 2 || std::strcmp(argv[1], "--runtime-smoke") != 0) {
        std::fputs("error: --runtime-smoke cannot be combined with other arguments\n", stderr);
        return 2;
    }

    QApplication app(argc, argv);
    std::fputs("ACHERON_RUNTIME_SMOKE_READY\n", stdout);
    std::fflush(stdout);

    registerMetatypes();

    Core::Theme::Manager::instance().load();
    Core::Theme::Manager::instance().apply();
    Core::HoverAnimator::instance().install();
    UI::DialogAnimator::instance().install();

    // Populate the custom-emoji registry so the emoji picker's Server tab has
    // real content to virtualize, like a logged-in guild would provide.
    // SMOKE_NO_EMOJI=1 keeps the registry empty (isolates the Server-grid path).
    if (!qEnvironmentVariableIsSet("SMOKE_NO_EMOJI")) {
        QVector<Core::EmojiCatalogItem> custom;
        for (int guild = 1; guild <= 4; ++guild) {
            for (int i = 0; i < 40; ++i) {
                Core::EmojiCatalogItem item;
                item.name = QStringLiteral("g%1_e%2").arg(guild).arg(i);
                item.customId = QStringLiteral("1%1%2%3").arg(guild).arg(i).arg(9200 + guild);
                item.guildId = QString::number(8000 + guild);
                item.guildName = QStringLiteral("Guild %1").arg(guild);
                item.animated = (i % 5 == 0);
                custom.append(item);
            }
        }
        Core::EmojiCatalog::registerCustomEmojis(custom);
    }

    // SMOKE_NO_SETTERS=1 skips the ChatView-style guild-order push (isolates
    // the constructor's own rebuild from the second rebuild it triggers).
    const bool skipSetters = qEnvironmentVariableIsSet("SMOKE_NO_SETTERS");

    QTimer::singleShot(0, &app, [skipSetters]() {
        // Open/close cycles mirror the reaction-popup usage: construct, push
        // the guild ordering like ChatView does, resize through a range of
        // widths (exercising the grid's responsive column relayout), switch
        // tabs, and tear down.
        for (int cycle = 0; cycle < 4; ++cycle) {
            std::fprintf(stderr, "SMOKE cycle %d: construct\n", cycle);
            auto *dlg = new UI::EmojiPickerDialog(nullptr);
            dlg->setWindowTitle(QStringLiteral("Add Reaction (smoke)"));
            dlg->setSearchPlaceholder(QStringLiteral("Search emoji"));
            if (!skipSetters) {
                dlg->setOrderedGuildIds({ QStringLiteral("8001"), QStringLiteral("8002"),
                                          QStringLiteral("8003") });
                dlg->setCurrentGuildId(QStringLiteral("8001"));
            }
            std::fprintf(stderr, "SMOKE cycle %d: show\n", cycle);
            dlg->show();
            for (int width : { 720, 840, 960, 780, 1100, 720 }) {
                std::fprintf(stderr, "SMOKE cycle %d: resize %d\n", cycle, width);
                dlg->resize(width, 560);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            }
            // All-tab category grid (sticky header path), then back to Server.
            if (auto tabs = dlg->findChild<QTabWidget *>(QStringLiteral("emojiCategoryTabs"))) {
                std::fprintf(stderr, "SMOKE cycle %d: tab -> All\n", cycle);
                tabs->setCurrentIndex(0);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
                std::fprintf(stderr, "SMOKE cycle %d: tab -> Server\n", cycle);
                tabs->setCurrentIndex(3);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            }
            std::fprintf(stderr, "SMOKE cycle %d: close\n", cycle);
            dlg->close();
            std::fprintf(stderr, "SMOKE cycle %d: deleteLater\n", cycle);
            dlg->deleteLater();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            std::fprintf(stderr, "SMOKE cycle %d: done\n", cycle);
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        std::fputs("ACHERON_RUNTIME_SMOKE_DONE\n", stdout);
        std::fflush(stdout);
        QCoreApplication::quit();
    });

    const int code = app.exec();
    return code;
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

    // App-wide dialog entrance animation (plain QDialogs that don't self-animate).
    UI::DialogAnimator::instance().install();

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
