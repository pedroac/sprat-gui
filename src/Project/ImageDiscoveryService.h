#pragma once

#include <QString>
#include <QStringList>

class ImageDiscoveryService {
public:
    static const QStringList& supportedImageFilters();
    static bool hasImageFiles(const QString& path);
    static QStringList imageDirectoriesOneLevel(const QString& root);
    static QStringList imageDirectoriesRecursive(const QString& root);
    static QStringList imagesInDirectory(const QString& path);
    static QStringList collectImagesRecursive(const QStringList& roots);
};
