cmake_minimum_required(VERSION 3.16)
project(sprat-gui-cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)

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
    src/SelectedSpriteFrame/SpriteSelectionPresenter.cpp
    src/SelectedSpriteFrame/SpriteSelectionPresenter.h
    src/Settings/SettingsCoordinator.cpp
    src/Settings/SettingsCoordinator.h
    src/App/MainWindow/MainWindowUiState.cpp
    src/App/MainWindow/MainWindowUiState.h
    src/Animation/Timelines/TimelineBuilder.cpp
    src/Animation/Timelines/TimelineBuilder.h
    src/Core/models.h
)

add_executable(sprat-gui-cpp ${PROJECT_SOURCES})

target_include_directories(sprat-gui-cpp PRIVATE
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
    src/Project
    src/Settings
    src/Core
)

target_link_libraries(sprat-gui-cpp PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent)
