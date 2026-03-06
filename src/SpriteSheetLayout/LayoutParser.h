#pragma once

#include <QString>
#include <QVector>
#include "models.h"

class LayoutParser {
public:
    static QVector<LayoutModel> parse(const QString& output, const QString& folderPath);
};
