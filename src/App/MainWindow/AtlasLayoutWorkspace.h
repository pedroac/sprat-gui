#pragma once
#include <QStringList>
#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QListWidget;
class QListWidgetItem;

class AtlasLayoutWorkspace : public QWidget {
    Q_OBJECT
public:
    explicit AtlasLayoutWorkspace(QWidget* parent = nullptr);

    void setCanvasWidget(QWidget* widget);
    void clearCanvasWidget();

    // Reparents MainWindow-owned view controls into right panel
    void setViewControls(QComboBox* resolutionCombo, QDoubleSpinBox* zoomSpin);

    // Profile list management
    void setProfiles(const QStringList& names, const QStringList& labels,
                     const QString& currentProfile,
                     const QStringList& enabledProfiles = {});
    void setSelectedProfile(const QString& name);  // no signal emitted
    QString     selectedProfile()  const;
    QStringList enabledProfiles()  const;

    // Atlas page picker (visible when count > 1)
    void setAtlasCount(int count);
    int  selectedAtlasIndex() const;

signals:
    void selectedProfileChanged(const QString& profileName);
    void profileEnablementChanged(const QStringList& enabledProfiles);
    void selectedAtlasChanged(int atlasIndex);
    void manageProfilesRequested();

private:
    void setupUi();
    void ensureValidSelection();

    QWidget*     m_canvasPane  = nullptr;
    QWidget*     m_viewGroup   = nullptr;
    QListWidget* m_profileList = nullptr;
    QGroupBox*   m_atlasGroup  = nullptr;
    QListWidget* m_atlasList   = nullptr;
    bool         m_syncing     = false;
};
