#include "SpratCliLocator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStringList>

QString findSiblingSpratCliBinary(const QString& name) {
    QDir appDir(QCoreApplication::applicationDirPath());
    QStringList roots;
    roots << appDir.absolutePath();
    if (appDir.cdUp()) {
        roots << appDir.absolutePath();
        if (appDir.cdUp()) {
            roots << appDir.absolutePath();
        }
    }

    QSet<QString> candidateDirs;
    for (const QString& root : roots) {
        const QString baseDir = QDir(root).absoluteFilePath("sprat-cli");
        candidateDirs.insert(baseDir);
        candidateDirs.insert(QDir(baseDir).absoluteFilePath("build"));
    }

    for (const QString& dirPath : candidateDirs) {
        QDir binDir(dirPath);
        if (!binDir.exists()) {
            continue;
        }

        QFileInfo candidate(binDir.filePath(name));
        if (candidate.exists() && candidate.isExecutable()) {
            return candidate.absoluteFilePath();
        }
    }

    return QString();
}
