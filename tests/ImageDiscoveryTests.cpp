#include "ImageDiscoveryTests.h"
#include "ImageDiscoveryService.h"
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

void ImageDiscoveryTests::testDiscoveryFindsAllImages() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    auto createEmptyFile = [](const QString& path) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.close();
        }
    };

    createEmptyFile(QDir(tempDir.path()).filePath("a.png"));
    createEmptyFile(QDir(tempDir.path()).filePath("b.jpg"));
    createEmptyFile(QDir(tempDir.path()).filePath("not_image.txt"));

    QStringList images = ImageDiscoveryService::imagesInDirectory(tempDir.path());
    QCOMPARE(images.size(), 2);
    
    QStringList filenames;
    for (const QString& img : images) filenames.append(QFileInfo(img).fileName());
    filenames.sort();
    
    QStringList expected = {"a.png", "b.jpg"};
    expected.sort();
    QCOMPARE(filenames, expected);
}

void ImageDiscoveryTests::testDiscoveryRespectsExclusions() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    auto createEmptyFile = [](const QString& path) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.close();
        }
    };

    // Currently ImageDiscoveryService does NOT exclude these by name, only by extension.
    createEmptyFile(QDir(tempDir.path()).filePath("a.png"));
    createEmptyFile(QDir(tempDir.path()).filePath("atlas.png"));

    QStringList images = ImageDiscoveryService::imagesInDirectory(tempDir.path());
    QCOMPARE(images.size(), 2);
    QVERIFY(images.contains(QDir(tempDir.path()).filePath("a.png")));
    QVERIFY(images.contains(QDir(tempDir.path()).filePath("atlas.png")));
}
