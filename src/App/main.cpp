#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QTranslator>
#include "MainWindow.h"

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
    QApplication app(argc, argv);

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
