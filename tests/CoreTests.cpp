#include "CoreTests.h"
#include "MarkerUtils.h"
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

void CoreTests::testMarkerNameNormalization() {
    QCOMPARE(normalizeMarkerName("pivot"), QString("pivot"));
    QCOMPARE(normalizeMarkerName("PIVOT"), QString("pivot"));
    QCOMPARE(normalizeMarkerName("  Pivot  "), QString("pivot"));
    QCOMPARE(normalizeMarkerName("  hand_left "), QString("hand_left"));
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

#ifndef Q_OS_WASM
#include "UpdateChecker.h"
#endif

void CoreTests::testUpdateOffersOnlyNewerVersions() {
#ifdef Q_OS_WASM
    QSKIP("The web build has no updater");
#else
    QVERIFY(UpdateChecker::isNewerVersion("v0.12.1", "v0.12.0"));
    QVERIFY(UpdateChecker::isNewerVersion("v0.13.0", "v0.12.0"));
    QVERIFY(UpdateChecker::isNewerVersion("v1.0.0", "v0.99.9"));
    QVERIFY(UpdateChecker::isNewerVersion("0.12.10", "v0.12.9")); // numeric, not text order

    QVERIFY(!UpdateChecker::isNewerVersion("v0.11.0", "v0.12.0")); // never downgrade
    QVERIFY(!UpdateChecker::isNewerVersion("v0.12.0", "v0.12.0"));
    QVERIFY(!UpdateChecker::isNewerVersion("", "v0.12.0"));
    QVERIFY(!UpdateChecker::isNewerVersion("latest", "v0.12.0"));
#endif
}
