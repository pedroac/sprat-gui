#include "PuppetTextCodec.h"
#include "CliTextFormat.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

namespace {

QString trPT(const char* text) {
    return QCoreApplication::translate("PuppetTextCodec", text);
}

bool isSpace(QChar c) {
    return c == QLatin1Char(' ') || c == QLatin1Char('\t');
}

void skipSpaces(const QString& line, qsizetype& pos) {
    while (pos < line.size() && isSpace(line[pos])) ++pos;
}

// A whole number, as sprat-cli reads it: an optional minus and digits, nothing else.
bool parseInt(const QString& text, int& out) {
    static const QRegularExpression wholeNumber(QStringLiteral("^-?[0-9]+$"));
    if (!wholeNumber.match(text).hasMatch()) return false;
    bool ok = false;
    const int value = text.toInt(&ok);
    if (ok) out = value;
    return ok;
}

// "X,Y" in whole numbers.
bool parsePair(const QString& text, QPoint& out) {
    const QStringList parts = text.split(QLatin1Char(','));
    int x = 0;
    int y = 0;
    if (parts.size() != 2 || !parseInt(parts[0], x) || !parseInt(parts[1], y)) return false;
    out = QPoint(x, y);
    return true;
}

// A quoted string at `pos` (on its opening quote). \" and \\ are escapes; any other
// backslash is kept. On success `pos` is just past the closing quote.
bool readQuoted(const QString& line, qsizetype& pos, QString& out) {
    if (pos >= line.size() || line[pos] != QLatin1Char('"')) return false;
    ++pos;
    out.clear();
    while (pos < line.size()) {
        const QChar c = line[pos++];
        if (c == QLatin1Char('\\')) {
            if (pos >= line.size()) return false;
            const QChar escaped = line[pos++];
            if (escaped != QLatin1Char('"') && escaped != QLatin1Char('\\')) out += QLatin1Char('\\');
            out += escaped;
        } else if (c == QLatin1Char('"')) {
            return true;
        } else {
            out += c;
        }
    }
    return false;
}

// A name, quoted or bare, after any spaces at `pos`. A bare name ends at any of `stops`.
// An empty name fails, as in sprat-cli.
bool readName(const QString& line, qsizetype& pos, QString& name, const QString& stops = QStringLiteral(" \t"),
              bool* quoted = nullptr) {
    skipSpaces(line, pos);
    const bool isQuoted = pos < line.size() && line[pos] == QLatin1Char('"');
    if (quoted) *quoted = isQuoted;
    if (isQuoted) return readQuoted(line, pos, name) && !name.isEmpty();
    const qsizetype start = pos;
    while (pos < line.size() && !stops.contains(line[pos])) ++pos;
    name = line.mid(start, pos - start);
    return !name.isEmpty();
}

// "PART":"MARKER", either name quoted or bare.
bool readPartMarker(const QString& line, qsizetype& pos, QString& part, QString& marker) {
    if (!readName(line, pos, part, QStringLiteral(" \t:")) || pos >= line.size() || line[pos] != QLatin1Char(':'))
        return false;
    ++pos;
    if (pos < line.size() && isSpace(line[pos])) return false;  // "PART: MARKER" is a typo
    return readName(line, pos, marker);
}

// "KEY:VALUE" settings; a value may be quoted to hold spaces. A word without a colon has an
// empty key. False on a broken quote.
bool splitSettings(const QString& text, QVector<QPair<QString, QString>>& out) {
    qsizetype pos = 0;
    while (true) {
        skipSpaces(text, pos);
        if (pos >= text.size()) return true;
        qsizetype end = pos;
        while (end < text.size() && !isSpace(text[end]) && text[end] != QLatin1Char(':')) ++end;
        if (end >= text.size() || text[end] != QLatin1Char(':')) {
            out.append({QString(), text.mid(pos, end - pos)});
            pos = end;
            continue;
        }
        const QString key = text.mid(pos, end - pos);
        pos = end + 1;
        QString value;
        if (pos < text.size() && text[pos] == QLatin1Char('"')) {
            if (!readQuoted(text, pos, value)) return false;
        } else {
            const qsizetype valueStart = pos;
            while (pos < text.size() && !isSpace(text[pos])) ++pos;
            value = text.mid(valueStart, pos - valueStart);
        }
        out.append({key, value});
    }
}

// A line's keyword: its first word, or "- WORD" for a list line.
QString lineKeyword(const QString& line) {
    const QStringList words = line.split(QRegularExpression(QStringLiteral("[ \\t]+")), Qt::SkipEmptyParts);
    if (words.isEmpty()) return QString();
    if (words[0] == QLatin1String("-")) return words.size() > 1 ? QStringLiteral("- ") + words[1] : QStringLiteral("-");
    return words[0];
}

// Position just past `word` in a line that starts with its keyword.
qsizetype afterWord(const QString& line, const QString& word) {
    return line.indexOf(word) + word.size();
}

QString settingText(const QPair<QString, QString>& setting) {
    return setting.first.isEmpty() ? setting.second : setting.first + QLatin1Char(':') + setting.second;
}

}  // namespace

PuppetTextReadResult PuppetTextCodec::read(const QString& text) {
    PuppetTextReadResult result;
    QSet<QString> puppetNames;
    QSet<QString> partNames;
    QSet<QString> variantNames;
    QSet<QString> poseNames;
    QSet<QString> sequenceNames;
    // Indexes into the current puppet's lists, or -1 when the last line didn't open one.
    int currentPart = -1;
    int currentPose = -1;
    int currentSequence = -1;
    bool skippingPart = false;  // after a part line that couldn't be read: its variants go with it

    const auto fail = [&result](const QString& error) {
        result.error = error;
        result.puppets.clear();
        return result;
    };
    const auto warn = [&result](const char* message, const QString& line) {
        result.warnings.append(trPT(message).arg(line));
    };

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const QString keyword = lineKeyword(line);

        if (keyword == QLatin1String("root")) {
            qsizetype pos = afterWord(line, keyword);
            skipSpaces(line, pos);
            QString root;
            if (!readQuoted(line, pos, root) || !line.mid(pos).trimmed().isEmpty()) {
                warn("Puppets file line \"%1\" is not root \"DIRECTORY\"; it is ignored", line);
            } else {
                result.root = root;
            }
            continue;
        }
        if (keyword == QLatin1String("puppet")) {
            qsizetype pos = afterWord(line, keyword);
            Puppet puppet;
            if (!readName(line, pos, puppet.name) || !line.mid(pos).trimmed().isEmpty())
                return fail(trPT("Puppets file line \"%1\" is not puppet \"NAME\"").arg(line));
            if (puppetNames.contains(puppet.name))
                return fail(trPT("Puppet \"%1\" is defined more than once").arg(puppet.name));
            puppetNames.insert(puppet.name);
            result.puppets.append(puppet);
            partNames.clear();
            poseNames.clear();
            sequenceNames.clear();
            currentPart = currentPose = currentSequence = -1;
            skippingPart = false;
            continue;
        }

        static const QStringList puppetKeywords = {
            QStringLiteral("part"), QStringLiteral("- sprite"), QStringLiteral("- animation"),
            QStringLiteral("attach"), QStringLiteral("pose"), QStringLiteral("- part"),
            QStringLiteral("sequence"), QStringLiteral("- pose")};
        if (!puppetKeywords.contains(keyword)) {
            warn("Puppets file line \"%1\" is not a root, puppet, part, - sprite, - animation, attach, pose, "
                 "- part, sequence or - pose line; it is ignored", line);
            continue;
        }
        if (result.puppets.isEmpty()) {
            warn("Puppets file line \"%1\" is not inside a puppet; it is ignored", line);
            continue;
        }
        Puppet& puppet = result.puppets.last();
        if (keyword != QLatin1String("- part")) currentPose = -1;      // a pose's lines follow it directly
        if (keyword != QLatin1String("- pose")) currentSequence = -1;  // and so do a sequence's

        if (keyword == QLatin1String("part")) {
            currentPart = -1;
            qsizetype pos = afterWord(line, keyword);
            PuppetPart part;
            skippingPart = !readName(line, pos, part.name);
            if (skippingPart) {
                warn("Puppets file line \"%1\" is not part \"NAME\" [at:X,Y]; the part is ignored", line);
                continue;
            }
            if (partNames.contains(part.name))
                return fail(trPT("Puppet \"%1\" has more than one part \"%2\"").arg(puppet.name, part.name));
            partNames.insert(part.name);
            for (const QString& token : line.mid(pos).split(QRegularExpression(QStringLiteral("[ \\t]+")),
                                                            Qt::SkipEmptyParts)) {
                QPoint position;
                if (token.startsWith(QLatin1String("at:")) && !part.position && parsePair(token.mid(3), position)) {
                    part.position = position;
                } else {
                    result.warnings.append(trPT("Part line \"%1\" has an unknown, repeated or unreadable setting "
                                                "\"%2\"; it is ignored").arg(line, token));
                }
            }
            puppet.parts.append(part);
            currentPart = puppet.parts.size() - 1;
            variantNames.clear();
        } else if (keyword == QLatin1String("- sprite") || keyword == QLatin1String("- animation")) {
            if (skippingPart) continue;  // already reported with the part
            if (currentPart < 0) {
                warn("Puppets file line \"%1\" is not inside a part; it is ignored", line);
                continue;
            }
            const bool isAnimation = keyword == QLatin1String("- animation");
            PuppetVariant variant;
            variant.kind = isAnimation ? PuppetVariant::Kind::Animation : PuppetVariant::Kind::Sprite;
            qsizetype pos = afterWord(line, isAnimation ? QStringLiteral("animation") : QStringLiteral("sprite"));
            bool quoted = false;
            bool ok = readName(line, pos, variant.source, QStringLiteral(" \t"), &quoted);
            if (ok) {
                const QString tail = line.mid(pos).trimmed();
                qsizetype namePos = 2;
                ok = tail.isEmpty()
                    || (tail.startsWith(QLatin1String("->")) && readName(tail, namePos, variant.name)
                        && tail.mid(namePos).trimmed().isEmpty());
            }
            if (!ok) {
                warn(isAnimation
                         ? "Puppets file line \"%1\" is not - animation \"ANIMATION\" [-> \"NAME\"]; it is ignored"
                         : "Puppets file line \"%1\" is not - sprite \"SPRITE\" [-> \"NAME\"]; it is ignored",
                     line);
                continue;
            }
            int index = 0;
            variant.byIndex = !isAnimation && !quoted && parseInt(variant.source, index);
            PuppetPart& part = puppet.parts[currentPart];
            const QString name = puppetVariantName(variant);
            if (variantNames.contains(name))
                return fail(trPT("Puppet \"%1\" part \"%2\" has more than one variant \"%3\"")
                                .arg(puppet.name, part.name, name));
            variantNames.insert(name);
            part.variants.append(variant);
        } else if (keyword == QLatin1String("attach")) {
            PuppetAttachment attachment;
            qsizetype pos = afterWord(line, keyword);
            QString to;
            const bool ok = readPartMarker(line, pos, attachment.child, attachment.childMarker)
                && readName(line, pos, to) && to == QLatin1String("to")
                && readPartMarker(line, pos, attachment.parent, attachment.parentMarker);
            if (!ok) {
                warn("Puppets file line \"%1\" is not attach \"PART\":\"MARKER\" to \"PART\":\"MARKER\"; "
                     "it is ignored", line);
                continue;
            }
            bool hasLeeway = false;
            for (const QString& token : line.mid(pos).split(QRegularExpression(QStringLiteral("[ \\t]+")),
                                                            Qt::SkipEmptyParts)) {
                const QString key = token.section(QLatin1Char(':'), 0, 0);
                const QString value = token.section(QLatin1Char(':'), 1);
                int leeway = 0;
                const QStringList range = value.split(QStringLiteral(".."));
                int min = 0;
                int max = 0;
                if (key == QLatin1String("leeway") && !hasLeeway && parseInt(value, leeway) && leeway >= 0) {
                    attachment.leeway = leeway;
                    hasLeeway = true;
                } else if (key == QLatin1String("angle") && !attachment.angleMin && range.size() == 2
                           && parseInt(range[0], min) && parseInt(range[1], max) && min <= max
                           && min >= -PUPPET_MAX_ANGLE && max <= PUPPET_MAX_ANGLE) {
                    attachment.angleMin = min;
                    attachment.angleMax = max;
                } else {
                    result.warnings.append(trPT("Attach line \"%1\" has an unknown, repeated or unreadable "
                                                "setting \"%2\"; it is ignored").arg(line, token));
                }
            }
            puppet.attachments.append(attachment);
        } else if (keyword == QLatin1String("pose")) {
            currentPart = -1;
            skippingPart = false;
            qsizetype pos = afterWord(line, keyword);
            PuppetPose pose;
            if (!readName(line, pos, pose.name) || !line.mid(pos).trimmed().isEmpty()) {
                warn("Puppets file line \"%1\" is not pose \"NAME\"; the pose is ignored", line);
                continue;
            }
            if (poseNames.contains(pose.name))
                return fail(trPT("Puppet \"%1\" has more than one pose \"%2\"").arg(puppet.name, pose.name));
            poseNames.insert(pose.name);
            puppet.poses.append(pose);
            currentPose = puppet.poses.size() - 1;
        } else if (keyword == QLatin1String("- part")) {
            if (currentPose < 0) {
                warn("Puppets file line \"%1\" is not inside a pose; it is ignored", line);
                continue;
            }
            PuppetPoseSetting setting;
            qsizetype pos = afterWord(line, QStringLiteral("part"));
            QVector<QPair<QString, QString>> settings;
            if (!readName(line, pos, setting.part) || !splitSettings(line.mid(pos), settings)) {
                warn("Puppets file line \"%1\" is not - part \"PART\" [variant:NAME] [angle:DEGREES] "
                     "[offset:X,Y] [at:X,Y]; it is ignored", line);
                continue;
            }
            for (const auto& item : settings) {
                const QString& key = item.first;
                const QString& value = item.second;
                int angle = 0;
                QPoint pair;
                if (key == QLatin1String("variant") && setting.variant.isEmpty() && !value.isEmpty()) {
                    setting.variant = value;
                } else if (key == QLatin1String("angle") && !setting.angle && parseInt(value, angle)
                           && angle >= -PUPPET_MAX_ANGLE && angle <= PUPPET_MAX_ANGLE) {
                    setting.angle = angle;
                } else if (key == QLatin1String("offset") && !setting.offset && parsePair(value, pair)) {
                    setting.offset = pair;
                } else if (key == QLatin1String("at") && !setting.position && parsePair(value, pair)) {
                    setting.position = pair;
                } else {
                    result.warnings.append(trPT("Pose line \"%1\" has an unknown, repeated or unreadable "
                                                "setting \"%2\"; it is ignored").arg(line, settingText(item)));
                }
            }
            if (setting.variant.isEmpty() && !setting.angle && !setting.offset && !setting.position) {
                warn("Pose line \"%1\" sets nothing; it is ignored", line);
                continue;
            }
            puppet.poses[currentPose].settings.append(setting);
        } else if (keyword == QLatin1String("sequence")) {
            currentPart = -1;
            skippingPart = false;
            qsizetype pos = afterWord(line, keyword);
            PuppetSequence sequence;
            if (!readName(line, pos, sequence.name)) {
                warn("Puppets file line \"%1\" is not sequence \"NAME\" [fps:N] [loop:MODE]; "
                     "the sequence is ignored", line);
                continue;
            }
            if (sequenceNames.contains(sequence.name))
                return fail(trPT("Puppet \"%1\" has more than one sequence \"%2\"").arg(puppet.name, sequence.name));
            sequenceNames.insert(sequence.name);
            QVector<QPair<QString, QString>> settings;
            if (!splitSettings(line.mid(pos), settings))
                warn("Sequence line \"%1\" has a broken quote; the settings after it are ignored", line);
            bool hasFps = false;
            bool hasLoop = false;
            for (const auto& item : settings) {
                int fps = 0;
                const auto loopMode = animationLoopModeFromName(item.second);
                if (item.first == QLatin1String("fps") && !hasFps && parseInt(item.second, fps) && fps >= 1) {
                    if (fps > MAX_ANIMATION_FPS)
                        return fail(trPT("Sequence line \"%1\" has fps %2, above the limit of %3")
                                        .arg(line, item.second).arg(MAX_ANIMATION_FPS));
                    sequence.fps = fps;
                    hasFps = true;
                } else if (item.first == QLatin1String("loop") && !hasLoop && loopMode) {
                    sequence.loopMode = *loopMode;
                    hasLoop = true;
                } else {
                    result.warnings.append(trPT("Sequence line \"%1\" has an unknown, repeated or unreadable "
                                                "setting \"%2\"; it is ignored").arg(line, settingText(item)));
                }
            }
            puppet.sequences.append(sequence);
            currentSequence = puppet.sequences.size() - 1;
        } else {  // "- pose"
            if (currentSequence < 0) {
                warn("Puppets file line \"%1\" is not inside a sequence; it is ignored", line);
                continue;
            }
            PuppetSequenceStep step;
            qsizetype pos = afterWord(line, QStringLiteral("pose"));
            QVector<QPair<QString, QString>> settings;
            if (!readName(line, pos, step.pose) || !splitSettings(line.mid(pos), settings)) {
                warn("Puppets file line \"%1\" is not - pose \"POSE\" [in:FRAMES] [hold:FRAMES] "
                     "[ease:linear|step]; it is ignored", line);
                continue;
            }
            bool hasIn = false;
            bool hasHold = false;
            bool hasEase = false;
            for (const auto& item : settings) {
                int frames = 0;
                const bool isFrames = parseInt(item.second, frames) && frames >= 0 && frames <= PUPPET_MAX_STEP_FRAMES;
                if (item.first == QLatin1String("in") && !hasIn && isFrames) {
                    step.in = frames;
                    hasIn = true;
                } else if (item.first == QLatin1String("hold") && !hasHold && isFrames) {
                    step.hold = frames;
                    hasHold = true;
                } else if (item.first == QLatin1String("ease") && !hasEase
                           && (item.second == QLatin1String("linear") || item.second == QLatin1String("step"))) {
                    step.snap = item.second == QLatin1String("step");
                    hasEase = true;
                } else {
                    result.warnings.append(trPT("Step line \"%1\" has an unknown, repeated or unreadable "
                                                "setting \"%2\"; it is ignored").arg(line, settingText(item)));
                }
            }
            if (step.in + step.hold == 0) {
                warn("Step line \"%1\" lasts no frame; give it in:FRAMES or hold:FRAMES; it is ignored", line);
                continue;
            }
            puppet.sequences[currentSequence].steps.append(step);
        }
    }
    return result;
}

QString PuppetTextCodec::write(const QVector<Puppet>& puppets, const QString& root) {
    QString text;
    QTextStream out(&text);
    const auto pair = [](const QPoint& point) {
        return QStringLiteral("%1,%2").arg(point.x()).arg(point.y());
    };
    if (!root.isEmpty()) out << "root " << cliQuoted(root) << "\n\n";
    for (const Puppet& puppet : puppets) {
        out << "puppet " << cliQuoted(puppet.name) << "\n\n";
        for (const PuppetPart& part : puppet.parts) {
            out << "part " << cliQuoted(part.name);
            if (part.position) out << " at:" << pair(*part.position);
            out << "\n";
            for (const PuppetVariant& variant : part.variants) {
                const bool isAnimation = variant.kind == PuppetVariant::Kind::Animation;
                out << (isAnimation ? "- animation " : "- sprite ")
                    << (variant.byIndex ? variant.source : cliQuoted(variant.source));
                if (!variant.name.isEmpty()) out << " -> " << cliQuoted(variant.name);
                out << "\n";
            }
            out << "\n";
        }
        for (const PuppetAttachment& attachment : puppet.attachments) {
            out << "attach " << cliQuoted(attachment.child) << ":" << cliQuoted(attachment.childMarker)
                << " to " << cliQuoted(attachment.parent) << ":" << cliQuoted(attachment.parentMarker);
            if (attachment.leeway != 0) out << " leeway:" << attachment.leeway;
            if (attachment.angleMin && attachment.angleMax)
                out << " angle:" << *attachment.angleMin << ".." << *attachment.angleMax;
            out << "\n";
        }
        if (!puppet.attachments.isEmpty()) out << "\n";
        for (const PuppetPose& pose : puppet.poses) {
            out << "pose " << cliQuoted(pose.name) << "\n";
            for (const PuppetPoseSetting& setting : pose.settings) {
                out << "- part " << cliQuoted(setting.part);
                if (!setting.variant.isEmpty()) out << " variant:" << cliQuoted(setting.variant);
                if (setting.angle) out << " angle:" << *setting.angle;
                if (setting.offset) out << " offset:" << pair(*setting.offset);
                if (setting.position) out << " at:" << pair(*setting.position);
                out << "\n";
            }
            out << "\n";
        }
        for (const PuppetSequence& sequence : puppet.sequences) {
            out << "sequence " << cliQuoted(sequence.name);
            if (sequence.fps != DEFAULT_ANIMATION_FPS) out << " fps:" << sequence.fps;
            if (sequence.loopMode != AnimationLoopMode::Loop) out << " loop:" << animationLoopModeName(sequence.loopMode);
            out << "\n";
            for (const PuppetSequenceStep& step : sequence.steps) {
                out << "- pose " << cliQuoted(step.pose);
                if (step.in != 0) out << " in:" << step.in;
                if (step.hold != 0) out << " hold:" << step.hold;
                if (step.snap) out << " ease:step";
                out << "\n";
            }
            out << "\n";
        }
    }
    return text;
}
