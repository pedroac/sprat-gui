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
private slots:
    void onDownloadSelected();
    void onDeleteSelected();
private:
    void setupUi();
    void populateTree();
    QString      m_folderPath;
    QLabel*      m_pathLabel   = nullptr;
    QTreeWidget* m_tree        = nullptr;
    QPushButton* m_downloadBtn = nullptr;
    QPushButton* m_deleteBtn   = nullptr;
};
#endif
