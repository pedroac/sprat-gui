#include "PuppetTests.h"
#include "ProjectPayloadCodec.h"
#include "PuppetEditing.h"
#include "PuppetEvaluator.h"
#include "PuppetMotion.h"
#include "PuppetPosing.h"
#include "PuppetSequencing.h"
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
- pose "look left" in:2 hold:4 ease:step linear:"pupil"
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

// An arm on a body placed at the origin: an upper arm, a forearm and a hand, each 10 px
// long and pointing right at rest, hanging by its pivot from the previous one's "end".
Puppet arm() {
    Puppet puppet;
    puppet.name = QStringLiteral("arm");
    const QStringList names{QStringLiteral("body"), QStringLiteral("upper"), QStringLiteral("fore"),
                            QStringLiteral("hand")};
    for (const QString& name : names) {
        PuppetPart part{name, {spriteVariant(QStringLiteral("/s/%1.png").arg(name))}, std::nullopt};
        puppet.parts.append(part);
    }
    puppet.parts.first().position = QPoint(0, 0);
    for (int i = 1; i < names.size(); ++i)
        puppet.attachments.append({names[i], QStringLiteral("pivot"), names[i - 1], QStringLiteral("end"), 0,
                                   std::nullopt, std::nullopt});
    return puppet;
}

QHash<QString, PuppetSpriteGeometry> armSprites() {
    PuppetSpriteGeometry body;
    body.markers.insert(QStringLiteral("end"), QPointF(0, 0));  // the shoulder, on the pivot
    PuppetSpriteGeometry bone;
    bone.markers.insert(QStringLiteral("end"), QPointF(10, 0));
    return {{QStringLiteral("/s/body.png"), body}, {QStringLiteral("/s/upper.png"), bone},
            {QStringLiteral("/s/fore.png"), bone}, {QStringLiteral("/s/hand.png"), bone}};
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
    // The pupil tweens while the fly snaps.
    QCOMPARE(fuss.steps[1].partSnaps, (QMap<QString, bool>{{QStringLiteral("pupil"), false}}));
    QVERIFY(!fuss.steps[1].snaps(QStringLiteral("pupil")));
    QVERIFY(fuss.steps[1].snaps(QStringLiteral("fly")));
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
    puppet.parts[1].hiddenMarkers = {QStringLiteral("chin"), QStringLiteral("ear")};
    // The editor's own settings: never written to sprat-cli's puppets file.
    QVERIFY(!PuppetTextCodec::write({puppet}).contains(QStringLiteral("chin")));
    puppet.parts[2].rigLocked = true;
    puppet.poses[0].lockedParts = {QStringLiteral("nose")};
    QCOMPARE(PuppetTextCodec::read(PuppetTextCodec::write({puppet})).puppets.first().poses[0].lockedParts,
             QStringList());
    QVERIFY(!PuppetTextCodec::read(PuppetTextCodec::write({puppet})).puppets.first().parts[2].rigLocked);
    // A frame of an animation is written as a plain pose; whose it is stays in the project.
    PuppetEditing::addAnimationPose(puppet, 0, puppet.poses.first().settings);
    QVERIFY(PuppetTextCodec::write({puppet}).contains(QStringLiteral("pose \"fuss frame 1\"\n")));

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
    const QJsonArray savedPoses =
        payload["atlases"].toArray()[0].toObject()["puppets"].toArray()[0].toObject()["poses"].toArray();
    QCOMPARE(savedPoses.last().toObject()["animation"].toString(), QStringLiteral("fuss"));
    QVERIFY(!savedPoses.first().toObject().contains("animation"));

    QVector<LayoutModel> layoutModels;
    const ProjectPayloadApplyResult applied =
        ProjectPayloadCodec::applyToLayout(payload, QStringLiteral("/tmp/project"), layoutModels);
    const AtlasEntry* loaded = nullptr;
    for (const AtlasEntry& a : applied.atlases) {
        if (a.id == atlas.id) loaded = &a;
    }
    QVERIFY(loaded);
    QCOMPARE(loaded->puppets.size(), 1);
    // A puppet saved without a Rest pose gets one, first, setting every part.
    Puppet withRest = puppet;
    PuppetEditing::keepRestPose(withRest);
    QCOMPARE(withRest.poses.first().name, PUPPET_REST_POSE);
    QCOMPARE(withRest.poses.first().settings.size(), puppet.parts.size());
    QVERIFY(loaded->puppets.first() == withRest);
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

void PuppetTests::testRestPoseStaysFirstAndFollowsTheRig() {
    using namespace PuppetEditing;
    Puppet puppet = faceWithNose();

    // Added first, setting every part as the rig has it.
    keepRestPose(puppet);
    QCOMPARE(poseNames(puppet), (QStringList{"rest", "lie"}));
    const QVector<PuppetPoseSetting>& rest = puppet.poses.first().settings;
    QCOMPARE(rest.size(), 2);
    QCOMPARE(rest[0].part, QString("face"));
    QCOMPARE(rest[0].variant, QString("face"));
    QCOMPARE(rest[0].angle, std::optional<int>(0));
    QCOMPARE(rest[0].position, std::optional<QPoint>(QPoint(0, -40)));
    QVERIFY(!rest[0].offset);
    QCOMPARE(rest[1].variant, QString("short"));
    QCOMPARE(rest[1].offset, std::optional<QPoint>(QPoint(0, 0)));
    QVERIFY(!rest[1].position);
    QVERIFY(puppetIsRestPose(puppet, 0));
    QVERIFY(!puppetIsRestPose(puppet, 1));

    // It can't be renamed, removed or moved, and stays first.
    QVERIFY(!poseNameProblem(puppet, 0, "idle").isEmpty());
    QVERIFY(poseNameProblem(puppet, 0, "rest").isEmpty());
    QVERIFY(!poseNameProblem(puppet, 1, "rest").isEmpty());
    Puppet edited = puppet;
    renamePose(edited, 0, "idle");
    removePose(edited, 0);
    movePose(edited, 0, 1);
    movePose(edited, 1, 0);
    QVERIFY(edited == puppet);
    edited.poses.move(0, 1);
    keepRestPose(edited);
    QVERIFY(edited == puppet);

    // A new part joins it; the rig's positions are kept, and a part attached since loses its.
    const Puppet before = puppet;
    puppet.parts.append({"hat", {}, QPoint(5, -60)});
    puppet.parts[0].position = QPoint(0, -50);
    keepRestPose(puppet, &before);
    QCOMPARE(puppet.poses.first().settings.size(), 3);
    QCOMPARE(puppet.poses.first().settings[0].position, std::optional<QPoint>(QPoint(0, -50)));
    QCOMPARE(puppet.poses.first().settings[2].part, QString("hat"));
    QCOMPARE(puppet.poses.first().settings[2].position, std::optional<QPoint>(QPoint(5, -60)));
    attachPart(puppet, 2, 0, "nose", "pivot");
    keepRestPose(puppet);
    QVERIFY(!puppet.poses.first().settings[2].position);
    QCOMPARE(puppet.poses.first().settings[2].angle, std::optional<int>(0));

    // A Rest pose read with another variant makes that one the rest variant.
    Puppet read = faceWithNose();
    PuppetPoseSetting longNose;
    longNose.part = "nose";
    longNose.variant = "nose-long";
    read.poses.prepend({PUPPET_REST_POSE, {longNose}});
    keepRestPose(read);
    QCOMPARE(variantNames(read.parts[1]), (QStringList{"nose-long", "short"}));
    QCOMPARE(read.poses.first().settings[0].variant, QString("nose-long"));

    // A part left out of it on purpose stays out after other edits.
    puppet.poses.first().settings.removeAt(1);
    const Puppet withoutNose = puppet;
    renamePart(puppet, 0, "head");
    keepRestPose(puppet, &withoutNose);
    QCOMPARE(puppet.poses.first().settings.size(), 2);
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

    // Listed by name, numbers in order, whatever the stored order.
    PuppetPart walk{"legs", {{PuppetVariant::Kind::Sprite, "/p/walk10.png", {}, false},
                             {PuppetVariant::Kind::Sprite, "/p/Walk2.png", {}, false},
                             {PuppetVariant::Kind::Animation, "arm", {}, false}}, std::nullopt};
    QCOMPARE(PuppetEditing::variantsByName(walk), (QVector<int>{2, 1, 0}));

    puppet.parts[1].variants.move(1, 0);
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

void PuppetTests::testJointsTurnWithinTheirLimits() {
    PuppetAttachment free{"nose", "pivot", "face", "nose"};
    QCOMPARE(PuppetMotion::clampAngle(free, 30.4), 30);
    QCOMPARE(PuppetMotion::clampAngle(free, 270), -90);  // a free part takes the short way
    QCOMPARE(PuppetMotion::clampAngle(free, -540), 180);
    QCOMPARE(PuppetMotion::freeAngle(-180), 180);
    QCOMPARE(PuppetMotion::freeAngle(181), -179);
    QCOMPARE(PuppetMotion::freeAngle(720.4), 0);

    PuppetAttachment limited = free;
    limited.angleMin = -15;
    limited.angleMax = 10;
    QCOMPARE(PuppetMotion::clampAngle(limited, 5), 5);
    QCOMPARE(PuppetMotion::clampAngle(limited, 90), 10);
    QCOMPARE(PuppetMotion::clampAngle(limited, -90), -15);
}

void PuppetTests::testPullingAPartTurnsThePartsItHangsFrom() {
    Puppet puppet = arm();
    const PuppetSpriteLookup lookup = lookupIn(armSprites());
    const QPointF fingers(5, 0);  // in the hand's sprite: 25 px from the shoulder at rest
    const auto fingersAt = [&](const QVector<PuppetPartState>& states) {
        return PuppetEvaluator::evaluate(puppet, states, lookup)[3].transform.map(fingers);
    };

    // Within reach: the arm bends so the fingers get there; the body, placed by position, stays.
    const QPointF target(5, 15);
    const QVector<PuppetPartState> reached = PuppetMotion::reach(puppet, {}, 3, fingers, target, lookup);
    QVERIFY2(QLineF(fingersAt(reached), target).length() < 1.5,
             qPrintable(QStringLiteral("fingers at %1,%2").arg(fingersAt(reached).x()).arg(fingersAt(reached).y())));
    QCOMPARE(reached[0].angle, 0.0);
    for (const PuppetPartState& state : reached) QCOMPARE(state.angle, std::round(state.angle));

    // Limits hold: a stiff elbow and wrist leave only the shoulder, which turns 30° at most.
    puppet.attachments[0].angleMin = -30;
    puppet.attachments[0].angleMax = 30;
    for (int i = 1; i < 3; ++i) {
        puppet.attachments[i].angleMin = 0;
        puppet.attachments[i].angleMax = 0;
    }
    const QVector<PuppetPartState> stiff = PuppetMotion::reach(puppet, {}, 3, fingers, QPointF(0, 25), lookup);
    QCOMPARE(stiff[1].angle, 30.0);
    QCOMPARE(stiff[2].angle, 0.0);
    QCOMPARE(stiff[3].angle, 0.0);

    // Only parts the pulled one hangs from turn: pulling the forearm leaves the hand's own angle.
    puppet = arm();
    const QVector<PuppetPartState> elbow = PuppetMotion::reach(puppet, {}, 2, QPointF(10, 0), QPointF(0, 20), lookup);
    QCOMPARE(elbow[3].angle, 0.0);
    QVERIFY(elbow[1].angle != 0.0);

    // A fixed (locked) upper arm holds still: only the elbow and wrist bend.
    const QVector<PuppetPartState> locked =
        PuppetMotion::reach(puppet, {}, 3, fingers, QPointF(15, 10), lookup, {}, QSet<int>{1});
    QCOMPARE(locked[1].angle, 0.0);
    QVERIFY(locked[2].angle != 0.0);
    // A fixed pulled part doesn't move at all.
    const QVector<PuppetPartState> held = PuppetMotion::reach(puppet, {}, 3, fingers, target, lookup, {}, QSet<int>{3});
    for (const PuppetPartState& state : held) QCOMPARE(state.angle, 0.0);
}

void PuppetTests::testPullingAPartSlidesItsJointFirst() {
    // A pupil hanging by its pivot in a round socket, radius 5, of a face placed at the origin.
    Puppet puppet;
    puppet.name = QStringLiteral("eye");
    puppet.parts = {{QStringLiteral("face"), {spriteVariant(QStringLiteral("/s/face.png"))}, QPoint(0, 0)},
                    {QStringLiteral("pupil"), {spriteVariant(QStringLiteral("/s/pupil.png"))}, std::nullopt}};
    puppet.attachments = {{QStringLiteral("pupil"), QStringLiteral("pivot"), QStringLiteral("face"),
                           QStringLiteral("socket"), 1, std::nullopt, std::nullopt}};
    PuppetSpriteGeometry face;
    face.markers.insert(QStringLiteral("socket"), QPointF(10, 10));
    PuppetSpriteGeometry pupil;
    pupil.pivot = QPointF(1, 1);
    const PuppetSpriteLookup lookup = lookupIn({{QStringLiteral("/s/face.png"), face}, {QStringLiteral("/s/pupil.png"), pupil}});
    NamedPoint socket;
    socket.kind = MarkerKind::Circle;
    socket.x = 10;
    socket.y = 10;
    socket.radius = 5;
    const PuppetMotion::PuppetParentMarker parentMarker = [socket](int part) {
        return part == 1 ? std::optional<NamedPoint>(socket) : std::nullopt;
    };

    // Inside the socket, the pupil just goes there.
    QVector<PuppetPartState> states = PuppetMotion::reach(puppet, {}, 1, QPointF(1, 1), QPointF(13, 8), lookup, parentMarker);
    QCOMPARE(states[1].offset, QPointF(3, -2));
    QCOMPARE(states[1].angle, 0.0);
    // Beyond it, it stops at the socket's edge plus the leeway: 6 px from the center.
    states = PuppetMotion::reach(puppet, {}, 1, QPointF(1, 1), QPointF(40, 10), lookup, parentMarker);
    QCOMPARE(states[1].offset, QPointF(6, 0));
    // Without the markers, joints only turn.
    states = PuppetMotion::reach(puppet, {}, 1, QPointF(1, 1), QPointF(13, 8), lookup);
    QCOMPARE(states[1].offset, QPointF());
}

void PuppetTests::testPoseStatesComeFromWhatItSets() {
    // face is placed by position at 0,-40; nose hangs from it, with variants "short" and "nose-long".
    const Puppet puppet = faceWithNose();
    PuppetPoseSetting moved;
    moved.part = QStringLiteral("face");
    moved.position = QPoint(5, -40);
    PuppetPoseSetting turned;
    turned.part = QStringLiteral("nose");
    turned.angle = 10;
    turned.offset = QPoint(1, 2);
    turned.variant = QStringLiteral("nose-long");
    PuppetPoseSetting unknown;  // a part the puppet doesn't have
    unknown.part = QStringLiteral("tail");
    unknown.angle = 5;
    const QVector<PuppetPartState> states =
        PuppetPosing::statesOf(puppet, {QStringLiteral("p"), {moved, turned, unknown}});
    QCOMPARE(states.size(), 2);
    QCOMPARE(states[0].position, std::optional<QPointF>(QPointF(5, -40)));
    QCOMPARE(states[0].angle, 0.0);
    QCOMPARE(states[1].angle, 10.0);
    QCOMPARE(states[1].offset, QPointF(1, 2));
    QCOMPARE(states[1].variant, 1);

    // A variant the part doesn't have leaves the rest one; an empty pose is the rest pose.
    turned.variant = QStringLiteral("nose-gone");
    QCOMPARE(PuppetPosing::statesOf(puppet, {QStringLiteral("p"), {turned}})[1].variant, 0);
    const QVector<PuppetPartState> rest = PuppetPosing::statesOf(puppet, {QStringLiteral("p"), {}});
    QCOMPARE(rest[1].angle, 0.0);
    QVERIFY(!rest[0].position);
}

void PuppetTests::testEditingPosesKeepsSequencesInStep() {
    Puppet puppet = faceWithNose();
    puppet.poses.append({QStringLiteral("truth"), {}});
    puppet.sequences = {{QStringLiteral("talk"), 8, AnimationLoopMode::Loop,
                         {{QStringLiteral("lie"), 2, 0, false}, {QStringLiteral("truth"), 2, 0, false}}}};
    QVERIFY(!PuppetEditing::poseNameProblem(puppet, 0, QStringLiteral("truth")).isEmpty());
    QVERIFY(!PuppetEditing::poseNameProblem(puppet, 0, QStringLiteral(" ")).isEmpty());
    QVERIFY(PuppetEditing::poseNameProblem(puppet, 0, QStringLiteral("fib")).isEmpty());

    PuppetEditing::renamePose(puppet, 0, QStringLiteral("fib"));
    QCOMPARE(puppet.sequences[0].steps[0].pose, QStringLiteral("fib"));
    PuppetEditing::movePose(puppet, 0, 1);
    QCOMPARE(PuppetEditing::poseNames(puppet), (QStringList{"truth", "fib"}));
    PuppetEditing::removePose(puppet, 1);
    QCOMPARE(PuppetEditing::poseNames(puppet), QStringList{"truth"});
    QCOMPARE(puppet.sequences[0].steps.size(), 1);
    QCOMPARE(puppet.sequences[0].steps[0].pose, QStringLiteral("truth"));
}

void PuppetTests::testAnimationPosesBelongToTheirAnimation() {
    Puppet puppet = faceWithNose();  // pose "lie" turns the nose
    puppet.sequences = {{QStringLiteral("talk"), 8, AnimationLoopMode::Loop, {{QStringLiteral("lie"), 2, 0, false}}},
                        {QStringLiteral("sing"), 8, AnimationLoopMode::Loop, {{QStringLiteral("lie"), 2, 0, false}}}};
    const PuppetPose lie = puppet.poses.first();

    // A frame of "talk": named after it, listed for its steps but not for another animation's.
    const QString frame = PuppetEditing::addAnimationPose(puppet, 0, lie.settings);
    QCOMPARE(frame, QStringLiteral("talk frame 1"));
    QCOMPARE(puppet.poses.last().owner, QStringLiteral("talk"));
    QCOMPARE(PuppetEditing::sharedPoseCount(puppet), 1);
    QCOMPARE(PuppetEditing::posesFor(puppet, 0), (QStringList{"lie", "talk frame 1"}));
    QCOMPARE(PuppetEditing::posesFor(puppet, 1), QStringList{"lie"});

    // Editing a step that plays a shared pose edits a copy only that step plays.
    QCOMPARE(PuppetEditing::ownStepPose(puppet, 0, 0), 2);
    QCOMPARE(puppet.sequences[0].steps[0].pose, QStringLiteral("talk frame 2"));
    QCOMPARE(puppet.sequences[1].steps[0].pose, QStringLiteral("lie"));
    QVERIFY(puppet.poses[0] == lie);
    QCOMPARE(PuppetEditing::ownStepPose(puppet, 0, 0), 2);  // already its own: no other copy
    QCOMPARE(puppet.poses.size(), 3);

    // Shared poses stay first; frames no step plays go.
    puppet.poses.move(0, 2);
    PuppetEditing::keepAnimationPosesLast(puppet);
    QCOMPARE(PuppetEditing::poseNames(puppet), (QStringList{"lie", "talk frame 1", "talk frame 2"}));
    PuppetEditing::removeUnusedAnimationPoses(puppet);
    QCOMPARE(PuppetEditing::poseNames(puppet), (QStringList{"lie", "talk frame 2"}));

    // Duplicating a frame copies its pose; duplicating a shared step doesn't.
    PuppetEditing::duplicateStep(puppet, 0, 0);
    QCOMPARE(puppet.sequences[0].steps[1].pose, QStringLiteral("talk frame 1"));
    PuppetEditing::duplicateStep(puppet, 1, 0);
    QCOMPARE(puppet.sequences[1].steps[1].pose, QStringLiteral("lie"));

    // A pose saved to the Poses tab goes after the shared ones and belongs to none.
    QCOMPARE(PuppetEditing::addSharedCopy(puppet, 1, QStringLiteral("whisper")), 1);
    QCOMPARE(PuppetEditing::poseNames(puppet), (QStringList{"lie", "whisper", "talk frame 2", "talk frame 1"}));
    QVERIFY(puppet.poses[1].owner.isEmpty());

    // Renaming the animation renames its frames; one the user named keeps its name.
    puppet.poses[3].name = QStringLiteral("shout");
    puppet.sequences[0].steps[1].pose = QStringLiteral("shout");
    PuppetEditing::renameSequence(puppet, 0, QStringLiteral("chat"));
    QCOMPARE(PuppetEditing::poseNames(puppet), (QStringList{"lie", "whisper", "chat frame 2", "shout"}));
    QCOMPARE(puppet.sequences[0].steps[0].pose, QStringLiteral("chat frame 2"));
    QCOMPARE(puppet.poses[3].owner, QStringLiteral("chat"));

    // A copied animation plays copies of its frames.
    QCOMPARE(PuppetEditing::duplicateSequence(puppet, 0), 1);
    QCOMPARE(puppet.sequences[1].name, QStringLiteral("chat 2"));
    QCOMPARE(puppet.sequences[1].steps[0].pose, QStringLiteral("chat 2 frame 1"));
    QCOMPARE(puppet.poses.last().owner, QStringLiteral("chat 2"));

    // Removing an animation removes its frames, and only those.
    PuppetEditing::removeSequence(puppet, 0);
    QCOMPARE(PuppetEditing::poseNames(puppet), (QStringList{"lie", "whisper", "chat 2 frame 1", "chat 2 frame 2"}));
}

void PuppetTests::testStepsEasePartsTheirOwnWay() {
    // Read: comma lists, either way; a part listed twice keeps the first, and one easing as
    // the whole step does is no exception.
    const PuppetTextReadResult read = PuppetTextCodec::read(QStringLiteral(
        "puppet \"p\"\npart \"a\" at:0,0\n- sprite a\npart \"b c\" at:0,0\n- sprite b\npart d at:0,0\n- sprite d\n"
        "pose \"q\"\n- part a angle:1\n- part \"b c\" angle:2\n"
        "sequence \"s\"\n- pose q in:2 step:a,\"b c\" linear:a\n- pose q in:2 ease:step step:d linear:d\n"));
    QVERIFY2(read.error.isEmpty(), qPrintable(read.error));
    const PuppetSequence& sequence = read.puppets.first().sequences.first();
    QCOMPARE(sequence.steps[0].partSnaps,
             (QMap<QString, bool>{{QStringLiteral("a"), true}, {QStringLiteral("b c"), true}}));
    QVERIFY(sequence.steps[1].partSnaps.isEmpty());
    QCOMPARE(read.warnings.size(), 3);
    QVERIFY(read.warnings[0].contains(QLatin1String("eases part \"a\" more than once")));
    QVERIFY(read.warnings[1].contains(QLatin1String("eases part \"d\" more than once")));
    QVERIFY(read.warnings[2].contains(QLatin1String("eases part \"d\" step, as the whole step does")));

    // Written in draw order, leaving out parts the pose doesn't set (sprat-cli would ignore them).
    Puppet puppet = read.puppets.first();
    puppet.sequences[0].steps[0].partSnaps.insert(QStringLiteral("d"), true);
    QVERIFY(PuppetTextCodec::write({puppet}).contains(QStringLiteral("- pose \"q\" in:2 step:\"a\",\"b c\"\n")));
    puppet.sequences[0].steps[1].partSnaps.insert(QStringLiteral("a"), false);
    QVERIFY(PuppetTextCodec::write({puppet}).contains(QStringLiteral("- pose \"q\" in:2 ease:step linear:\"a\"\n")));

    // Renaming or removing a part follows it.
    PuppetEditing::renamePart(puppet, 0, QStringLiteral("arm"));
    QVERIFY(puppet.sequences[0].steps[0].partSnaps.contains(QStringLiteral("arm")));
    QVERIFY(!puppet.sequences[0].steps[0].partSnaps.contains(QStringLiteral("a")));
    PuppetEditing::removePart(puppet, 1);
    QVERIFY(!puppet.sequences[0].steps[0].partSnaps.contains(QStringLiteral("b c")));

    // Played: the face tweens while the nose, easing its own way, snaps at the end of `in`.
    Puppet face = faceWithNose();
    PuppetPoseSetting faceUp;
    faceUp.part = QStringLiteral("face");
    faceUp.angle = 10;
    PuppetPoseSetting noseUp = faceUp;
    noseUp.part = QStringLiteral("nose");
    face.poses = {{QStringLiteral("up"), {faceUp, noseUp}}};
    PuppetSequenceStep step{QStringLiteral("up"), 4, 0, false};
    step.partSnaps.insert(QStringLiteral("nose"), true);
    const PuppetSequence nod{QStringLiteral("nod"), 8, AnimationLoopMode::Once, {step}};
    const QVector<PuppetPartState> halfWay = PuppetSequencing::statesAt(face, nod, 1, {});
    QCOMPARE(halfWay[0].angle, 5.0);
    QCOMPARE(halfWay[1].angle, 0.0);
    const QVector<PuppetPartState> arrived = PuppetSequencing::statesAt(face, nod, 3, {});
    QCOMPARE(arrived[0].angle, 10.0);
    QCOMPARE(arrived[1].angle, 10.0);
}

void PuppetTests::testSequencesTweenPosesOverTheirFrames() {
    // nose turns to 10° in "up", to -10° showing "nose-long" in "down".
    Puppet puppet = faceWithNose();
    PuppetPoseSetting up;
    up.part = QStringLiteral("nose");
    up.angle = 10;
    PuppetPoseSetting down = up;
    down.angle = -10;
    down.variant = QStringLiteral("nose-long");
    puppet.poses = {{QStringLiteral("up"), {up}}, {QStringLiteral("down"), {down}}};
    PuppetSequence nod{QStringLiteral("nod"), 8, AnimationLoopMode::Loop,
                       {{QStringLiteral("up"), 2, 1, false}, {QStringLiteral("down"), 4, 0, false}}};
    QCOMPARE(PuppetSequencing::passFrames(nod), 7);
    QCOMPARE(PuppetSequencing::cycleFrames(nod), 7);
    QCOMPARE(PuppetSequencing::stepStart(nod, 1), 3);
    QCOMPARE(PuppetSequencing::stepAt(nod, 3), 1);
    const auto nose = [&](const PuppetSequence& sequence, int frame) {
        return PuppetSequencing::statesAt(puppet, sequence, frame, {})[1];
    };

    // "up" over 2 frames, held 1; "down" over 4, switching variant when it arrives.
    QCOMPARE(nose(nod, 0).angle, 5.0);
    QCOMPARE(nose(nod, 1).angle, 10.0);
    QCOMPARE(nose(nod, 2).angle, 10.0);
    QCOMPARE(nose(nod, 3).angle, 5.0);
    QCOMPARE(nose(nod, 3).variant, 0);
    QCOMPARE(nose(nod, 6).angle, -10.0);
    QCOMPARE(nose(nod, 6).variant, 1);
    // Looping, it starts again from where it left off: half way from -10° to 10°, still long.
    QCOMPARE(nose(nod, 7).angle, 0.0);
    QCOMPARE(nose(nod, 7).variant, 1);

    // Once holds the last frame; ease:step keeps the angle, then snaps.
    PuppetSequence once = nod;
    once.loopMode = AnimationLoopMode::Once;
    QCOMPARE(nose(once, 100).angle, -10.0);
    PuppetSequence snap = nod;
    snap.steps[1].snap = true;
    QCOMPARE(nose(snap, 4).angle, 10.0);
    QCOMPARE(nose(snap, 6).angle, -10.0);

    // Ping-pong goes back through "up" without repeating "down": the track shows it there.
    PuppetSequence pingPong = nod;
    pingPong.loopMode = AnimationLoopMode::PingPong;
    QCOMPARE(PuppetSequencing::cycleFrames(pingPong), 10);
    QCOMPARE(nose(pingPong, 7).angle, 0.0);
    QCOMPARE(PuppetSequencing::passFrameOf(pingPong, 7), 0);
    QCOMPARE(PuppetSequencing::passFrameOf(pingPong, 12), 2);

    // What no pose sets stays as the base has it; an unknown pose sets nothing.
    QVector<PuppetPartState> base(2);
    base[0].angle = 5;
    QCOMPARE(PuppetSequencing::statesAt(puppet, nod, 1, base)[0].angle, 5.0);
    PuppetSequence unknown{QStringLiteral("u"), 8, AnimationLoopMode::Loop, {{QStringLiteral("gone"), 2, 0, false}}};
    QCOMPARE(PuppetSequencing::statesAt(puppet, unknown, 1, base)[1].angle, 0.0);

    // Names and moving steps.
    puppet.sequences = {nod, unknown};
    QVERIFY(!PuppetEditing::sequenceNameProblem(puppet, 1, QStringLiteral("nod")).isEmpty());
    QVERIFY(PuppetEditing::sequenceNameProblem(puppet, 1, QStringLiteral("shake")).isEmpty());
    PuppetEditing::moveStep(puppet, 0, 0, 1);
    QCOMPARE(puppet.sequences[0].steps[0].pose, QStringLiteral("down"));
}

void PuppetTests::testJointsDriftWithinTheirMarkerAndLeeway() {
    NamedPoint point;
    point.x = 10;
    point.y = 10;
    QCOMPARE(PuppetMotion::clampOffset(point, 0, QPointF(3, 4)), QPoint(0, 0));
    QCOMPARE(PuppetMotion::clampOffset(point, 10, QPointF(3, 4)), QPoint(3, 4));
    QCOMPARE(PuppetMotion::clampOffset(point, 2, QPointF(30, 40)), QPoint(1, 2));  // 2 px along (3,4)
    QVERIFY(PuppetMotion::jointArea(point, 0).isEmpty());
    QVERIFY(PuppetMotion::jointArea(point, 2).contains(QPointF(11, 11)));

    NamedPoint socket;  // an eye socket: the pupil may go anywhere in it
    socket.kind = MarkerKind::Circle;
    socket.x = 0;
    socket.y = 0;
    socket.radius = 5;
    QCOMPARE(PuppetMotion::clampOffset(socket, 0, QPointF(3, -2)), QPoint(3, -2));
    QCOMPARE(PuppetMotion::clampOffset(socket, 0, QPointF(10, 0)), QPoint(5, 0));
    QCOMPARE(PuppetMotion::clampOffset(socket, 1, QPointF(10, 0)), QPoint(6, 0));

    NamedPoint box;  // offsets are from the rectangle's corner
    box.kind = MarkerKind::Rectangle;
    box.x = 0;
    box.y = 0;
    box.w = 8;
    box.h = 4;
    QCOMPARE(PuppetMotion::clampOffset(box, 0, QPointF(6, 3)), QPoint(6, 3));
    QCOMPARE(PuppetMotion::clampOffset(box, 0, QPointF(20, -5)), QPoint(8, 0));

    NamedPoint triangle;  // offsets are from its first vertex
    triangle.kind = MarkerKind::Polygon;
    triangle.polygonPoints = {QPoint(0, 0), QPoint(10, 0), QPoint(0, 10)};
    QCOMPARE(PuppetMotion::clampOffset(triangle, 0, QPointF(2, 2)), QPoint(2, 2));
    QCOMPARE(PuppetMotion::clampOffset(triangle, 0, QPointF(10, 10)), QPoint(5, 5));
    QCOMPARE(PuppetMotion::clampOffset(triangle, 0, QPointF(-4, 3)), QPoint(0, 3));
}
