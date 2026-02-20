#pragma once

#include <QStringList>

class ResolutionsConfig {
public:
    static QString configPath();
    static QStringList loadResolutionOptions();
};
