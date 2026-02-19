#pragma once

#include <QString>
#include "models.h"

class LayoutParser {
public:
    static LayoutModel parse(const QString& output, const QString& folderPath);
};
