#pragma once

/// @file ExportPipelineTests.h
/// End-to-end tests for the export pipeline: ProjectSaveService driving the real sprat-cli
/// tools (embedded, or the executables named by SPRAT_CLI_DIR) on generated sprites.

#include <QJsonObject>
#include <QObject>
#include <QTemporaryDir>
#include <QVector>

#include "ExportModels.h"
#include "SpratProfilesConfig.h"

class ExportPipelineTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testLayoutOutputParsesInGui();
    void testJsonExportWritesAtlasAndMetadata();
    void testLibGdxExportWithRotatedSprites();
    void testZopfliProfileExportsPng();
    void testMultipackExportWritesEveryAtlas();
    void testZipDestinationContainsProfileFolder();
    void testUnicodePaths();
    void testNineSliceInsetsAreExportedAndFitted();
    void testProjectTextCantInjectCliRecords();

private:
    struct ExportRun {
        bool ok = false;
        QString error;
        QVector<ExportLogEntry> log;
    };

    ExportRun exportSprites(const QString& spritesDir, const QString& destination,
                            const QString& transform, const SpratProfile& profile,
                            const QJsonObject& projectPayload = QJsonObject()) const;
    QString toolPath(const QString& toolName) const;

    QTemporaryDir m_root;
    QString m_spritesDir;
    QString m_cliDir;
};
