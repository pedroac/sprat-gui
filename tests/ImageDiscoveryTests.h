#pragma once
#include <QtTest>

class ImageDiscoveryTests : public QObject {
    Q_OBJECT
private slots:
    void testDiscoveryFindsAllImages();
    void testDiscoveryRespectsExclusions();
};
