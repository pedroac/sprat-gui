#include <QApplication>
#include <QtTest>
#include <iostream>

#include "LayoutTests.h"
#include "AnimationTests.h"
#include "ProjectTests.h"
#include "CoreTests.h"
#include "ImageDiscoveryTests.h"
#include "ProjectSessionTests.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    int status = 0;
    
    auto runTest = [&](QObject* testObj) {
        status |= QTest::qExec(testObj, argc, argv);
    };

    LayoutTests layoutTests;
    runTest(&layoutTests);

    AnimationTests animationTests;
    runTest(&animationTests);

    ProjectTests projectTests;
    runTest(&projectTests);

    CoreTests coreTests;
    runTest(&coreTests);

    ImageDiscoveryTests imageDiscoveryTests;
    runTest(&imageDiscoveryTests);

    ProjectSessionTests projectSessionTests;
    runTest(&projectSessionTests);

    return status;
}
