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
    int targetResolutionWidth = 1024;
    int targetResolutionHeight = 1024;
    bool targetResolutionUseSource = false;
    QString resolutionReference = "largest";
    int padding = 0;
    int maxCombinations = 0;
    int threads = 0;
    bool trimTransparent = true;
};

class SpratProfilesConfig {
public:
    static QString configPath();
    static QVector<SpratProfile> loadProfileDefinitions(QString* errorMessage = nullptr);
    static bool saveProfileDefinitions(const QVector<SpratProfile>& profiles);
    static QStringList loadProfiles();
};
