cmake_minimum_required(VERSION 3.16)

option(USE_VCPKG "Use vcpkg toolchain (set VCPKG_ROOT to a vcpkg checkout)" ON)

# Must happen before project()
if(USE_VCPKG AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(NOT DEFINED VCPKG_ROOT AND DEFINED ENV{VCPKG_ROOT})
        set(VCPKG_ROOT "$ENV{VCPKG_ROOT}" CACHE PATH "Vcpkg checkout root")
    endif()
    if(NOT DEFINED VCPKG_ROOT)
        set(VCPKG_ROOT "C:/vcpkg" CACHE PATH "Vcpkg checkout root")
    endif()
    set(CMAKE_TOOLCHAIN_FILE
        "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
    if(NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
        message(FATAL_ERROR
            "vcpkg toolchain not found at ${CMAKE_TOOLCHAIN_FILE}.\n"
            "Set VCPKG_ROOT to a vcpkg checkout (e.g. `git clone https://github.com/microsoft/vcpkg`), "
            "or run build.ps1 which bootstraps it automatically.")
    endif()
endif()

project(acheron LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

# Unity/jumbo builds: merge source files into batches to reduce header re-parsing
# ~2-4x faster compilation on MSVC.
set(CMAKE_UNITY_BUILD OFF)
set(CMAKE_UNITY_BUILD_BATCH_SIZE 8)

# Embed debug info in .obj files instead of .pdb to avoid MSBuild PDB locking overhead
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)

# Compiler caching — ccache dramatically speeds up incremental rebuilds
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "ccache found at ${CCACHE_PROGRAM} — incremental rebuilds will be faster")
else()
    message(STATUS "ccache not found — install with: winget install ccache")
endif()

set(CMAKE_AUTORCC ON)

option(BUILD_TESTS "Build test targets" ON)
option(BUILD_TRANSLATIONS "Build bundled translations" OFF)
option(USE_PCH "Use the shared application precompiled header" ON)
option(ENABLE_VOICE "Build with voice/audio support" ON)
option(ENABLE_RNNOISE "Build with RNNoise noise suppression (requires ENABLE_VOICE)" ON)
option(ENABLE_RNNOISE_RTCD "Use RNNoise x86 runtime SIMD dispatch (SSE4.1/AVX2)" ON)
option(ALLOW_PLAIN_CURL "Permit building against plain libcurl without curl-impersonate" OFF)
if(BUILD_TESTS)
    enable_testing()
endif()

# curl-impersonate (browser TLS fingerprinting) is provided via explicit
# -DCURL_INCLUDE_DIR/-DCURL_LIBRARY (build.ps1 / CI download it). When those
# are supplied, use them directly for CURL::libcurl so find_package(CURL)
# (which otherwise resolves to vcpkg's plain libcurl via config mode) does not
# override them.
if(CURL_INCLUDE_DIR AND CURL_LIBRARY
   AND NOT CURL_INCLUDE_DIR MATCHES "NOTFOUND"
   AND NOT CURL_LIBRARY MATCHES "NOTFOUND")
    set(CURL_INCLUDE_DIRS "${CURL_INCLUDE_DIR}")
    if(NOT TARGET CURL::libcurl)
        add_library(CURL::libcurl INTERFACE IMPORTED)
        set_target_properties(CURL::libcurl PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${CURL_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "${CURL_LIBRARY}")
    endif()
else()
    find_package(CURL REQUIRED)
    if(NOT TARGET CURL::libcurl AND TARGET CURL::libcurl-impersonate)
        add_library(CURL::libcurl INTERFACE IMPORTED)
        set_target_properties(CURL::libcurl PROPERTIES
            INTERFACE_LINK_LIBRARIES CURL::libcurl-impersonate
            INTERFACE_INCLUDE_DIRECTORIES "${CURL_INCLUDE_DIRS}")
    endif()
endif()
find_package(OpenSSL REQUIRED)
if(ENABLE_VOICE)
    find_package(unofficial-sodium CONFIG REQUIRED)
    find_package(Opus CONFIG REQUIRED)

    set(SODIUM_TARGET unofficial-sodium::sodium)
    set(OPUS_TARGET Opus::opus)
endif()

find_package(Qt6
    COMPONENTS
        Core
        Gui
        Widgets
        Svg
        Network
        Sql
        Concurrent
        Test
        LinguistTools
        Multimedia
        MultimediaWidgets
)
find_package(Qt6 COMPONENTS ZlibPrivate QUIET)
qt_standard_project_setup()

# If Qt::ZlibPrivate is not available, create a QtZlib compatibility include
# directory so that #include <QtZlib/zlib.h> resolves using system zlib.
if(NOT TARGET Qt::ZlibPrivate)
    find_package(ZLIB REQUIRED)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/qtzlib-compat/QtZlib")
    file(WRITE "${CMAKE_BINARY_DIR}/qtzlib-compat/QtZlib/zlib.h" "#include <zlib.h>\n")
    file(WRITE "${CMAKE_BINARY_DIR}/qtzlib-compat/QtZlib/zconf.h" "#include <zconf.h>\n")
    add_library(QtZlibCompat INTERFACE)
    target_include_directories(QtZlibCompat INTERFACE
        "${CMAKE_BINARY_DIR}/qtzlib-compat"
        ${ZLIB_INCLUDE_DIRS}
    )
    target_link_libraries(QtZlibCompat INTERFACE ZLIB::ZLIB)
endif()

# Unified zlib target: Qt's private zlib when available, else the system-zlib
# shim above. Headers like IngestThread.hpp do #include <QtZlib/zlib.h>, so any
# target compiling them needs this on its include path.
if(TARGET Qt::ZlibPrivate)
    set(ACHERON_ZLIB_TARGET Qt::ZlibPrivate)
else()
    set(ACHERON_ZLIB_TARGET QtZlibCompat)
endif()

find_package(Qt6Keychain QUIET)
if(Qt6Keychain_FOUND)
    message(STATUS "Using system-installed QtKeychain")
else()
    message(STATUS "System QtKeychain not found, using vendored submodule")
    set(BUILD_WITH_QT6 ON CACHE BOOL "" FORCE)
    set(BUILD_TEST_APPLICATION OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_TRANSLATIONS OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory(vendor/qtkeychain EXCLUDE_FROM_ALL)
endif()

if(ENABLE_VOICE)
    option(USE_SYSTEM_MLSPP "Use a separately installed mlspp package" OFF)
    if(USE_SYSTEM_MLSPP)
        find_package(MLSPP QUIET)
    endif()
    if(NOT TARGET MLSPP::mlspp)
        message(STATUS "mlspp not found, using subproject")
        set(DISABLE_GREASE ON CACHE BOOL "" FORCE)
        set(TESTING OFF CACHE BOOL "" FORCE)
        set(MLS_CXX_NAMESPACE "mlspp" CACHE STRING "" FORCE)
        set(CMAKE_EXPORT_PACKAGE_REGISTRY OFF CACHE BOOL "" FORCE)
        add_subdirectory(vendor/mlspp EXCLUDE_FROM_ALL)
        if(NOT TARGET MLSPP::mlspp)
            add_library(MLSPP::mlspp ALIAS mlspp)
        endif()
        if(NOT TARGET MLSPP::hpke)
            add_library(MLSPP::hpke ALIAS hpke)
        endif()
        set_target_properties(mlspp PROPERTIES UNITY_BUILD OFF)
        set_target_properties(hpke PROPERTIES UNITY_BUILD OFF)
        set(MLSPP_FOUND TRUE CACHE BOOL "" FORCE)
        # Write a shim config so libdave's find_dependency(MLSPP) finds this
        # instead of the broken build-tree config that add_subdirectory generates
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/mlspp-shim/MLSPPConfig.cmake"
            "set(MLSPP_FOUND TRUE)\n")
        set(MLSPP_DIR "${CMAKE_CURRENT_BINARY_DIR}/mlspp-shim" CACHE PATH "" FORCE)
    else()
        message(STATUS "Found system mlspp")
    endif()

    # Prevent find_package from finding mlspp's broken build-tree config via registry
    set(CMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY ON)
    # libdave (Discord Audio/Video E2E Encryption)
    set(TESTING OFF CACHE BOOL "" FORCE)
    set(PERSISTENT_KEYS OFF CACHE BOOL "" FORCE)
    # Prevent libdave's own vcpkg manifest from being processed; our
    # project-level vcpkg.json already provides all required packages.
    set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "" FORCE)
    add_subdirectory(vendor/libdave/cpp EXCLUDE_FROM_ALL)
    set_target_properties(libdave PROPERTIES UNITY_BUILD OFF)
    if(MSVC)
        target_compile_options(libdave PRIVATE /UWITH_BORINGSSL)
    else()
        target_compile_options(libdave PRIVATE -UWITH_BORINGSSL)
    endif()
endif()

# find or build rnnoise
include(rnnoise.cmake)

set(PROJECT_SOURCES
    resources.qrc
    src/main.cpp
    src/Core/Snowflake.cpp
    src/Core/ClientInstance.cpp
    src/Core/Session.cpp
    src/Core/Logging.cpp
    src/Core/MessageManager.cpp
    src/Core/ForumManager.cpp
    src/Core/ImageManager.cpp
    src/Core/UserManager.cpp
    src/Core/TypingTracker.cpp
    src/Core/PermissionManager.cpp
    src/Core/PermissionComputer.cpp
    src/Core/ReadStateManager.cpp
    src/Core/MemberListManager.cpp
    src/Core/RelationshipManager.cpp
    src/Core/MurmurHash3.cpp
    src/Core/EmojiSegmenter.cpp
    src/Core/EmojiCatalog.cpp
    src/Core/Markdown/Parser.cpp
    src/Core/Theme/Tokens.cpp
    src/Core/Theme/Manager.cpp
    src/Core/Theme/Icons.cpp
    src/Core/Theme/Generator.cpp
    src/Core/Theme/Stylesheet.cpp
    src/Core/Theme/Fonts.cpp
    src/Core/ScrollBarStyle.cpp
    src/Core/Notification/NotificationManager.cpp
    src/Core/Notification/NotificationTypes.cpp
    src/Core/Notification/SoundManager.cpp
    src/UI/SplitterHandle.cpp
    src/UI/Splitter.cpp
    src/UI/MainWindow.cpp
    src/UI/BrowserCaptchaResolver.cpp
    src/UI/TypingIndicator.cpp
    src/UI/SlowModeIndicator.cpp
    src/UI/ConnectionBanner.cpp
    src/UI/ImageViewer.cpp
    src/UI/FriendsPage.cpp
    src/UI/Accounts/AccountsWindow.cpp
    src/UI/Settings/SettingsWindow.cpp
    src/UI/Settings/GeneralPage.cpp
    src/UI/Settings/AppearancePage.cpp
    src/UI/Settings/VoicePage.cpp
    src/UI/Settings/NotificationsPage.cpp
    src/UI/Settings/SoundOverrideWidget.cpp
    src/UI/Settings/PrivacySettingsPage.cpp
    src/UI/Settings/ConnectionsPage.cpp
    src/UI/Settings/AuthorizedAppsPage.cpp
    src/UI/Settings/StreamerModePage.cpp
    src/UI/Widgets/CustomStatusEdit.cpp
    src/UI/Widgets/ToastNotification.cpp
    src/UI/Widgets/ToastContainer.cpp
    src/UI/ChannelList/ChannelDelegate.cpp
    src/UI/ChannelList/ChannelTreeModel.cpp
    src/UI/ChannelList/ChannelFilterProxyModel.cpp
    src/UI/ChannelList/ChannelTreeView.cpp
    src/UI/ChannelList/ServerRailModel.cpp
    src/UI/ChannelList/ServerRailDelegate.cpp
    src/UI/ChannelList/ServerRailView.cpp
    src/UI/TabBar/TabBar.cpp
    src/UI/Chat/ChatModel.cpp
    src/UI/Chat/ChatDelegate.cpp
    src/UI/Chat/ChatView.cpp
    src/UI/Chat/ChatLayout.cpp
    src/UI/Forum/ForumPostModel.cpp
    src/UI/Forum/ForumPostDelegate.cpp
    src/UI/Forum/ForumBrowser.cpp
    src/UI/Forum/NewPostDialog.cpp
    src/UI/ThreadBrowser/ThreadBrowserPopup.cpp
    src/UI/Dialogs/TokenInputDialog.cpp
    src/UI/Dialogs/QRLoginDialog.cpp
    src/UI/Dialogs/BasePopup.cpp
    src/UI/Dialogs/ConfirmPopup.cpp
    src/UI/Dialogs/ShortcutSheet.cpp
    src/UI/Dialogs/UserProfilePopup.cpp
    src/UI/Dialogs/ChannelQuickSwitch.cpp
    src/UI/Dialogs/EmojiPickerDialog.cpp
    src/UI/Dialogs/EmojiPreferences.cpp
    src/UI/Dialogs/GifPickerDialog.cpp
    src/UI/Dialogs/StickerPickerDialog.cpp
    src/UI/Dialogs/EditProfileDialog.cpp
    src/UI/Dialogs/GuildSettingsPage.cpp
    src/UI/Dialogs/GuildSettingsDialog.cpp
    src/UI/Dialogs/VideoPlayerDialog.cpp
    src/UI/Views/GuildSettings/OverviewPage.cpp
    src/UI/Views/GuildSettings/RolesPage.cpp
    src/UI/Views/GuildSettings/EmojiPage.cpp
    src/UI/Views/GuildSettings/StickersPage.cpp
    src/UI/Widgets/GuildSettings/RoleListWidget.cpp
    src/UI/Widgets/GuildSettings/RoleEditorWidget.cpp
    src/UI/Widgets/GuildSettings/BanListWidget.cpp
    src/UI/Widgets/GuildSettings/InvitesListWidget.cpp
    src/UI/Widgets/GuildSettings/AuditLogWidget.cpp
    src/UI/Widgets/GuildSettings/WebhooksWidget.cpp
    src/UI/Widgets/GuildSettings/IntegrationsWidget.cpp
    src/UI/MemberList/MemberListModel.cpp
    src/UI/MemberList/MemberListDelegate.cpp
    src/UI/MemberList/MemberListView.cpp
    src/UI/Input/MessageInput.cpp
    src/UI/Input/AttachmentPreviewPanel.cpp
    src/Storage/DatabaseManager.cpp
    src/Storage/BaseRepository.cpp
    src/Storage/Transaction.cpp
    src/Storage/AccountRepository.cpp
    src/Storage/GuildRepository.cpp
    src/Storage/ChannelRepository.cpp
    src/Storage/MessageRepository.cpp
    src/Storage/MemberRepository.cpp
    src/Storage/RoleRepository.cpp
)

if(ENABLE_VOICE)
    list(APPEND PROJECT_SOURCES
        src/UI/VoiceStatusBar.cpp
        src/UI/VoiceWindow.cpp
        src/Discord/AV/VoiceGateway.cpp
        src/Discord/AV/UdpTransport.cpp
        src/Discord/AV/VoiceClient.cpp
        src/Discord/AV/RtpPacket.cpp
        src/Discord/AV/VoiceEncryption.cpp
        src/Discord/AV/DaveSession.cpp
        src/Core/AV/VoiceManager.cpp
        src/Core/AV/AudioPipeline.cpp
        src/Core/AV/IAudioBackend.cpp
        src/Core/AV/MiniaudioAudioBackend.cpp
        src/Core/AV/AudioBackends.cpp
        src/Core/AV/OpusEncoder.cpp
        src/Core/AV/OpusDecoder.cpp
        src/Core/AV/JitterBuffer.cpp
        src/Core/AV/AudioMixer.cpp
        src/Core/AV/NoiseSuppressor.cpp
        src/UI/Settings/AudioPage.cpp
    )
endif()

# Keep the executable source list complete as modules are added.  The explicit
# list above documents the main application layout; this catches shared core,
# Discord, storage, and widget translation units that must also be linked.
file(GLOB_RECURSE PROJECT_CPP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(APPEND PROJECT_SOURCES ${PROJECT_CPP_SOURCES})
list(REMOVE_DUPLICATES PROJECT_SOURCES)

find_package(unofficial-nayuki-qr-code-generator CONFIG QUIET)
if(TARGET unofficial::nayuki-qr-code-generator::nayuki-qr-code-generator)
    message(STATUS "Using installed nayuki-qr-code-generator")
else()
    message(STATUS "nayuki-qr-code-generator not found, using vendored version")
    list(APPEND PROJECT_SOURCES vendor/qrcodegen/cpp/qrcodegen.cpp)
endif()

qt_add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})
set_target_properties(${PROJECT_NAME} PROPERTIES UNITY_BUILD OFF)

target_include_directories(${PROJECT_NAME} PRIVATE vendor/emoji-segmenter)
if(TARGET unofficial::nayuki-qr-code-generator::nayuki-qr-code-generator)
    target_link_libraries(${PROJECT_NAME} PRIVATE unofficial::nayuki-qr-code-generator::nayuki-qr-code-generator)
else()
    target_include_directories(${PROJECT_NAME} PRIVATE vendor/qrcodegen/cpp)
endif()

if(BUILD_TESTS)
    qt_add_executable(TestMarkdown tests/tst_Markdown.cpp src/Core/Markdown/Parser.cpp src/Core/Markdown/CodeHighlighter.cpp src/Core/EmojiSegmenter.cpp)
    target_link_libraries(TestMarkdown PRIVATE Qt::Core Qt::Test)
    target_include_directories(TestMarkdown PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/vendor/emoji-segmenter)
    add_test(NAME MarkdownTests COMMAND TestMarkdown)

    qt_add_executable(TestDeserialization
        tests/tst_Deserialization.cpp
        src/Core/Snowflake.cpp
    )
    target_link_libraries(TestDeserialization PRIVATE Qt::Core Qt::Gui Qt::Test)
    target_include_directories(TestDeserialization PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME DeserializationTests COMMAND TestDeserialization)

    qt_add_executable(TestEmojiCatalog
        tests/tst_EmojiCatalog.cpp
        src/Core/EmojiCatalog.cpp
        src/UI/Dialogs/EmojiPreferences.cpp
    )
    target_link_libraries(TestEmojiCatalog PRIVATE Qt::Core Qt::Test)
    target_include_directories(TestEmojiCatalog PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME EmojiCatalogTests COMMAND TestEmojiCatalog)

    qt_add_executable(TestLinkification
        tests/tst_Linkification.cpp
        src/UI/Chat/ChatLinkification.cpp
    )
    target_link_libraries(TestLinkification PRIVATE Qt::Core Qt::Gui Qt::Widgets Qt::Network Qt::Sql Qt::Test CURL::libcurl ${ACHERON_ZLIB_TARGET})
    target_include_directories(TestLinkification PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CURL_INCLUDE_DIRS}
        ${Qt6Core_INCLUDE_DIRS}
        ${Qt6Gui_INCLUDE_DIRS}
        ${Qt6Widgets_INCLUDE_DIRS}
        ${Qt6Network_INCLUDE_DIRS}
        ${Qt6Sql_INCLUDE_DIRS}
    )
    target_compile_definitions(TestLinkification PRIVATE NOMINMAX)
    add_test(NAME LinkificationTests COMMAND TestLinkification)

    qt_add_executable(TestProtoReader tests/tst_ProtoReader.cpp src/Proto/ProtoReader.cpp src/Core/Logging.cpp)
    target_link_libraries(TestProtoReader PRIVATE Qt::Core Qt::Test)
    target_include_directories(TestProtoReader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME ProtoReaderTests COMMAND TestProtoReader)

    qt_add_executable(TestEmojiSegmenter tests/tst_EmojiSegmenter.cpp src/Core/EmojiSegmenter.cpp)
    target_link_libraries(TestEmojiSegmenter PRIVATE Qt::Core Qt::Test)
    target_include_directories(TestEmojiSegmenter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/vendor/emoji-segmenter)
    add_test(NAME EmojiSegmenterTests COMMAND TestEmojiSegmenter)

    qt_add_executable(TestMemberList
        tests/tst_MemberList.cpp
        src/Core/MemberListManager.cpp
        src/Core/Snowflake.cpp
        src/Core/MurmurHash3.cpp
        src/Core/Logging.cpp
        src/Storage/BaseRepository.cpp
        src/Storage/ChannelRepository.cpp
        src/Storage/RoleRepository.cpp
        src/Storage/DatabaseManager.cpp
        src/Storage/Transaction.cpp
    )
    target_link_libraries(TestMemberList PRIVATE Qt::Core Qt::Gui Qt::Sql Qt::Test)
    target_include_directories(TestMemberList PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME MemberListTests COMMAND TestMemberList)

    add_test(NAME RuntimeSmoke COMMAND $<TARGET_FILE:${PROJECT_NAME}> --runtime-smoke)
    set_tests_properties(RuntimeSmoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        PASS_REGULAR_EXPRESSION "ACHERON_RUNTIME_SMOKE_READY"
        TIMEOUT 15
    )

    if(DEFINED Qt6_DIR)
        get_filename_component(QT_INSTALL_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
        # Set Qt platform variables for test executables.  Qt's DLL directory
        # is expected to be on PATH already (install-qt-action sets it).
        set_tests_properties(
            MarkdownTests
            DeserializationTests
            EmojiCatalogTests
            LinkificationTests
            ProtoReaderTests
            EmojiSegmenterTests
            RuntimeSmoke
            PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins/platforms"
        )
    endif()
endif()

if(BUILD_TRANSLATIONS)
    qt_add_translations(${PROJECT_NAME})
endif()

if (MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:/INCREMENTAL>
        $<$<NOT:$<CONFIG:Debug>>:/INCREMENTAL:NO>
    )
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /FS /bigobj /MP4 /Zm500)

    if(BUILD_TESTS)
        target_link_options(TestMarkdown PRIVATE
            $<$<CONFIG:Debug>:/INCREMENTAL>
            $<$<NOT:$<CONFIG:Debug>>:/INCREMENTAL:NO>
        )
        target_compile_options(TestMarkdown PRIVATE $<$<CONFIG:Debug>:/ZI> $<$<NOT:$<CONFIG:Debug>>:/Zi>)
        target_compile_options(TestMarkdown PRIVATE /utf-8 /FS /bigobj)

        target_link_options(TestDeserialization PRIVATE
            $<$<CONFIG:Debug>:/INCREMENTAL>
            $<$<NOT:$<CONFIG:Debug>>:/INCREMENTAL:NO>
        )
        target_compile_options(TestDeserialization PRIVATE /utf-8 /FS /bigobj)

        target_link_options(TestEmojiCatalog PRIVATE
            $<$<CONFIG:Debug>:/INCREMENTAL>
            $<$<NOT:$<CONFIG:Debug>>:/INCREMENTAL:NO>
        )
        target_compile_options(TestEmojiCatalog PRIVATE /utf-8 /FS /bigobj)

        target_link_options(TestLinkification PRIVATE
            $<$<CONFIG:Debug>:/INCREMENTAL>
            $<$<NOT:$<CONFIG:Debug>>:/INCREMENTAL:NO>
        )
        target_compile_options(TestLinkification PRIVATE /utf-8 /FS /bigobj)
    endif()
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

set_target_properties(${PROJECT_NAME}
    PROPERTIES
        WIN32_EXECUTABLE TRUE
)

# Embed the application icon (and version info) into acheron.exe.
if(WIN32)
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows/app.rc")
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX)
if(NOT ENABLE_VOICE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ACHERON_NO_VOICE)
endif()

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::Svg
        Qt::Network
        Qt::Sql
        Qt::Concurrent
        Qt::Multimedia
        Qt::MultimediaWidgets
        CURL::libcurl
    PRIVATE
        qt6keychain
        OpenSSL::Crypto
)
if(ENABLE_VOICE)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${SODIUM_TARGET}
        ${OPUS_TARGET}
        libdave
    )

    target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/miniaudio")
    target_compile_definitions(${PROJECT_NAME} PRIVATE ACHERON_HAVE_MINIAUDIO)
    if(ENABLE_RNNOISE)
        target_link_libraries(${PROJECT_NAME} PRIVATE rnnoise)
        target_compile_definitions(${PROJECT_NAME} PRIVATE ACHERON_HAVE_RNNOISE)
    endif()
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE ${ACHERON_ZLIB_TARGET})
if(USE_PCH)
    target_precompile_headers(${PROJECT_NAME} PUBLIC src/pch.h)
endif()

# Exclude files with anonymous namespace constants that conflict under unity builds
set_source_files_properties(
    src/UI/Dialogs/EmojiPickerDialog.cpp
    src/UI/Dialogs/GifPickerDialog.cpp
    src/UI/Widgets/GuildSettings/EmojiListWidget.cpp
    src/UI/Input/AttachmentPreviewPanel.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)

include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_INCLUDES ${CURL_INCLUDE_DIRS})
check_cxx_source_compiles("
    #include <curl/curl.h>
    static CURLoption opt() { return (CURLoption)CURLOPT_IMPERSONATE; }
    int main() { (void)opt(); return 0; }
" HAS_CURL_IMPERSONATE)
unset(CMAKE_REQUIRED_INCLUDES)

if(HAS_CURL_IMPERSONATE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_CURL_IMPERSONATE)
    target_include_directories(${PROJECT_NAME} BEFORE PRIVATE ${CURL_INCLUDE_DIRS})
    message(STATUS "curl-impersonate detected - browser TLS fingerprinting enabled")
elseif(WIN32 AND NOT ALLOW_PLAIN_CURL)
    message(FATAL_ERROR
        "curl_easy_impersonate not found in the curl headers at:\n"
        "  ${CURL_INCLUDE_DIRS}\n"
        "Point CURL_INCLUDE_DIR at curl-impersonate's headers, or pass "
        "-DALLOW_PLAIN_CURL=ON to build against plain libcurl anyway.")
else()
    message(STATUS "curl-impersonate not found; building against plain libcurl (browser TLS fingerprinting disabled)")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/certs/cacert.pem")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/certs"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/certs/cacert.pem"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/certs/cacert.pem"
        COMMENT "Copying cacert.pem to build directory"
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/TwemojiCOLRv0.ttf")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/TwemojiCOLRv0.ttf"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/fonts/TwemojiCOLRv0.ttf"
        COMMENT "Copying TwemojiCOLRv0.ttf to build directory"
    )
endif()

if(MSVC)
    get_filename_component(QT_INSTALL_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
    find_program(QT_WINDEPLOYQT_EXECUTABLE NAMES windeployqt windeployqt.exe
        HINTS "${QT_INSTALL_PREFIX}/bin"
        NO_DEFAULT_PATH
        REQUIRED
    )

    # Keep a deployable application separate from the build tree.  Qt's deployment
    # tool discovers the actual linked libraries and plugins, avoiding a brittle
    # per-DLL copy list as Qt evolves.
    # These files are deliberately copied into the target output by the
    # post-build rules above.  The deployment step consumes that target output
    # instead of reaching back into a developer's source checkout.
    set(ACHERON_DEPLOY_REQUIRED_RUNTIME_FILES
        "certs/cacert.pem"
        "fonts/TwemojiCOLRv0.ttf"
    )
    if(ENABLE_VOICE)
        # Opus is a direct executable import and is not discovered by
        # windeployqt because it is not a Qt dependency.
        list(APPEND ACHERON_DEPLOY_REQUIRED_RUNTIME_FILES "opus.dll")
    endif()

    add_custom_target(acheron_deploy
        COMMAND ${CMAKE_COMMAND}
            "-DAPP_EXECUTABLE=$<TARGET_FILE:${PROJECT_NAME}>"
            "-DDEPLOY_DIRECTORY=${CMAKE_BINARY_DIR}/deploy/$<CONFIG>"
            "-DWINDEPLOYQT_EXECUTABLE=${QT_WINDEPLOYQT_EXECUTABLE}"
            "-DCONFIGURATION=$<CONFIG>"
            "-DREQUIRED_RUNTIME_FILES=${ACHERON_DEPLOY_REQUIRED_RUNTIME_FILES}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployQtRuntime.cmake"
        DEPENDS ${PROJECT_NAME}
        COMMENT "Deploying Acheron with Qt runtime dependencies"
        VERBATIM
    )
endif()
