#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct SpratProfile {
    QString name;
    QString mode;
    QString optimize;
    int maxWidth = -1;
    int maxHeight = -1;
    int padding = 0;
    int maxCombinations = 0;
    double scale = 1.0;
    bool trimTransparent = true;
};

class SpratProfilesConfig {
public:
    static QString configPath();
    static QVector<SpratProfile> loadProfileDefinitions(QString* errorMessage = nullptr);
    static bool saveProfileDefinitions(const QVector<SpratProfile>& profiles);
    static QStringList loadProfiles();
};
