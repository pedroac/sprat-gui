#include "CoreTests.h"
#include "models.h"

void CoreTests::testMarkerKindConversions() {
    QCOMPARE(markerKindToString(MarkerKind::Point), QString("point"));
    QCOMPARE(markerKindToString(MarkerKind::Circle), QString("circle"));
    QCOMPARE(markerKindToString(MarkerKind::Rectangle), QString("rectangle"));
    QCOMPARE(markerKindToString(MarkerKind::Polygon), QString("polygon"));

    QCOMPARE(markerKindFromString(QStringView(u"circle")), MarkerKind::Circle);
    QCOMPARE(markerKindFromString(QStringView(u"rect")), MarkerKind::Rectangle);
    QCOMPARE(markerKindFromString(QStringView(u"rectangle")), MarkerKind::Rectangle);
    QCOMPARE(markerKindFromString(QStringView(u"polygon")), MarkerKind::Polygon);
    QCOMPARE(markerKindFromString(QStringView(u"unknown")), MarkerKind::Point); // Fallback
}

#include "ResolutionUtils.h"

void CoreTests::testResolutionUtils() {
    int w, h;
    QVERIFY(parseResolutionText("1920x1080", w, h));
    QCOMPARE(w, 1920);
    QCOMPARE(h, 1080);

    QVERIFY(parseResolutionText(" 640 x 480 ", w, h));
    QCOMPARE(w, 640);
    QCOMPARE(h, 480);

    QVERIFY(!parseResolutionText("invalid", w, h));
    QVERIFY(!parseResolutionText("100x", w, h));
    QVERIFY(!parseResolutionText("x100", w, h));
    QVERIFY(!parseResolutionText("-10x10", w, h));

    QCOMPARE(formatResolutionText(800, 600), QString("800x600"));
}
