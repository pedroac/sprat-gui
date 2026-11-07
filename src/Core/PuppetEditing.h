#pragma once
#include <optional>
#include <QPoint>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include "PuppetModels.h"

/// Edits of puppets that keep them consistent: renaming or removing a part also updates
/// the attachments and poses that name it, and names stay unique as sprat-cli requires.
/// Pure: the Puppet workspace wraps each edit in one undo command.
namespace PuppetEditing {

/// @p base, or "base 2", "base 3"... the first one not in @p taken.
QString uniqueName(const QString& base, const QStringList& taken);

QStringList puppetNames(const QVector<Puppet>& puppets);
QStringList partNames(const Puppet& puppet);
QStringList poseNames(const Puppet& puppet);
/// The names the part's variants go by (see puppetVariantName()).
QStringList variantNames(const PuppetPart& part);

/// Why @p name can't be the name of puppet @p index (empty, or another puppet's), or an
/// empty string when it can. Use index -1 for a new puppet.
QString puppetNameProblem(const QVector<Puppet>& puppets, int index, const QString& name);
QString partNameProblem(const Puppet& puppet, int index, const QString& name);
QString poseNameProblem(const Puppet& puppet, int index, const QString& name);
/// @p name is what the variant would go by: an empty name means the default name.
QString variantNameProblem(const PuppetPart& part, int index, const QString& name);

/// Renames a part, and the attachments and pose settings that name it.
void renamePart(Puppet& puppet, int partIndex, const QString& name);
/// Removes a part with its attachments (as child or parent) and its pose settings.
void removePart(Puppet& puppet, int partIndex);
/// Moves a part in the draw order; @p to is its index afterwards.
void movePart(Puppet& puppet, int from, int to);

/// Names a variant, updating the poses that show it. An empty name, or the default
/// one, goes back to the default.
void setVariantName(Puppet& puppet, int partIndex, int variantIndex, const QString& name);
/// Removes a variant; poses that showed it keep the part's current variant instead.
void removeVariant(Puppet& puppet, int partIndex, int variantIndex);
/// Moves a variant; the first is the rest variant. @p to is its index afterwards.
void moveVariant(Puppet& puppet, int partIndex, int from, int to);

/// The parts that hang from part @p partIndex, directly or through others, by their
/// parent attachments. A part can't be attached to any of them: that would make a loop.
QSet<int> partsHangingFrom(const Puppet& puppet, int partIndex);
/// Whether part @p childIndex may hang from part @p parentIndex.
bool canAttach(const Puppet& puppet, int childIndex, int parentIndex);

/// Hangs a part from @p parentMarker of part @p parentIndex by its own @p childMarker.
/// An attached part keeps its leeway and angle limits; a positioned part loses its
/// position, and the new attachment goes before its others so it becomes the parent.
void attachPart(Puppet& puppet, int partIndex, int parentIndex, const QString& parentMarker,
                const QString& childMarker);
/// Places an attached part by @p position instead, removing its parent attachment.
void detachPart(Puppet& puppet, int partIndex, const QPoint& position);
/// Sets how far an attached part's joint may drift and how far it may turn.
void setAttachmentLimits(Puppet& puppet, int partIndex, int leeway, std::optional<int> angleMin,
                         std::optional<int> angleMax);

/// Renames a pose, and the sequence steps that play it.
void renamePose(Puppet& puppet, int poseIndex, const QString& name);
/// Removes a pose, and the sequence steps that play it.
void removePose(Puppet& puppet, int poseIndex);
/// Moves a pose in the list; @p to is its index afterwards.
void movePose(Puppet& puppet, int from, int to);

/// Adds a sprite variant for each path the part doesn't show yet. A sprite whose name
/// another variant already goes by gets a unique explicit name. Returns how many it added.
int addSpriteVariants(Puppet& puppet, int partIndex, const QStringList& spritePaths);

}  // namespace PuppetEditing
