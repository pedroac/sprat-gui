#pragma once

#include <QString>
#include <QStringList>

/**
 * @brief Parses a resolution string in the form "<width>x<height>".
 * @param value Raw input text.
 * @param width Parsed width output.
 * @param height Parsed height output.
 * @return true when the input is valid and both values are > 0.
 */
inline bool parseResolutionText(const QString& value, int& width, int& height) {
    const QStringList parts = value.trimmed().toLower().split('x', Qt::SkipEmptyParts);
    if (parts.size() != 2) {
        return false;
    }

    bool okW = false;
    bool okH = false;
    const int parsedWidth = parts[0].trimmed().toInt(&okW);
    const int parsedHeight = parts[1].trimmed().toInt(&okH);
    if (!okW || !okH || parsedWidth <= 0 || parsedHeight <= 0) {
        return false;
    }

    width = parsedWidth;
    height = parsedHeight;
    return true;
}

/**
 * @brief Formats a resolution as "<width>x<height>".
 */
inline QString formatResolutionText(int width, int height) {
    return QString("%1x%2").arg(width).arg(height);
}
