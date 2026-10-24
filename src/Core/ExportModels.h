#pragma once
#include <QString>
#include <QStringList>

/// Transform id of the built-in export that writes sprat-cli's own text files
/// (layout.txt, markers.txt, animations.txt) without running spratconvert.
/// Projects older than schema 5 stored it as "raw", which sprat-cli now uses
/// for its own raw.jsonnet transform.
inline const QString kLayoutTextTransform = QStringLiteral("layout-text");

/**
 * @struct SaveConfig
 * @brief Configuration for saving the project/spritesheet.
 */
struct SaveConfig {
    QString destination;
    QString outputPath;
    QString transform;
    QStringList profiles;
    bool profilesGlobal = true;
    QString scaleFilter;
    int     colors = 0;    // 0 = off, 2-256 = color count for quantization
    bool    dither = false;
    bool syncSprites = false;
    QString postExportCommand;
    QString atlasSubdir;
};

struct ExportLogEntry {
    enum class Kind { FileWritten, Info, Error };
    Kind    kind = Kind::FileWritten;
    QString path;
    qint64  size = -1;
};
