#pragma once

#include <QString>
#include <functional>

#ifdef Q_OS_WASM
// Open a browser file picker and import into the Emscripten virtual FS.
// When files are ready, sprat_on_file_picked will be called.
void wasmOpenFileDialog(bool selectFolder);
void wasmOpenFileDialogMode(int mode);

// Override the default MainWindow file-picked handler until cleared.
void wasmSetFilePickedHandler(const std::function<void(const QString&, int)>& handler);
void wasmClearFilePickedHandler();

// Install HTML5 drag-and-drop handlers that upload files into the virtual FS.
void wasmInstallDropHandlers();

// Setup keyboard focus to fix Qt 6.10 WASM keyboard event handling.
void wasmSetupKeyboardFocus();

// Block all drag-drop events to prevent Qt WASM segfaults.
void wasmBlockAllDrags();

// Called after JS file picker writes files into the virtual FS.
void wasmHandleFilePicked(const QString& path, int mode);

// Trigger a browser download for a file in the virtual FS.
void wasmDownloadFile(const QString& path);
#endif
