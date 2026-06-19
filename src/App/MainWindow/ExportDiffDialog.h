#pragma once
#include <QDialog>
#include <QVector>
#include "ExportModels.h"
#include "ProjectModels.h"
#include "SpratProfilesConfig.h"

class QTreeWidget;

class ExportDiffDialog : public QDialog {
    Q_OBJECT
public:
    struct DiffEntry {
        QString path;
        bool    exists;       // true = will overwrite
        bool    isDirectory;
    };

    /** Show the diff dialog. Returns true if the user clicks Proceed, false on Cancel. */
    static bool show(QWidget* parent,
                     const SaveConfig& config,
                     const QVector<AtlasEntry>& atlases,
                     const QVector<SpratProfile>& profiles);

private:
    explicit ExportDiffDialog(const QVector<DiffEntry>& entries, QWidget* parent);

    static QVector<DiffEntry> buildEntries(const SaveConfig& config,
                                           const QVector<AtlasEntry>& atlases,
                                           const QVector<SpratProfile>& profiles);
};
