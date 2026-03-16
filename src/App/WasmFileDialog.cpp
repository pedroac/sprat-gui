#include "WasmFileDialog.h"

#ifdef Q_OS_WASM
#include <emscripten.h>
#include <QMetaObject>
#include <QTimer>
#include "MainWindow.h"

namespace {
EM_JS(void, sprat_open_file_dialog, (int mode), {
    var input = document.createElement('input');
    input.type = 'file';
    if (mode === 1) {
        input.multiple = true;
        input.webkitdirectory = true;
    }

    input.onchange = function () {
        var files = input.files;
        if (!files || files.length === 0) {
            return;
        }
        var base = '/home/webuser/uploads';
        FS.mkdirTree(base);
        var firstRoot = null;
        for (var i = 0; i < files.length; i++) {
            var rel = files[i].webkitRelativePath || files[i].name;
            if (mode === 1 && !firstRoot && rel.indexOf('/') >= 0) {
                firstRoot = rel.split('/')[0];
            }
        }

        var idx = 0;
        function next() {
            if (idx >= files.length) {
                var finalPath = base;
                if (mode === 1) {
                    if (firstRoot) {
                        finalPath = base + '/' + firstRoot;
                    }
                } else {
                    finalPath = base + '/' + files[0].name;
                }
                // Defer into a fresh task so we don't call into WASM while an async JS
                // operation is still in flight (Asyncify allows only one at a time).
                setTimeout(function() {
                    Module.ccall('sprat_on_file_picked', null, ['string', 'number'], [finalPath, mode]);
                }, 0);
                return;
            }
            var f = files[idx++];
            var rel = f.webkitRelativePath || f.name;
            var dest = base + '/' + (mode === 1 ? rel : f.name);
            var dir = dest.substring(0, dest.lastIndexOf('/'));
            if (dir) {
                FS.mkdirTree(dir);
            }
            var reader = new FileReader();
            reader.onload = function () {
                var data = new Uint8Array(reader.result);
                FS.writeFile(dest, data);
                next();
            };
            reader.onerror = function () {
                next();
            };
            reader.readAsArrayBuffer(f);
        }
        next();
    };

    input.click();
});

EM_JS(void, sprat_install_drop_handlers, (), {
    if (Module.spratDropHandlersInstalled) {
        return;
    }
    Module.spratDropHandlersInstalled = true;
    Module.spratDropInProgress = false;
    Module.spratDropDragDepth = 0;
    var supportsDirectoryDrop = (typeof DataTransferItem !== 'undefined' &&
        DataTransferItem.prototype &&
        typeof DataTransferItem.prototype.webkitGetAsEntry === 'function');

    function hasFiles(dt) {
        if (!dt) return false;
        if (dt.items) {
            for (var i = 0; i < dt.items.length; i++) {
                if (dt.items[i].kind === 'file') return true;
            }
        }
        return dt.files && dt.files.length > 0;
    }

    function collectFiles(dt, onDone) {
        var queue = [];
        var results = [];

        if (dt.items) {
            for (var i = 0; i < dt.items.length; i++) {
                var item = dt.items[i];
                if (item.kind !== 'file') continue;
                if (item.webkitGetAsEntry) {
                    var entry = item.webkitGetAsEntry();
                    if (entry) {
                        queue.push({ entry: entry, relPath: "" });
                        continue;
                    }
                }
                var fileFromItem = item.getAsFile();
                if (fileFromItem) {
                    queue.push({ file: fileFromItem, relPath: fileFromItem.name });
                }
            }
        }

        if (!queue.length && dt.files) {
            for (var j = 0; j < dt.files.length; j++) {
                var fileEntry = dt.files[j];
                queue.push({ file: fileEntry, relPath: fileEntry.name });
            }
        }

        if (!queue.length) {
            onDone([]);
            return;
        }

        function processQueue() {
            if (!queue.length) {
                onDone(results);
                return;
            }
            var item = queue.shift();
            if (item.file) {
                results.push({ file: item.file, relPath: item.relPath });
                processQueue();
                return;
            }
            var entry = item.entry;
            if (!entry) {
                processQueue();
                return;
            }
            if (entry.isFile) {
                entry.file(function (file) {
                    var path = item.relPath + file.name;
                    results.push({ file: file, relPath: path });
                    processQueue();
                }, function () {
                    processQueue();
                });
                return;
            }
            if (entry.isDirectory) {
                var reader = entry.createReader();
                var prefix = item.relPath + (entry.name ? entry.name + '/' : '');
                function readBatch() {
                    reader.readEntries(function (entries) {
                        if (!entries || entries.length === 0) {
                            processQueue();
                            return;
                        }
                        for (var k = entries.length - 1; k >= 0; k--) {
                            queue.unshift({ entry: entries[k], relPath: prefix });
                        }
                        readBatch();
                    }, function () {
                        processQueue();
                    });
                }
                readBatch();
                return;
            }
            processQueue();
        }

        processQueue();
    }

    function ensureOverlay() {
        if (Module.spratDropOverlay) return Module.spratDropOverlay;
        var overlay = document.createElement('div');
        overlay.id = 'sprat-drop-overlay';
        overlay.style.position = 'fixed';
        overlay.style.left = '0';
        overlay.style.top = '0';
        overlay.style.width = '100%';
        overlay.style.height = '100%';
        overlay.style.display = 'none';
        overlay.style.alignItems = 'center';
        overlay.style.justifyContent = 'center';
        overlay.style.background = 'rgba(20, 20, 20, 0.65)';
        overlay.style.color = '#fff';
        overlay.style.fontFamily = 'system-ui, sans-serif';
        overlay.style.fontSize = '20px';
        overlay.style.zIndex = '9999';
        overlay.style.border = '2px dashed rgba(255, 255, 255, 0.6)';
        overlay.style.boxSizing = 'border-box';
        overlay.style.flexDirection = 'column';
        overlay.style.textAlign = 'center';
        var title = document.createElement('div');
        title.textContent = 'Drop files to import';
        overlay.appendChild(title);
        if (!supportsDirectoryDrop) {
            var note = document.createElement('div');
            note.textContent = 'Folder drop not supported in this browser. Use Load Images Folder.';
            note.style.fontSize = '14px';
            note.style.marginTop = '8px';
            note.style.opacity = '0.8';
            overlay.appendChild(note);
        }
        document.body.appendChild(overlay);
        Module.spratDropOverlay = overlay;
        return overlay;
    }

    function showToast(message) {
        var toast = document.createElement('div');
        toast.textContent = message;
        toast.style.position = 'fixed';
        toast.style.left = '50%';
        toast.style.bottom = '24px';
        toast.style.transform = 'translateX(-50%)';
        toast.style.padding = '10px 14px';
        toast.style.background = 'rgba(0, 0, 0, 0.8)';
        toast.style.color = '#fff';
        toast.style.fontFamily = 'system-ui, sans-serif';
        toast.style.fontSize = '14px';
        toast.style.borderRadius = '6px';
        toast.style.zIndex = '10000';
        toast.style.pointerEvents = 'none';
        toast.style.transition = 'opacity 200ms ease';
        document.body.appendChild(toast);
        setTimeout(function() {
            toast.style.opacity = '0';
            setTimeout(function() {
                if (toast.parentNode) toast.parentNode.removeChild(toast);
            }, 250);
        }, 2200);
    }

    function showOverlay() {
        var overlay = ensureOverlay();
        overlay.style.display = 'flex';
    }

    function hideOverlay() {
        if (Module.spratDropOverlay) {
            Module.spratDropOverlay.style.display = 'none';
        }
    }

    document.addEventListener('dragenter', function (e) {
        if (!hasFiles(e.dataTransfer)) return;
        Module.spratDropDragDepth++;
        showOverlay();
        e.preventDefault();
        e.stopPropagation();
    }, true);

    document.addEventListener('dragover', function (e) {
        if (!hasFiles(e.dataTransfer)) return;
        e.preventDefault();
        e.stopPropagation();
    }, true);

    document.addEventListener('dragleave', function (e) {
        if (!hasFiles(e.dataTransfer)) return;
        Module.spratDropDragDepth = Math.max(0, Module.spratDropDragDepth - 1);
        if (Module.spratDropDragDepth === 0) {
            hideOverlay();
        }
        e.preventDefault();
        e.stopPropagation();
    }, true);

    document.addEventListener('drop', function (e) {
        if (!hasFiles(e.dataTransfer)) return;
        e.preventDefault();
        e.stopPropagation();
        Module.spratDropDragDepth = 0;
        hideOverlay();
        if (Module.spratDropInProgress) return;
        Module.spratDropInProgress = true;
        collectFiles(e.dataTransfer, function (files) {
            if (!files.length) {
                if (!supportsDirectoryDrop) {
                    showToast('Folder drop not supported. Use Load Images Folder.');
                }
                Module.spratDropInProgress = false;
                return;
            }
            var base = '/home/webuser/uploads';
            var dropRoot = base + '/drop-' + Date.now();
            FS.mkdirTree(dropRoot);

            var firstRoot = null;
            var hasDirectories = false;
            for (var i = 0; i < files.length; i++) {
                var rel = files[i].relPath || files[i].file.name;
                if (rel.indexOf('/') >= 0) {
                    hasDirectories = true;
                    if (!firstRoot) {
                        firstRoot = rel.split('/')[0];
                    }
                }
            }

            var idx = 0;
            function next() {
                if (idx >= files.length) {
                    var finalPath = dropRoot;
                    var mode = 1;
                    if (!hasDirectories && files.length === 1) {
                        finalPath = dropRoot + '/' + (files[0].relPath || files[0].file.name);
                        mode = 0;
                    } else if (hasDirectories && firstRoot) {
                        finalPath = dropRoot + '/' + firstRoot;
                    }
                    setTimeout(function() {
                        Module.ccall('sprat_on_file_picked', null, ['string', 'number'], [finalPath, mode]);
                        Module.spratDropInProgress = false;
                    }, 0);
                    return;
                }
                var entry = files[idx++];
                var file = entry.file || entry;
                var relPath = entry.relPath || file.name;
                var dest = dropRoot + '/' + relPath;
                var dir = dest.substring(0, dest.lastIndexOf('/'));
                if (dir) {
                    FS.mkdirTree(dir);
                }
                var reader = new FileReader();
                reader.onload = function () {
                    var data = new Uint8Array(reader.result);
                    FS.writeFile(dest, data);
                    next();
                };
                reader.onerror = function () {
                    next();
                };
                reader.readAsArrayBuffer(file);
            }
            next();
        });
    }, false);
});

}

extern "C" {
EMSCRIPTEN_KEEPALIVE
void sprat_on_file_picked(const char* path, int mode) {
    if (!path) {
        return;
    }
    static QString pendingPath;
    static int pendingMode = 0;
    static bool scheduled = false;
    pendingPath = QString::fromUtf8(path);
    pendingMode = mode;
    if (scheduled) {
        return;
    }
    scheduled = true;
    QTimer::singleShot(0, []() {
        scheduled = false;
        wasmHandleFilePicked(pendingPath, pendingMode == 1);
    });
}
}

void wasmOpenFileDialog(bool selectFolder) {
    sprat_open_file_dialog(selectFolder ? 1 : 0);
}

void wasmInstallDropHandlers() {
    sprat_install_drop_handlers();
}

void wasmHandleFilePicked(const QString& path, bool isFolder) {
    MainWindow* window = MainWindow::wasmInstance();
    if (!window) {
        return;
    }
    QMetaObject::invokeMethod(window, [window, path, isFolder]() {
        window->onWasmFilePicked(path, isFolder);
    }, Qt::QueuedConnection);
}

#endif
