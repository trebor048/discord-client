# Deploy a Windows Qt application using the windeployqt that belongs to the Qt
# kit selected at configure time.  This file runs at build time via the
# acheron_deploy target; do not use a PATH-resolved deployment tool here.

foreach(required_variable APP_EXECUTABLE DEPLOY_DIRECTORY WINDEPLOYQT_EXECUTABLE CONFIGURATION REQUIRED_RUNTIME_FILES)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "DeployQtRuntime.cmake requires -D${required_variable}=...")
    endif()
endforeach()

if(NOT EXISTS "${APP_EXECUTABLE}")
    message(FATAL_ERROR "Cannot deploy missing application executable: ${APP_EXECUTABLE}")
endif()
if(NOT EXISTS "${WINDEPLOYQT_EXECUTABLE}")
    message(FATAL_ERROR "The windeployqt selected during configuration no longer exists: ${WINDEPLOYQT_EXECUTABLE}")
endif()

file(REMOVE_RECURSE "${DEPLOY_DIRECTORY}")
file(MAKE_DIRECTORY "${DEPLOY_DIRECTORY}")
get_filename_component(app_filename "${APP_EXECUTABLE}" NAME)
get_filename_component(app_runtime_directory "${APP_EXECUTABLE}" DIRECTORY)
set(deployed_executable "${DEPLOY_DIRECTORY}/${app_filename}")
file(COPY_FILE "${APP_EXECUTABLE}" "${deployed_executable}" ONLY_IF_DIFFERENT)

# Project assets and non-Qt DLLs must already be present next to the built
# executable.  Keeping this source as the target output avoids deployment
# silently depending on an arbitrary source checkout or developer machine.
foreach(required_runtime_file IN LISTS REQUIRED_RUNTIME_FILES)
    set(runtime_source "${app_runtime_directory}/${required_runtime_file}")
    if(NOT EXISTS "${runtime_source}")
        message(FATAL_ERROR
            "Application runtime output is incomplete; required file is missing: ${runtime_source}")
    endif()

    get_filename_component(runtime_destination_directory
        "${DEPLOY_DIRECTORY}/${required_runtime_file}" DIRECTORY)
    file(MAKE_DIRECTORY "${runtime_destination_directory}")
    file(COPY_FILE "${runtime_source}"
        "${DEPLOY_DIRECTORY}/${required_runtime_file}" ONLY_IF_DIFFERENT)
endforeach()

if(CONFIGURATION STREQUAL "Debug")
    set(qt_build_type --debug)
    set(multimedia_dll Qt6Multimediad.dll)
    set(windows_plugin platforms/qwindowsd.dll)
    set(offscreen_plugin platforms/qoffscreend.dll)
else()
    set(qt_build_type --release)
    set(multimedia_dll Qt6Multimedia.dll)
    set(windows_plugin platforms/qwindows.dll)
    set(offscreen_plugin platforms/qoffscreen.dll)
endif()

# Deploy the whole plugin categories the app needs. Note that windeployqt's
# --include-plugins matches *individual* plugin names (e.g. qjpeg), while
# --add-plugin-types deploys entire plugin type folders (platforms,
# imageformats, multimedia). Using the type-based option ensures the image
# format plugins (qwebp, qjpeg, qpng, qgif) and the Qt Multimedia video backend
# (multimedia/ffmpegmediaplugin*) ship alongside the platform plugins.
execute_process(
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
        "${qt_build_type}"
        --compiler-runtime
        --no-translations
        --force
        --add-plugin-types platforms,imageformats,multimedia
        --dir "${DEPLOY_DIRECTORY}"
        "${deployed_executable}"
    RESULT_VARIABLE deploy_result
    OUTPUT_VARIABLE deploy_stdout
    ERROR_VARIABLE deploy_stderr
)
if(NOT deploy_result EQUAL 0)
    message(FATAL_ERROR "windeployqt failed (${deploy_result})\n${deploy_stdout}\n${deploy_stderr}")
endif()

foreach(required_runtime "${DEPLOY_DIRECTORY}/${multimedia_dll}"
        "${DEPLOY_DIRECTORY}/${windows_plugin}"
        "${DEPLOY_DIRECTORY}/${offscreen_plugin}")
    if(NOT EXISTS "${required_runtime}")
        message(FATAL_ERROR "Qt deployment is incomplete; required runtime was not deployed: ${required_runtime}")
    endif()
endforeach()

message(STATUS "Qt runtime deployed to ${DEPLOY_DIRECTORY}")
