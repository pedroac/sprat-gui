#pragma once
#include <QDialog>
#include "models.h"

class QListWidget;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QPushButton;
class QGroupBox;

struct SuggestedMarkerPosition {
    QPoint pos;
    int baseSize = 20;
};

class MarkersDialog : public QDialog {
    Q_OBJECT
public:
    explicit MarkersDialog(SpritePtr sprite, const SuggestedMarkerPosition& suggestion, QWidget* parent = nullptr);

signals:
    void markersChanged();

private slots:
    void onAddClicked();
    void onRemoveClicked();
    void onSelectionChanged();
    void onFieldChanged();
    void onClearPolygon();

private:
    void setupUi();
    void refreshList();
    void updateEditorState();

    SpritePtr m_sprite;
    SuggestedMarkerPosition m_suggestion;
    QListWidget* m_listWidget;
    QLineEdit* m_addNameEdit;
    QComboBox* m_addTypeCombo;
    
    QGroupBox* m_editorGroup;
    QLineEdit* m_nameEdit;
    QComboBox* m_typeCombo;
    QSpinBox* m_xSpin;
    QSpinBox* m_ySpin;
    QSpinBox* m_radiusSpin;
    QSpinBox* m_wSpin;
    QSpinBox* m_hSpin;
    QPushButton* m_clearPolyBtn;
    QWidget* m_xyRow;
    QWidget* m_radiusRow;
    QWidget* m_rectRow;
    QWidget* m_polyRow;
    
    bool m_updating = false;
};