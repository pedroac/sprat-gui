cmake_minimum_required(VERSION 3.16)
project(sprat-gui LANGUAGES CXX)
include(CTest)

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

# === Qt6 Configuration ===
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# === Find Dependencies ===
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Network)
find_package(Qt6 QUIET COMPONENTS LinguistTools)

# 1. Try to find LibArchive in a specific 'cli' folder (sibling or subfolder)
# We check common development and deployment locations.
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
    
    # Try to find ZLIB as libarchive depends on it for ZIP decompression (deflate)
    find_package(ZLIB QUIET)
    if(NOT ZLIB_FOUND)
        message(STATUS "ZLIB not found, downloading it via FetchContent...")
        FetchContent_Declare(
            zlib
            URL https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz
            URL_HASH SHA256=17e88863f3600672ab49182f217281b6fc4d3c762bde361935e436a95214d05c
        )
        FetchContent_MakeAvailable(zlib)
        set(ZLIB_INCLUDE_DIR ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR} CACHE PATH "" FORCE)
        if(TARGET zlibstatic)
            set(ZLIB_LIBRARY zlibstatic CACHE STRING "" FORCE)
            set(ZLIB_LIBRARIES zlibstatic CACHE STRING "" FORCE)
        elseif(TARGET zlib)
            set(ZLIB_LIBRARY zlib CACHE STRING "" FORCE)
            set(ZLIB_LIBRARIES zlib CACHE STRING "" FORCE)
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
    set(ENABLE_ACL OFF CACHE BOOL "" FORCE)
    set(ENABLE_XATTR OFF CACHE BOOL "" FORCE)
    set(ENABLE_ICONV OFF CACHE BOOL "" FORCE)
    
    # Aggressively disable crypto and optional dependencies to avoid DLL hell on Wine/Windows
    set(CMAKE_DISABLE_FIND_PACKAGE_OpenSSL ON CACHE BOOL "" FORCE)
    set(ENABLE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(ENABLE_CRYPTO OFF CACHE BOOL "" FORCE)
    set(ENABLE_CNG OFF CACHE BOOL "" FORCE)
    set(ENABLE_MBEDTLS OFF CACHE BOOL "" FORCE)
    set(ENABLE_NETTLE OFF CACHE BOOL "" FORCE)
    set(ENABLE_LIBB2 OFF CACHE BOOL "" FORCE)
    
    set(ENABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(ENABLE_BZIP2 OFF CACHE BOOL "" FORCE)
    set(ENABLE_LIBXML2 OFF CACHE BOOL "" FORCE)
    set(ENABLE_EXPAT OFF CACHE BOOL "" FORCE)
    set(ENABLE_LZ4 OFF CACHE BOOL "" FORCE)
    set(ENABLE_LZMA OFF CACHE BOOL "" FORCE)
    set(ENABLE_LZO OFF CACHE BOOL "" FORCE)
    set(ENABLE_ZSTD OFF CACHE BOOL "" FORCE)
    
    set(ENABLE_TAR ON CACHE BOOL "" FORCE) # Support tar.gz/tar.bz2 etc.
    
    if(WIN32)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    endif()
    FetchContent_MakeAvailable(libarchive)
    if(TARGET archive_static)
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
)

# === Executable ===
if(WIN32)
    add_executable(sprat-gui WIN32 ${PROJECT_SOURCES})
elseif(APPLE)
    add_executable(sprat-gui MACOSX_BUNDLE ${PROJECT_SOURCES})
else()
    add_executable(sprat-gui ${PROJECT_SOURCES})
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
set(BUILD_TESTING ON CACHE BOOL "Enable testing" FORCE)

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
