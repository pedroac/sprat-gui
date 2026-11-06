#include "PuppetTests.h"
#include "ProjectPayloadCodec.h"
#include "PuppetEditing.h"
#include "PuppetEvaluator.h"
#include "PuppetTextCodec.h"

#include <QJsonArray>
#include <QJsonObject>

namespace {

// The example of sprat-cli's README, "Puppets".
const char* const kReadmeExample = R"(root "./pinocchio"
puppet "pinocchio"

part "face" at:0,-40
- sprite "face.png"

part "nose"
- sprite "nose-short.png" -> "short"
- sprite "nose-long.png" -> "long"

part "mouth"
- sprite "mouth-smile.png" -> "smile"
- sprite "mouth-angry.png" -> "angry"
- animation "talk"

part "eye"
- sprite "eye.png"

part "pupil"
- sprite "pupil.png"

attach "nose":"pivot" to "face":"nose" angle:-10..10
attach "mouth":"pivot" to "face":"mouth" leeway:2
attach "eye":"pivot" to "face":"eye"
attach "pupil":"pivot" to "eye":"socket"

part "fly" at:24,-70
- animation "buzz"

pose "angry"
- part "mouth" variant:"angry"
- part "nose" angle:-8

pose "look left"
- part "pupil" offset:-2,0
- part "fly" at:12,-75

sequence "fuss" fps:12 loop:pingpong
- pose "angry" in:3 hold:2
- pose "look left" hold:4 ease:step
)";

PuppetVariant spriteVariant(const QString& source) {
    PuppetVariant variant;
    variant.source = source;
    return variant;
}

// A lookup over sprites keyed by their path.
PuppetSpriteLookup lookupIn(const QHash<QString, PuppetSpriteGeometry>& sprites) {
    return [sprites](const PuppetVariant& variant) -> std::optional<PuppetSpriteGeometry> {
        const auto it = sprites.constFind(variant.source);
        if (it == sprites.constEnd()) return std::nullopt;
        return *it;
    };
}

void comparePoints(const QPointF& actual, const QPointF& expected) {
    QVERIFY2(qAbs(actual.x() - expected.x()) < 1e-6 && qAbs(actual.y() - expected.y()) < 1e-6,
             qPrintable(QStringLiteral("(%1,%2) != (%3,%4)")
                            .arg(actual.x()).arg(actual.y()).arg(expected.x()).arg(expected.y())));
}

// face positioned at 0,-40 with its pivot at 10,10 and a "nose" marker at 15,20; nose
// attached by its pivot (2,3) to it.
Puppet faceAndNose() {
    Puppet puppet;
    puppet.name = QStringLiteral("p");
    PuppetPart face{QStringLiteral("face"), {spriteVariant(QStringLiteral("/s/face.png"))}, QPoint(0, -40)};
    PuppetPart nose{QStringLiteral("nose"), {spriteVariant(QStringLiteral("/s/nose.png"))}, std::nullopt};
    // The child comes first in draw order: parents are still placed first.
    puppet.parts = {nose, face};
    puppet.attachments = {{QStringLiteral("nose"), QStringLiteral("pivot"), QStringLiteral("face"),
                           QStringLiteral("nose"), 0, std::nullopt, std::nullopt}};
    return puppet;
}

QHash<QString, PuppetSpriteGeometry> faceAndNoseSprites() {
    PuppetSpriteGeometry face;
    face.pivot = QPointF(10, 10);
    face.markers.insert(QStringLiteral("nose"), QPointF(15, 20));
    PuppetSpriteGeometry nose;
    nose.pivot = QPointF(2, 3);  // no "pivot" marker: the sprite's pivot is used
    return {{QStringLiteral("/s/face.png"), face}, {QStringLiteral("/s/nose.png"), nose}};
}

}  // namespace

void PuppetTests::testReadsReadmeExample() {
    const PuppetTextReadResult result = PuppetTextCodec::read(QString::fromUtf8(kReadmeExample));
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join('\n')));
    QCOMPARE(result.root, QStringLiteral("./pinocchio"));
    QCOMPARE(result.puppets.size(), 1);

    const Puppet& puppet = result.puppets.first();
    QCOMPARE(puppet.name, QStringLiteral("pinocchio"));
    QCOMPARE(puppet.parts.size(), 6);
    QCOMPARE(puppet.parts[0].name, QStringLiteral("face"));
    QCOMPARE(puppet.parts[0].position, std::optional<QPoint>(QPoint(0, -40)));
    QCOMPARE(puppet.parts[5].name, QStringLiteral("fly"));
    QCOMPARE(puppet.parts[5].position, std::optional<QPoint>(QPoint(24, -70)));
    QVERIFY(!puppet.parts[1].position);

    const PuppetPart& mouth = puppet.parts[2];
    QCOMPARE(mouth.variants.size(), 3);
    QCOMPARE(puppetVariantName(mouth.variants[0]), QStringLiteral("smile"));
    QCOMPARE(mouth.variants[0].source, QStringLiteral("mouth-smile.png"));
    QCOMPARE(mouth.variants[2].kind, PuppetVariant::Kind::Animation);
    QCOMPARE(puppetVariantName(mouth.variants[2]), QStringLiteral("talk"));
    QCOMPARE(puppetVariantName(puppet.parts[0].variants[0]), QStringLiteral("face"));

    QCOMPARE(puppet.attachments.size(), 4);
    const PuppetAttachment& nose = puppet.attachments[0];
    QCOMPARE(nose.child, QStringLiteral("nose"));
    QCOMPARE(nose.childMarker, QStringLiteral("pivot"));
    QCOMPARE(nose.parent, QStringLiteral("face"));
    QCOMPARE(nose.parentMarker, QStringLiteral("nose"));
    QCOMPARE(nose.angleMin, std::optional<int>(-10));
    QCOMPARE(nose.angleMax, std::optional<int>(10));
    QCOMPARE(puppet.attachments[1].leeway, 2);
    QVERIFY(!puppet.attachments[1].angleMin);
    QCOMPARE(puppetParentAttachment(puppet, QStringLiteral("pupil"))->parent, QStringLiteral("eye"));
    QVERIFY(!puppetParentAttachment(puppet, QStringLiteral("face")));

    QCOMPARE(puppet.poses.size(), 2);
    QCOMPARE(puppet.poses[0].settings[0].variant, QStringLiteral("angry"));
    QCOMPARE(puppet.poses[0].settings[1].angle, std::optional<int>(-8));
    QCOMPARE(puppet.poses[1].name, QStringLiteral("look left"));
    QCOMPARE(puppet.poses[1].settings[0].offset, std::optional<QPoint>(QPoint(-2, 0)));
    QCOMPARE(puppet.poses[1].settings[1].position, std::optional<QPoint>(QPoint(12, -75)));

    QCOMPARE(puppet.sequences.size(), 1);
    const PuppetSequence& fuss = puppet.sequences.first();
    QCOMPARE(fuss.fps, 12);
    QCOMPARE(fuss.loopMode, AnimationLoopMode::PingPong);
    QCOMPARE(fuss.steps.size(), 2);
    QCOMPARE(fuss.steps[0].in, 3);
    QCOMPARE(fuss.steps[0].hold, 2);
    QVERIFY(!fuss.steps[0].snap);
    QCOMPARE(fuss.steps[1].pose, QStringLiteral("look left"));
    QVERIFY(fuss.steps[1].snap);
}

void PuppetTests::testWriteThenReadKeepsEverything() {
    const PuppetTextReadResult original = PuppetTextCodec::read(QString::fromUtf8(kReadmeExample));
    const QString written = PuppetTextCodec::write(original.puppets, original.root);
    const PuppetTextReadResult reread = PuppetTextCodec::read(written);
    QVERIFY2(reread.error.isEmpty(), qPrintable(reread.error));
    QVERIFY2(reread.warnings.isEmpty(), qPrintable(reread.warnings.join('\n')));
    QCOMPARE(reread.root, original.root);
    QVERIFY(reread.puppets == original.puppets);

    // Without a root, no root line: the layout stream has its own.
    QVERIFY(!PuppetTextCodec::write(original.puppets).contains(QLatin1String("root ")));
    QVERIFY(PuppetTextCodec::write({}).isEmpty());
}

void PuppetTests::testReadSkipsBadLinesWithWarnings() {
    const PuppetTextReadResult result = PuppetTextCodec::read(QStringLiteral(
        "part \"orphan\"\n"                       // before any puppet
        "puppet \"p\"\n"
        "# a comment\n"
        "part \"a\" at:1,2 size:3\n"              // unknown setting
        "- sprite \"a.png\" extra\n"              // not a variant line
        "- sprite \"b.png\"\n"
        "attach \"b\": \"pivot\" to \"a\":\"x\"\n"  // space after the colon
        "attach \"b\":\"pivot\" to \"a\":\"x\" angle:10..-10\n"  // MIN above MAX
        "bogus line\n"
        "- part \"a\" angle:5\n"                  // not inside a pose
        "pose \"empty\"\n"
        "- part \"a\"\n"                          // sets nothing
        "sequence \"s\"\n"
        "- pose \"empty\"\n"                      // lasts no frame
        "- pose \"empty\" hold:1\n"));
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QCOMPARE(result.puppets.size(), 1);
    const Puppet& puppet = result.puppets.first();
    QCOMPARE(puppet.parts.size(), 1);
    QCOMPARE(puppet.parts[0].position, std::optional<QPoint>(QPoint(1, 2)));
    QCOMPARE(puppet.parts[0].variants.size(), 1);
    QCOMPARE(puppet.parts[0].variants[0].source, QStringLiteral("b.png"));
    QCOMPARE(puppet.attachments.size(), 1);
    QVERIFY(!puppet.attachments[0].angleMin);
    QCOMPARE(puppet.poses.size(), 1);
    QVERIFY(puppet.poses[0].settings.isEmpty());
    QCOMPARE(puppet.sequences[0].steps.size(), 1);
    QCOMPARE(result.warnings.size(), 9);
}

void PuppetTests::testReadRejectsRepeatedNames() {
    const auto errorOf = [](const char* text) {
        return PuppetTextCodec::read(QString::fromUtf8(text));
    };
    const char* const cases[] = {
        "puppet \"p\"\npuppet \"p\"\n",
        "puppet \"p\"\npart \"a\" at:0,0\npart \"a\" at:0,0\n",
        // Both variants go by "a" (one by default, one by name).
        "puppet \"p\"\npart \"x\" at:0,0\n- sprite \"dir/a.png\"\n- sprite \"b.png\" -> \"a\"\n",
        "puppet \"p\"\npose \"x\"\n- part \"a\" angle:1\npose \"x\"\n",
        "puppet \"p\"\nsequence \"s\"\nsequence \"s\"\n",
        "puppet \"p\"\nsequence \"s\" fps:101\n",
        "puppet p q\n",
    };
    for (const char* text : cases) {
        const PuppetTextReadResult result = errorOf(text);
        QVERIFY2(!result.error.isEmpty(), text);
        QVERIFY(result.puppets.isEmpty());
    }
    // The same part name in two puppets is fine.
    QVERIFY(errorOf("puppet \"p\"\npart \"a\" at:0,0\npuppet \"q\"\npart \"a\" at:0,0\n").error.isEmpty());
}

void PuppetTests::testQuotedNamesAndSpriteIndexesRoundTrip() {
    Puppet puppet;
    puppet.name = QStringLiteral("say \"hi\"");
    PuppetPart part;
    part.name = QStringLiteral("back\\slash part");
    part.position = QPoint(-3, 4);
    PuppetVariant byIndex;
    byIndex.source = QStringLiteral("3");
    byIndex.byIndex = true;
    PuppetVariant numberedPath;  // a path that looks like a number stays a path
    numberedPath.source = QStringLiteral("7");
    PuppetVariant named = spriteVariant(QStringLiteral("/sprites/run back.png"));
    named.name = QStringLiteral("run back");
    part.variants = {byIndex, numberedPath, named};
    puppet.parts = {part};
    PuppetPose pose{QStringLiteral("p"), {{part.name, QStringLiteral("run back"), std::nullopt, std::nullopt,
                                           std::nullopt}}};
    puppet.poses = {pose};

    const QString written = PuppetTextCodec::write({puppet});
    QVERIFY(written.contains(QLatin1String("- sprite 3\n")));
    QVERIFY(written.contains(QLatin1String("- sprite \"7\"\n")));
    QVERIFY(written.contains(QLatin1String("variant:\"run back\"")));
    const PuppetTextReadResult reread = PuppetTextCodec::read(written);
    QVERIFY2(reread.error.isEmpty(), qPrintable(reread.error));
    QVERIFY2(reread.warnings.isEmpty(), qPrintable(reread.warnings.join('\n')));
    QCOMPARE(reread.puppets.size(), 1);
    QVERIFY(reread.puppets.first() == puppet);
}

void PuppetTests::testDefaultVariantNamesFollowSpratCli() {
    QCOMPARE(puppetSpriteName(QStringLiteral("/a/b/walk_01.png")), QStringLiteral("walk_01"));
    QCOMPARE(puppetSpriteName(QStringLiteral("a\\b.c.png")), QStringLiteral("b.c"));
    QCOMPARE(puppetSpriteName(QStringLiteral("/a/.hidden")), QStringLiteral(".hidden"));
    QCOMPARE(puppetSpriteName(QStringLiteral("button.9.png")), QStringLiteral("button"));
    QCOMPARE(puppetSpriteName(QStringLiteral("noext")), QStringLiteral("noext"));

    PuppetVariant animation;
    animation.kind = PuppetVariant::Kind::Animation;
    animation.source = QStringLiteral("talk.fast");
    QCOMPARE(puppetVariantName(animation), QStringLiteral("talk.fast"));
    animation.name = QStringLiteral("chatter");
    QCOMPARE(puppetVariantName(animation), QStringLiteral("chatter"));
}

void PuppetTests::testEvaluatorPlacesRestPose() {
    const Puppet puppet = faceAndNose();
    const QVector<PuppetPartPlacement> placements =
        PuppetEvaluator::evaluateRest(puppet, lookupIn(faceAndNoseSprites()));
    QCOMPARE(placements.size(), 2);
    const PuppetPartPlacement& nose = placements[0];
    const PuppetPartPlacement& face = placements[1];
    QVERIFY2(face.placed, qPrintable(face.problem));
    QVERIFY2(nose.placed, qPrintable(nose.problem));

    // The face's pivot sits at its position; its nose marker lands 5,10 further.
    comparePoints(face.transform.map(QPointF(10, 10)), QPointF(0, -40));
    comparePoints(face.pivot, QPointF(0, -40));
    // The nose's pivot goes on the face's nose marker.
    comparePoints(nose.transform.map(QPointF(2, 3)), QPointF(5, -30));
    comparePoints(nose.pivot, QPointF(5, -30));
    QCOMPARE(nose.totalAngle, 0.0);
}

void PuppetTests::testEvaluatorTurnsChildrenWithTheirParent() {
    const Puppet puppet = faceAndNose();
    QVector<PuppetPartState> states(2);
    states[1].angle = 90;                    // the face turns around its pivot
    states[0].angle = 10;                    // the nose turns further
    states[0].offset = QPointF(1, 0);        // and moves along the face's x axis
    const QVector<PuppetPartPlacement> placements =
        PuppetEvaluator::evaluate(puppet, states, lookupIn(faceAndNoseSprites()));
    const PuppetPartPlacement& nose = placements[0];
    const PuppetPartPlacement& face = placements[1];
    QVERIFY(face.placed && nose.placed);

    // Clockwise with Y down: the face's +x points down, so its nose marker (5,10 from the
    // pivot) lands at -10,+5 from it, and the offset (1,0) moves it one pixel down.
    comparePoints(face.transform.map(QPointF(15, 20)), QPointF(-10, -35));
    comparePoints(nose.pivot, QPointF(-10, -34));
    comparePoints(nose.transform.map(QPointF(2, 3)), QPointF(-10, -34));
    QCOMPARE(nose.totalAngle, 100.0);

    // A positioned part's state position replaces its at:.
    states[1].position = QPointF(7, 8);
    comparePoints(PuppetEvaluator::evaluate(puppet, states, lookupIn(faceAndNoseSprites()))[1].pivot,
                  QPointF(7, 8));
}

void PuppetTests::testEvaluatorReportsPartsItCannotPlace() {
    const QHash<QString, PuppetSpriteGeometry> sprites = faceAndNoseSprites();
    // Each part's problem, or "<placed with a problem>" when placed and problem disagree.
    const auto problems = [&sprites](const Puppet& puppet) {
        QStringList out;
        for (const PuppetPartPlacement& p : PuppetEvaluator::evaluateRest(puppet, lookupIn(sprites))) {
            out.append(p.placed == p.problem.isEmpty() ? p.problem : QStringLiteral("<placed with a problem>"));
        }
        return out;
    };

    Puppet missingMarker = faceAndNose();
    missingMarker.attachments[0].parentMarker = QStringLiteral("ear");
    QVERIFY(problems(missingMarker)[0].contains(QLatin1String("\"ear\"")));
    QVERIFY(problems(missingMarker)[1].isEmpty());

    Puppet unknownParent = faceAndNose();
    unknownParent.attachments[0].parent = QStringLiteral("hat");
    QVERIFY(problems(unknownParent)[0].contains(QLatin1String("\"hat\"")));

    Puppet unplaced = faceAndNose();
    unplaced.parts[1].position.reset();  // the face is now neither positioned nor attached
    const QStringList unplacedProblems = problems(unplaced);
    QVERIFY(!unplacedProblems[1].isEmpty());
    QVERIFY(unplacedProblems[0].contains(QLatin1String("\"face\"")));  // the nose hangs from it

    Puppet cycle = faceAndNose();
    cycle.parts[1].position.reset();
    cycle.attachments.append({QStringLiteral("face"), QStringLiteral("pivot"), QStringLiteral("nose"),
                              QStringLiteral("pivot"), 0, std::nullopt, std::nullopt});
    for (const QString& problem : problems(cycle)) QVERIFY(!problem.isEmpty());

    Puppet missingSprite = faceAndNose();
    missingSprite.parts[1].variants[0].source = QStringLiteral("/s/gone.png");
    QVERIFY(!problems(missingSprite)[1].isEmpty());
    QVERIFY(!problems(missingSprite)[0].isEmpty());

    Puppet noVariants = faceAndNose();
    noVariants.parts[1].variants.clear();
    QVERIFY(!problems(noVariants)[1].isEmpty());
}

void PuppetTests::testProjectPayloadKeepsPuppets() {
    Puppet puppet = PuppetTextCodec::read(QString::fromUtf8(kReadmeExample)).puppets.first();
    puppet.parts[0].variants[0].source = QStringLiteral("/tmp/project/sprites/face.png");
    PuppetVariant byName;  // a sprite a puppets file named by name, not a path
    byName.source = QStringLiteral("eye");
    puppet.parts[3].variants = {byName};

    AtlasEntry atlas;
    atlas.id = QStringLiteral("main");
    atlas.name = QStringLiteral("Main");
    atlas.puppets = {puppet};
    ProjectPayloadBuildInput input;
    input.currentFolder = QStringLiteral("/tmp/project");
    input.portablePaths = true;
    input.atlases = {atlas};

    const QJsonObject payload = ProjectPayloadCodec::build(input);
    QCOMPARE(payload["schema_version"].toInt(), 6);
    const QJsonObject savedPart =
        payload["atlases"].toArray()[0].toObject()["puppets"].toArray()[0].toObject()["parts"].toArray()[0].toObject();
    QCOMPARE(savedPart["variants"].toArray()[0].toObject()["source"].toString(),
             QStringLiteral("sprites/face.png"));

    QVector<LayoutModel> layoutModels;
    const ProjectPayloadApplyResult applied =
        ProjectPayloadCodec::applyToLayout(payload, QStringLiteral("/tmp/project"), layoutModels);
    const AtlasEntry* loaded = nullptr;
    for (const AtlasEntry& a : applied.atlases) {
        if (a.id == atlas.id) loaded = &a;
    }
    QVERIFY(loaded);
    QCOMPARE(loaded->puppets.size(), 1);
    QVERIFY(loaded->puppets.first() == puppet);
}

namespace {

/// A face with a nose hanging from it, and a pose that turns the nose and shows its long variant.
Puppet faceWithNose() {
    Puppet puppet;
    puppet.name = "pinocchio";
    PuppetVariant face{PuppetVariant::Kind::Sprite, "/p/face.png", {}, false};
    PuppetVariant shortNose{PuppetVariant::Kind::Sprite, "/p/nose-short.png", "short", false};
    PuppetVariant longNose{PuppetVariant::Kind::Sprite, "/p/nose-long.png", {}, false};
    puppet.parts = {{"face", {face}, QPoint(0, -40)}, {"nose", {shortNose, longNose}, std::nullopt}};
    puppet.attachments = {{"nose", "pivot", "face", "nose"}};
    PuppetPoseSetting lie;
    lie.part = "nose";
    lie.variant = "nose-long";
    lie.angle = 10;
    puppet.poses = {{"lie", {lie}}};
    return puppet;
}

}  // namespace

void PuppetTests::testEditingChecksNames() {
    using namespace PuppetEditing;
    QCOMPARE(uniqueName("part", {}), QString("part"));
    QCOMPARE(uniqueName("part", {"part", "part 2"}), QString("part 3"));

    const QVector<Puppet> puppets{faceWithNose()};
    QVERIFY(puppetNameProblem(puppets, 0, "pinocchio").isEmpty());  // its own name
    QVERIFY(!puppetNameProblem(puppets, -1, "pinocchio").isEmpty());
    QVERIFY(!puppetNameProblem(puppets, 0, " ").isEmpty());

    const Puppet& puppet = puppets.first();
    QVERIFY(!partNameProblem(puppet, 1, "face").isEmpty());
    QVERIFY(partNameProblem(puppet, 1, "beak").isEmpty());

    const PuppetPart& nose = puppet.parts[1];
    QVERIFY(!variantNameProblem(nose, 0, "nose-long").isEmpty());
    // Empty goes back to the default name, nose-short, which nothing else uses.
    QVERIFY(variantNameProblem(nose, 0, "").isEmpty());
}

void PuppetTests::testRenamingAPartRenamesItsJointsAndPoses() {
    Puppet puppet = faceWithNose();
    PuppetEditing::renamePart(puppet, 0, "head");
    QCOMPARE(puppet.parts[0].name, QString("head"));
    QCOMPARE(puppet.attachments[0].parent, QString("head"));
    PuppetEditing::renamePart(puppet, 1, "beak");
    QCOMPARE(puppet.attachments[0].child, QString("beak"));
    QCOMPARE(puppet.poses[0].settings[0].part, QString("beak"));

    PuppetEditing::movePart(puppet, 1, 0);
    QCOMPARE(puppet.parts[0].name, QString("beak"));
    PuppetEditing::movePart(puppet, 0, 5);  // out of range: nothing
    QCOMPARE(puppet.parts[0].name, QString("beak"));
}

void PuppetTests::testRemovingAPartRemovesItsJointsAndPoses() {
    Puppet puppet = faceWithNose();
    PuppetEditing::removePart(puppet, 0);  // the face the nose hangs from
    QCOMPARE(puppet.parts.size(), 1);
    QVERIFY(puppet.attachments.isEmpty());
    QCOMPARE(puppet.poses[0].settings.size(), 1);

    PuppetEditing::removePart(puppet, 0);
    QVERIFY(puppet.poses[0].settings.isEmpty());
    QCOMPARE(puppet.poses.size(), 1);  // the pose itself stays
}

void PuppetTests::testNamingVariants() {
    Puppet puppet = faceWithNose();
    PuppetEditing::setVariantName(puppet, 1, 1, "long");
    QCOMPARE(puppet.parts[1].variants[1].name, QString("long"));
    QCOMPARE(puppet.poses[0].settings[0].variant, QString("long"));

    // The default name is stored as no name, so it follows the sprite.
    PuppetEditing::setVariantName(puppet, 1, 1, "nose-long");
    QVERIFY(puppet.parts[1].variants[1].name.isEmpty());
    QCOMPARE(puppet.poses[0].settings[0].variant, QString("nose-long"));
    PuppetEditing::setVariantName(puppet, 1, 0, "");
    QCOMPARE(puppetVariantName(puppet.parts[1].variants[0]), QString("nose-short"));

    PuppetEditing::moveVariant(puppet, 1, 1, 0);
    QCOMPARE(puppetVariantName(puppet.parts[1].variants[0]), QString("nose-long"));

    PuppetEditing::removeVariant(puppet, 1, 0);
    QCOMPARE(puppet.parts[1].variants.size(), 1);
    // The pose keeps turning the nose, with whatever variant it shows.
    QVERIFY(puppet.poses[0].settings[0].variant.isEmpty());
    QCOMPARE(puppet.poses[0].settings[0].angle, std::optional<int>(10));
}

void PuppetTests::testAddingSpriteVariants() {
    Puppet puppet = faceWithNose();
    const int added = PuppetEditing::addSpriteVariants(
        puppet, 0, {"/p/face.png", "/p/happy/face.png", "/p/sad/face.png", "/p/eye.png"});
    QCOMPARE(added, 3);
    QStringList names;
    for (const PuppetVariant& variant : puppet.parts[0].variants) names.append(puppetVariantName(variant));
    QCOMPARE(names, (QStringList{"face", "face 2", "face 3", "eye"}));
    QCOMPARE(PuppetEditing::addSpriteVariants(puppet, 7, {"/p/eye.png"}), 0);
}

void PuppetTests::testAttachingAndDetachingParts() {
    Puppet puppet = faceWithNose();
    PuppetPart eye{"eye", {{PuppetVariant::Kind::Sprite, "/p/eye.png", {}, false}}, QPoint(5, 5)};
    puppet.parts.append(eye);
    // A positioned part's attachments are constraints; this one stays one.
    puppet.attachments.append({"eye", "pivot", "nose", "pivot"});

    PuppetEditing::attachPart(puppet, 2, 0, "socket", "pivot");
    QVERIFY(!puppet.parts[2].position);
    QCOMPARE(puppet.attachments.size(), 3);
    // The new one goes before the old constraint, so it is the eye's parent.
    QCOMPARE(puppetParentAttachment(puppet, "eye")->parent, QString("face"));
    QCOMPARE(puppetParentAttachment(puppet, "eye")->parentMarker, QString("socket"));
    QCOMPARE(puppet.attachments[2].parent, QString("nose"));

    // Moving the joint keeps its limits.
    PuppetEditing::setAttachmentLimits(puppet, 2, 3, 20, -10);  // given backwards
    PuppetEditing::attachPart(puppet, 2, 1, "tip", "pivot");
    const PuppetAttachment* joint = puppetParentAttachment(puppet, "eye");
    QCOMPARE(joint->parent, QString("nose"));
    QCOMPARE(joint->parentMarker, QString("tip"));
    QCOMPARE(joint->leeway, 3);
    QCOMPARE(joint->angleMin, std::optional<int>(-10));
    QCOMPARE(joint->angleMax, std::optional<int>(20));
    PuppetEditing::setAttachmentLimits(puppet, 2, 0, std::nullopt, std::nullopt);
    QVERIFY(!puppetParentAttachment(puppet, "eye")->angleMin);

    PuppetEditing::detachPart(puppet, 2, QPoint(7, -3));
    QCOMPARE(puppet.parts[2].position, std::optional<QPoint>(QPoint(7, -3)));
    QVERIFY(!puppetParentAttachment(puppet, "eye"));
    // Only the parent attachment went; the old constraint is still there.
    QCOMPARE(puppet.attachments.size(), 2);
    QCOMPARE(puppet.attachments[1].child, QString("eye"));
}

void PuppetTests::testAttachingRefusesLoops() {
    Puppet puppet = faceWithNose();
    PuppetPart pupil{"pupil", {}, std::nullopt};
    puppet.parts.append(pupil);
    puppet.attachments.append({"pupil", "pivot", "nose", "tip"});  // face > nose > pupil

    QCOMPARE(PuppetEditing::partsHangingFrom(puppet, 0), (QSet<int>{1, 2}));
    QVERIFY(PuppetEditing::partsHangingFrom(puppet, 2).isEmpty());
    QVERIFY(!PuppetEditing::canAttach(puppet, 0, 2));  // the face from its own pupil
    QVERIFY(!PuppetEditing::canAttach(puppet, 1, 1));
    QVERIFY(PuppetEditing::canAttach(puppet, 2, 0));

    const Puppet before = puppet;
    PuppetEditing::attachPart(puppet, 0, 2, "pivot", "pivot");
    QVERIFY(puppet == before);
}
