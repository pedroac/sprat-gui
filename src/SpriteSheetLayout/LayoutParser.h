#pragma once

#include <QString>
#include <QVector>
#include "LayoutModels.h"

class LayoutParser {
public:
    static QVector<LayoutModel> parse(const QString& output, const QString& folderPath,
                                      const QString& sourceFolder = QString());
};
