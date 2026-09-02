#include "App.hpp"
#include "UI/MainWindow.hpp"
#include "UI/Dialogs/EmojiPickerDialog.hpp"
#include "UI/Dialogs/VirtualEmojiGrid.hpp"
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
#include <QEventLoop>
#include <QEvent>

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
    // The smoke opens/closes dialogs in cycles; without this, closing the
    // dialog (the only window) quits the application, poisoning every later
    // nested event loop (QEventLoop::exec returns immediately).
    app.setQuitOnLastWindowClosed(false);
    // Isolate the smoke's QSettings from the real app's ("ouwou"/"Acheron") so
    // geometry persistence tests can't clobber a user's saved picker spot.
    app.setOrganizationName(QStringLiteral("acheron-smoke"));
    app.setApplicationName(QStringLiteral("runtime-smoke"));
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
        // Fits within the offscreen platform's default 800x600 virtual screen
        // (the dialog's minimum is 720x520) so no clamping skews the round-trip.
        const QSize kSavedSize(730, 530);
        QPoint savedPos(10, 10); // filled from the dialog's actual settled pos
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
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            if (cycle > 0) {
                // Regression guard: the previous cycle closed the dialog at a
                // distinctive geometry, so a freshly shown picker must have
                // restored it from QSettings. The exact size match is the
                // strong assertion (default min size is 720x520, so 730x530 can
                // only come from restoreGeometry); the position is compared
                // loosely because the offscreen platform emulates a window
                // frame that offsets the client origin a few px.
                const bool sizeOk = dlg->size() == kSavedSize;
                const bool posOk = qAbs(dlg->frameGeometry().x() - savedPos.x()) <= 2
                                && qAbs(dlg->frameGeometry().y() - savedPos.y()) <= 25;
                if (!sizeOk || !posOk) {
                    std::fprintf(stderr,
                                 "SMOKE cycle %d: geometry restore FAILED: size=%dx%d "
                                 "frame=%d,%d (expected %dx%d frame at %d,%d)\n",
                                 cycle, dlg->width(), dlg->height(), dlg->frameGeometry().x(),
                                 dlg->frameGeometry().y(), kSavedSize.width(), kSavedSize.height(),
                                 savedPos.x(), savedPos.y());
                    std::fputs("ACHERON_SMOKE_FAIL_GEOMETRY\n", stdout);
                    std::fflush(stdout);
                    QCoreApplication::exit(4);
                    return;
                }
                std::fprintf(stderr, "SMOKE cycle %d: geometry restored OK\n", cycle);
            }
            for (int width : { 720, 840, 960, 780, 1100, 720 }) {
                std::fprintf(stderr, "SMOKE cycle %d: resize %d\n", cycle, width);
                dlg->resize(width, 560);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            }
            // The Server tab is the default and its grid starts its fade-in
            // during construction, before show. Regression guard for the bug
            // where DialogAnimator's show-time dialog fade killed that
            // in-flight animation, leaving the grid at opacity 0.01 until a
            // tab switch. Spin a real nested event loop (like the app's exec()
            // would) so the 200ms fade actually runs to completion, then assert
            // the fade removed its effect.
            {
                QEventLoop spin;
                QTimer::singleShot(600, &spin, &QEventLoop::quit);
                spin.exec();
                const auto grids = dlg->findChildren<UI::VirtualEmojiGrid *>();
                bool fadeCleaned = true;
                for (auto *grid : grids) {
                    if (grid->graphicsEffect())
                        fadeCleaned = false;
                }
                if (!fadeCleaned) {
                    std::fputs("ACHERON_SMOKE_FAIL_GRID_FADE\n", stdout);
                    std::fflush(stdout);
                    QCoreApplication::exit(3);
                    return;
                }
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
            // Position the dialog at a distinctive geometry so the next cycle
            // can assert the position+size were persisted and restored. Record
            // the frame top-left, which is what saveGeometry() round-trips.
            dlg->move(savedPos);
            dlg->resize(kSavedSize);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            savedPos = dlg->frameGeometry().topLeft();
            dlg->close();
            std::fprintf(stderr, "SMOKE cycle %d: deleteLater\n", cycle);
            dlg->deleteLater();
            // processEvents() alone does not drain the DeferredDelete queue,
            // so the dialog's destructor (which persists the geometry) would
            // not have run before the next cycle constructs its dialog.
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
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
        // Run the same teardown as the success path: the logger's writer thread
        // is still parked on its mutex/CV, and skipping cleanup here would let
        // it outlive the static synchronization objects at process exit (hang
        // / UB), and leave curl's global state un-finalized.
        curl_global_cleanup();
        Core::Logger::cleanup();
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
