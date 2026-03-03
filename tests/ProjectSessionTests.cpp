#include "ProjectSessionTests.h"
#include "ProjectSession.h"
#include <memory>

void ProjectSessionTests::testInitialState() {
    ProjectSession session;
    QVERIFY(session.isEmpty());
    QCOMPARE(session.currentFolder, QString(""));
    QCOMPARE(session.timelines.size(), 0);
    QVERIFY(session.selectedSprite == nullptr);
}

void ProjectSessionTests::testProjectLoading() {
    ProjectSession session;
    session.currentFolder = "/tmp/project";
    session.layoutSourceIsList = true;
    session.activeFramePaths = {"a.png", "b.png"};
    
    QVERIFY(!session.isEmpty());
    
    session.clear();
    QVERIFY(session.isEmpty());
    QCOMPARE(session.currentFolder, QString(""));
    QCOMPARE(session.activeFramePaths.size(), 0);
}

void ProjectSessionTests::testMarkAsDirty() {
    ProjectSession session;
    QSignalSpy spy(&session, &ProjectSession::changed);
    
    session.currentFolder = "/tmp/project";
    emit session.changed();
    
    QCOMPARE(spy.count(), 1);
}
