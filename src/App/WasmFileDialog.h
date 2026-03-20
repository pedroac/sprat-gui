#pragma once

#include <QString>

#ifdef Q_OS_WASM
// Open a browser file picker and import into the Emscripten virtual FS.
// When files are ready, sprat_on_file_picked will be called.
void wasmOpenFileDialog(bool selectFolder);

// Install HTML5 drag-and-drop handlers that upload files into the virtual FS.
void wasmInstallDropHandlers();


// Called after JS file picker writes files into the virtual FS.
void wasmHandleFilePicked(const QString& path, int mode);
#endif
