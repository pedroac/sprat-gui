#pragma once

#include <QCoreApplication>
#include <QString>
#include <QStringList>

class QWidget;

class ImageFolderSelectionDialog {
    Q_DECLARE_TR_FUNCTIONS(ImageFolderSelectionDialog)

public:
    static bool pickSingleFolderWithImages(QWidget* parent, const QString& root, QString& selection, bool* canceled = nullptr);
    static bool pickMultipleFoldersWithImages(QWidget* parent, const QString& root, QStringList& selections, bool* canceled = nullptr);
};
