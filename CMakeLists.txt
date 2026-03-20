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
    
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    # Early find_package to initialize WASM macros
    list(APPEND CMAKE_MODULE_PATH "/opt/qt6-wasm/lib/cmake/Qt6")
    find_package(Qt6 REQUIRED COMPONENTS Core)
endif()
include(CTest)
include(FindPkgConfig)


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

option(SPRAT_EMBEDDED_CLI "Link CLI tools directly into the GUI (required for WASM)" OFF)
option(SPRAT_WASM_DEBUG "Enable extra Emscripten debug flags for WASM builds" OFF)
option(SPRAT_WASM_JSPI "Enable JSPI (disable Asyncify) for WASM builds" ON)
if(EMSCRIPTEN)
    set(SPRAT_EMBEDDED_CLI ON)
endif()

if(SPRAT_EMBEDDED_CLI)
    add_definitions(-DSPRAT_EMBEDDED_CLI)
    set(SPRAT_CLI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sprat-cli")
    include_directories("${SPRAT_CLI_SOURCE_DIR}/src")
endif()

# === Qt6 Configuration ===
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# === Find Dependencies ===
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Network)
find_package(Qt6 QUIET COMPONENTS LinguistTools)

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
    src/App/MainWindow/MainWindow.h
    src/App/WasmFileDialog.cpp
    src/App/WasmFileDialog.h
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
    src/CLITools/CliToolsUi.cpp
    src/CLITools/CliToolsUi.h
    src/Animation/AnimationManager.cpp
    src/Animation/AnimationManager.h
    src/CLITools/CliToolsConfig.cpp
    src/CLITools/SpratCliLocator.cpp
    src/CLITools/CliToolsConfig.h
    src/CLITools/LayoutRunner.cpp
    src/CLITools/LayoutRunner.h
    src/SelectedSpriteFrame/Markers/MarkersDialog.cpp
    src/SelectedSpriteFrame/Markers/MarkersDialog.h
    src/Project/SaveDialog.cpp
    src/Project/SaveDialog.h
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
    src/Core/ZoomableGraphicsView.cpp
    src/Core/ZoomableGraphicsView.h
    src/Core/ArchiveExtractor.cpp
    src/Core/ArchiveExtractor.h
    src/Core/models.h
    src/Core/ViewUtils.cpp
)

if(SPRAT_EMBEDDED_CLI)
    list(APPEND PROJECT_SOURCES
        src/CLITools/EmbeddedCli.cpp
        src/CLITools/EmbeddedCli.h
    )
    add_subdirectory("${SPRAT_CLI_SOURCE_DIR}" "sprat-cli-build")
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

if(EMSCRIPTEN)
    target_compile_options(sprat-gui PRIVATE "SHELL:--bind")
    target_link_options(sprat-gui PRIVATE
        "SHELL:--bind"
        "-sALLOW_MEMORY_GROWTH=1"
        "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/../sprat-cli/spratprofiles.cfg@/spratprofiles.cfg"
        "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/../sprat-cli/transforms@/transforms"
        "SHELL:--js-library=${CMAKE_CURRENT_SOURCE_DIR}/wasm/mktime_fix.js"
    )
    if(SPRAT_WASM_JSPI)
        target_link_options(sprat-gui PRIVATE "SHELL:-sJSPI")
    else()
        target_link_options(sprat-gui PRIVATE
            "-sASYNCIFY"
            "-sASYNCIFY_STACK_SIZE=128000"
            "-sASSERTIONS=1"
            "-sASYNCIFY_IMPORTS=[invoke_iii,invoke_vii,invoke_iiii,invoke_diiiii]"
        )
    endif()
    # Let Qt's wasm macros append these to its default exported methods.
    # NOTE: This must be a single comma-separated string; a CMake list would be split into extra args.
    set_property(TARGET sprat-gui PROPERTY QT_WASM_EXTRA_EXPORTED_METHODS "ccall,cwrap,Asyncify")
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
