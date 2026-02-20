#pragma once

#include <QJsonObject>
#include <QString>

/**
 * @class ProjectFileLoader
 * @brief Static class for loading project files.
 * 
 * This class provides static methods for loading project files
 * in JSON format, handling file I/O and JSON parsing.
 */
class ProjectFileLoader {
public:
    /**
     * @brief Loads a project file from disk.
     * 
     * This method reads a project file from the specified path,
     * parses the JSON content, and returns the parsed data.
     * 
     * @param path Path to the project file
     * @param root Reference to store the parsed JSON object
     * @param error Reference to store error message if loading fails
     * @return bool True if file was loaded successfully, false otherwise
     */
    static bool load(const QString& path, QJsonObject& root, QString& error);
};
