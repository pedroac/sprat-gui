cmake_minimum_required(VERSION 3.16)
project(sprat-gui LANGUAGES CXX)
include(CTest)

# === Configuration ===
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# === Qt6 Configuration ===
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# === Find Dependencies ===
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)
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
    src/App/MainWindow/MainWindow.h
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
    src/Animation/Timelines/AnimationTimelineOps.cpp
    src/Animation/Timelines/AnimationTimelineOps.h
    src/Animation/Test/AnimationTestOps.cpp
    src/Animation/Test/AnimationTestOps.h
    src/Animation/AnimationPlaybackService.cpp
    src/Animation/AnimationPlaybackService.h
    src/Animation/AnimationPreviewService.cpp
    src/Animation/AnimationPreviewService.h
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
    src/Core/models.h
)

# === Executable ===
add_executable(sprat-gui ${PROJECT_SOURCES})

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
    add_executable(sprat-gui-tests
        tests/TestMain.cpp
        src/Animation/AnimationPreviewService.cpp
        src/Animation/Timelines/TimelineBuilder.cpp
        src/Project/ProjectPayloadCodec.cpp
    )

    target_include_directories(sprat-gui-tests PRIVATE
        src
        src/Animation
        src/Animation/Timelines
        src/Project
        src/Core
    )

    target_link_libraries(sprat-gui-tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )

    add_test(NAME sprat-gui-tests COMMAND sprat-gui-tests)
    set_tests_properties(sprat-gui-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
