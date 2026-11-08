#pragma once
#include <optional>
#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>
#include "AnimationModels.h"

// Puppets are sprat-cli's cutout rigs: parts drawn in order (first at the back), each
// showing one of its variants (sprites or animations), joined by attachments between
// their markers. See sprat-cli's README, "Puppets" and "Playing a puppet".

/// Most degrees a joint's angle limits, or a pose's angle, may reach either way.
constexpr int PUPPET_MAX_ANGLE = 360;

/// Most a pose may scale a part either way; a negative factor flips it.
constexpr double PUPPET_MAX_SCALE = 16.0;

/// Most frames a sequence step's `in` or `hold` may take.
constexpr int PUPPET_MAX_STEP_FRAMES = 100000;

/// A sprite or animation a part can show.
struct PuppetVariant {
    enum class Kind { Sprite, Animation };
    Kind kind = Kind::Sprite;
    /// A sprite path (absolute in the app) or an animation (timeline) name. A puppets
    /// file may also name a sprite by its name, or by its index (see byIndex).
    QString source;
    /// Explicit name (`-> "NAME"`); empty uses the default, see puppetVariantName().
    QString name;
    /// The source is a bare sprite index from a puppets file, not a path or name.
    bool byIndex = false;

    bool operator==(const PuppetVariant&) const = default;
};

/// A part of a puppet. Placed by its parent attachment or, without one, by position.
struct PuppetPart {
    QString name;
    QVector<PuppetVariant> variants;  ///< The first is the rest variant
    /// Where the sprite's pivot goes in the puppet's space (origin = puppet pivot, Y down,
    /// original-image pixels), for a part with no parent.
    std::optional<QPoint> position;
    /// Markers of its sprites the editor neither shows nor offers for joints. Kept in the
    /// project only: sprat-cli has no such setting, so it is never exported.
    QStringList hiddenMarkers;

    bool operator==(const PuppetPart&) const = default;
};

/// An `attach` line: the child's point marker goes on the parent's marker (any shape).
/// A part's first attachment is its parent; further ones are constraints.
struct PuppetAttachment {
    QString child;
    QString childMarker;
    QString parent;
    QString parentMarker;
    int leeway = 0;  ///< Pixels the child's point may drift beyond the parent marker
    /// Turn limits relative to the parent, in degrees clockwise; none = turns freely.
    std::optional<int> angleMin;
    std::optional<int> angleMax;

    bool operator==(const PuppetAttachment&) const = default;
};

/// What a pose sets for one part; unset values keep what earlier poses gave.
struct PuppetPoseSetting {
    QString part;
    QString variant;               ///< Empty keeps the variant
    std::optional<int> angle;      ///< Degrees clockwise from the rest angle
    std::optional<QPoint> offset;  ///< Attached parts: moves the joint from the parent marker
    std::optional<QPoint> position;  ///< Positioned parts: replaces `at:`
    /// Stretches the part's sprite along its own axes (x, y), before it turns; a negative
    /// factor flips it. The part's own: its children aren't scaled.
    std::optional<QPointF> scale;
    std::optional<double> opacity;  ///< 0 (invisible) to 1; the part's own
    std::optional<QColor> tint;     ///< Multiplies the part's colors (white leaves them); its own

    /// Whether it sets anything: a pose line must.
    bool setsSomething() const {
        return !variant.isEmpty() || angle || offset || position || scale || opacity || tint;
    }

    bool operator==(const PuppetPoseSetting&) const = default;
};

struct PuppetPose {
    QString name;
    QVector<PuppetPoseSetting> settings;
    /// The animation (sequence) that made this pose as one of its frames, or empty for a pose
    /// of the Poses tab, which any animation may play. Kept in the project only: sprat-cli
    /// has no such setting, so it is exported as a plain pose.
    QString owner;

    bool operator==(const PuppetPose&) const = default;
};

/// The name of every puppet's Rest pose: a pose like the others, kept first, that can't be
/// removed, renamed or moved. It is exported as a plain `pose`, so animations can play it;
/// sprat-cli's own rest (first variants, angle 0, offset 0) is still what poses start from.
inline const QString PUPPET_REST_POSE = QStringLiteral("rest");

/// One step of a sequence: reach a pose in `in` frames, then hold it `hold` frames.
struct PuppetSequenceStep {
    QString pose;
    int in = 0;
    int hold = 0;
    bool snap = false;  ///< ease:step: keep the old pose through `in`, then snap
    /// Parts of the pose that ease the other way from the step (`step:` parts of a linear
    /// step, `linear:` parts of a snapping one). Only parts the pose sets count.
    QStringList otherEaseParts;

    bool operator==(const PuppetSequenceStep&) const = default;
};

struct PuppetSequence {
    QString name;
    int fps = DEFAULT_ANIMATION_FPS;
    AnimationLoopMode loopMode = AnimationLoopMode::Loop;
    QVector<PuppetSequenceStep> steps;

    bool operator==(const PuppetSequence&) const = default;
};

/// A puppet of one atlas. The app edits its rig; poses and sequences are kept as read.
struct Puppet {
    QString name;
    QVector<PuppetPart> parts;  ///< In draw order, the first at the back
    QVector<PuppetAttachment> attachments;  ///< In file order
    QVector<PuppetPose> poses;
    QVector<PuppetSequence> sequences;

    bool operator==(const Puppet&) const = default;
};

/// The name sprat-cli gives a sprite at @p path: its file name without the last
/// extension, keeping a leading dot, and without the ".9" of a nine-patch.
inline QString puppetSpriteName(const QString& path) {
    QString fileName = path.section(QLatin1Char('/'), -1).section(QLatin1Char('\\'), -1);
    const int dot = fileName.lastIndexOf(QLatin1Char('.'));
    QString name = (dot <= 0 || fileName == QLatin1String("..")) ? fileName : fileName.left(dot);
    if (name.endsWith(QLatin1String(".9")) && path.endsWith(QLatin1String(".9.png"), Qt::CaseInsensitive)) {
        name.chop(2);
    }
    if (!name.isEmpty()) return name;
    return fileName.isEmpty() ? path : fileName;
}

/// The name a variant goes by: its explicit name, or what sprat-cli names it by default
/// (an animation's name, a sprite's name). A sprite given by index goes by the index here,
/// since its name depends on the layout.
inline QString puppetVariantName(const PuppetVariant& variant) {
    if (!variant.name.isEmpty()) return variant.name;
    if (variant.kind == PuppetVariant::Kind::Animation || variant.byIndex) return variant.source;
    return puppetSpriteName(variant.source);
}

/// Whether part @p part snaps (keeps its old values through `in`) in @p step.
inline bool puppetStepPartSnaps(const PuppetSequenceStep& step, const QString& part) {
    return step.snap != step.otherEaseParts.contains(part);
}

/// Whether pose @p poseIndex is the puppet's Rest pose.
inline bool puppetIsRestPose(const Puppet& puppet, int poseIndex) {
    return poseIndex >= 0 && poseIndex < puppet.poses.size() && puppet.poses[poseIndex].name == PUPPET_REST_POSE;
}

/// The part named @p name, or nullptr.
inline const PuppetPart* puppetPart(const Puppet& puppet, const QString& name) {
    for (const PuppetPart& part : puppet.parts) {
        if (part.name == name) return &part;
    }
    return nullptr;
}

/// The attachment that places @p part: its first one, unless the part has a position
/// (then all its attachments are constraints). nullptr when it has none.
inline const PuppetAttachment* puppetParentAttachment(const Puppet& puppet, const QString& part) {
    const PuppetPart* found = puppetPart(puppet, part);
    if (found && found->position) return nullptr;
    for (const PuppetAttachment& attachment : puppet.attachments) {
        if (attachment.child == part) return &attachment;
    }
    return nullptr;
}
