#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QTranslator>
#include "MainWindow.h"
#include "CliToolsConfig.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/bind.h>

extern "C" {
    EMSCRIPTEN_KEEPALIVE
    void sync_idbfs() {
        EM_ASM(
            FS.syncfs(false, function (err) {
                if (err) console.error('FS.syncfs error:', err);
            });
        );
    }
}

// Force embind symbols to be included
EMSCRIPTEN_BINDINGS(sprat_gui_dummy) {
    emscripten::function("sprat_gui_dummy_func", &sync_idbfs);
}
#endif

/**
 * @brief Main entry point for the sprat-gui application.
 * 
 * Initializes the Qt application, sets up internationalization,
 * and launches the main window.
 * 
 * @param argc Command line argument count
 * @param argv Command line arguments
 * @return int Application exit code
 */
int main(int argc, char *argv[]) {
#ifdef Q_OS_WASM
    // Mount IndexedDB for persistence
    EM_ASM({
        try {
            var path = '/home/webuser/.config';
            FS.mkdirTree(path);
            // Check if already mounted to avoid errors on restart
            var mnt = FS.findObject(path).mount;
            if (!mnt) {
                FS.mount(IDBFS, {}, path);
                FS.syncfs(true, function (err) {
                    if (err) console.error('FS.syncfs error:', err);
                });
            }
        } catch (e) {
            console.warn('IndexedDB mount skipped:', e);
        }
    });
#endif
    QApplication app(argc, argv);

    // Initialize CLI tools configuration
    CliToolsConfig::ensureConfigExists();

    // Set up internationalization
    QTranslator translator;
    const QString systemLocale = QLocale::system().name();
    const QString translationBaseName = QString("sprat-gui_%1").arg(systemLocale);
    const QString applicationDirectory = QCoreApplication::applicationDirPath();

    // Search for translation files in multiple locations
    const QStringList translationDirectories = {
        applicationDirectory + "/i18n",
        applicationDirectory,
        QDir::currentPath() + "/i18n"
    };

    bool translationLoaded = false;
    for (const QString& directory : translationDirectories) {
        if (translator.load(translationBaseName, directory)) {
            app.installTranslator(&translator);
            translationLoaded = true;
            break;
        }
    }

    // Log translation loading status for debugging
    if (!translationLoaded) {
        qWarning() << "Warning: Translation file not found for locale" << systemLocale;
    }

    // Create and show main window
    MainWindow mainWindow;
    mainWindow.show();

    // Start Qt event loop
    return app.exec();
}
