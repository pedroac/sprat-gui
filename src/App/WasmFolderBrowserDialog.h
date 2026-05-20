#pragma once

#include <QString>

#ifdef Q_OS_WASM
#include <QDialog>
#include <QStringList>

class QLabel; class QTreeWidget; class QTreeWidgetItem; class QPushButton;

class WasmFolderBrowserDialog : public QDialog {
    Q_OBJECT
public:
    explicit WasmFolderBrowserDialog(const QString& folderPath, QWidget* parent = nullptr);
signals:
    void filesDeleted(const QStringList& paths);
    void filesAdded(const QStringList& paths);
private slots:
    void onDownloadSelected();
    void onDeleteSelected();
    void onImportFiles();
    void onCreateFolder();
private:
    QTreeWidgetItem* ensureDirectoryItem(const QString& relativeDirPath);
    QString selectedDirectoryPath() const;
    void importPickedFiles(const QString& pickedPath, int mode);
    QString resolveDestinationPath(const QString& sourcePath, const QString& targetDirectory) const;
    bool copyImportedFile(const QString& sourcePath, const QString& targetDirectory, QString* finalPath);
    void moveDroppedFiles(const QStringList& sourcePaths, const QString& targetDirectory);
    void setupUi();
    void populateTree();
    QString      m_folderPath;
    QLabel*      m_pathLabel   = nullptr;
    QTreeWidget* m_tree        = nullptr;
    QPushButton* m_importBtn   = nullptr;
    QPushButton* m_newFolderBtn = nullptr;
    QPushButton* m_downloadBtn = nullptr;
    QPushButton* m_deleteBtn   = nullptr;
};
#endif
