#pragma once
#include <QtTest>

class LayoutTests : public QObject {
    Q_OBJECT
private slots:
    void testLayoutParserHandlesEscapedQuotes();
    void testLayoutParserCleansRelativeRoot();
    void testTimelineBuilderParsesSupportedPatterns();
    void testLayoutArgumentsIncludeResolutionAndDedupThreshold();
};
