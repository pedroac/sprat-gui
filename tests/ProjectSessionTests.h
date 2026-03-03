#pragma once
#include <QtTest>

class ProjectSessionTests : public QObject {
    Q_OBJECT
private slots:
    void testInitialState();
    void testProjectLoading();
    void testMarkAsDirty();
};
