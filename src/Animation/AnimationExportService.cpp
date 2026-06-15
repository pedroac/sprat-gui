#include "AnimationExportService.h"
#include "MessageDialog.h"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QFileDialog>
#include <QImage>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace {
QString trAnimationExport(const char* text) {
    return QCoreApplication::translate("AnimationExportService", text);
}
}

QString AnimationExportService::chooseOutputPath(QWidget* parent) {
    QString ffmpegExe = QStandardPaths::findExecutable("ffmpeg");
    QString magickExe = QStandardPaths::findExecutable("magick");
    if (magickExe.isEmpty()) {
        magickExe = QStandardPaths::findExecutable("convert");
    }

    if (ffmpegExe.isEmpty() && magickExe.isEmpty()) {
#if defined(Q_OS_WIN)
        const QString hint = trAnimationExport(
            "Install one of:\n"
            "  \u2022 FFmpeg       \u2014 winget install ffmpeg\n"
            "  \u2022 ImageMagick  \u2014 winget install ImageMagick");
#elif defined(Q_OS_LINUX)
        const QString hint = trAnimationExport(
            "Install one of:\n"
            "  \u2022 FFmpeg       \u2014 sudo apt install ffmpeg\n"
            "  \u2022 ImageMagick  \u2014 sudo apt install imagemagick");
#elif defined(Q_OS_MACOS)
        const QString hint = trAnimationExport(
            "Install one of:\n"
            "  \u2022 FFmpeg       \u2014 brew install ffmpeg\n"
            "  \u2022 ImageMagick  \u2014 brew install imagemagick");
#else
        const QString hint = trAnimationExport(
            "Install FFmpeg or ImageMagick using your system package manager.");
#endif
        MessageDialog::warning(parent,
            trAnimationExport("Missing Tools"),
            trAnimationExport("Animation export requires ImageMagick or FFmpeg.\n\n") + hint);
        return {};
    }

    QStringList filters;
    filters << trAnimationExport("GIF Image (*.gif)");
    if (!ffmpegExe.isEmpty()) {
        filters << trAnimationExport("MP4 Video (*.mp4)")
                << trAnimationExport("WebM Video (*.webm)")
                << trAnimationExport("Ogg Video (*.ogv)")
                << trAnimationExport("AVI Video (*.avi)");
    }
    QString selectedFilter;
    return QFileDialog::getSaveFileName(parent,
                                        trAnimationExport("Save Animation"),
                                        trAnimationExport("animation.gif"),
                                        filters.join(";;"),
                                        &selectedFilter);
}

bool AnimationExportService::exportAnimation(
    const QVector<AnimationTimeline>& timelines,
    int selectedTimelineIndex,
    const QVector<LayoutModel>& layoutModels,
    int fps,
    const QString& outPath,
    ExportCallbacks callbacks) {
    const auto& setLoading = callbacks.setLoading;
    const auto& setStatus  = callbacks.setStatus;
    auto showError = [&](const QString& title, const QString& msg) {
        if (callbacks.showError) callbacks.showError(title, msg);
    };
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size()) {
        return false;
    }
    const auto& frames = timelines[selectedTimelineIndex].frames;
    if (frames.isEmpty()) {
        return false;
    }

    QString converterExe = QStandardPaths::findExecutable("magick");
    if (converterExe.isEmpty()) {
        converterExe = QStandardPaths::findExecutable("convert");
    }
    QString ffmpegExe = QStandardPaths::findExecutable("ffmpeg");
    bool isGif = outPath.endsWith(".gif", Qt::CaseInsensitive);
    bool useMagick = isGif && !converterExe.isEmpty();
    if (!useMagick && ffmpegExe.isEmpty()) {
        if (!isGif) {
            showError(trAnimationExport("FFmpeg Required"), trAnimationExport("FFmpeg is required for video export."));
            return false;
        }
        if (converterExe.isEmpty()) {
            showError(trAnimationExport("No Export Tool Found"),
                      trAnimationExport("No suitable export tool found (FFmpeg or ImageMagick)."));
            return false;
        }
    }

    setLoading(true);
    setStatus(trAnimationExport("Generating animation..."));

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        setLoading(false);
        return false;
    }

    // Build O(1) pivot lookup to avoid O(N×M) nested search inside the frame loop.
    QHash<QString, SpritePtr> spriteMap;
    for (const auto& model : layoutModels) {
        for (const auto& s : model.sprites)
            spriteMap.insert(s->path, s);
    }

    struct FrameData {
        QString path;
        int pivotX;
        int pivotY;
    };
    QVector<FrameData> frameDataList;
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool first = true;

    for (const QString& path : frames) {
        int px = 0, py = 0;
        QImage pm(path);
        if (pm.isNull())
            continue;
        const auto it = spriteMap.constFind(path);
        if (it != spriteMap.constEnd()) {
            px = it.value()->pivotX;
            py = it.value()->pivotY;
        }
        if (px == 0 && py == 0 && (pm.width() > 0 || pm.height() > 0)) {
            px = pm.width() / 2;
            py = pm.height() / 2;
        }
        frameDataList.append({path, px, py});
        int l = -px;
        int t = -py;
        int r = pm.width() - px;
        int b = pm.height() - py;
        if (first) {
            minX = l;
            minY = t;
            maxX = r;
            maxY = b;
            first = false;
        } else {
            minX = qMin(minX, l);
            minY = qMin(minY, t);
            maxX = qMax(maxX, r);
            maxY = qMax(maxY, b);
        }
    }

    if (frameDataList.isEmpty()) {
        setLoading(false);
        return false;
    }

    int canvasW = maxX - minX;
    int canvasH = maxY - minY;
    if (canvasW <= 0 || canvasH <= 0) {
        setLoading(false);
        return false;
    }

    for (int i = 0; i < frameDataList.size(); ++i) {
        const auto& fd = frameDataList[i];
        QImage pm(fd.path);
        QImage img(canvasW, canvasH, QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.drawImage(-minX - fd.pivotX, -minY - fd.pivotY, pm);
        p.end();
        QString framePath = tempDir.filePath(QString("frame_%1.png").arg(i, 4, 10, QChar('0')));
        if (!img.save(framePath)) {
            setLoading(false);
            return false;
        }
    }

#ifndef SPRAT_EMBEDDED_CLI
    QProcess proc;
    QStringList args;
    if (useMagick) {
        args << "-delay" << QString::number(100 / fps) << "-loop" << "0"
             << tempDir.path() + "/frame_*.png" << outPath;
        proc.start(converterExe, args);
    } else if (!ffmpegExe.isEmpty()) {
        args << "-y" << "-framerate" << QString::number(fps)
             << "-i" << tempDir.path() + "/frame_%04d.png"
             << "-c:v" << "libx264" << "-pix_fmt" << "yuv420p" << outPath;
        proc.start(ffmpegExe, args);
    } else {
        setLoading(false);
        return false;
    }
    constexpr int kExportTimeoutMs = 5 * 60 * 1000; // 5 minutes
    if (!proc.waitForFinished(kExportTimeoutMs)) {
        proc.kill();
        setStatus(trAnimationExport("Export timed out"));
        showError(trAnimationExport("Export Failed"), trAnimationExport("Export process timed out after 5 minutes."));
        setLoading(false);
        return false;
    }
    bool ok = QFile::exists(outPath) && proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
#else
    Q_UNUSED(useMagick);
    Q_UNUSED(converterExe);
    Q_UNUSED(ffmpegExe);
    bool ok = false;
    #ifdef Q_OS_WASM
        showError(trAnimationExport("Not Supported"), trAnimationExport("Exporting to GIF/Video is not supported in the web version."));
    #endif
#endif
    if (!ok) {
        setStatus(trAnimationExport("Failed to generate animation"));
        showError(trAnimationExport("Export Failed"), trAnimationExport("Exporting animation failed. Check console output for details."));
    }
    setLoading(false);
    return ok;
}
