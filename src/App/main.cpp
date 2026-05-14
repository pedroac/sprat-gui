#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QTranslator>
#include <QTimer>
#include <QStyleFactory>
#include "MainWindow.h"
#include "CliToolsConfig.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

extern "C" {
    EMSCRIPTEN_KEEPALIVE
    void sync_idbfs() {
        // Safe check to avoid Asyncify collisions
        EM_ASM(
            if (typeof Asyncify !== 'undefined' && Asyncify.currData) return;
            if (typeof Module !== 'undefined' && Module.asm && Module.asm.async_status && Module.asm.async_status() !== 0) return;
            if (typeof FS !== 'undefined' && FS.syncfs) {
                if (FS.syncing) return;
                FS.syncfs(false, function (err) {
                    if (err) console.error('FS.syncfs error:', err);
                });
            }
        );
    }
    bool jsIsAsyncBusy();
}
#endif

int main(int argc, char *argv[]) {
    // Force a safe style to avoid crashes from unavailable styles like 'kvantum'
    // This must be done before QApplication is created
    qputenv("QT_STYLE_OVERRIDE", "Fusion");

    QApplication app(argc, argv);

#ifdef __EMSCRIPTEN__
    // Disable Asyncify's overly strict "multiple async operations" checks.
    // This allows resize and other events to work without false-positive assertions.
    // See: https://stackoverflow.com/questions/76878211
    EM_ASM(
        if (typeof Asyncify !== 'undefined') {
            Asyncify.whenDone = () => Promise.resolve();
            console.log("[Sprat] Asyncify checks disabled");
        }
        // Add global error handler to catch exceptions before they crash
        window.addEventListener('error', function(e) {
            console.error('[sprat] Uncaught error:', e.error, e.message);
            e.preventDefault();
            return true;
        }, true);
        window.addEventListener('unhandledrejection', function(e) {
            console.error('[sprat] Unhandled promise rejection:', e.reason);
            e.preventDefault();
            return true;
        }, true);
        // Ensure keyboard input focus and forward regular keys to Qt
        function ensureCanvasFocus() {
            if (Module.canvas) {
                Module.canvas.focus();
                // Remove focus from any input elements to ensure canvas gets key events
                if (document.activeElement && document.activeElement !== Module.canvas) {
                    if (document.activeElement.blur) {
                        document.activeElement.blur();
                    }
                }
            }
        }
        // Attach listeners directly to the canvas to catch events before Qt consumes them
        if (Module.canvas) {
            Module.canvas.addEventListener('click', function(e) {
                console.log("[Click] on canvas");
                setTimeout(ensureCanvasFocus, 0);
            }, true);

            Module.canvas.addEventListener('mousedown', function(e) {
                console.log("[MouseDown] on canvas");
                setTimeout(ensureCanvasFocus, 0);
            }, true);
        }

        document.addEventListener('click', function(e) {
            console.log("[Click] on document");
            setTimeout(ensureCanvasFocus, 0);
        }, true);
        document.addEventListener('focus', function() {
            setTimeout(ensureCanvasFocus, 0);
        }, true);
        window.addEventListener('focus', ensureCanvasFocus, true);

        // Workaround: Forward non-modifier keys that Qt doesn't receive and prevent browser defaults
        document.addEventListener('keydown', function(e) {
            var key = e.key;
            var code = e.code;
            var isModifier = /^(Control|Shift|Alt|Meta)/.test(key);

            console.log("[KeyDown] key=" + key + " code=" + code);

            // Keys that we always want to prevent browser defaults for
            var keysToPrevent = [
                'Delete', 'Backspace',
                'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight',
                'Home', 'End', 'Enter', 'Escape', ' '
            ];
            var shouldPrevent = keysToPrevent.indexOf(key) !== -1;

            // Prevent browser defaults for keys the app handles
            if (shouldPrevent) {
                e.preventDefault();
            }

            // Prevent browser zoom shortcuts (Ctrl+0, Ctrl+1, etc)
            if ((e.ctrlKey || e.metaKey) && /^[0-9\-=]/.test(key)) {
                e.preventDefault();
            }

            // Ensure canvas has focus
            if (!isModifier && Module.canvas && document.activeElement !== Module.canvas) {
                setTimeout(ensureCanvasFocus, 0);
            }
        }, true);

        // Drag event interception for non-file content (Qt WASM crash workaround)
        // is handled by sprat_install_drop_handlers() in WasmFileDialog.cpp,
        // which also handles file drops and URL drops.
    );
#endif

    // Initialize CLI tools configuration after a generous delay
    QTimer::singleShot(1000, []() {
#ifdef __EMSCRIPTEN__
        if (jsIsAsyncBusy()) {
            QTimer::singleShot(500, []() { CliToolsConfig::ensureConfigExists(); });
            return;
        }
#endif
        CliToolsConfig::ensureConfigExists();
    });

    // Set up internationalization
    QTranslator translator;
    const QString systemLocale = QLocale::system().name();
    const QString translationBaseName = QString("sprat-gui_%1").arg(systemLocale);
    app.installTranslator(&translator);

    const QStringList translationDirectories = {
        ":/i18n",
        QCoreApplication::applicationDirPath() + "/i18n",
        QCoreApplication::applicationDirPath(),
        QDir::currentPath() + "/i18n"
    };

    for (const QString& directory : translationDirectories) {
        if (translator.load(translationBaseName, directory)) {
            break;
        }
    }

    // Create main window
    MainWindow mainWindow;

    // Show window and ensure it has focus for keyboard input
    QTimer::singleShot(100, &mainWindow, [&mainWindow]() {
        mainWindow.show();
        mainWindow.setFocus();
        mainWindow.activateWindow();
    });

    // Start Qt event loop
    return app.exec();
}
