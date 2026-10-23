cmake_minimum_required(VERSION 3.16)

# Qt 6.5 supports macOS 11 and later. Without a deployment target, binaries only run on the
# build machine's macOS version or newer. It must be set before project().
if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET AND NOT DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version to run on")
endif()

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
# Read the version from DEPENDENCIES first, then allow the cache to override.
set(_SPRAT_CLI_DEPS_VERSION "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/DEPENDENCIES")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/DEPENDENCIES" DEPENDENCIES_CONTENT)
    string(STRIP "${DEPENDENCIES_CONTENT}" _SPRAT_CLI_DEPS_VERSION)
endif()
if(NOT _SPRAT_CLI_DEPS_VERSION)
    set(_SPRAT_CLI_DEPS_VERSION "v0.0.0")
endif()
set(SPRAT_CLI_VERSION "${_SPRAT_CLI_DEPS_VERSION}" CACHE STRING "sprat-cli git tag to embed; reads DEPENDENCIES file if empty" FORCE)
add_definitions(-DSPRAT_CLI_VERSION="${SPRAT_CLI_VERSION}")

# === Configuration ===
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# === MSVC Compatibility ===
# Fix Qt 6.5.3 compatibility with newer MSVC versions (stdext namespace issue)
if(MSVC)
    # Add stdext namespace compatibility for MSVC
    # These defines help Qt headers compile with newer MSVC versions
    add_compile_definitions(
        _HAS_STDEXT=1
        _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
    )

    # Use C++latest to get all features including stdext
    add_compile_options(/std:c++latest)

    # Add these compatibility flags
    if(MSVC_VERSION GREATER_EQUAL 1920)
        # VS 2019 and later
        add_compile_definitions(_ENABLE_EXTENDED_ALIGNED_STORAGE)
    endif()

    # Suppress specific warnings
    add_compile_options(/wd4616 /wd4996)


endif()

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
    # None of the archive types the app opens need these. libarchive would otherwise link
    # whatever copy pkg-config finds (on macOS, Homebrew's, built for the build machine only).
    set(ENABLE_ZSTD OFF CACHE BOOL "" FORCE)
    set(ENABLE_LZ4 OFF CACHE BOOL "" FORCE)
    set(ENABLE_LIBB2 OFF CACHE BOOL "" FORCE)
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

# The end-to-end tests run exports through sprat-cli: linked in when the CLI is embedded,
# otherwise through executables built from the same sprat-cli source.
option(SPRAT_E2E_TESTS "Build the end-to-end export tests (builds sprat-cli when it is not embedded)" ON)
set(_sprat_build_cli ${SPRAT_EMBEDDED_CLI})
if(BUILD_TESTING AND SPRAT_E2E_TESTS AND NOT EMSCRIPTEN)
    set(_sprat_build_cli ON)
endif()

# The macOS app bundle carries sprat-cli's tools in Contents/Resources/cli. They are built
# here from the same source, so they match the app's architectures and minimum macOS.
set(_sprat_bundle_cli OFF)
if(APPLE AND NOT SPRAT_EMBEDDED_CLI)
    option(SPRAT_BUNDLE_CLI "Build sprat-cli's tools into the macOS app bundle" ON)
    set(_sprat_bundle_cli ${SPRAT_BUNDLE_CLI})
endif()
if(_sprat_bundle_cli)
    set(_sprat_build_cli ON)
endif()

if(_sprat_build_cli)
    if(SPRAT_EMBEDDED_CLI)
        add_definitions(-DSPRAT_EMBEDDED_CLI)
    endif()
    set(SPRAT_DOWNLOAD_STB ON CACHE BOOL "Download STB headers for sprat-cli" FORCE)

    # Force C++20 for sprat-cli
    set(CMAKE_CXX_STANDARD 20 CACHE STRING "" FORCE)

    # Locate sprat-cli source
    set(SPRAT_CLI_LOCAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sprat-cli")
    set(_sprat_use_local FALSE)

    if(EXISTS "${SPRAT_CLI_LOCAL_DIR}/CMakeLists.txt")
        # Only use the local directory if its VERSION matches DEPENDENCIES.
        # A mismatch means the local checkout is out of date; fall back to
        # FetchContent so the correct version is always embedded.
        set(_local_cli_version "")
        if(EXISTS "${SPRAT_CLI_LOCAL_DIR}/VERSION")
            file(READ "${SPRAT_CLI_LOCAL_DIR}/VERSION" _local_cli_version)
            string(STRIP "${_local_cli_version}" _local_cli_version)
        endif()
        if(_local_cli_version STREQUAL SPRAT_CLI_VERSION)
            set(_sprat_use_local TRUE)
            message(STATUS "Using local sprat-cli ${_local_cli_version} at ${SPRAT_CLI_LOCAL_DIR}")
        else()
            message(WARNING
                "Local sprat-cli version '${_local_cli_version}' does not match "
                "required version '${SPRAT_CLI_VERSION}' (from DEPENDENCIES). "
                "Falling back to FetchContent.")
        endif()
    endif()

    # sprat-cli's own test suite needs tools such as ImageMagick and is covered by its CI;
    # keep it out of this build's ctest run.
    set(_sprat_gui_build_testing ${BUILD_TESTING})
    set(BUILD_TESTING OFF)

    if(_sprat_use_local)
        set(SPRAT_CLI_SOURCE_DIR "${SPRAT_CLI_LOCAL_DIR}")
        if(NOT TARGET spratcore)
            add_subdirectory("${SPRAT_CLI_SOURCE_DIR}" "sprat-cli-build")
        endif()
    else()
        message(STATUS "Fetching sprat-cli ${SPRAT_CLI_VERSION} from GitHub...")
        include(FetchContent)
        FetchContent_Declare(
            sprat_cli
            GIT_REPOSITORY https://github.com/pedroac/sprat-cli.git
            GIT_TAG ${SPRAT_CLI_VERSION}
            GIT_SUBMODULES ""
        )
        FetchContent_MakeAvailable(sprat_cli)
        set(SPRAT_CLI_SOURCE_DIR "${sprat_cli_SOURCE_DIR}")
    endif()
    set(BUILD_TESTING ${_sprat_gui_build_testing})

    if(SPRAT_EMBEDDED_CLI)
        include_directories("${SPRAT_CLI_SOURCE_DIR}/src")

        # Since sprat-cli v0.19, run_spratpack lives in the spratpack executable rather than
        # spratcore, because its optional codecs are configured on that target. Build the
        # command into a library that mirrors spratpack's settings so the same codecs are used.
        add_library(spratpackcommand STATIC
            "${SPRAT_CLI_SOURCE_DIR}/src/commands/spratpack_command.cpp")
        target_compile_definitions(spratpackcommand PRIVATE $<TARGET_PROPERTY:spratpack,COMPILE_DEFINITIONS>)
        target_compile_options(spratpackcommand PRIVATE $<TARGET_PROPERTY:spratpack,COMPILE_OPTIONS>)
        target_include_directories(spratpackcommand PRIVATE $<TARGET_PROPERTY:spratpack,INCLUDE_DIRECTORIES>)
        # PUBLIC so a static library's link dependencies reach sprat-gui's link line.
        target_link_libraries(spratpackcommand PUBLIC $<TARGET_PROPERTY:spratpack,LINK_LIBRARIES>)
        target_link_directories(spratpackcommand PUBLIC $<TARGET_PROPERTY:spratpack,LINK_DIRECTORIES>)
        # sprat-cli sets some definitions (e.g. SPRAT_VERSION) per directory, not per target.
        get_directory_property(_sprat_cli_definitions DIRECTORY "${SPRAT_CLI_SOURCE_DIR}" COMPILE_DEFINITIONS)
        target_compile_definitions(spratpackcommand PRIVATE ${_sprat_cli_definitions})
    endif()
endif()

# === Qt6 Configuration ===
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# macOS Tahoe (Xcode 26) removed the AGL framework; Qt 6.5.x still lists it as
# a link dependency.  Defining an empty INTERFACE target named AGL here causes
# CMake to resolve all "AGL" entries in Qt imported-target INTERFACE_LINK_LIBRARIES
# against this stub instead of falling through to the framework path that emits
# -framework AGL and causes a linker error.
if(APPLE)
    if(NOT TARGET AGL)
        add_library(AGL INTERFACE IMPORTED GLOBAL)
    endif()
endif()

# === Find Dependencies ===
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Network Svg OpenGL)
find_package(Qt6 QUIET COMPONENTS LinguistTools)

if(APPLE)
    # Qt 6.5.x pre-built binaries have -framework AGL hardcoded in their cmake
    # imported-target properties.  AGL was removed from macOS Tahoe (Xcode 26);
    # strip it from every relevant target property now that find_package has run
    # and the imported targets exist.  The add_library(AGL ...) stub above
    # handles the bare "AGL" target-name form; this loop handles the raw
    # "-framework AGL" flag-string form that bypasses target resolution.
    foreach(_agl_tgt IN ITEMS
            Qt6::Gui Qt6::GuiPrivate
            Qt6::Widgets Qt6::WidgetsPrivate
            Qt6::OpenGL Qt6::OpenGLWidgets Qt6::OpenGLPrivate
            Qt6::QCocoaIntegrationPlugin)
        if(NOT TARGET ${_agl_tgt})
            continue()
        endif()
        foreach(_agl_prop IN ITEMS
                INTERFACE_LINK_LIBRARIES INTERFACE_LINK_OPTIONS
                IMPORTED_LINK_DEPENDENT_LIBRARIES
                IMPORTED_LINK_DEPENDENT_LIBRARIES_RELEASE
                IMPORTED_LINK_DEPENDENT_LIBRARIES_RELWITHDEBINFO
                IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG
                IMPORTED_LINK_DEPENDENT_LIBRARIES_MINSIZEREL)
            get_target_property(_agl_v ${_agl_tgt} ${_agl_prop})
            if(NOT _agl_v)
                continue()
            endif()
            string(REPLACE "-framework AGL"  ""  _agl_v "${_agl_v}")
            string(REPLACE "-framework;AGL"  ""  _agl_v "${_agl_v}")
            string(REPLACE ";AGL;"           ";" _agl_v "${_agl_v}")
            string(REPLACE ";AGL"            ""  _agl_v "${_agl_v}")
            string(REPLACE "AGL;"            ""  _agl_v "${_agl_v}")
            string(REPLACE "AGL"             ""  _agl_v "${_agl_v}")
            string(REGEX REPLACE "LINK_ONLY:;+" "LINK_ONLY:" _agl_v "${_agl_v}")
            string(REGEX REPLACE ";+"        ";" _agl_v "${_agl_v}")
            string(REGEX REPLACE "^;|;$"     ""  _agl_v "${_agl_v}")
            set_target_properties(${_agl_tgt} PROPERTIES ${_agl_prop} "${_agl_v}")
        endforeach()
    endforeach()
    unset(_agl_tgt)
    unset(_agl_prop)
    unset(_agl_v)

    # Qt 6.5's FindWrapOpenGL also links AGL through WrapOpenGL::WrapOpenGL.
    if(TARGET WrapOpenGL::WrapOpenGL)
        get_target_property(_wrap_opengl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
        if(_wrap_opengl_libs)
            list(FILTER _wrap_opengl_libs EXCLUDE REGEX "AGL")
            set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "${_wrap_opengl_libs}")
        endif()
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
    src/App/MainWindow/MainWindow.ImageLoading.cpp
    src/App/MainWindow/MainWindow.SmokeTest.cpp
    src/App/MainWindow/MainWindow.Navigator.cpp
    src/App/Workspaces/Shared/NavigatorTreeWidget.h
    src/App/Workspaces/Shared/NavigatorTreeWidget.cpp
    src/App/MainWindow/SpritePreviewTooltip.h
    src/App/MainWindow/SpritePreviewTooltip.cpp
    src/App/MainWindow/TimelineTreeWidget.h
    src/App/MainWindow/TimelineTreeWidget.cpp
    src/App/MainWindow/MainWindow.h
    src/App/MainWindow/LayoutOrchestrator.h
    src/App/MainWindow/LayoutOrchestrator.cpp
    src/App/MainWindow/ExportCoordinator.h
    src/App/MainWindow/ExportCoordinator.cpp
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
    src/App/Workspaces/IWorkspace.h
    src/App/Workspaces/Shared/IAtlasViewport.h
    src/App/Workspaces/Shared/NavigatorPanel.h
    src/App/Workspaces/Shared/NavigatorPanel.cpp
    src/App/Workspaces/Atlas/AtlasWorkspace.h
    src/App/Workspaces/Atlas/AtlasWorkspace.cpp
    src/App/Workspaces/Atlas/SpriteEditorPanel.h
    src/App/Workspaces/Atlas/SpriteEditorPanel.cpp
    src/App/Workspaces/FrameAnimation/FrameAnimationWorkspace.h
    src/App/Workspaces/FrameAnimation/FrameAnimationWorkspace.cpp
    src/App/Workspaces/FrameAnimation/AnimationPreviewPanel.h
    src/App/Workspaces/FrameAnimation/AnimationPreviewPanel.cpp
    src/App/Workspaces/FrameAnimation/TimelineEditorPanel.h
    src/App/Workspaces/FrameAnimation/TimelineEditorPanel.cpp
    src/App/Workspaces/Export/ExportWorkspace.h
    src/App/Workspaces/Export/ExportWorkspace.cpp
    src/App/Workspaces/AtlasesManagement/AtlasesManagementWorkspace.h
    src/App/Workspaces/AtlasesManagement/AtlasesManagementWorkspace.cpp
    src/App/Workspaces/AtlasesManagement/PackedAtlasView.h
    src/App/Workspaces/AtlasesManagement/PackedAtlasView.cpp
    src/App/Workspaces/NineSlice/NineSliceWorkspace.h
    src/App/Workspaces/NineSlice/NineSliceWorkspace.cpp
    src/MapPaint/MapPaintCanvas.h
    src/MapPaint/MapPaintCanvas.cpp
    src/NormalMap/NormalMapPaintCanvas.h
    src/NormalMap/NormalMapPaintCanvas.cpp
    src/App/Workspaces/NormalMap/NormalMapWorkspace.h
    src/App/Workspaces/NormalMap/NormalMapWorkspace.cpp
    src/App/Workspaces/NormalMap/NormalWheelWidget.h
    src/App/Workspaces/NormalMap/NormalWheelWidget.cpp
    src/NineSlice/NineSliceEditorCanvas.h
    src/NineSlice/NineSliceEditorCanvas.cpp
    src/NineSlice/NineSlicePreviewWidget.h
    src/NineSlice/NineSlicePreviewWidget.cpp
    src/App/MainWindow/ExportDiffDialog.h
    src/App/MainWindow/ExportDiffDialog.cpp
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
    src/Project/ProjectData.cpp
    src/Project/ProjectData.h
    src/Project/Repositories/SpriteRepository.cpp
    src/Project/Repositories/SpriteRepository.h
    src/Project/Repositories/SourceRepository.cpp
    src/Project/Repositories/SourceRepository.h
    src/Project/Repositories/MarkerRepository.cpp
    src/Project/Repositories/MarkerRepository.h
    src/Project/Repositories/ExportRepository.cpp
    src/Project/Repositories/ExportRepository.h

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
    assets/icons.qrc
)

if(SPRAT_EMBEDDED_CLI)
    list(APPEND PROJECT_SOURCES
        src/CLITools/EmbeddedCli.cpp
        src/CLITools/EmbeddedCli.h
    )
endif()

if(NOT EMSCRIPTEN)
    list(APPEND PROJECT_SOURCES
        src/Update/UpdateChecker.h
        src/Update/UpdateChecker.cpp
        src/Update/UpdateInstaller.h
        src/Update/UpdateInstaller.cpp
        src/Update/UpdateDialog.h
        src/Update/UpdateDialog.cpp
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
    src/App/Workspaces
    src/App/Workspaces/Atlas
    src/App/Workspaces/AtlasesManagement
    src/App/Workspaces/Export
    src/App/Workspaces/FrameAnimation
    src/App/Workspaces/NineSlice
    src/App/Workspaces/Shared
    src/App/Workspaces/NormalMap
    src/MapPaint
    src/NormalMap
    src/NineSlice
    src/Animation
    src/Animation/Timelines
    src/Animation/Test
    src/SpriteSheetLayout
    src/SelectedSpriteFrame
    src/SelectedSpriteFrame/Markers
    src/CLITools
    src/Profiles
    src/Project
    src/Project/Repositories
    src/Settings
    src/Core
    src/Update
)

# === Link Libraries ===
target_link_libraries(sprat-gui PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Network
    Qt6::Svg
    Qt6::OpenGL
    LibArchive::LibArchive
)

# Copy sprat-cli assets (config and transforms) for host builds
if(NOT EMSCRIPTEN)
    if(SPRAT_CLI_SOURCE_DIR AND EXISTS "${SPRAT_CLI_SOURCE_DIR}/spratprofiles.cfg")
        if(APPLE)
            # Contents/MacOS must hold only code, or codesign rejects the bundle.
            set(SPRAT_ASSETS_DEST "$<TARGET_BUNDLE_CONTENT_DIR:sprat-gui>/Resources/cli")
        else()
            set(SPRAT_ASSETS_DEST "$<TARGET_FILE_DIR:sprat-gui>")
        endif()
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

if(_sprat_bundle_cli)
    set(_sprat_bundled_tools spratlayout spratpack spratconvert spratframes spratunpack)
    add_dependencies(sprat-gui ${_sprat_bundled_tools})
    foreach(_tool IN LISTS _sprat_bundled_tools)
        add_custom_command(TARGET sprat-gui POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${_tool}>"
            "$<TARGET_BUNDLE_CONTENT_DIR:sprat-gui>/Resources/cli/$<TARGET_FILE_NAME:${_tool}>"
            COMMENT "Bundling ${_tool}..."
        )
    endforeach()
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
        COMMAND ${CMAKE_COMMAND} -E remove "$<TARGET_FILE_DIR:sprat-gui>/sprat-gui.html"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/deploy/sprat-gui-itch/index.html" "$<TARGET_FILE_DIR:sprat-gui>/index.html"
        COMMENT "Installing custom itch.io index.html"
    )
endif()

if(SPRAT_EMBEDDED_CLI)
    target_link_libraries(sprat-gui PRIVATE spratpackcommand spratcore)
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
    if(NOT EMSCRIPTEN)
        target_sources(sprat-gui-tests PRIVATE src/Update/UpdateChecker.cpp)
    endif()

    target_include_directories(sprat-gui-tests PRIVATE
        tests
        src
        src/Animation
        src/Animation/Timelines
        src/NineSlice
        src/Project
        src/Core
        src/SpriteSheetLayout
        src/Update
    )

    target_link_libraries(sprat-gui-tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::Network
        Qt6::Test
        LibArchive::LibArchive
    )

    add_test(NAME sprat-gui-tests COMMAND sprat-gui-tests)
    set_tests_properties(sprat-gui-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(BUILD_TESTING AND SPRAT_E2E_TESTS AND NOT EMSCRIPTEN)
    # End-to-end export tests: ProjectSaveService through the real sprat-cli tools.
    add_executable(sprat-gui-e2e
        tests/e2e/ExportPipelineTests.cpp
        tests/e2e/ExportPipelineTests.h
        src/Project/ProjectSaveService.cpp
        src/SpriteSheetLayout/LayoutParser.cpp
        src/Core/ArchiveExtractor.cpp
    )
    target_include_directories(sprat-gui-e2e PRIVATE
        src/Core
        src/Project
        src/Profiles
        src/CLITools
        src/SpriteSheetLayout
    )
    target_link_libraries(sprat-gui-e2e PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
        LibArchive::LibArchive
    )
    set(_e2e_environment "QT_QPA_PLATFORM=offscreen")
    if(SPRAT_EMBEDDED_CLI)
        target_sources(sprat-gui-e2e PRIVATE src/CLITools/EmbeddedCli.cpp)
        target_link_libraries(sprat-gui-e2e PRIVATE spratpackcommand spratcore)
        # The in-process CLI looks for its assets next to the test executable.
        add_custom_command(TARGET sprat-gui-e2e POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SPRAT_CLI_SOURCE_DIR}/spratprofiles.cfg"
            "$<TARGET_FILE_DIR:sprat-gui-e2e>/spratprofiles.cfg"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SPRAT_CLI_SOURCE_DIR}/transforms"
            "$<TARGET_FILE_DIR:sprat-gui-e2e>/transforms"
            COMMENT "Copying sprat-cli assets for sprat-gui-e2e..."
        )
    else()
        add_dependencies(sprat-gui-e2e spratlayout spratpack spratconvert)
        list(APPEND _e2e_environment "SPRAT_CLI_DIR=$<TARGET_FILE_DIR:spratlayout>")
    endif()
    add_test(NAME sprat-gui-e2e COMMAND sprat-gui-e2e)
    set_tests_properties(sprat-gui-e2e PROPERTIES ENVIRONMENT "${_e2e_environment}")
endif()

# === Windows Deployment & Launcher ===
if(WIN32)
    # Move the GUI and tests into a 'bin' subdirectory to share DLLs
    # and keep the root directory clean.
    set_target_properties(sprat-gui PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    foreach(_test_target sprat-gui-tests sprat-gui-e2e)
        if(TARGET ${_test_target})
            set_target_properties(${_test_target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        endif()
    endforeach()

    # sprat-cli takes paths as narrow strings, which Windows reads in the process code page.
    # Qt hands it UTF-8, so every executable that runs the CLI in-process needs a UTF-8 code page.
    if(MSVC)
        set(_utf8_manifest "${CMAKE_CURRENT_SOURCE_DIR}/deploy/windows/sprat-gui.manifest")
    else()
        set(_utf8_manifest "${CMAKE_CURRENT_SOURCE_DIR}/deploy/windows/sprat-gui-manifest.rc")
    endif()
    foreach(_cli_target sprat-gui sprat-gui-e2e)
        if(TARGET ${_cli_target})
            target_sources(${_cli_target} PRIVATE "${_utf8_manifest}")
        endif()
    endforeach()

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

    # spratpackcommand links spratpack's optional codecs; sprat-cli copies their DLLs only
    # next to spratpack.exe, so copy the ones it found (cached by find_file) here too.
    if(SPRAT_EMBEDDED_CLI)
        set(_codec_dlls "")
        foreach(_codec_dll_var ZOPFLIPNG_DLL SQUISH_DLL WEBP_DLL AVIF_DLL)
            if(${_codec_dll_var})
                list(APPEND _codec_dlls "${${_codec_dll_var}}")
            endif()
        endforeach()
        if(_codec_dlls)
            add_custom_command(TARGET sprat-gui POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_codec_dlls} $<TARGET_FILE_DIR:sprat-gui>
                COMMENT "Copying sprat-cli codec DLLs"
            )
        endif()
    endif()
endif()
