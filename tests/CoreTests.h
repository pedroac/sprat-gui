#pragma once
#include <QtTest>

class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void testMarkerKindConversions();
    void testMarkerNameNormalization();
    void testResolutionUtils();
};
