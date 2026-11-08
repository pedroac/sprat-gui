#include "PuppetEditing.h"
#include "NaturalOrder.h"

#include <algorithm>
#include <numeric>
#include <QHash>
#include <QCoreApplication>

namespace {

QString trPEd(const char* text) {
    return QCoreApplication::translate("PuppetEditing", text);
}

bool validPart(const Puppet& puppet, int partIndex) {
    return partIndex >= 0 && partIndex < puppet.parts.size();
}

bool validVariant(const Puppet& puppet, int partIndex, int variantIndex) {
    return validPart(puppet, partIndex) && variantIndex >= 0
        && variantIndex < puppet.parts[partIndex].variants.size();
}

/// Whether @p name is taken by an entry of @p names other than @p index.
bool takenByOther(const QStringList& names, int index, const QString& name) {
    for (int i = 0; i < names.size(); ++i) {
        if (i != index && names[i] == name) return true;
    }
    return false;
}

/// The index of the attachment that places part @p name (its first one, when it has no
/// position), or -1.
int parentAttachmentIndex(const Puppet& puppet, const QString& name) {
    const PuppetPart* part = puppetPart(puppet, name);
    if (!part || part->position) return -1;
    for (int i = 0; i < puppet.attachments.size(); ++i) {
        if (puppet.attachments[i].child == name) return i;
    }
    return -1;
}

}  // namespace

namespace PuppetEditing {

QString uniqueName(const QString& base, const QStringList& taken) {
    if (!taken.contains(base)) return base;
    for (int n = 2;; ++n) {
        const QString candidate = QStringLiteral("%1 %2").arg(base).arg(n);
        if (!taken.contains(candidate)) return candidate;
    }
}

QStringList puppetNames(const QVector<Puppet>& puppets) {
    QStringList names;
    for (const Puppet& puppet : puppets) names.append(puppet.name);
    return names;
}

QStringList partNames(const Puppet& puppet) {
    QStringList names;
    for (const PuppetPart& part : puppet.parts) names.append(part.name);
    return names;
}

QStringList poseNames(const Puppet& puppet) {
    QStringList names;
    for (const PuppetPose& pose : puppet.poses) names.append(pose.name);
    return names;
}

QStringList sequenceNames(const Puppet& puppet) {
    QStringList names;
    for (const PuppetSequence& sequence : puppet.sequences) names.append(sequence.name);
    return names;
}

QStringList variantNames(const PuppetPart& part) {
    QStringList names;
    for (const PuppetVariant& variant : part.variants) names.append(puppetVariantName(variant));
    return names;
}

QString puppetNameProblem(const QVector<Puppet>& puppets, int index, const QString& name) {
    if (name.trimmed().isEmpty()) return trPEd("A puppet needs a name.");
    if (takenByOther(puppetNames(puppets), index, name))
        return trPEd("Another puppet is already called \"%1\".").arg(name);
    return {};
}

QString partNameProblem(const Puppet& puppet, int index, const QString& name) {
    if (name.trimmed().isEmpty()) return trPEd("A part needs a name.");
    if (takenByOther(partNames(puppet), index, name))
        return trPEd("Puppet \"%1\" already has a part called \"%2\".").arg(puppet.name, name);
    return {};
}

QString poseNameProblem(const Puppet& puppet, int index, const QString& name) {
    if (puppetIsRestPose(puppet, index) && name != PUPPET_REST_POSE)
        return trPEd("The Rest pose keeps its name: animations play it as \"%1\".").arg(PUPPET_REST_POSE);
    if (name.trimmed().isEmpty()) return trPEd("A pose needs a name.");
    if (takenByOther(poseNames(puppet), index, name))
        return trPEd("Puppet \"%1\" already has a pose called \"%2\".").arg(puppet.name, name);
    return {};
}

QString sequenceNameProblem(const Puppet& puppet, int index, const QString& name) {
    if (name.trimmed().isEmpty()) return trPEd("An animation needs a name.");
    if (takenByOther(sequenceNames(puppet), index, name))
        return trPEd("Puppet \"%1\" already has an animation called \"%2\".").arg(puppet.name, name);
    return {};
}

void moveSequence(Puppet& puppet, int from, int to) {
    const int count = puppet.sequences.size();
    if (from < 0 || from >= count || to < 0 || to >= count || from == to) return;
    puppet.sequences.move(from, to);
}

void moveStep(Puppet& puppet, int sequenceIndex, int from, int to) {
    if (sequenceIndex < 0 || sequenceIndex >= puppet.sequences.size()) return;
    QVector<PuppetSequenceStep>& steps = puppet.sequences[sequenceIndex].steps;
    if (from < 0 || from >= steps.size() || to < 0 || to >= steps.size() || from == to) return;
    steps.move(from, to);
}

void renamePose(Puppet& puppet, int poseIndex, const QString& name) {
    if (poseIndex < 0 || poseIndex >= puppet.poses.size() || puppetIsRestPose(puppet, poseIndex)) return;
    const QString old = puppet.poses[poseIndex].name;
    puppet.poses[poseIndex].name = name;
    for (PuppetSequence& sequence : puppet.sequences) {
        for (PuppetSequenceStep& step : sequence.steps) {
            if (step.pose == old) step.pose = name;
        }
    }
}

void removePose(Puppet& puppet, int poseIndex) {
    if (poseIndex < 0 || poseIndex >= puppet.poses.size() || puppetIsRestPose(puppet, poseIndex)) return;
    const QString name = puppet.poses[poseIndex].name;
    puppet.poses.removeAt(poseIndex);
    for (PuppetSequence& sequence : puppet.sequences) {
        sequence.steps.removeIf([&name](const PuppetSequenceStep& step) { return step.pose == name; });
    }
}

void movePose(Puppet& puppet, int from, int to) {
    const int count = puppet.poses.size();
    if (from < 0 || from >= count || to < 0 || to >= count || from == to) return;
    if (puppetIsRestPose(puppet, from) || puppetIsRestPose(puppet, to)) return;
    puppet.poses.move(from, to);
}

PuppetPoseSetting restSetting(const Puppet& puppet, int partIndex) {
    PuppetPoseSetting setting;
    if (!validPart(puppet, partIndex)) return setting;
    const PuppetPart& part = puppet.parts[partIndex];
    setting.part = part.name;
    if (!part.variants.isEmpty()) setting.variant = puppetVariantName(part.variants.first());
    setting.angle = 0;
    if (puppetParentAttachment(puppet, part.name)) setting.offset = QPoint(0, 0);
    else if (part.position) setting.position = *part.position;
    return setting;
}

void keepRestPose(Puppet& puppet, const Puppet* before) {
    int rest = poseNames(puppet).indexOf(PUPPET_REST_POSE);
    if (rest < 0) {
        PuppetPose pose;
        pose.name = PUPPET_REST_POSE;
        for (int i = 0; i < puppet.parts.size(); ++i) pose.settings.append(restSetting(puppet, i));
        puppet.poses.insert(0, pose);
        return;
    }
    if (rest > 0) puppet.poses.move(rest, 0);
    PuppetPose& pose = puppet.poses.first();
    const QStringList names = partNames(puppet);
    if (before && puppet.parts.size() > before->parts.size()) {
        const QStringList had = partNames(*before);
        for (int i = 0; i < puppet.parts.size(); ++i) {
            const bool set = std::any_of(pose.settings.cbegin(), pose.settings.cend(),
                [&names, i](const PuppetPoseSetting& setting) { return setting.part == names[i]; });
            if (!had.contains(names[i]) && !set) pose.settings.append(restSetting(puppet, i));
        }
        std::stable_sort(pose.settings.begin(), pose.settings.end(),
            [&names](const PuppetPoseSetting& a, const PuppetPoseSetting& b) {
                return names.indexOf(a.part) < names.indexOf(b.part);
            });
    }
    const PuppetPose* restBefore = nullptr;
    if (before) {
        for (const PuppetPose& candidate : before->poses) {
            if (candidate.name == PUPPET_REST_POSE) restBefore = &candidate;
        }
    }
    for (PuppetPoseSetting& setting : pose.settings) {
        const int partIndex = names.indexOf(setting.part);
        if (partIndex < 0) continue;
        PuppetPart& variantsOf = puppet.parts[partIndex];
        // The variant Rest shows is the rest variant, the first one, as sprat-cli has it. Rest
        // picking another moves that one first; the variants reordered moves Rest to the new first.
        const QStringList variants = variantNames(variantsOf);
        if (!variants.isEmpty()) {
            const int shown = variants.indexOf(setting.variant);
            bool restPicked = shown > 0;
            if (restPicked && restBefore) {
                for (const PuppetPoseSetting& old : restBefore->settings) {
                    if (old.part == setting.part && old.variant == setting.variant) restPicked = false;
                }
            }
            if (restPicked) variantsOf.variants.move(shown, 0);
            else setting.variant = variants.first();
        }
        const PuppetPart* part = &variantsOf;
        if (puppetParentAttachment(puppet, part->name)) {
            setting.position.reset();
        } else {
            setting.offset.reset();
            if (setting.position && part->position) setting.position = *part->position;
        }
        // A pose line must set something.
        if (setting.variant.isEmpty() && !setting.angle && !setting.offset && !setting.position) setting.angle = 0;
    }
}

QString variantNameProblem(const PuppetPart& part, int index, const QString& name) {
    QString effective = name;
    if (effective.isEmpty() && index >= 0 && index < part.variants.size()) {
        PuppetVariant unnamed = part.variants[index];
        unnamed.name.clear();
        effective = puppetVariantName(unnamed);
    }
    if (effective.trimmed().isEmpty()) return trPEd("A variant needs a name.");
    if (takenByOther(variantNames(part), index, effective))
        return trPEd("Part \"%1\" already has a variant called \"%2\".").arg(part.name, effective);
    return {};
}

void renamePart(Puppet& puppet, int partIndex, const QString& name) {
    if (!validPart(puppet, partIndex)) return;
    const QString old = puppet.parts[partIndex].name;
    if (old == name) return;
    puppet.parts[partIndex].name = name;
    for (PuppetAttachment& attachment : puppet.attachments) {
        if (attachment.child == old) attachment.child = name;
        if (attachment.parent == old) attachment.parent = name;
    }
    for (PuppetPose& pose : puppet.poses) {
        for (PuppetPoseSetting& setting : pose.settings) {
            if (setting.part == old) setting.part = name;
        }
    }
    for (PuppetSequence& sequence : puppet.sequences) {
        for (PuppetSequenceStep& step : sequence.steps) {
            if (step.partSnaps.contains(old)) step.partSnaps.insert(name, step.partSnaps.take(old));
        }
    }
    for (PuppetPose& pose : puppet.poses) {
        for (QString& locked : pose.lockedParts) {
            if (locked == old) locked = name;
        }
    }
}

void removePart(Puppet& puppet, int partIndex) {
    if (!validPart(puppet, partIndex)) return;
    const QString name = puppet.parts[partIndex].name;
    puppet.parts.removeAt(partIndex);
    puppet.attachments.removeIf([&name](const PuppetAttachment& attachment) {
        return attachment.child == name || attachment.parent == name;
    });
    for (PuppetPose& pose : puppet.poses) {
        pose.settings.removeIf([&name](const PuppetPoseSetting& setting) { return setting.part == name; });
    }
    for (PuppetSequence& sequence : puppet.sequences) {
        for (PuppetSequenceStep& step : sequence.steps) step.partSnaps.remove(name);
    }
    for (PuppetPose& pose : puppet.poses) pose.lockedParts.removeAll(name);
}

void movePart(Puppet& puppet, int from, int to) {
    if (!validPart(puppet, from) || !validPart(puppet, to) || from == to) return;
    puppet.parts.move(from, to);
}

void setVariantName(Puppet& puppet, int partIndex, int variantIndex, const QString& name) {
    if (!validVariant(puppet, partIndex, variantIndex)) return;
    PuppetPart& part = puppet.parts[partIndex];
    PuppetVariant& variant = part.variants[variantIndex];
    const QString old = puppetVariantName(variant);
    PuppetVariant unnamed = variant;
    unnamed.name.clear();
    // The default name is written without `-> "NAME"`, so it follows the sprite.
    variant.name = (name.isEmpty() || name == puppetVariantName(unnamed)) ? QString() : name;
    const QString now = puppetVariantName(variant);
    if (now == old) return;
    for (PuppetPose& pose : puppet.poses) {
        for (PuppetPoseSetting& setting : pose.settings) {
            if (setting.part == part.name && setting.variant == old) setting.variant = now;
        }
    }
}

void removeVariant(Puppet& puppet, int partIndex, int variantIndex) {
    if (!validVariant(puppet, partIndex, variantIndex)) return;
    PuppetPart& part = puppet.parts[partIndex];
    const QString name = puppetVariantName(part.variants[variantIndex]);
    part.variants.removeAt(variantIndex);
    for (PuppetPose& pose : puppet.poses) {
        for (PuppetPoseSetting& setting : pose.settings) {
            if (setting.part == part.name && setting.variant == name) setting.variant.clear();
        }
    }
}

QVector<int> variantsByName(const PuppetPart& part) {
    // Not QCollator: its numeric mode is ignored by Qt builds without ICU.
    const QStringList names = variantNames(part);
    QVector<int> order(names.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return compareNaturally(names[a], names[b]) < 0; });
    return order;
}

QSet<int> partsHangingFrom(const Puppet& puppet, int partIndex) {
    QSet<int> hanging;
    if (!validPart(puppet, partIndex)) return hanging;
    QStringList pending{puppet.parts[partIndex].name};
    while (!pending.isEmpty()) {
        const QString parent = pending.takeFirst();
        for (int i = 0; i < puppet.parts.size(); ++i) {
            if (i == partIndex || hanging.contains(i)) continue;
            const PuppetAttachment* joint = puppetParentAttachment(puppet, puppet.parts[i].name);
            if (joint && joint->parent == parent) {
                hanging.insert(i);
                pending.append(puppet.parts[i].name);
            }
        }
    }
    return hanging;
}

bool canAttach(const Puppet& puppet, int childIndex, int parentIndex) {
    return validPart(puppet, childIndex) && validPart(puppet, parentIndex) && childIndex != parentIndex
        && !partsHangingFrom(puppet, childIndex).contains(parentIndex);
}

void attachPart(Puppet& puppet, int partIndex, int parentIndex, const QString& parentMarker,
                const QString& childMarker) {
    if (!canAttach(puppet, partIndex, parentIndex)) return;
    PuppetPart& part = puppet.parts[partIndex];
    const int current = parentAttachmentIndex(puppet, part.name);
    if (current >= 0) {
        PuppetAttachment& joint = puppet.attachments[current];
        joint.parent = puppet.parts[parentIndex].name;
        joint.parentMarker = parentMarker;
        joint.childMarker = childMarker;
        return;
    }
    // Its attachments so far were constraints; the first one becomes the parent once the
    // position is gone, so the new one goes before them.
    int insertAt = puppet.attachments.size();
    for (int i = 0; i < puppet.attachments.size(); ++i) {
        if (puppet.attachments[i].child == part.name) {
            insertAt = i;
            break;
        }
    }
    PuppetAttachment joint;
    joint.child = part.name;
    joint.childMarker = childMarker;
    joint.parent = puppet.parts[parentIndex].name;
    joint.parentMarker = parentMarker;
    puppet.attachments.insert(insertAt, joint);
    part.position.reset();
}

void detachPart(Puppet& puppet, int partIndex, const QPoint& position) {
    if (!validPart(puppet, partIndex)) return;
    PuppetPart& part = puppet.parts[partIndex];
    const int current = parentAttachmentIndex(puppet, part.name);
    if (current >= 0) puppet.attachments.removeAt(current);
    part.position = position;
}

void setAttachmentLimits(Puppet& puppet, int partIndex, int leeway, std::optional<int> angleMin,
                         std::optional<int> angleMax) {
    if (!validPart(puppet, partIndex)) return;
    const int current = parentAttachmentIndex(puppet, puppet.parts[partIndex].name);
    if (current < 0) return;
    PuppetAttachment& joint = puppet.attachments[current];
    joint.leeway = qMax(0, leeway);
    if (angleMin && angleMax) {
        joint.angleMin = qBound(-PUPPET_MAX_ANGLE, qMin(*angleMin, *angleMax), PUPPET_MAX_ANGLE);
        joint.angleMax = qBound(-PUPPET_MAX_ANGLE, qMax(*angleMin, *angleMax), PUPPET_MAX_ANGLE);
    } else {
        joint.angleMin.reset();
        joint.angleMax.reset();
    }
}

int addSpriteVariants(Puppet& puppet, int partIndex, const QStringList& spritePaths) {
    if (!validPart(puppet, partIndex)) return 0;
    PuppetPart& part = puppet.parts[partIndex];
    int added = 0;
    for (const QString& path : spritePaths) {
        const bool shown = std::any_of(part.variants.cbegin(), part.variants.cend(), [&path](const PuppetVariant& v) {
            return v.kind == PuppetVariant::Kind::Sprite && !v.byIndex && v.source == path;
        });
        if (shown) continue;
        PuppetVariant variant;
        variant.kind = PuppetVariant::Kind::Sprite;
        variant.source = path;
        const QStringList taken = variantNames(part);
        const QString defaultName = puppetVariantName(variant);
        if (taken.contains(defaultName)) variant.name = uniqueName(defaultName, taken);
        part.variants.append(variant);
        ++added;
    }
    return added;
}

}  // namespace PuppetEditing

namespace {

bool validSequence(const Puppet& puppet, int sequenceIndex) {
    return sequenceIndex >= 0 && sequenceIndex < puppet.sequences.size();
}

int poseIndexOf(const Puppet& puppet, const QString& name) {
    for (int i = 0; i < puppet.poses.size(); ++i) {
        if (puppet.poses[i].name == name) return i;
    }
    return -1;
}

/// "SEQUENCE frame ": what an animation's frames are named after, so they read as frames.
QString framePrefix(const QString& sequence) {
    return sequence + QStringLiteral(" frame ");
}

/// The first "SEQUENCE frame N" pose name not taken, N from 1.
QString animationPoseName(const Puppet& puppet, const QString& sequence) {
    const QStringList taken = PuppetEditing::poseNames(puppet);
    for (int n = 1;; ++n) {
        const QString candidate = framePrefix(sequence) + QString::number(n);
        if (!taken.contains(candidate)) return candidate;
    }
}

/// A copy of pose @p poseIndex owned by sequence @p sequence, added last. Returns its name.
QString addAnimationCopy(Puppet& puppet, int poseIndex, const QString& sequence) {
    PuppetPose copy = puppet.poses[poseIndex];
    copy.name = animationPoseName(puppet, sequence);
    copy.owner = sequence;
    puppet.poses.append(copy);
    return copy.name;
}

}  // namespace

namespace PuppetEditing {

void keepAnimationPosesLast(Puppet& puppet) {
    std::stable_partition(puppet.poses.begin(), puppet.poses.end(),
                          [](const PuppetPose& pose) { return pose.owner.isEmpty(); });
}

int sharedPoseCount(const Puppet& puppet) {
    return int(std::count_if(puppet.poses.cbegin(), puppet.poses.cend(),
                             [](const PuppetPose& pose) { return pose.owner.isEmpty(); }));
}

QStringList posesFor(const Puppet& puppet, int sequenceIndex) {
    const QString owner = validSequence(puppet, sequenceIndex) ? puppet.sequences[sequenceIndex].name : QString();
    QStringList shared;
    QStringList own;
    for (const PuppetPose& pose : puppet.poses) {
        if (pose.owner.isEmpty()) shared.append(pose.name);
        else if (!owner.isEmpty() && pose.owner == owner) own.append(pose.name);
    }
    return shared + own;
}

QString addAnimationPose(Puppet& puppet, int sequenceIndex, const QVector<PuppetPoseSetting>& settings) {
    if (!validSequence(puppet, sequenceIndex)) return {};
    PuppetPose pose;
    pose.owner = puppet.sequences[sequenceIndex].name;
    pose.name = animationPoseName(puppet, pose.owner);
    pose.settings = settings;
    puppet.poses.append(pose);
    return pose.name;
}

int ownStepPose(Puppet& puppet, int sequenceIndex, int stepIndex) {
    if (!validSequence(puppet, sequenceIndex)) return -1;
    PuppetSequence& sequence = puppet.sequences[sequenceIndex];
    if (stepIndex < 0 || stepIndex >= sequence.steps.size()) return -1;
    const int pose = poseIndexOf(puppet, sequence.steps[stepIndex].pose);
    if (pose < 0 || puppet.poses[pose].owner == sequence.name) return pose;
    // Shared, or another animation's: only this step plays the copy.
    sequence.steps[stepIndex].pose = addAnimationCopy(puppet, pose, sequence.name);
    return int(puppet.poses.size()) - 1;
}

int addSharedCopy(Puppet& puppet, int poseIndex, const QString& name) {
    if (poseIndex < 0 || poseIndex >= puppet.poses.size()) return -1;
    PuppetPose copy = puppet.poses[poseIndex];
    copy.name = name;
    copy.owner.clear();
    const int index = sharedPoseCount(puppet);
    puppet.poses.insert(index, copy);
    return index;
}

void removeUnusedAnimationPoses(Puppet& puppet) {
    QSet<QString> played;
    for (const PuppetSequence& sequence : puppet.sequences) {
        for (const PuppetSequenceStep& step : sequence.steps) played.insert(step.pose);
    }
    puppet.poses.removeIf([&played](const PuppetPose& pose) {
        return !pose.owner.isEmpty() && !played.contains(pose.name);
    });
}

void renameSequence(Puppet& puppet, int sequenceIndex, const QString& name) {
    if (!validSequence(puppet, sequenceIndex)) return;
    const QString old = puppet.sequences[sequenceIndex].name;
    if (old == name) return;
    puppet.sequences[sequenceIndex].name = name;
    const QString prefix = framePrefix(old);
    for (int i = 0; i < puppet.poses.size(); ++i) {
        PuppetPose& pose = puppet.poses[i];
        if (pose.owner != old) continue;
        pose.owner = name;
        // "walk frame 3" becomes "run frame 3"; a pose the user named otherwise keeps its name.
        bool numbered = false;
        const QString number = pose.name.startsWith(prefix) ? pose.name.mid(prefix.size()) : QString();
        number.toInt(&numbered);
        if (!numbered) continue;
        const QString renamed = framePrefix(name) + number;
        if (poseIndexOf(puppet, renamed) < 0) renamePose(puppet, i, renamed);
    }
}

void removeSequence(Puppet& puppet, int sequenceIndex) {
    if (!validSequence(puppet, sequenceIndex)) return;
    puppet.sequences.removeAt(sequenceIndex);
    removeUnusedAnimationPoses(puppet);
}

int duplicateSequence(Puppet& puppet, int sequenceIndex) {
    if (!validSequence(puppet, sequenceIndex)) return -1;
    PuppetSequence copy = puppet.sequences[sequenceIndex];
    copy.name = uniqueName(copy.name, sequenceNames(puppet));
    // The copy plays copies of the original's poses, so changing one changes only it.
    QHash<QString, QString> copied;
    for (PuppetSequenceStep& step : copy.steps) {
        const int pose = poseIndexOf(puppet, step.pose);
        if (pose < 0 || puppet.poses[pose].owner != puppet.sequences[sequenceIndex].name) continue;
        if (!copied.contains(step.pose)) copied.insert(step.pose, addAnimationCopy(puppet, pose, copy.name));
        step.pose = copied.value(step.pose);
    }
    puppet.sequences.insert(sequenceIndex + 1, copy);
    return sequenceIndex + 1;
}

void duplicateStep(Puppet& puppet, int sequenceIndex, int stepIndex) {
    if (!validSequence(puppet, sequenceIndex)) return;
    QVector<PuppetSequenceStep>& steps = puppet.sequences[sequenceIndex].steps;
    if (stepIndex < 0 || stepIndex >= steps.size()) return;
    PuppetSequenceStep copy = steps[stepIndex];
    const int pose = poseIndexOf(puppet, copy.pose);
    const QString owner = puppet.sequences[sequenceIndex].name;
    if (pose >= 0 && puppet.poses[pose].owner == owner) copy.pose = addAnimationCopy(puppet, pose, owner);
    puppet.sequences[sequenceIndex].steps.insert(stepIndex + 1, copy);
}

}  // namespace PuppetEditing
