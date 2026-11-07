#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include "PuppetModels.h"

/// What PuppetTextCodec::read() found in a puppets file.
struct PuppetTextReadResult {
    QVector<Puppet> puppets;
    QString root;          ///< The file's `root` line, or empty
    QStringList warnings;  ///< Lines that were ignored, and why
    QString error;         ///< Set when the file can't be used (e.g. a repeated name); puppets is then empty
};

/// Reads and writes sprat-cli's hand-written puppets file (`puppets.txt`): `root`, `puppet`,
/// `part`, `- sprite`, `- animation`, `attach`, `pose`, `- part`, `sequence` and `- pose`
/// lines, as sprat-cli's README ("Puppets") describes.
///
/// Reading follows spratconvert: a line that doesn't fit is reported and skipped, and a
/// repeated puppet, part, variant, pose or sequence name is an error. References (sprites,
/// animations, attached parts, posed parts and variants, sequenced poses) are kept as
/// written; spratconvert checks them against the layout.
class PuppetTextCodec {
public:
    /// Parses a puppets file.
    static PuppetTextReadResult read(const QString& text);

    /// Writes @p puppets as a puppets file. Parts are written in draw order with their
    /// variants, then the attachments, poses and sequences of each puppet.
    /// @param root Written as a `root` line when not empty; leave it out for a layout stream
    static QString write(const QVector<Puppet>& puppets, const QString& root = QString());
};
