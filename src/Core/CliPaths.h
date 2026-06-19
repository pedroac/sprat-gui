#pragma once
#include <QString>

/**
 * @struct CliPaths
 * @brief Paths to CLI tool binaries.
 */
struct CliPaths {
    QString baseDir;
    QString layoutBinary;
    QString packBinary;
    QString convertBinary;
    QString framesBinary;
    QString unpackBinary;
};
