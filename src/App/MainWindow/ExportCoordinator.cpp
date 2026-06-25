#include "ExportCoordinator.h"
#include "ILayoutContext.h"
#include "LayoutOrchestrator.h"
#include "LayoutCanvas.h"
#include "PackedAtlasView.h"
#include "ExportWorkspace.h"
#include "LayoutParser.h"
#include "ProjectSession.h"
#include "ProjectSaveService.h"
#include "MessageDialog.h"
#include "models.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScrollBar>
#include <QTemporaryFile>
#include <QTextStream>
#include <QTimer>
#include <QtConcurrent>
#include <QApplication>

#ifdef Q_OS_WASM
#include <emscripten.h>
#endif

// Returns the output subdirectory for an atlas, automatically deriving one from the
// atlas name when multiple atlases would otherwise all write to the same root directory.
static QString effectiveOutputSubdir(const AtlasEntry& atlas, int rootExporterCount) {
    if (!atlas.outputSubdir.isEmpty())
        return atlas.outputSubdir;
    if (rootExporterCount <= 1)
        return {};
    if (atlas.isNeutral)
        return QStringLiteral("sprites");
    const QString slug = atlas.name.trimmed().toLower().replace(QLatin1Char(' '), QLatin1Char('_'));
    return slug.isEmpty() ? QStringLiteral("atlas") : slug;
}

ExportCoordinator::ExportCoordinator(const Config& cfg, QObject* parent)
    : QObject(parent), m_cfg(cfg)
{
    connect(&m_exportWatcher, &QFutureWatcherBase::finished,
            this, &ExportCoordinator::onExportWatcherFinished);
    connect(&m_previewPackWatcher, &QFutureWatcherBase::finished,
            this, &ExportCoordinator::onPreviewPackWatcherFinished);
}

ExportCoordinator::~ExportCoordinator()
{
    // Signal any running background tasks to exit at the next cancellation
    // check point, then block until they actually finish so that captured
    // references to our members (m_exportCanceled, m_previewPackCanceled,
    // setStatus, shouldCancel) are never accessed after we return.
    m_exportCanceled      = true;
    m_previewPackCanceled->store(true);
    if (m_previewPackDebounceTimer) m_previewPackDebounceTimer->stop();
    m_exportWatcher.waitForFinished();
    m_previewPackWatcher.waitForFinished();
}

void ExportCoordinator::setAppSettings(const AppSettings& settings) {
    m_settings = settings;
}

void ExportCoordinator::setCliReady(bool ready) {
    m_cliReady = ready;
}

void ExportCoordinator::setLayoutBinary(const QString& path) {
    m_layoutBinary = path;
}

void ExportCoordinator::setPackBinary(const QString& path) {
    m_packBinary = path;
}

void ExportCoordinator::setConvertBinary(const QString& path) {
    m_convertBinary = path;
}

void ExportCoordinator::setExportWorkspaceActive(bool active) {
    m_exportWorkspaceActive = active;
}

void ExportCoordinator::setExportPreviewAtlasIndex(int idx) {
    m_exportPreviewAtlasIndex = idx;
}

void ExportCoordinator::invalidatePreviewCache() {
    m_cachedPackedImage.clear();
    m_cachedPackLayout.clear();
}

bool ExportCoordinator::isExportRunning() const {
    return m_exportWatcher.isRunning();
}

bool ExportCoordinator::isPreviewRunning() const {
    return m_previewPackWatcher.isRunning();
}

void ExportCoordinator::cancelPreview() {
    m_previewPackCanceled->store(true);
    if (m_previewPackDebounceTimer) m_previewPackDebounceTimer->stop();
}

void ExportCoordinator::cancelExport() {
    m_exportCanceled = true;
}

void ExportCoordinator::emitStatus(const QString& msg) {
    emit statusChanged(msg);
#ifdef Q_OS_WASM
    QApplication::processEvents();
#endif
}

void ExportCoordinator::onExportWatcherFinished() {
    handleExportResult(m_exportWatcher.result());
}

void ExportCoordinator::onPreviewPackWatcherFinished() {
    handlePackPreviewResult(m_previewPackWatcher.result());
}

bool ExportCoordinator::runExport(SaveConfig config) {
    // Map the export-specific output path into destination for ProjectSaveService.
    if (!config.outputPath.isEmpty()) {
        config.destination = config.outputPath;
    }

    if (m_layoutBinary.isEmpty() || m_packBinary.isEmpty()) {
        if (!m_cliReady) {
            MessageDialog::critical(m_cfg.parentWidget, tr("Error"), tr("Missing spratlayout or spratpack binaries."));
            return false;
        }
        if (m_cfg.session->layoutSourcePath.isEmpty()) {
            MessageDialog::critical(m_cfg.parentWidget, tr("Error"), tr("No layout source selected."));
            return false;
        }
    }
    if (m_exportWatcher.isRunning()) {
        return false;
    }

    // Stop any in-flight layout runner so it releases the tool mutex promptly.
    if (m_cfg.layoutOrchestrator) {
        m_cfg.layoutOrchestrator->stop();
    }

    // If the export-workspace preview pack is still running it holds the tool mutex.
    // Defer the export until it finishes rather than letting the export task block
    // silently on the mutex.
    if (m_previewPackWatcher.isRunning()) {
        connect(&m_previewPackWatcher, &QFutureWatcher<PackPreviewResult>::finished,
                this, [this, config]() { runExport(config); },
                Qt::SingleShotConnection);
        return true;
    }

    m_exportCanceled = false;
    emitStatus(tr("Saving..."));
    emit loadingStateChanged(true);

    auto setStatus = [this](const QString& status) {
#ifdef Q_OS_WASM
        emitStatus(status);
#else
        QMetaObject::invokeMethod(this, [this, status]() {
            emitStatus(status);
        }, Qt::QueuedConnection);
#endif
    };

    auto shouldCancel = [this]() {
#ifdef Q_OS_WASM
        QApplication::processEvents();
#endif
        return m_exportCanceled.load();
    };

    // Snapshot atlas data and configured profiles for background thread safety.
    // Large containers are moved into the lambda capture to avoid a redundant copy.
    QVector<AtlasEntry> atlasSnapshot = m_cfg.session->atlases;
    QStringList allFramePaths = m_cfg.session->activeFramePaths;
    const QString layoutSourcePath = m_cfg.session->layoutSourcePath;
    const QString sourceFolder = m_cfg.session->sourceFolder;
    QVector<SpratProfile> profiles = m_cfg.layoutContext->configuredProfiles();
    const QString deduplicateMode = m_settings.deduplicateMode;
    const QString spratLayoutBin = m_layoutBinary;
    const QString spratPackBin = m_packBinary;
    const QString spratConvertBin = m_convertBinary;
    QJsonObject projectPayload = m_cfg.buildProjectPayload(config, true);

    auto saveTask = [this, config,
                     atlasSnapshot   = std::move(atlasSnapshot),
                     allFramePaths   = std::move(allFramePaths),
                     layoutSourcePath,
                     sourceFolder,
                     profiles        = std::move(profiles),
                     deduplicateMode,
                     spratLayoutBin, spratPackBin, spratConvertBin,
                     projectPayload  = std::move(projectPayload),
                     setStatus, shouldCancel]() {
        ExportResult result;

        auto runToolBound = [this](const QString& tool, const QStringList& args, const QString& /*step*/, const QByteArray* input, QByteArray* output) {
            return m_cfg.runTool(tool, args, input, output, nullptr);
        };

        QVector<ExportLogEntry> logEntries;
        auto logEntryFn = [&logEntries](const ExportLogEntry& e) { logEntries.append(e); };

        // Collect non-empty atlases for per-atlas export
        QVector<AtlasEntry> atlasesToExport;
        for (const auto& atlas : atlasSnapshot) {
            if (!atlas.spritePaths.isEmpty()) {
                atlasesToExport.append(atlas);
            }
        }

        // Single-atlas (neutral-only) or empty project: use legacy path (backward compat)
        const bool multiAtlas = atlasesToExport.size() > 1
            || (!atlasesToExport.isEmpty() && !atlasesToExport.first().isNeutral);
        if (!multiAtlas) {
            result.success = ProjectSaveService::save(
                config,
                layoutSourcePath,
                allFramePaths,
                sourceFolder,
                profiles,
                QString(),
                spratLayoutBin,
                spratPackBin,
                spratConvertBin,
                projectPayload,
                result.savedDestination,
                result.error,
                deduplicateMode,
                {nullptr, setStatus, shouldCancel, runToolBound, logEntryFn}
            );
        } else {
            // Multi-atlas: export each atlas to its own subdirectory
            // Count atlases with no explicit subdir so we can auto-assign subdirs
            // when multiple atlases would otherwise collide on the same output path.
            int rootExporterCount = 0;
            for (const auto& a : atlasesToExport)
                if (a.outputSubdir.isEmpty()) ++rootExporterCount;

            bool first = true;
            for (const auto& atlas : atlasesToExport) {
                if (shouldCancel()) { result.canceled = true; break; }

                SaveConfig atlasConfig = config;
                // Apply per-atlas export overrides (empty = inherit global)
                if (!atlas.exportConfig.profiles.isEmpty())
                    atlasConfig.profiles = atlas.exportConfig.profiles;
                if (!atlas.exportConfig.transform.isEmpty())
                    atlasConfig.transform = atlas.exportConfig.transform;
                if (!atlas.exportConfig.scaleFilter.isEmpty())
                    atlasConfig.scaleFilter = atlas.exportConfig.scaleFilter;
                // Nest this atlas inside each profile folder rather than at the top level.
                // ProjectSaveService writes to <outputPath>/<profile>/<atlasSubdir>/.
                atlasConfig.atlasSubdir = effectiveOutputSubdir(atlas, rootExporterCount);

                // Write project JSON only on first atlas call
                const QJsonObject payload = first ? projectPayload : QJsonObject();
                first = false;

                setStatus(tr("Exporting '%1'...").arg(atlas.name));

                QString atlasDestination, atlasError;
                const bool ok = ProjectSaveService::save(
                    atlasConfig,
                    layoutSourcePath,
                    atlas.spritePaths,
                    sourceFolder,
                    profiles,
                    QString(),
                    spratLayoutBin,
                    spratPackBin,
                    spratConvertBin,
                    payload,
                    atlasDestination,
                    atlasError,
                    deduplicateMode,
                    {nullptr, setStatus, shouldCancel, runToolBound, logEntryFn}
                );
                if (!ok) {
                    result.success = false;
                    result.error = atlasError;
                    break;
                }
                if (result.savedDestination.isEmpty()) {
                    result.savedDestination = atlasDestination;
                }
            }
            if (!result.canceled && result.error.isEmpty()) {
                result.success = true;
            }
        }

        if (!result.success && shouldCancel()) {
            result.canceled = true;
        }

        // Run post-export hook command (in background thread; blocks until done)
        const QString hookCmd = config.postExportCommand.trimmed();
#ifndef Q_OS_WASM
        if (result.success && !hookCmd.isEmpty()) {
            QProcess proc;
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("SPRAT_EXPORT_PATH"), result.savedDestination);
            proc.setProcessEnvironment(env);
#ifdef Q_OS_WIN
            proc.start(QStringLiteral("cmd"), {QStringLiteral("/c"), hookCmd});
#else
            proc.start(QStringLiteral("sh"), {QStringLiteral("-c"), hookCmd});
#endif
            const bool finished = proc.waitForFinished(30000);
            ExportLogEntry hookEntry;
            if (!finished || proc.exitCode() != 0) {
                hookEntry.kind = ExportLogEntry::Kind::Error;
                hookEntry.path = finished
                    ? tr("Post-export hook exited with code %1: %2").arg(proc.exitCode()).arg(hookCmd)
                    : tr("Post-export hook timed out or failed to start: %1").arg(hookCmd);
            } else {
                hookEntry.kind = ExportLogEntry::Kind::Info;
                hookEntry.path = tr("Post-export hook completed: %1").arg(hookCmd);
            }
            logEntries.append(hookEntry);
        }
#else
        Q_UNUSED(hookCmd)
#endif

        result.logEntries = std::move(logEntries);
        return result;
    };

#ifdef Q_OS_WASM
    QTimer::singleShot(0, this, [this, saveTask]() {
        ExportResult result = saveTask();
        handleExportResult(result);
    });
#else
    m_exportWatcher.setFuture(QtConcurrent::run(saveTask));
#endif
    return true;
}

void ExportCoordinator::handleExportResult(const ExportResult& result) {
    emit loadingStateChanged(false);

    if (!result.logEntries.isEmpty())
        emit exportLogReady(result.logEntries, result.savedDestination);

    if (result.canceled) {
        emitStatus(tr("Save canceled"));
        return;
    }

    if (result.success) {
        if (m_cfg.promoteSourceFolderAfterSave) m_cfg.promoteSourceFolderAfterSave(result.savedDestination);

#ifdef Q_OS_WASM
        emitStatus(tr("Preparing download..."));
#else
        emitStatus(tr("Saved to ") + result.savedDestination);
#endif
#ifdef Q_OS_WASM
        // Trigger browser download for the saved file
        QFile file(result.savedDestination);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray content = file.readAll();
            QString fileName = QFileInfo(result.savedDestination).fileName();

            EM_ASM({
                var content = HEAPU8.slice($0, $0 + $1);
                var fileName = UTF8ToString($2);
                var blob = new Blob([content], {type: "application/octet-stream"});
                var url = window.URL.createObjectURL(blob);
                var a = document.createElement("a");
                a.href = url;
                a.download = fileName;
                document.body.appendChild(a);
                a.click();
                window.URL.revokeObjectURL(url);
                document.body.removeChild(a);
            }, content.constData(), content.size(), fileName.toUtf8().constData());

            file.close();
        }
        if (!result.savedDestination.isEmpty()) {
            QFile::remove(result.savedDestination);
        }
        emitStatus(tr("Download started"));
#endif
#ifdef Q_OS_WASM
        MessageDialog::information(m_cfg.parentWidget, tr("Saved"), tr("Download started."));
#else
        MessageDialog::information(m_cfg.parentWidget, tr("Saved"), tr("Project saved successfully to:\n") + result.savedDestination);
#endif
    } else {
        emitStatus(tr("Save failed"));
        MessageDialog::critical(m_cfg.parentWidget, tr("Save Failed"), result.error.isEmpty() ? tr("An unknown error occurred during save.") : result.error);
    }
}

void ExportCoordinator::refreshPreview(const QString& profileName, const QString& scaleFilter) {
    cancelPreview();
    invalidatePreviewCache();
    schedulePreviewPack(profileName, scaleFilter);
}

void ExportCoordinator::schedulePreviewPack(const QString& profileName, const QString& scaleFilter) {
    m_previewPackProfile = profileName;
    m_previewPackScaleFilter = scaleFilter;
    if (!m_previewPackDebounceTimer) {
        m_previewPackDebounceTimer = new QTimer(this);
        m_previewPackDebounceTimer->setSingleShot(true);
        connect(m_previewPackDebounceTimer, &QTimer::timeout,
                this, &ExportCoordinator::doRunPreviewPack);
    }
    m_previewPackDebounceTimer->start(400);
}

void ExportCoordinator::doRunPreviewPack() {
    if (!m_exportWorkspaceActive || !m_cliReady) return;

    // When a specific atlas is selected use only its sprites; otherwise use all.
    const bool hasAtlasFilter = m_exportPreviewAtlasIndex >= 0
        && m_exportPreviewAtlasIndex < m_cfg.session->atlases.size();
    const QStringList atlasScopedPaths = hasAtlasFilter
        ? m_cfg.session->atlases[m_exportPreviewAtlasIndex].spritePaths
        : m_cfg.session->activeFramePaths;
    if (atlasScopedPaths.isEmpty() && m_cfg.session->layoutSourcePath.isEmpty()) return;

    // Signal any in-flight task to exit early by retiring its token, then
    // issue a fresh token for the new task.  Using a per-task shared_ptr
    // ensures the old task sees 'true' even after m_previewPackCanceled has
    // been replaced, eliminating the true→false race of a single shared flag.
    m_previewPackCanceled->store(true);
    m_previewPackCanceled = std::make_shared<std::atomic<bool>>(false);

    // Capture all state from the main thread before dispatching
    const QString profileName      = m_previewPackProfile;
    const QString scaleFilter      = m_previewPackScaleFilter;
    const QString layoutBin        = m_layoutBinary;
    const QString packBin          = m_packBinary;
    const QStringList framePaths   = atlasScopedPaths;
    const QString layoutSourcePath = m_cfg.session->layoutSourcePath;
    const QString cachedLayout     = m_cfg.session->cachedLayoutOutput;
    const double  cachedLayoutScale = m_cfg.session->cachedLayoutScale;
    const QString lastProfile      = m_cfg.session->lastSuccessfulProfile;
    const QString deduplicateMode  = m_settings.deduplicateMode;
    const QVector<SpratProfile> profiles = m_cfg.layoutContext->configuredProfiles();
    const auto canceledPtr = m_previewPackCanceled;

    const QByteArray cachedImage       = m_cachedPackedImage;
    const QByteArray cachedPackLayout  = m_cachedPackLayout;
    const QString    cachedSF          = m_cachedPackScaleFilter;
    const int        cachedDilate      = m_cachedPackDilate;

    // Per-task cancellation flag for the queued layout-update callback.
    // Cancelling the old flag prevents a stale callback from a previous task
    // overwriting the canvas after a profile change.
    if (m_previewPackLayoutUpdateCanceled)
        m_previewPackLayoutUpdateCanceled->store(true);
    m_previewPackLayoutUpdateCanceled = std::make_shared<std::atomic<bool>>(false);
    const auto layoutUpdateCanceled    = m_previewPackLayoutUpdateCanceled;
    LayoutCanvas* const exportLC       = m_cfg.exportLayoutCanvas;
    const QString layoutParserFolderPath = m_cfg.layoutContext->layoutParserFolder();
    const QString currentFolderPath    = m_cfg.session->currentFolder;
    const ExportZoomOnChange exportZoomMode = m_settings.exportZoomOnChange;

    if (m_cfg.exportLayoutCanvas && m_cfg.exportLayoutCanvas) {
        QVector<LayoutModel> models;
        if (m_exportPreviewAtlasIndex >= 0
                && m_exportPreviewAtlasIndex < m_cfg.session->atlases.size()) {
            // Show only the selected atlas's current layout while packing.
            models = m_cfg.session->atlases[m_exportPreviewAtlasIndex].layoutModels;
        } else {
            const bool cachedModelsOk = !m_cachedPackModels.isEmpty()
                && m_cachedPackModelsProfile == profileName;
            models = cachedModelsOk ? m_cachedPackModels : QVector<LayoutModel>();
        }
        m_cfg.exportLayoutCanvas->setModels(models);
        if (m_cfg.exportWorkspace) m_cfg.exportWorkspace->setViewport(m_cfg.exportLayoutCanvas);
        switch (exportZoomMode) {
        case ExportZoomOnChange::Fit:
            m_cfg.exportLayoutCanvas->setZoomManual(false);
            m_cfg.exportLayoutCanvas->initialFit();
            break;
        case ExportZoomOnChange::Reset100:
            m_cfg.exportLayoutCanvas->setZoomManual(false);
            m_cfg.exportLayoutCanvas->setZoom(1.0);
            break;
        case ExportZoomOnChange::NoChange:
        default:
            break;
        }
        m_cfg.exportLayoutCanvas->setLoadingHint(true);
    } else if (m_cfg.packedAtlasView) {
        m_cfg.packedAtlasView->setLoading();
    }

    auto task = [=, this]() -> PackPreviewResult {
        // Find effective profile definition
        SpratProfile effectiveProfile;
        for (const SpratProfile& p : profiles) {
            if (p.name.trimmed() == profileName) {
                effectiveProfile = p;
                break;
            }
        }
        const double profileScale = qBound(0.01,
            effectiveProfile.scale > 0.0 ? effectiveProfile.scale : 1.0, 1.0);

        // Write frame list to a temp file when needed
        QString layoutInputPath = layoutSourcePath;
        QTemporaryFile frameListFile;
        if (!framePaths.isEmpty()) {
            frameListFile.setFileTemplate(
                QDir::temp().filePath("sprat-preview-frames-XXXXXX.txt"));
            if (!frameListFile.open()) return {{}, tr("Could not create temporary frame list"), {}, {}, -1, {}};
            {
                QTextStream out(&frameListFile);
                for (const QString& p : framePaths) out << p << "\n";
                out.flush();
            }
            frameListFile.flush();
            layoutInputPath = frameListFile.fileName();
            frameListFile.close();
        }

        if (canceledPtr->load()) return {};

        // Use cached layout when it matches the selected profile + scale
        QByteArray layoutData;
        constexpr double kTolerance = 1e-6;
        if (!cachedLayout.isEmpty()
            && lastProfile == profileName
            && std::abs(cachedLayoutScale - profileScale) < kTolerance
            && cachedLayout.contains(QLatin1String("atlas "))) {
            layoutData = cachedLayout.toUtf8();
        } else {
            QStringList layoutArgs;
            if (!layoutInputPath.isEmpty()) layoutArgs << layoutInputPath;
            if (!effectiveProfile.preset.trimmed().isEmpty())
                layoutArgs << "--preset" << effectiveProfile.preset.trimmed();
            if (effectiveProfile.maxWidth > 0)
                layoutArgs << "--max-width" << QString::number(effectiveProfile.maxWidth);
            if (effectiveProfile.maxHeight > 0)
                layoutArgs << "--max-height" << QString::number(effectiveProfile.maxHeight);
            layoutArgs << "--padding" << QString::number(qMax(0, effectiveProfile.padding));
            if (effectiveProfile.extrude > 0)
                layoutArgs << "--extrude" << QString::number(effectiveProfile.extrude);
            layoutArgs << "--scale" << QString::number(profileScale);
            if (effectiveProfile.trimTransparent) layoutArgs << "--trim-transparent";
            if (effectiveProfile.allowRotation)   layoutArgs << "--rotate";
            if (effectiveProfile.multipack)        layoutArgs << "--multipack";
            if (!effectiveProfile.sort.trimmed().isEmpty())
                layoutArgs << "--sort" << effectiveProfile.sort.trimmed();
            if (!deduplicateMode.isEmpty() && deduplicateMode != QLatin1String("none"))
                layoutArgs << "--deduplicate" << deduplicateMode;

            QByteArray layoutStderr;
            if (!m_cfg.runTool(layoutBin, layoutArgs, nullptr, &layoutData, &layoutStderr)) {
                const QString msg = QString::fromUtf8(layoutStderr).trimmed();
                return {{}, msg.isEmpty() ? tr("Layout generation failed") : msg, {}, {}, -1, {}};
            }
        }

        if (canceledPtr->load()) return {};

        if (layoutData.isEmpty() || !layoutData.contains("atlas ")) return {};

        const int dilate = effectiveProfile.dilate;
        if (!cachedImage.isEmpty()
            && layoutData == cachedPackLayout
            && scaleFilter == cachedSF
            && dilate == cachedDilate) {
            return {cachedImage, {}, {}, {}, -1, {}};  // cache hit — layout unchanged
        }

        // Parse layout models and push them to the UI immediately so the placeholder
        // shows the correct sprite arrangement for this profile before sprat-pack finishes.
        const QVector<LayoutModel> previewModels = LayoutParser::parse(
            QString::fromUtf8(layoutData), layoutParserFolderPath, currentFolderPath);
        if (!canceledPtr->load() && exportLC) {
            QMetaObject::invokeMethod(exportLC,
                [exportLC, previewModels, layoutUpdateCanceled, exportZoomMode]() {
                    if (!layoutUpdateCanceled->load()) {
                        exportLC->setModels(previewModels);
                        switch (exportZoomMode) {
                        case ExportZoomOnChange::Fit:
                            exportLC->initialFit();
                            break;
                        case ExportZoomOnChange::Reset100:
                            exportLC->setZoom(1.0);
                            break;
                        case ExportZoomOnChange::NoChange:
                        default:
                            break;
                        }
                    }
                }, Qt::QueuedConnection);
        }

        if (canceledPtr->load()) return {};

        // Run spratpack with layout data as stdin
        QStringList packArgs;
        if (dilate > 0)
            packArgs << "--dilate" << QString::number(dilate);
        if (!scaleFilter.isEmpty() && scaleFilter != QLatin1String("nearest"))
            packArgs << "--scale-filter" << scaleFilter;

        QByteArray packOutput;
        QByteArray packStderr;
        if (!m_cfg.runTool(packBin, packArgs, &layoutData, &packOutput, &packStderr)) {
            const QString msg = QString::fromUtf8(packStderr).trimmed();
            return {{}, msg.isEmpty() ? tr("Packing failed") : msg, {}, {}, -1, {}};
        }

        return {packOutput, {}, layoutData, scaleFilter, dilate, previewModels};
    };

#ifdef Q_OS_WASM
    // WASM has no real background threads (Asyncify/JSPI, not pthreads).
    // Run the task on the next event-loop tick to keep the UI responsive,
    // then handle the result directly — same pattern as runExport().
    QTimer::singleShot(0, this, [this, task]() {
        PackPreviewResult result = task();
        handlePackPreviewResult(result);
    });
#else
    m_previewPackWatcher.setFuture(QtConcurrent::run(task));
#endif
}

void ExportCoordinator::handlePackPreviewResult(const PackPreviewResult& result) {
    if (!m_exportWorkspaceActive || m_previewPackCanceled->load()) return;
    if (!m_cfg.packedAtlasView) return;

    // Update PNG cache when a real pack ran (non-empty layoutUsed means it wasn't a cache hit)
    if (!result.imageData.isEmpty() && !result.layoutUsed.isEmpty()) {
        m_cachedPackedImage     = result.imageData;
        m_cachedPackLayout      = result.layoutUsed;
        m_cachedPackScaleFilter = result.scaleFilterUsed;
        m_cachedPackDilate      = result.dilateUsed;
    }
    // Update layout model cache from the already-parsed result (avoids re-parsing here)
    if (!result.layoutModels.isEmpty()) {
        m_cachedPackModels        = result.layoutModels;
        m_cachedPackModelsProfile = m_previewPackProfile;
    }

    // Capture layout canvas zoom/scroll before the swap so the packed atlas
    // continues from the exact same view position (seamless transition).
    // Always capture regardless of isZoomManual — initialFit() already computed
    // the correct fit zoom even though the manual flag stays false.
    const double savedZoom    = m_cfg.exportLayoutCanvas ? m_cfg.exportLayoutCanvas->zoom()    : 0.0;
    const int    savedScrollH = m_cfg.exportLayoutCanvas ? m_cfg.exportLayoutCanvas->horizontalScrollBar()->value() : 0;
    const int    savedScrollV = m_cfg.exportLayoutCanvas ? m_cfg.exportLayoutCanvas->verticalScrollBar()->value()   : 0;

    // Prevent auto-fit from firing when packed atlas is re-parented into the pane
    if (savedZoom > 0.0 && m_cfg.packedAtlasView)
        m_cfg.packedAtlasView->setZoomManual(true);

    // Swap back from layout placeholder to packed atlas view
    if (m_cfg.exportLayoutCanvas) m_cfg.exportLayoutCanvas->setLoadingHint(false);
    if (m_cfg.exportWorkspace && m_cfg.packedAtlasView)
        m_cfg.exportWorkspace->setViewport(m_cfg.packedAtlasView);

    if (result.imageData.isEmpty()) {
        m_cfg.packedAtlasView->setError(
            result.errorMsg.isEmpty() ? tr("Preview generation failed") : result.errorMsg);
    } else {
        m_cfg.packedAtlasView->setImage(result.imageData);
    }

    // Apply the saved zoom/scroll to preserve the view position across the swap
    if (savedZoom > 0.0 && m_cfg.packedAtlasView) {
        m_cfg.packedAtlasView->setZoom(savedZoom);
        m_cfg.packedAtlasView->horizontalScrollBar()->setValue(savedScrollH);
        m_cfg.packedAtlasView->verticalScrollBar()->setValue(savedScrollV);
    }

    if (m_cfg.packedAtlasView) m_cfg.packedAtlasView->setFocus();
}
