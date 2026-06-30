#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct SpratProfile {
    QString name;
    QString label;
    QString preset = "quality";  // "fast", "quality", "small", "pot"
    int maxWidth = -1;
    int maxHeight = -1;
    int targetResolutionWidth = 1024;
    int targetResolutionHeight = 1024;
    bool targetResolutionUseSource = false;
    QString resolutionReference = "largest";
    int padding = 0;
    int extrude = 0;
    int threads = 0;
    bool trimTransparent = true;
    bool allowRotation = false;
    double scale = 1.0;
    bool multipack = false;
    QString sort = "none";
    QString gpuCompress = "";  // "" = none, "dxt1", "dxt5"
    int dilate = 0;             // 0 = disabled, N = passes
    QString imageFormat = "png";    // "png", "webp", "avif"
    int imageQuality = 100;         // 0-100 (100 = lossless)
    bool zopfli = false;            // Zopfli PNG optimization
    bool frameLines = false;        // Draw sprite outlines
    int frameLineWidth = 1;         // Line thickness in pixels
    QString frameLineColor = "255,0,0,255"; // R,G,B,A
    int atlasIndex = -1;            // -1 = all atlases, >= 0 = export only this atlas
    bool autoAnimations = false;    // Pass --auto-animations to spratconvert
};

class SpratProfilesConfig {
public:
    static QString configPath();
    static QString findProfilesConfigPath();
    static QVector<SpratProfile> loadProfileDefinitions(QString* errorMessage = nullptr);
    static bool saveProfileDefinitions(const QVector<SpratProfile>& profiles);
    static QStringList loadProfiles();
};
