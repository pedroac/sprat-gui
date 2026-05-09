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

    // Show window after event loop starts with a small delay
    QTimer::singleShot(100, &mainWindow, &MainWindow::show);

    // Start Qt event loop
    return app.exec();
}
