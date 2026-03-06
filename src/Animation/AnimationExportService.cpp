#include "AnimationExportService.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QPainter>
#include <QPixmapCache>
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
        QMessageBox::warning(parent,
                             trAnimationExport("Missing Tools"),
                             trAnimationExport("To export animations, you need ImageMagick or FFmpeg installed."));
        return QString();
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
    QWidget* parent,
    const QVector<AnimationTimeline>& timelines,
    int selectedTimelineIndex,
    const QVector<LayoutModel>& layoutModels,
    int fps,
    const QString& outPath,
    const std::function<void(bool)>& setLoading,
    const std::function<void(const QString&)>& setStatus) {
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
            QMessageBox::warning(parent, trAnimationExport("Error"), trAnimationExport("FFmpeg is required for video export."));
            return false;
        }
        if (converterExe.isEmpty()) {
            QMessageBox::warning(parent,
                                 trAnimationExport("Error"),
                                 trAnimationExport("No suitable export tool found (FFmpeg or ImageMagick)."));
            return false;
        }
    }

    setLoading(true);
    setStatus(trAnimationExport("Generating animation..."));
    QApplication::processEvents();

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        setLoading(false);
        return false;
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
        QPixmap pm;
        if (!QPixmapCache::find(path, &pm)) {
            pm.load(path);
            QPixmapCache::insert(path, pm);
        }
        if (pm.isNull()) {
            continue;
        }
        for (const auto& model : layoutModels) {
            for (const auto& s : model.sprites) {
                if (s->path == path) {
                    px = s->pivotX;
                    py = s->pivotY;
                    break;
                }
            }
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
        QPixmap pm;
        if (!QPixmapCache::find(fd.path, &pm)) {
            pm.load(fd.path);
            QPixmapCache::insert(fd.path, pm);
        }
        QImage img(canvasW, canvasH, QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.drawPixmap(-minX - fd.pivotX, -minY - fd.pivotY, pm);
        p.end();
        QString framePath = tempDir.filePath(QString("frame_%1.png").arg(i, 4, 10, QChar('0')));
        if (!img.save(framePath)) {
            setLoading(false);
            return false;
        }
    }

    QProcess proc;
    if (useMagick) {
        int delay = qRound(100.0 / fps);
        QStringList args;
        args << "-dispose" << "background" << "-delay" << QString::number(delay) << "-loop" << "0";
        for (int i = 0; i < frameDataList.size(); ++i) {
            args << tempDir.filePath(QString("frame_%1.png").arg(i, 4, 10, QChar('0')));
        }
        args << outPath;
        proc.start(converterExe, args);
    } else {
        QStringList args;
        args << "-framerate" << QString::number(fps) << "-i" << tempDir.filePath("frame_%04d.png");
        if (outPath.endsWith(".mp4", Qt::CaseInsensitive)) {
            args << "-c:v" << "libx264" << "-pix_fmt" << "yuv420p";
        } else if (outPath.endsWith(".webm", Qt::CaseInsensitive)) {
            args << "-c:v" << "libvpx-vp9" << "-pix_fmt" << "yuva420p";
        }
        args << "-y" << outPath;
        proc.start(ffmpegExe, args);
    }
    proc.waitForFinished();

    bool ok = QFile::exists(outPath) && proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    if (!ok) {
        setStatus(trAnimationExport("Failed to generate animation"));
        QMessageBox::critical(parent, trAnimationExport("Error"), trAnimationExport("Exporting animation failed. Check console output."));
    }
    setLoading(false);
    return ok;
}
