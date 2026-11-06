#include "PuppetEditing.h"

#include <algorithm>
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

void moveVariant(Puppet& puppet, int partIndex, int from, int to) {
    if (!validVariant(puppet, partIndex, from) || !validVariant(puppet, partIndex, to) || from == to) return;
    puppet.parts[partIndex].variants.move(from, to);
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
