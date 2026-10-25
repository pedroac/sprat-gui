#pragma once

#include <QObject>

class NineSliceWorkspaceTests : public QObject {
    Q_OBJECT

private slots:
    void testInsetsThatDontFitAreFittedOnLoad();
    void testInsetsThatFitAreKeptOnLoad();
};
