#pragma once
#include <QString>
#include <QStringList>

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
