#include "ExportDiffDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QStyle>
#include <QApplication>

namespace {

QStringList effectiveProfiles(const AtlasEntry& atlas, const SaveConfig& config) {
    if (!atlas.exportConfig.profiles.isEmpty())
        return atlas.exportConfig.profiles;
    return config.profiles;
}

QString spriteSheetExtension(const QString& profileName,
                              const QVector<SpratProfile>& profiles) {
    for (const SpratProfile& p : profiles) {
        if (p.name.trimmed() == profileName) {
            if (!p.gpuCompress.trimmed().isEmpty())
                return QStringLiteral(".dds");
            break;
        }
    }
    return QStringLiteral(".png");
}

// Returns the filename written by spratconvert for a given transform name.
// spratconvert produces "<transform_stem>.<extension>" where extension is
// declared by each transform's Jsonnet template.
// Returns an empty string for transforms that produce multiple files with
// unpredictable names (unity.anim) or for unrecognised transform IDs.
QString transformOutputFilename(const QString& transform) {
    static const QHash<QString, QString> kExt{
        {QStringLiteral("json"),         QStringLiteral(".json")},
        {QStringLiteral("xml"),          QStringLiteral(".xml")},
        {QStringLiteral("csv"),          QStringLiteral(".csv")},
        {QStringLiteral("css"),          QStringLiteral(".css")},
        {QStringLiteral("plist"),        QStringLiteral(".plist")},
        {QStringLiteral("libgdx"),       QStringLiteral(".atlas")},
        {QStringLiteral("godot"),        QStringLiteral(".json")},
        {QStringLiteral("aseprite"),     QStringLiteral(".json")},
        {QStringLiteral("phaser-hash"),  QStringLiteral(".json")},
        {QStringLiteral("phaser-array"), QStringLiteral(".json")},
        {QStringLiteral("phaser-anims"), QStringLiteral(".json")},
        // unity.json and unity.meta are single-file transforms.
        {QStringLiteral("unity.json"),   QStringLiteral(".json")},
        {QStringLiteral("unity.meta"),   QStringLiteral(".meta")},
        // unity.anim produces one .anim file per animation (unpredictable names).
    };
    const auto it = kExt.constFind(transform);
    if (it == kExt.constEnd()) return {};  // unknown / multi-file transform
    return transform + it.value();
}

QString effectiveTransform(const AtlasEntry& atlas, const SaveConfig& config) {
    if (!atlas.exportConfig.transform.isEmpty())
        return atlas.exportConfig.transform;
    return config.transform;
}

// Returns the output subdirectory for an atlas, automatically deriving one from the
// atlas name when multiple atlases would otherwise all write to the same root directory.
QString effectiveOutputSubdir(const AtlasEntry& atlas, int rootExporterCount) {
    if (!atlas.outputSubdir.isEmpty())
        return atlas.outputSubdir;
    if (rootExporterCount <= 1)
        return {};
    if (atlas.isNeutral)
        return QStringLiteral("sprites");
    const QString slug = atlas.name.trimmed().toLower().replace(QLatin1Char(' '), QLatin1Char('_'));
    return slug.isEmpty() ? QStringLiteral("atlas") : slug;
}

}  // namespace

QVector<ExportDiffDialog::DiffEntry> ExportDiffDialog::buildEntries(
    const SaveConfig& config,
    const QVector<AtlasEntry>& atlases,
    const QVector<SpratProfile>& profiles)
{
    QVector<DiffEntry> entries;
    bool firstAtlas = true;

    // Count atlases with no explicit outputSubdir so we can auto-assign subdirs
    // when multiple atlases would otherwise write to the same directory.
    int rootExporterCount = 0;
    for (const auto& a : atlases) {
        if (!a.isExcluded && !a.spritePaths.isEmpty() && a.outputSubdir.isEmpty())
            ++rootExporterCount;
    }

    for (const AtlasEntry& atlas : atlases) {
        if (atlas.isExcluded) continue;
        if (atlas.spritePaths.isEmpty()) continue;

        const QString subdir = effectiveOutputSubdir(atlas, rootExporterCount);
        const QStringList profs = effectiveProfiles(atlas, config);
        const QString transform = effectiveTransform(atlas, config);

        for (const QString& profileName : profs) {
            // Structure: <outputPath>/<profileName>/<atlasSubdir>/
            // When there is only one atlas (subdir empty): <outputPath>/<profileName>/
            const QString dir = subdir.isEmpty()
                ? QDir(config.outputPath).filePath(profileName)
                : QDir(config.outputPath).filePath(profileName + QLatin1Char('/') + subdir);
            entries.append({dir, QDir(dir).exists(), true});

            const QString ext = spriteSheetExtension(profileName, profiles);
            const QString sheetPath = QDir(dir).filePath(QStringLiteral("spritesheet") + ext);
            entries.append({sheetPath, QFile::exists(sheetPath), false});

            if (transform == QStringLiteral("raw")) {
                for (const QString& name : {QStringLiteral("layout.txt"),
                                             QStringLiteral("markers.txt"),
                                             QStringLiteral("animations.txt")}) {
                    const QString p = QDir(dir).filePath(name);
                    entries.append({p, QFile::exists(p), false});
                }
            } else if (!transform.isEmpty() && transform != QStringLiteral("none")) {
                const QString metaFile = transformOutputFilename(transform);
                if (!metaFile.isEmpty()) {
                    const QString metaPath = QDir(dir).filePath(metaFile);
                    entries.append({metaPath, QFile::exists(metaPath), false});
                }
            }
        }

        if (firstAtlas) {
            firstAtlas = false;
            const QString projectJson = QDir(config.outputPath).filePath(QStringLiteral("project.spart.json"));
            entries.append({projectJson, QFile::exists(projectJson), false});
        }
    }

    return entries;
}

ExportDiffDialog::ExportDiffDialog(const QVector<DiffEntry>& entries, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Export summary"));
    setMinimumSize(600, 400);
    resize(660, 460);

    auto* mainLayout = new QVBoxLayout(this);

    const bool hasOverwrites = std::any_of(entries.begin(), entries.end(),
        [](const DiffEntry& e){ return e.exists; });

    if (hasOverwrites) {
        auto* warnLabel = new QLabel(
            tr("The following files already exist and will be replaced."), this);
        warnLabel->setWordWrap(true);
        QFont f = warnLabel->font();
        f.setBold(true);
        warnLabel->setFont(f);
        mainLayout->addWidget(warnLabel);
    } else {
        mainLayout->addWidget(new QLabel(tr("The following files will be written:"), this));
    }

    int fileNewCount = 0, fileOverCount = 0;
    for (const DiffEntry& e : entries) {
        if (!e.isDirectory) {
            if (e.exists) ++fileOverCount;
            else ++fileNewCount;
        }
    }
    QString summaryText;
    if (fileOverCount > 0 && fileNewCount > 0)
        summaryText = tr("New: %1  ·  Will overwrite: %2").arg(fileNewCount).arg(fileOverCount);
    else if (fileOverCount > 0)
        summaryText = tr("Will overwrite: %1 file(s)").arg(fileOverCount);
    else
        summaryText = tr("New: %1 file(s)").arg(fileNewCount);
    auto* summaryLabel = new QLabel(summaryText, this);
    summaryLabel->setStyleSheet(QStringLiteral("color: #888;"));
    mainLayout->addWidget(summaryLabel);

    auto* tree = new QTreeWidget(this);
    tree->setHeaderHidden(true);
    tree->setRootIsDecorated(true);

    const QIcon folderIcon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
    const QIcon fileIcon   = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
    const QIcon warnIcon   = QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning);

    auto* newItem  = new QTreeWidgetItem(tree, QStringList{tr("New")});
    auto* overItem = new QTreeWidgetItem(tree, QStringList{tr("Will overwrite")});
    newItem->setExpanded(true);
    overItem->setExpanded(true);

    for (const DiffEntry& e : entries) {
        auto* parentItem = e.exists ? overItem : newItem;
        auto* item = new QTreeWidgetItem(parentItem, QStringList{e.path});
        item->setIcon(0, e.isDirectory ? folderIcon : (e.exists ? warnIcon : fileIcon));
    }

    if (overItem->childCount() == 0) {
        delete overItem;
    }
    if (newItem->childCount() == 0) {
        delete newItem;
    }

    mainLayout->addWidget(tree, 1);

    auto* buttons = new QDialogButtonBox(this);
    auto* proceedBtn = buttons->addButton(tr("Proceed"), QDialogButtonBox::AcceptRole);
    proceedBtn->setDefault(true);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
}

bool ExportDiffDialog::show(QWidget* parent,
                             const SaveConfig& config,
                             const QVector<AtlasEntry>& atlases,
                             const QVector<SpratProfile>& profiles)
{
    const QVector<DiffEntry> entries = buildEntries(config, atlases, profiles);
    ExportDiffDialog dlg(entries, parent);
    return dlg.exec() == QDialog::Accepted;
}
