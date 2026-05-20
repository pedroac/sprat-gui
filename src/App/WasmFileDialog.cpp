#include "WasmFileDialog.h"

#ifdef Q_OS_WASM
#include <emscripten.h>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QFileInfo>
#include "MainWindow.h"

namespace {
std::function<void(const QString&, int)> g_filePickedHandler;

EM_JS(void, sprat_open_file_dialog, (int mode), {
    console.log('[sprat] open file dialog mode=', mode);
    function ensureUploadOverlay() {
        if (Module.spratUploadOverlay) return Module.spratUploadOverlay;
        var overlay = document.createElement('div');
        overlay.id = 'sprat-upload-overlay';
        overlay.style.position = 'fixed';
        overlay.style.left = '0';
        overlay.style.top = '0';
        overlay.style.width = '100%';
        overlay.style.height = '100%';
        overlay.style.display = 'none';
        overlay.style.alignItems = 'center';
        overlay.style.justifyContent = 'center';
        overlay.style.background = 'rgba(20, 20, 20, 0.55)';
        overlay.style.color = '#fff';
        overlay.style.fontFamily = 'system-ui, sans-serif';
        overlay.style.fontSize = '18px';
        overlay.style.zIndex = '10000';
        overlay.style.flexDirection = 'column';
        overlay.style.textAlign = 'center';
        var text = document.createElement('div');
        text.textContent = 'Uploading files...';
        text.style.marginBottom = '10px';
        var bar = document.createElement('div');
        bar.style.width = '60%';
        bar.style.maxWidth = '420px';
        bar.style.height = '8px';
        bar.style.background = 'rgba(255,255,255,0.2)';
        bar.style.borderRadius = '4px';
        var fill = document.createElement('div');
        fill.style.height = '100%';
        fill.style.width = '0%';
        fill.style.background = '#41cd52';
        fill.style.borderRadius = '4px';
        bar.appendChild(fill);
        overlay.appendChild(text);
        overlay.appendChild(bar);
        document.body.appendChild(overlay);
        Module.spratUploadOverlay = overlay;
        Module.spratUploadOverlayText = text;
        Module.spratUploadOverlayFill = fill;
        return overlay;
    }
    function showUploadOverlay(message, progress) {
        var overlay = ensureUploadOverlay();
        if (Module.spratUploadOverlayText) {
            Module.spratUploadOverlayText.textContent = message;
        }
        if (Module.spratUploadOverlayFill && typeof progress === 'number') {
            Module.spratUploadOverlayFill.style.width = Math.max(0, Math.min(100, progress)) + '%';
        }
        overlay.style.display = 'flex';
    }
    function hideUploadOverlay() {
        if (Module.spratUploadOverlay) {
            Module.spratUploadOverlay.style.display = 'none';
        }
    }
    var input = document.createElement('input');
    input.type = 'file';
    if (mode === 1) {
        input.multiple = true;
        input.webkitdirectory = true;
    } else if (mode === 2) {
        input.multiple = true;
    }

    input.onchange = function () {
        var files = input.files;
        if (!files || files.length === 0) {
            console.warn('[sprat] File picker returned no files.');
            return;
        }
        console.log('[sprat] File picker selected', files.length, 'files');
        showUploadOverlay('Preparing files... (0/' + files.length + ')', 0);
        var base = '/home/webuser/uploads/' + Date.now() + '-' + Math.floor(Math.random() * 1000000);
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
                } else if (mode === 2) {
                    finalPath = base;
                } else {
                    finalPath = base + '/' + files[0].name;
                }
                
                console.log('[sprat] All files read. Final path', finalPath);
                showUploadOverlay('Finalizing...', 100);
                var waitForAsyncifyIdle = function(onReady) {
                    var asyncify = (typeof Asyncify !== 'undefined' ? Asyncify : (typeof Module !== 'undefined' ? Module['Asyncify'] : null));
                    if (!asyncify || !asyncify.currData) {
                        onReady();
                        return;
                    }
                    var state = asyncify.state;
                    var normal = asyncify.State ? asyncify.State.Normal : 0;
                    if (state !== undefined && state === normal) {
                        onReady();
                        return;
                    }
                    showUploadOverlay('Waiting for app to be ready...', 100);
                    if (typeof asyncify.whenDone === 'function' && !asyncify.asyncPromiseHandlers) {
                        asyncify.whenDone().then(onReady).catch(function () {
                            setTimeout(function () { waitForAsyncifyIdle(onReady); }, 100);
                        });
                        return;
                    }
                    setTimeout(function () { waitForAsyncifyIdle(onReady); }, 100);
                };
                var callIntoWasm = function() {
                    if (!Module || typeof Module.ccall !== 'function') {
                        console.error('[sprat] Module.ccall not available; cannot notify wasm of picked files.');
                        hideUploadOverlay();
                        return;
                    }
                    console.log('[sprat] Calling wasm file picked', finalPath, mode);
                    Module.ccall('sprat_on_file_picked', null, ['string', 'number'], [finalPath, mode]);
                    hideUploadOverlay();
                };
                setTimeout(function () { waitForAsyncifyIdle(callIntoWasm); }, 0);
                return;
            }
            var f = files[idx++];
            var rel = f.webkitRelativePath || f.name;
            console.log('[sprat] Reading file', idx + '/' + files.length, rel, f.size);
            showUploadOverlay('Uploading files... (' + idx + '/' + files.length + ')', (idx / files.length) * 100);
            var dest = base + '/' + (mode === 1 ? rel : f.name);
            var dir = dest.substring(0, dest.lastIndexOf('/'));
            if (dir) {
                FS.mkdirTree(dir);
            }
            var reader = new FileReader();
            reader.onload = function () {
                console.log('[sprat] Read complete', rel, f.size);
                var data = new Uint8Array(reader.result);
                try {
                    FS.writeFile(dest, data);
                } catch (e) {
                    console.error('[sprat] FS.writeFile failed for', dest, e);
                }
                next();
            };
            reader.onerror = function () {
                console.warn('[sprat] Failed to read file:', f && f.name ? f.name : rel);
                showUploadOverlay('Uploading files... (' + idx + '/' + files.length + ')', (idx / files.length) * 100);
                next();
            };
            reader.onabort = function () {
                console.warn('[sprat] File read aborted:', f && f.name ? f.name : rel);
                showUploadOverlay('Uploading files... (' + idx + '/' + files.length + ')', (idx / files.length) * 100);
                next();
            };
            reader.readAsArrayBuffer(f);
        }
        next();
    };

    input.click();
});

EM_JS(void, sprat_setup_keyboard_focus, (), {
    console.log('[sprat] sprat_setup_keyboard_focus called!');

    // Fix Qt 6.10 WASM keyboard focus issue
    // Based on: https://forum.qt.io/topic/159639/wasm-apparently-doesn-t-handle-keyboard-shortcuts/12

    function ensureFocus() {
        try {
            const shadowContainer = document.getElementById('qt-shadow-container');
            if (!shadowContainer || !shadowContainer.shadowRoot) {
                return false;
            }
            const button = shadowContainer.shadowRoot.querySelector('.hidden-visually-read-by-screen-reader');
            if (button) {
                button.focus({ preventScroll: true });
                console.log('[sprat] Keyboard focus restored');
                return true;
            }
        } catch (e) {
            // Shadow DOM might be closed or not ready
        }
        return false;
    }

    // Restore focus whenever an element loses focus with no replacement
    document.addEventListener('focusout', function(event) {
        if (event.relatedTarget === null) {
            console.log('[sprat] focusout detected, restoring focus');
            // Restore immediately and also with a small delay for stability
            ensureFocus();
            setTimeout(ensureFocus, 10);
        }
    }, true);

    // Also restore on blur and regain on focus
    window.addEventListener('blur', function() {
        console.log('[sprat] Window blurred');
    });

    window.addEventListener('focus', function() {
        console.log('[sprat] Window focused, restoring keyboard focus');
        setTimeout(ensureFocus, 50);
    });

    // Attempt initial focus setup (Qt 6.10 may already do this, but doesn't hurt)
    console.log('[sprat] Keyboard focus setup installed');
});

EM_JS(void, sprat_block_all_drags, (), {
    // Block all drag/drop events to prevent Qt WASM segfaults
    ['dragstart', 'dragenter', 'dragover', 'dragleave', 'drop'].forEach(function(eventType) {
        document.addEventListener(eventType, function(e) {
            e.preventDefault();
            e.stopPropagation();
            return false;
        }, true);
    });
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
        try {
            // Safely check for files without triggering crashes on non-file drags
            if (dt.types && Array.from(dt.types).indexOf('Files') >= 0) {
                return true;
            }
        } catch (e) {
            console.warn('[sprat] Error checking dt.types:', e);
        }
        try {
            if (dt.items) {
                for (var i = 0; i < dt.items.length; i++) {
                    if (dt.items[i].kind === 'file') return true;
                }
            }
        } catch (e) {
            console.warn('[sprat] Error checking dt.items:', e);
            return false;
        }
        try {
            return dt.files && dt.files.length > 0;
        } catch (e) {
            console.warn('[sprat] Error checking dt.files:', e);
            return false;
        }
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
                var prefix = item.relPath + (entry.name ? entry.name + '/' : "");
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

    function ensureUploadOverlay() {
        if (Module.spratUploadOverlay) return Module.spratUploadOverlay;
        var overlay = document.createElement('div');
        overlay.id = 'sprat-upload-overlay';
        overlay.style.position = 'fixed';
        overlay.style.left = '0';
        overlay.style.top = '0';
        overlay.style.width = '100%';
        overlay.style.height = '100%';
        overlay.style.display = 'none';
        overlay.style.alignItems = 'center';
        overlay.style.justifyContent = 'center';
        overlay.style.background = 'rgba(20, 20, 20, 0.55)';
        overlay.style.color = '#fff';
        overlay.style.fontFamily = 'system-ui, sans-serif';
        overlay.style.fontSize = '18px';
        overlay.style.zIndex = '10000';
        overlay.style.flexDirection = 'column';
        overlay.style.textAlign = 'center';
        var text = document.createElement('div');
        text.textContent = 'Uploading files...';
        text.style.marginBottom = '10px';
        var bar = document.createElement('div');
        bar.style.width = '60%';
        bar.style.maxWidth = '420px';
        bar.style.height = '8px';
        bar.style.background = 'rgba(255,255,255,0.2)';
        bar.style.borderRadius = '4px';
        var fill = document.createElement('div');
        fill.style.height = '100%';
        fill.style.width = '0%';
        fill.style.background = '#41cd52';
        fill.style.borderRadius = '4px';
        bar.appendChild(fill);
        overlay.appendChild(text);
        overlay.appendChild(bar);
        document.body.appendChild(overlay);
        Module.spratUploadOverlay = overlay;
        Module.spratUploadOverlayText = text;
        Module.spratUploadOverlayFill = fill;
        return overlay;
    }
    function showUploadOverlay(message, progress) {
        var overlay = ensureUploadOverlay();
        if (Module.spratUploadOverlayText) {
            Module.spratUploadOverlayText.textContent = message;
        }
        if (Module.spratUploadOverlayFill && typeof progress === 'number') {
            Module.spratUploadOverlayFill.style.width = Math.max(0, Math.min(100, progress)) + '%';
        }
        overlay.style.display = 'flex';
    }
    function hideUploadOverlay() {
        if (Module.spratUploadOverlay) {
            Module.spratUploadOverlay.style.display = 'none';
        }
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

    // Use capture-phase listeners so these fire before Qt's canvas bubble handlers
    document.addEventListener('dragenter', function (e) {
        try {
            if (!e || !e.dataTransfer) return false;
            e.preventDefault();
            e.stopPropagation();
            var isFile = hasFiles(e.dataTransfer);
            var isUrl = !isFile && e.dataTransfer.types &&
                        Array.from(e.dataTransfer.types).indexOf('text/uri-list') >= 0;
            if (!isFile && !isUrl) return false;
            try {
                Module.spratDropDragDepth++;
                showOverlay();
            } catch (ex) {
                console.warn('[sprat] dragenter WASM error:', ex);
                return false;
            }
            return false;
        } catch (ex) {
            console.warn('[sprat] dragenter error:', ex);
            return false;
        }
    }, true);

    document.addEventListener('dragover', function (e) {
        try {
            if (!e || !e.dataTransfer) return false;
            e.preventDefault();
            e.stopPropagation();
            return false;
        } catch (ex) {
            console.warn('[sprat] dragover error:', ex);
            return false;
        }
    }, true);

    document.addEventListener('dragleave', function (e) {
        try {
            if (!e || !e.dataTransfer) return false;
            e.preventDefault();
            e.stopPropagation();
            var isFile = hasFiles(e.dataTransfer);
            var isUrl = !isFile && e.dataTransfer.types &&
                        Array.from(e.dataTransfer.types).indexOf('text/uri-list') >= 0;
            if (!isFile && !isUrl) return false;
            try {
                Module.spratDropDragDepth = Math.max(0, Module.spratDropDragDepth - 1);
                if (Module.spratDropDragDepth === 0) {
                    hideOverlay();
                }
            } catch (ex) {
                console.warn('[sprat] dragleave WASM error:', ex);
            }
            return false;
        } catch (ex) {
            console.warn('[sprat] dragleave error:', ex);
            return false;
        }
    }, true);

    document.addEventListener('drop', function (e) {
        try {
            if (!e || !e.dataTransfer) {
                return false;
            }
            e.preventDefault();
            e.stopPropagation();
            var hasFilesResult = hasFiles(e.dataTransfer);
            if (!hasFilesResult) {
                try {
                    var uriList = e.dataTransfer.getData('text/uri-list');
                    var rawText = e.dataTransfer.getData('text/plain');
                    var candidate = (uriList || rawText || '').trim().split('\n')[0].trim();
                    if (candidate && (candidate.indexOf('http://') === 0 || candidate.indexOf('https://') === 0)) {
                        console.log('[sprat] URL drop:', candidate);
                        Module.spratDropDragDepth = 0;
                        hideOverlay();
                        showUploadOverlay('Loading URL...', 0);
                        var droppedUrl = candidate;
                        setTimeout(function() {
                            var waitIdle = function(cb) {
                                var asyncify = (typeof Asyncify !== 'undefined' ? Asyncify : (Module ? Module['Asyncify'] : null));
                                if (!asyncify || !asyncify.currData) { cb(); return; }
                                var state = asyncify.state;
                                var normal = asyncify.State ? asyncify.State.Normal : 0;
                                if (state !== undefined && state === normal) { cb(); return; }
                                setTimeout(function() { waitIdle(cb); }, 100);
                            };
                            waitIdle(function() {
                                Module.ccall('sprat_on_file_picked', null, ['string', 'number'], [droppedUrl, 0]);
                                hideUploadOverlay();
                            });
                        }, 0);
                    }
                } catch (urlEx) {
                    console.warn('[sprat] URL drop detection error:', urlEx);
                }
                return false;
            }
        } catch (ex) {
            console.warn('[sprat] drop event error:', ex);
            return false;
        }
        try {
            Module.spratDropDragDepth = 0;
            hideOverlay();
            if (Module.spratDropInProgress) return false;
            Module.spratDropInProgress = true;
            console.log('[sprat] Drop received');
            collectFiles(e.dataTransfer, function (files) {
            if (!files.length) {
                if (!supportsDirectoryDrop) {
                    showToast('Folder drop not supported. Use Load Images Folder.');
                }
                Module.spratDropInProgress = false;
                return;
            }
            console.log('[sprat] Drop collected', files.length, 'files');
            showUploadOverlay('Preparing files... (0/' + files.length + ')', 0);
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
                    
                    console.log('[sprat] Drop all files read. Final path', finalPath);
                    showUploadOverlay('Finalizing...', 100);
                    var waitForAsyncifyIdle = function(onReady) {
                        var asyncify = (typeof Asyncify !== 'undefined' ? Asyncify : (typeof Module !== 'undefined' ? Module['Asyncify'] : null));
                        if (!asyncify || !asyncify.currData) {
                            onReady();
                            return;
                        }
                        var state = asyncify.state;
                        var normal = asyncify.State ? asyncify.State.Normal : 0;
                        if (state !== undefined && state === normal) {
                            onReady();
                            return;
                        }
                        showUploadOverlay('Waiting for app to be ready...', 100);
                        if (typeof asyncify.whenDone === 'function' && !asyncify.asyncPromiseHandlers) {
                            asyncify.whenDone().then(onReady).catch(function () {
                                setTimeout(function () { waitForAsyncifyIdle(onReady); }, 100);
                            });
                            return;
                        }
                        setTimeout(function () { waitForAsyncifyIdle(onReady); }, 100);
                    };
                    var callIntoWasm = function() {
                        if (!Module || typeof Module.ccall !== 'function') {
                            console.error('[sprat] Module.ccall not available; cannot notify wasm of dropped files.');
                            Module.spratDropInProgress = false;
                            hideUploadOverlay();
                            return;
                        }
                        console.log('[sprat] Calling wasm drop picked', finalPath, mode);
                        Module.ccall('sprat_on_file_picked', null, ['string', 'number'], [finalPath, mode]);
                        Module.spratDropInProgress = false;
                        hideUploadOverlay();
                    };
                    setTimeout(function () { waitForAsyncifyIdle(callIntoWasm); }, 0);
                    return;
                }
                var entry = files[idx++];
                var file = entry.file || entry;
                var relPath = entry.relPath || file.name;
                showUploadOverlay('Uploading files... (' + (idx + 1) + '/' + files.length + ')', ((idx + 1) / files.length) * 100);
                var dest = dropRoot + '/' + relPath;
                var dir = dest.substring(0, dest.lastIndexOf('/'));
                if (dir) {
                    FS.mkdirTree(dir);
                }
                var reader = new FileReader();
                reader.onload = function () {
                    var data = new Uint8Array(reader.result);
                    try {
                        FS.writeFile(dest, data);
                    } catch (e) {
                        console.error('[sprat] FS.writeFile failed for', dest, e);
                    }
                    next();
                };
                reader.onerror = function () {
                    console.warn('[sprat] Failed to read dropped file:', file && file.name ? file.name : relPath);
                    showUploadOverlay('Uploading files... (' + (idx + 1) + '/' + files.length + ')', ((idx + 1) / files.length) * 100);
                    next();
                };
                reader.readAsArrayBuffer(file);
            }
            next();
        });
        } catch (ex) {
            console.warn('[sprat] drop processing error:', ex);
            Module.spratDropInProgress = false;
        }
        return false;
    }, true);

    if (!Module.spratResizeGuardInstalled) {
        Module.spratResizeGuardInstalled = true;
        var isAsyncBusy = function() {
            var asyncify = (typeof Asyncify !== 'undefined' ? Asyncify : (typeof Module !== 'undefined' ? Module['Asyncify'] : null));
            return !!(asyncify && asyncify.currData);
        };
        var scheduleResizeReplay = function() {
            if (!Module.spratPendingResize) return;
            if (isAsyncBusy()) {
                setTimeout(scheduleResizeReplay, 100);
                return;
            }
            Module.spratPendingResize = false;
            window.dispatchEvent(new Event('resize'));
        };
        window.addEventListener('resize', function (e) {
            if (!isAsyncBusy()) {
                return;
            }
            if (e) {
                try {
                    e.preventDefault();
                    e.stopImmediatePropagation();
                } catch (ex) {
                    console.warn('[sprat] resize event handling error:', ex);
                }
            }
            Module.spratPendingResize = true;
            setTimeout(scheduleResizeReplay, 100);
        }, true);
    }
});

EM_JS(void, sprat_download_file, (const char* path, const char* filename), {
    var pathStr = UTF8ToString(path);
    var filenameStr = UTF8ToString(filename);
    try {
        var data = FS.readFile(pathStr);
        var blob = new Blob([data]);
        var url  = URL.createObjectURL(blob);
        var a    = document.createElement('a');
        a.href = url; a.download = filenameStr;
        document.body.appendChild(a); a.click(); document.body.removeChild(a);
        setTimeout(function() { URL.revokeObjectURL(url); }, 1000);
    } catch (e) { console.error('[sprat] download failed for', pathStr, e); }
});

}

void wasmDownloadFile(const QString& path) {
    QFileInfo fi(path);
    sprat_download_file(path.toUtf8().constData(), fi.fileName().toUtf8().constData());
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
        wasmHandleFilePicked(pendingPath, pendingMode);
    });
}
}

void wasmOpenFileDialog(bool selectFolder) {
    sprat_open_file_dialog(selectFolder ? 1 : 0);
}

void wasmOpenFileDialogMode(int mode) {
    sprat_open_file_dialog(mode);
}

void wasmSetFilePickedHandler(const std::function<void(const QString&, int)>& handler) {
    g_filePickedHandler = handler;
}

void wasmClearFilePickedHandler() {
    g_filePickedHandler = {};
}

void wasmInstallDropHandlers() {
    sprat_install_drop_handlers();
}

void wasmSetupKeyboardFocus() {
    sprat_setup_keyboard_focus();
}

void wasmBlockAllDrags() {
    sprat_block_all_drags();
}

void wasmHandleFilePicked(const QString& path, int mode) {
    if (g_filePickedHandler) {
        g_filePickedHandler(path, mode);
        return;
    }

    MainWindow* window = MainWindow::wasmInstance();
    if (!window) {
        return;
    }
    QMetaObject::invokeMethod(window, [window, path, mode]() {
        window->onWasmFilePicked(path, mode);
    }, Qt::QueuedConnection);
}

#endif
