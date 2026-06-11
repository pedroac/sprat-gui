cmake_minimum_required(VERSION 3.16)
project(sprat-gui LANGUAGES CXX)

if(EMSCRIPTEN)
    # Bypass the broken helper functions globally
    macro(_qt_test_emscripten_version)
    endmacro()
    macro(__qt_internal_query_emsdk_version)
    endmacro()
    macro(__qt_internal_get_emroot_path_suffix_from_emsdk_env)
    endmacro()
    macro(_qt_internal_should_include_targets)
    endmacro()
    
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    # Early find_package to initialize WASM macros
    list(APPEND CMAKE_MODULE_PATH "/opt/qt6-wasm/lib/cmake/Qt6")
    find_package(Qt6 REQUIRED COMPONENTS Core)
endif()
include(CTest)
include(FindPkgConfig)

# Reconfigure automatically when DEPENDENCIES changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/DEPENDENCIES"
)


# === Project Version ===
set(SPRAT_GUI_VERSION "v0.0.0")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VERSION_CONTENT)
    string(STRIP "${VERSION_CONTENT}" SPRAT_GUI_VERSION)
endif()
add_definitions(-DSPRAT_GUI_VERSION="${SPRAT_GUI_VERSION}")

# === CLI Dependency ===
set(SPRAT_CLI_VERSION "v0.0.0")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/DEPENDENCIES")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/DEPENDENCIES" DEPENDENCIES_CONTENT)
    string(STRIP "${DEPENDENCIES_CONTENT}" SPRAT_CLI_VERSION)
endif()
add_definitions(-DSPRAT_CLI_VERSION="${SPRAT_CLI_VERSION}")

# === Configuration ===
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DEFAULT_SPRAT_EMBEDDED_CLI OFF)
if(EMSCRIPTEN OR WIN32)
    set(DEFAULT_SPRAT_EMBEDDED_CLI ON)
endif()

option(SPRAT_EMBEDDED_CLI "Link CLI tools directly into the GUI (required for WASM)" ${DEFAULT_SPRAT_EMBEDDED_CLI})
option(SPRAT_WASM_DEBUG "Enable extra Emscripten debug flags for WASM builds" OFF)
option(SPRAT_WASM_JSPI "Enable JSPI (disable Asyncify) for WASM builds" ON)
if(EMSCRIPTEN)
    set(SPRAT_EMBEDDED_CLI ON)
endif()

# === LibArchive (needed before embedded CLI configure) ===
# 1. Try to find LibArchive in a specific 'cli' folder (sibling or subfolder)
set(LIBARCHIVE_SEARCH_PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}/cli"
    "${CMAKE_CURRENT_SOURCE_DIR}/../cli"
    "${CMAKE_CURRENT_SOURCE_DIR}/../sprat-cli/build"
    "${CMAKE_CURRENT_SOURCE_DIR}/../sprat-cli"
)

find_path(LIBARCHIVE_INCLUDE_DIR NAMES archive.h
          HINTS ${LIBARCHIVE_SEARCH_PATHS}
          PATH_SUFFIXES include libarchive)

find_library(LIBARCHIVE_LIBRARY NAMES archive libarchive
             HINTS ${LIBARCHIVE_SEARCH_PATHS}
             PATH_SUFFIXES lib bin)

if(LIBARCHIVE_INCLUDE_DIR AND LIBARCHIVE_LIBRARY)
    if(NOT TARGET LibArchive::LibArchive)
        add_library(LibArchive::LibArchive UNKNOWN IMPORTED)
        set_target_properties(LibArchive::LibArchive PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBARCHIVE_INCLUDE_DIR}"
            IMPORTED_LOCATION "${LIBARCHIVE_LIBRARY}"
        )
    endif()
    set(LibArchive_FOUND TRUE)
    message(STATUS "Found LibArchive in local or sibling directory: ${LIBARCHIVE_LIBRARY}")
else()
    # 2. Fallback to standard system search
    find_package(LibArchive QUIET)
endif()

if(NOT LibArchive_FOUND)
    message(STATUS "LibArchive not found, using FetchContent to download it...")
    include(FetchContent)
    
    if(EMSCRIPTEN)
        # Force a local zlib build so libarchive has headers/libs available.
        set(ZLIB_FOUND FALSE)
    else()
        find_package(ZLIB QUIET)
    endif()

    if(ZLIB_FOUND)
        set(ZLIB_LIBRARY ZLIB::ZLIB CACHE STRING "" FORCE)
        set(ZLIB_LIBRARIES ZLIB::ZLIB CACHE STRING "" FORCE)
    else()
        message(STATUS "ZLIB not found, downloading it via FetchContent...")
        FetchContent_Declare(
            zlib
            URL https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz
            URL_HASH SHA256=17e88863f3600672ab49182f217281b6fc4d3c762bde361935e436a95214d05c
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(zlib)
        set(ZLIB_FOUND TRUE CACHE BOOL "" FORCE)
        set(ZLIB_INCLUDE_DIR ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR} CACHE PATH "" FORCE)
        if(TARGET zlibstatic)
            set(ZLIB_LIBRARY zlibstatic CACHE STRING "" FORCE)
            set(ZLIB_LIBRARIES zlibstatic CACHE STRING "" FORCE)
        elseif(TARGET zlib)
            set(ZLIB_LIBRARY zlib CACHE STRING "" FORCE)
            set(ZLIB_LIBRARIES zlib CACHE STRING "" FORCE)
        else()
            set(ZLIB_LIBRARY ${zlib_BINARY_DIR}/libz.a CACHE FILEPATH "" FORCE)
            set(ZLIB_LIBRARIES ${zlib_BINARY_DIR}/libz.a CACHE STRING "" FORCE)
        endif()
    endif()

    FetchContent_Declare(
        libarchive
        URL https://github.com/libarchive/libarchive/releases/download/v3.8.5/libarchive-3.8.5.tar.gz
        URL_HASH SHA256=8a60f3a7bfd59c54ce82ae805a93dba65defd04148c3333b7eaa2102f03b7ffd
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(ENABLE_TEST OFF CACHE BOOL "" FORCE)
    set(ENABLE_TAR OFF CACHE BOOL "" FORCE)
    set(ENABLE_CPIO OFF CACHE BOOL "" FORCE)
    set(ENABLE_CAT OFF CACHE BOOL "" FORCE)
    if(EMSCRIPTEN)
        # Avoid building libarchive tools/shared libs in wasm (saves time and avoids emcc cache lock issues)
        set(ENABLE_UNZIP OFF CACHE BOOL "" FORCE)
        set(ENABLE_UNZIP_SHARED OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    endif()
    set(ENABLE_ACL OFF CACHE BOOL "" FORCE)
    set(ENABLE_XATTR OFF CACHE BOOL "" FORCE)
    set(ENABLE_ICONV OFF CACHE BOOL "" FORCE)
    
    set(CMAKE_DISABLE_FIND_PACKAGE_OpenSSL ON CACHE BOOL "" FORCE)
    set(ENABLE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(ENABLE_CRYPTO OFF CACHE BOOL "" FORCE)
    
    set(ENABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(ENABLE_BZIP2 OFF CACHE BOOL "" FORCE)
    set(ENABLE_LIBXML2 OFF CACHE BOOL "" FORCE)
    set(ENABLE_EXPAT OFF CACHE BOOL "" FORCE)
    
    if(WIN32)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    endif()
    FetchContent_MakeAvailable(libarchive)
    set(LibArchive_FOUND TRUE)
    set(LIBARCHIVE_INCLUDE_DIRS ${libarchive_SOURCE_DIR}/libarchive)
    if(TARGET archive_static)
        set(LIBARCHIVE_LIBRARY archive_static CACHE INTERNAL "")
        add_library(LibArchive::LibArchive ALIAS archive_static)
        target_include_directories(archive_static INTERFACE ${libarchive_SOURCE_DIR}/libarchive)
        if(WIN32)
            target_compile_definitions(archive_static INTERFACE LIBARCHIVE_STATIC)
        endif()
        if(TARGET zlibstatic OR TARGET zlib)
             target_link_libraries(archive_static ${ZLIB_LIBRARY})
        endif()
    elseif(TARGET archive)
        add_library(LibArchive::LibArchive ALIAS archive)
    endif()
endif()

if(SPRAT_EMBEDDED_CLI)
    add_definitions(-DSPRAT_EMBEDDED_CLI)
    set(SPRAT_DOWNLOAD_STB ON CACHE BOOL "Download STB headers for sprat-cli" FORCE)
    
    # Locate sprat-cli source
    set(SPRAT_CLI_LOCAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sprat-cli")
    
    if(EXISTS "${SPRAT_CLI_LOCAL_DIR}/CMakeLists.txt")
        message(STATUS "Using local sprat-cli at ${SPRAT_CLI_LOCAL_DIR}")
        set(SPRAT_CLI_SOURCE_DIR "${SPRAT_CLI_LOCAL_DIR}")
        # Check if already added (unlikely but safe)
        if(NOT TARGET spratcore)
            add_subdirectory("${SPRAT_CLI_SOURCE_DIR}" "sprat-cli-build")
        endif()
    else()
        message(STATUS "sprat-cli source not found locally, fetching from GitHub...")
        include(FetchContent)
        FetchContent_Declare(
            sprat_cli
            GIT_REPOSITORY https://github.com/pedroac/sprat-cli.git
            GIT_TAG ${SPRAT_CLI_VERSION}
        )
        FetchContent_MakeAvailable(sprat_cli)
        set(SPRAT_CLI_SOURCE_DIR "${sprat_cli_SOURCE_DIR}")
    endif()

    include_directories("${SPRAT_CLI_SOURCE_DIR}/src")
endif()

# === Qt6 Configuration ===
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# === Find Dependencies ===
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Network)
find_package(Qt6 QUIET COMPONENTS LinguistTools)

# === Source Files ===
set(PROJECT_SOURCES
    src/App/main.cpp
    src/App/MainWindow/MainWindow.cpp
    src/App/MainWindow/MainWindow.Ui.cpp
    src/App/MainWindow/MainWindow.Project.cpp
    src/App/MainWindow/MainWindow.Animation.cpp
    src/App/MainWindow/MainWindow.Cli.cpp
    src/App/MainWindow/MainWindow.Events.cpp
    src/App/MainWindow/MainWindow.LayoutFlow.cpp
    src/App/MainWindow/MainWindow.Marker.cpp
    src/App/MainWindow/MainWindow.TimelineEditor.cpp
    src/App/MainWindow/MainWindow.ImageLoading.cpp
    src/App/MainWindow/MainWindow.Navigator.cpp
    src/App/MainWindow/NavigatorTreeWidget.h
    src/App/MainWindow/NavigatorTreeWidget.cpp
    src/App/MainWindow/TimelineTreeWidget.h
    src/App/MainWindow/TimelineTreeWidget.cpp
    src/App/MainWindow/MainWindow.h
    src/App/MainWindow/LayoutOrchestrator.h
    src/App/MainWindow/LayoutOrchestrator.cpp
    src/App/MainWindow/ProjectController.h
    src/App/MainWindow/ProjectController.cpp
    src/App/WasmFileDialog.cpp
    src/App/WasmFileDialog.h
    src/App/WasmFolderBrowserDialog.cpp
    src/App/WasmFolderBrowserDialog.h
    src/App/MainWindow/FrameDetectionDialog.cpp
    src/App/MainWindow/FrameDetectionDialog.h
    src/App/MainWindow/FrameDetectionCanvas.cpp
    src/App/MainWindow/FrameDetectionCanvas.h
    src/SpriteSheetLayout/LayoutCanvas.cpp
    src/SpriteSheetLayout/LayoutCanvas.h
    src/SpriteSheetLayout/SpriteItem.cpp
    src/SpriteSheetLayout/SpriteItem.h
    src/SelectedSpriteFrame/PreviewCanvas.cpp
    src/SelectedSpriteFrame/PreviewCanvas.h
    src/SelectedSpriteFrame/EditorOverlayItem.cpp
    src/SelectedSpriteFrame/EditorOverlayItem.h
    src/Animation/Timelines/TimelineListWidget.cpp
    src/Animation/Timelines/TimelineListWidget.h
    src/CLITools/CliToolInstaller.cpp
    src/CLITools/CliToolInstaller.h
    src/CLITools/CliSetupController.cpp
    src/CLITools/CliSetupController.h
    src/CLITools/CliToolsUi.cpp
    src/CLITools/CliToolsUi.h
    src/Animation/AnimationManager.cpp
    src/Animation/AnimationManager.h
    src/CLITools/CliToolsConfig.cpp
    src/CLITools/SpratCliLocator.cpp
    src/CLITools/CliToolsConfig.h
    src/CLITools/LayoutRunner.cpp
    src/CLITools/LayoutRunner.h
    src/CLITools/SourceFolderWatcher.cpp
    src/CLITools/SourceFolderWatcher.h
    src/CLITools/FolderSyncService.cpp
    src/CLITools/FolderSyncService.h
    src/SelectedSpriteFrame/Markers/MarkersDialog.cpp
    src/SelectedSpriteFrame/Markers/MarkersDialog.h
    src/App/MainWindow/IAtlasViewport.h
    src/App/MainWindow/PackedAtlasView.cpp
    src/App/MainWindow/PackedAtlasView.h
    src/App/MainWindow/ExportWorkspace.cpp
    src/App/MainWindow/ExportWorkspace.h
    src/App/MainWindow/AtlasesManagementWorkspace.cpp
    src/App/MainWindow/AtlasesManagementWorkspace.h
    src/App/MainWindow/NavigatorPanel.h
    src/App/MainWindow/NavigatorPanel.cpp
    src/App/MainWindow/FrameAnimationWorkspace.h
    src/App/MainWindow/FrameAnimationWorkspace.cpp
    src/App/MainWindow/SpriteEditorPanel.h
    src/App/MainWindow/SpriteEditorPanel.cpp
    src/App/MainWindow/AnimationPreviewPanel.h
    src/App/MainWindow/AnimationPreviewPanel.cpp
    src/App/MainWindow/TimelineEditorPanel.h
    src/App/MainWindow/TimelineEditorPanel.cpp
    src/Settings/SettingsDialog.cpp
    src/Settings/SettingsDialog.h
    src/SpriteSheetLayout/LayoutParser.cpp
    src/SpriteSheetLayout/LayoutParser.h
    src/Project/ProjectPayloadCodec.cpp
    src/Project/ProjectPayloadCodec.h
    src/Project/AutosaveProjectStore.cpp
    src/Project/AutosaveProjectStore.h
    src/Project/ProjectSession.cpp
    src/Project/ProjectSession.h

    src/Animation/Timelines/AnimationTimelineOps.cpp
    src/Animation/Timelines/AnimationTimelineOps.h
    src/Animation/Test/AnimationTestOps.cpp
    src/Animation/Test/AnimationTestOps.h
    src/Animation/AnimationPlaybackService.cpp
    src/Animation/AnimationPlaybackService.h
    src/Animation/AnimationPreviewService.cpp
    src/Animation/AnimationPreviewService.h
    src/Animation/AnimationCanvas.cpp
    src/Animation/AnimationCanvas.h
    src/Animation/AnimationExportService.cpp
    src/Animation/AnimationExportService.h
    src/Project/ProjectFileLoader.cpp
    src/Project/ProjectFileLoader.h
    src/Animation/Timelines/TimelineGenerationService.cpp
    src/Animation/Timelines/TimelineGenerationService.h
    src/Animation/Timelines/TimelineUi.cpp
    src/Animation/Timelines/TimelineUi.h
    src/Project/ProjectSaveService.cpp
    src/Project/ProjectSaveService.h
    src/Project/ImageDiscoveryService.cpp
    src/Project/ImageDiscoveryService.h
    src/Project/ImageFolderSelectionDialog.cpp
    src/Project/ImageFolderSelectionDialog.h
    src/SelectedSpriteFrame/SpriteSelectionPresenter.cpp
    src/SelectedSpriteFrame/SpriteSelectionPresenter.h
    src/Settings/SettingsCoordinator.cpp
    src/Settings/SettingsCoordinator.h
    src/App/MainWindow/MainWindowUiState.cpp
    src/App/MainWindow/MainWindowUiState.h
    src/Profiles/SpratProfilesConfig.cpp
    src/Profiles/SpratProfilesConfig.h
    src/Profiles/ResolutionsConfig.cpp
    src/Profiles/ResolutionsConfig.h
    src/Profiles/ProfilesDialog.cpp
    src/Profiles/ProfilesDialog.h
    src/Animation/Timelines/TimelineBuilder.cpp
    src/Animation/Timelines/TimelineBuilder.h
    src/App/MainWindow/ElidedLabel.h
    src/Core/MessageDialog.cpp
    src/Core/MessageDialog.h
    src/Core/SpriteTreeUtils.cpp
    src/Core/SpriteTreeUtils.h
    src/Core/ZoomableGraphicsView.cpp
    src/Core/ZoomableGraphicsView.h
    src/Core/ArchiveExtractor.cpp
    src/Core/ArchiveExtractor.h
    src/Core/models.h
    src/Core/ViewUtils.cpp
    src/Core/WasmResizeDebounce.cpp
    src/Core/WasmResizeDebounce.h
)

if(SPRAT_EMBEDDED_CLI)
    list(APPEND PROJECT_SOURCES
        src/CLITools/EmbeddedCli.cpp
        src/CLITools/EmbeddedCli.h
    )
endif()

# === Executable ===
if(WIN32)
    qt_add_executable(sprat-gui WIN32 ${PROJECT_SOURCES})
elseif(APPLE)
    qt_add_executable(sprat-gui MACOSX_BUNDLE ${PROJECT_SOURCES})
else()
    qt_add_executable(sprat-gui ${PROJECT_SOURCES})
endif()

if(EMSCRIPTEN)
    foreach(preload_file ${WASM_PRELOAD_FILES})
        target_link_options(sprat-gui PRIVATE "--preload-file" "${preload_file}")
    endforeach()
endif()

# === Include Directories ===
target_include_directories(sprat-gui PRIVATE
    src
    src/App
    src/App/MainWindow
    src/Animation
    src/Animation/Timelines
    src/Animation/Test
    src/SpriteSheetLayout
    src/SelectedSpriteFrame
    src/SelectedSpriteFrame/Markers
    src/CLITools
    src/Profiles
    src/Project
    src/Settings
    src/Core
)

# === Link Libraries ===
target_link_libraries(sprat-gui PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Network
    LibArchive::LibArchive
)

# Copy sprat-cli assets (config and transforms) for host builds
if(NOT EMSCRIPTEN)
    if(SPRAT_CLI_SOURCE_DIR AND EXISTS "${SPRAT_CLI_SOURCE_DIR}/spratprofiles.cfg")
        set(SPRAT_ASSETS_DEST "$<TARGET_FILE_DIR:sprat-gui>")
        add_custom_command(TARGET sprat-gui POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SPRAT_CLI_SOURCE_DIR}/spratprofiles.cfg"
            "${SPRAT_ASSETS_DEST}/spratprofiles.cfg"
            COMMENT "Copying spratprofiles.cfg..."
        )
        if(EXISTS "${SPRAT_CLI_SOURCE_DIR}/transforms")
            add_custom_command(TARGET sprat-gui POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${SPRAT_CLI_SOURCE_DIR}/transforms"
                "${SPRAT_ASSETS_DEST}/transforms"
                COMMENT "Copying transforms directory..."
            )
        endif()
    else()
        message(STATUS "sprat-cli assets not found; skipping spratprofiles.cfg/transforms copy")
    endif()
endif()

if(EMSCRIPTEN)
    target_compile_options(sprat-gui PRIVATE "SHELL:--bind")
    target_link_options(sprat-gui PRIVATE
        "SHELL:--bind"
        "-sALLOW_MEMORY_GROWTH=1"
        "SHELL:--preload-file ${SPRAT_CLI_SOURCE_DIR}/spratprofiles.cfg@/spratprofiles.cfg"
        "SHELL:--preload-file ${SPRAT_CLI_SOURCE_DIR}/transforms@/transforms"
        "SHELL:--js-library=${CMAKE_CURRENT_SOURCE_DIR}/wasm/mktime_fix.js"
    )
    if(SPRAT_WASM_JSPI)
        target_link_options(sprat-gui PRIVATE "SHELL:-sJSPI")
        set_property(TARGET sprat-gui PROPERTY QT_WASM_EXTRA_EXPORTED_METHODS "ccall,cwrap")
    else()
        target_link_options(sprat-gui PRIVATE
            "-sASYNCIFY"
            "-sASYNCIFY_STACK_SIZE=2097152"
            "-sASSERTIONS=1"
            "-sASYNCIFY_IMPORTS=[invoke_iii,invoke_vii,invoke_iiii,invoke_diiiii]"
            "-sINITIAL_MEMORY=536870912"
        )
        set_property(TARGET sprat-gui PROPERTY QT_WASM_EXTRA_EXPORTED_METHODS "ccall,cwrap,Asyncify")
    endif()
    target_link_options(sprat-gui PRIVATE "SHELL:-sEXPORTED_FUNCTIONS=[_main,_jsIsAsyncBusy,_jsHaveAsyncify,_jsHaveJspi,_sprat_on_file_picked]")

    # Explicitly include embind/emval JS libraries (some toolchains don't auto-add them).
    get_filename_component(EMSCRIPTEN_ROOT_PATH "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(EMSCRIPTEN_JS_LIB_DIR "${EMSCRIPTEN_ROOT_PATH}/src/lib")
    target_link_options(sprat-gui PRIVATE
        "SHELL:--js-library=${EMSCRIPTEN_JS_LIB_DIR}/libemval.js"
        "SHELL:--js-library=${EMSCRIPTEN_JS_LIB_DIR}/libembind.js"
    )

    if(SPRAT_WASM_DEBUG)
        target_compile_options(sprat-gui PRIVATE "-g")
        target_link_options(sprat-gui PRIVATE
            "-sASSERTIONS=2"
            "-sSTACK_OVERFLOW_CHECK=2"
            "-sSAFE_HEAP=1"
            "-sNO_DISABLE_EXCEPTION_CATCHING=1"
            "-sEXIT_RUNTIME=0"
        )
    endif()

    add_custom_command(TARGET sprat-gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rename "$<TARGET_FILE_DIR:sprat-gui>/sprat-gui.html" "$<TARGET_FILE_DIR:sprat-gui>/index.html"
        COMMENT "Renaming sprat-gui.html to index.html"
    )
endif()

if(SPRAT_EMBEDDED_CLI)
    target_link_libraries(sprat-gui PRIVATE spratcore)
endif()


# === Translations ===
set(TS_FILES
    i18n/sprat-gui_en.ts
)

if(Qt6LinguistTools_FOUND)
    qt_add_translations(sprat-gui
        TS_FILES ${TS_FILES}
    )
else()
    message(STATUS "Qt6 LinguistTools not found: skipping automatic .ts/.qm generation.")
endif()

# === Testing ===

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(sprat-gui-tests
        tests/TestMain.cpp
        tests/LayoutTests.cpp
        tests/AnimationTests.cpp
        tests/ProjectTests.cpp
        tests/CoreTests.cpp
        tests/ImageDiscoveryTests.cpp
        tests/ProjectSessionTests.cpp
        src/Animation/AnimationPreviewService.cpp
        src/Animation/Timelines/TimelineBuilder.cpp
        src/Animation/Timelines/TimelineGenerationService.cpp
        src/Project/ProjectPayloadCodec.cpp
        src/Project/ProjectFileLoader.cpp
        src/Project/AutosaveProjectStore.cpp
        src/Project/ProjectSession.cpp
        src/Project/ImageDiscoveryService.cpp
        src/SpriteSheetLayout/LayoutParser.cpp
        src/Core/ArchiveExtractor.cpp
    )

    target_include_directories(sprat-gui-tests PRIVATE
        tests
        src
        src/Animation
        src/Animation/Timelines
        src/Project
        src/Core
        src/SpriteSheetLayout
    )

    target_link_libraries(sprat-gui-tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        Qt6::Test
        LibArchive::LibArchive
    )

    add_test(NAME sprat-gui-tests COMMAND sprat-gui-tests)
    set_tests_properties(sprat-gui-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# === Windows Deployment & Launcher ===
if(WIN32)
    # Move the GUI and tests into a 'bin' subdirectory to share DLLs
    # and keep the root directory clean.
    set_target_properties(sprat-gui PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(TARGET sprat-gui-tests)
        set_target_properties(sprat-gui-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    endif()

    # Generate a batch file in the root to launch the GUI
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/sprat.bat"
         CONTENT "@echo off\nstart \"\" \"%~dp0bin\\sprat-gui.exe\" %*\n")

    # Run windeployqt to copy Qt DLLs
    get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET sprat-gui POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --dir "$<TARGET_FILE_DIR:sprat-gui>"
            --no-opengl-sw
            --no-system-d3d-compiler
            --no-compiler-runtime
            "$<TARGET_FILE:sprat-gui>"
            COMMENT "Running windeployqt..."
        )
    endif()

    # Still copy necessary DLLs from search paths to the 'bin' folder
    # so the build is immediately runnable.
    foreach(search_path ${LIBARCHIVE_SEARCH_PATHS})
        if(EXISTS "${search_path}" AND IS_DIRECTORY "${search_path}")
            file(GLOB_RECURSE FOUND_DLLS "${search_path}/*.dll")
            foreach(dll ${FOUND_DLLS})
                add_custom_command(TARGET sprat-gui POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${dll}"
                    $<TARGET_FILE_DIR:sprat-gui>
                    COMMENT "Copying runtime dependency ${dll}"
                )
            endforeach()
        endif()
    endforeach()
endif()
