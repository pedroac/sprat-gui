#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QTranslator>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QTranslator appTranslator;
    const QString locale = QLocale::system().name();
    const QString baseName = QString("sprat-gui_%1").arg(locale);
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList translationDirs = {
        appDir + "/i18n",
        appDir,
        QDir::currentPath() + "/i18n"
    };
    for (const QString& dir : translationDirs) {
        if (appTranslator.load(baseName, dir)) {
            app.installTranslator(&appTranslator);
            break;
        }
    }

    MainWindow window;
    window.show();

    return app.exec();
}
