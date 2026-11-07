#include "CoreTests.h"
#include "MarkerUtils.h"
#include "models.h"
#include "ArchiveExtractor.h"
#include "PathSafety.h"
#include "Undo/TrashBin.h"
#include "EditorOverlayItem.h"

#include <archive.h>
#include <archive_entry.h>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>

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

void CoreTests::testUpdateAssetNeedsSha256Digest() {
#ifdef Q_OS_WASM
    QSKIP("The web build has no updater");
#else
    const QString digest = QStringLiteral("sha256:") + QString(64, QLatin1Char('A'));
    const QJsonArray assets{
        QJsonObject{{"name", "other.zip"}, {"browser_download_url", "https://x/other.zip"}, {"digest", digest}},
        QJsonObject{{"name", "app.zip"}, {"browser_download_url", "https://x/app.zip"}, {"digest", digest}},
        QJsonObject{{"name", "nodigest.zip"}, {"browser_download_url", "https://x/nodigest.zip"}},
        QJsonObject{{"name", "baddigest.zip"}, {"browser_download_url", "https://x/b.zip"}, {"digest", "md5:abc"}},
    };
    QString url;
    QString sha256;
    QVERIFY(UpdateChecker::findAsset(assets, "app.zip", url, sha256));
    QCOMPARE(url, QString("https://x/app.zip"));
    QCOMPARE(sha256, QString(64, QLatin1Char('a')));
    QVERIFY(!UpdateChecker::findAsset(assets, "nodigest.zip", url, sha256));
    QVERIFY(!UpdateChecker::findAsset(assets, "baddigest.zip", url, sha256));
    QVERIFY(!UpdateChecker::findAsset(assets, "missing.zip", url, sha256));
#endif
}

void CoreTests::testPathSafetyRejectsEscapingPaths() {
    QVERIFY(PathSafety::isContainedRelativePath("ui"));
    QVERIFY(PathSafety::isContainedRelativePath("ui/icons"));
    QVERIFY(PathSafety::isContainedRelativePath("a..b"));
    QVERIFY(!PathSafety::isContainedRelativePath(""));
    QVERIFY(!PathSafety::isContainedRelativePath("/home/user"));
    QVERIFY(!PathSafety::isContainedRelativePath("\\\\server\\share"));
    QVERIFY(!PathSafety::isContainedRelativePath("C:/Users"));
    QVERIFY(!PathSafety::isContainedRelativePath(".."));
    QVERIFY(!PathSafety::isContainedRelativePath("a/../../b"));
    QVERIFY(!PathSafety::isContainedRelativePath("a\\..\\..\\b"));

    QVERIFY(PathSafety::isPlainName("mobile"));
    QVERIFY(!PathSafety::isPlainName(".."));
    QVERIFY(!PathSafety::isPlainName("."));
    QVERIFY(!PathSafety::isPlainName("a/b"));
    QVERIFY(!PathSafety::isPlainName("/home"));

    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(PathSafety::isInsideFolder(root.filePath("a/b"), root.path()));
    QVERIFY(!PathSafety::isInsideFolder(root.path(), root.path()));
    QVERIFY(!PathSafety::isInsideFolder(root.filePath("a/../.."), root.path()));
    QVERIFY(!PathSafety::isInsideFolder(root.path() + "2/x", root.path())); // sibling with same prefix

    QCOMPARE(PathSafety::folderNameFromLabel("UI Menus"), QString("ui_menus"));
    QCOMPARE(PathSafety::folderNameFromLabel("a/b\\c"), QString("a_b_c"));
    QCOMPARE(PathSafety::folderNameFromLabel(".."), QString("atlas"));
    QCOMPARE(PathSafety::folderNameFromLabel("  "), QString("atlas"));
}

void CoreTests::testArchiveExtractionSkipsLinks() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString victim = root.filePath("victim.txt");
    {
        QFile f(victim);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("ORIGINAL");
    }

    // A tar with a hard link to a file outside the extraction folder carrying data
    // (which would overwrite it), a symlink, and one regular sprite.
    const QString tarPath = root.filePath("evil.tar");
    struct archive* a = archive_write_new();
    archive_write_set_format_pax_restricted(a);
    QCOMPARE(archive_write_open_filename(a, tarPath.toUtf8().constData()), ARCHIVE_OK);
    auto addEntry = [a](const char* name, const QByteArray& data, const char* hardlink, const char* symlink) {
        struct archive_entry* e = archive_entry_new();
        archive_entry_set_pathname(e, name);
        archive_entry_set_filetype(e, symlink ? AE_IFLNK : AE_IFREG);
        archive_entry_set_perm(e, 0644);
        archive_entry_set_size(e, data.size());
        if (hardlink) archive_entry_set_hardlink(e, hardlink);
        if (symlink) archive_entry_set_symlink(e, symlink);
        archive_write_header(a, e);
        if (!data.isEmpty()) archive_write_data(a, data.constData(), static_cast<size_t>(data.size()));
        archive_entry_free(e);
    };
    addEntry("link.png", "PWNED", victim.toUtf8().constData(), nullptr);
    addEntry("sym.png", {}, nullptr, victim.toUtf8().constData());
    addEntry("sprite.png", "PNG", nullptr, nullptr);
    archive_write_close(a);
    archive_write_free(a);

    const QString out = root.filePath("out");
    QString error;
    QVERIFY2(ArchiveExtractor::extractToDirectory(tarPath, out, error), qPrintable(error));

    QFile f(victim);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("ORIGINAL"));
    QVERIFY(QFileInfo::exists(QDir(out).filePath("sprite.png")));
    QVERIFY(!QFileInfo::exists(QDir(out).filePath("link.png")));
    QVERIFY(!QFileInfo(QDir(out).filePath("sym.png")).isSymLink());
    QCOMPARE(ArchiveExtractor::listEntries(tarPath, error), QStringList{"sprite.png"});
}

void CoreTests::testCreateZipRoundTripsAndKeepsOriginalOnFailure() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString source = root.filePath("source");
    QVERIFY(QDir().mkpath(QDir(source).filePath("sub")));
    for (const QString& name : {QString("a.png"), QString("sub/b.png")}) {
        QFile f(QDir(source).filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(name.toUtf8());
    }

    const QString zipPath = root.filePath("sprites.zip");
    QString error;
    QVERIFY2(ArchiveExtractor::createZip(source, zipPath, error), qPrintable(error));
    QStringList entries = ArchiveExtractor::listEntries(zipPath, error);
    entries.sort();
    QCOMPARE(entries, (QStringList{"a.png", "sub/b.png"}));
    QByteArray data;
    QVERIFY2(ArchiveExtractor::readFileFromArchive(zipPath, "sub/b.png", data, error, true), qPrintable(error));
    QCOMPARE(data, QByteArray("sub/b.png"));

#if defined(Q_OS_UNIX)
    // An unreadable file makes the ZIP fail; the existing ZIP must stay as it was.
    const QString unreadable = QDir(source).filePath("locked.png");
    {
        QFile f(unreadable);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    QFile::setPermissions(unreadable, QFileDevice::Permissions());
    if (QFile(unreadable).open(QIODevice::ReadOnly))
        QSKIP("Running with permissions that can read any file");
    QFile original(zipPath);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const QByteArray before = original.readAll();
    original.close();

    QVERIFY(!ArchiveExtractor::createZip(source, zipPath, error));
    QVERIFY(error.contains("locked.png"));
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), before);
    QFile::setPermissions(unreadable, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#endif
}

void CoreTests::testTrashBinMoveFolderKeepsExistingTarget() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString from = root.filePath("from");
    const QString to = root.filePath("to");
    QVERIFY(QDir().mkpath(QDir(from).filePath("sub")));
    QVERIFY(QDir().mkpath(to));
    QFile keep(QDir(to).filePath("keep.txt"));
    QVERIFY(keep.open(QIODevice::WriteOnly));
    keep.close();

    // An existing target is never replaced or removed.
    QVERIFY(!TrashBin::moveFolder(from, to));
    QVERIFY(QFileInfo::exists(QDir(to).filePath("keep.txt")));
    QVERIFY(QDir(from).exists());

    const QString moved = root.filePath("moved");
    QVERIFY(TrashBin::moveFolder(from, moved));
    QVERIFY(!QDir(from).exists());
    QVERIFY(QDir(QDir(moved).filePath("sub")).exists());
}

namespace {
void clickOverlay(QGraphicsScene& scene, EditorOverlayItem* overlay, const QPointF& pos) {
    for (auto type : {QEvent::GraphicsSceneMousePress, QEvent::GraphicsSceneMouseRelease}) {
        QGraphicsSceneMouseEvent event(type);
        event.setButton(Qt::LeftButton);
        event.setButtons(type == QEvent::GraphicsSceneMousePress ? Qt::LeftButton : Qt::NoButton);
        event.setPos(pos);
        event.setScenePos(pos);
        scene.sendEvent(overlay, &event);
    }
}
}

void CoreTests::testClickOnEmptySpacePositionsSelectedMarker() {
    auto sprite = std::make_shared<Sprite>();
    sprite->pivotX = 5;
    sprite->pivotY = 5;
    NamedPoint hand;
    hand.name = "hand";
    hand.x = 10;
    hand.y = 10;
    NamedPoint box;
    box.name = "box";
    box.kind = MarkerKind::Rectangle;
    box.x = 60;
    box.y = 60;
    box.w = 10;
    box.h = 20;
    sprite->points = {hand, box};

    QGraphicsScene scene;
    auto* overlay = new EditorOverlayItem;
    scene.addItem(overlay);
    overlay->setSceneSize(QSize(100, 100));
    overlay->setSprites({sprite});
    QSignalSpy finished(overlay, &EditorOverlayItem::markerDragFinished);

    overlay->setSelectedMarker("hand");
    clickOverlay(scene, overlay, QPointF(40, 30));
    QCOMPARE(QPoint(sprite->points[0].x, sprite->points[0].y), QPoint(40, 30));
    QCOMPARE(QPoint(sprite->pivotX, sprite->pivotY), QPoint(5, 5));
    QCOMPARE(overlay->selectedMarkerName(), QString("hand"));
    QCOMPARE(finished.count(), 1);

    // A rectangle is centred on the click.
    overlay->setSelectedMarker("box");
    clickOverlay(scene, overlay, QPointF(20, 80));
    QCOMPARE(QRect(sprite->points[1].x, sprite->points[1].y, sprite->points[1].w, sprite->points[1].h),
             QRect(15, 70, 10, 20));
    QCOMPARE(QPoint(sprite->points[0].x, sprite->points[0].y), QPoint(40, 30));

    // With no marker selected, the click places the pivot.
    overlay->setSelectedMarker(QString());
    clickOverlay(scene, overlay, QPointF(90, 10));
    QCOMPARE(QPoint(sprite->pivotX, sprite->pivotY), QPoint(90, 10));
    QCOMPARE(QPoint(sprite->points[0].x, sprite->points[0].y), QPoint(40, 30));
}
