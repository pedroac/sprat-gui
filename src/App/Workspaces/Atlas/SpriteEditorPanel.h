#pragma once
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QDoubleSpinBox;
class QComboBox;
class QToolButton;
class PreviewCanvas;
class ElidedLabel;

/**
 * @class SpriteEditorPanel
 * @brief Widget that owns the selected-sprite editor area.
 *
 * Creates and lays out all sprite-editor child widgets (name edit, pivot/handle
 * controls, preview canvas, footer labels).  Exposes accessor methods so that
 * MainWindow can keep its existing raw-pointer members valid with a single
 * "wire-up" call after construction.
 */
class SpriteEditorPanel : public QWidget {
    Q_OBJECT
public:
    explicit SpriteEditorPanel(QWidget* parent = nullptr);

    // ── Widget accessors ─────────────────────────────────────────────────────
    QLabel*         multiSelectionLabel()  const { return m_multiSelectionLabel; }
    QLineEdit*      spriteNameEdit()       const { return m_spriteNameEdit; }
    QPushButton*    editAliasesBtn()       const { return m_editAliasesBtn; }
    QDoubleSpinBox* previewZoomSpin()      const { return m_previewZoomSpin; }
    QPushButton*    showTrimRectBtn()      const { return m_showTrimRectBtn; }
    QPushButton*    onionSkinBtn()         const { return m_onionSkinBtn; }
    QPushButton*    showGridBtn()          const { return m_showGridBtn; }
    QComboBox*      handleCombo()          const { return m_handleCombo; }
    QDoubleSpinBox* pivotXSpin()           const { return m_pivotXSpin; }
    QDoubleSpinBox* pivotYSpin()           const { return m_pivotYSpin; }
    QComboBox*      coordUnitCombo()       const { return m_coordUnitCombo; }
    QPushButton*    configPointsBtn()      const { return m_configPointsBtn; }
    QToolButton*    markerTemplatesBtn()   const { return m_markerTemplatesBtn; }
    PreviewCanvas*  previewCanvas()        const { return m_previewView; }
    ElidedLabel*    spriteNameFooterLabel() const { return m_spriteNameFooterLabel; }
    QLabel*         spriteDimsLabel()      const { return m_spriteDimsLabel; }

private:
    QLabel*         m_multiSelectionLabel  = nullptr;
    QLineEdit*      m_spriteNameEdit       = nullptr;
    QPushButton*    m_editAliasesBtn       = nullptr;
    QDoubleSpinBox* m_previewZoomSpin      = nullptr;
    QPushButton*    m_showTrimRectBtn      = nullptr;
    QPushButton*    m_onionSkinBtn         = nullptr;
    QPushButton*    m_showGridBtn          = nullptr;
    QComboBox*      m_handleCombo          = nullptr;
    QDoubleSpinBox* m_pivotXSpin           = nullptr;
    QDoubleSpinBox* m_pivotYSpin           = nullptr;
    QComboBox*      m_coordUnitCombo       = nullptr;
    QPushButton*    m_configPointsBtn      = nullptr;
    QToolButton*    m_markerTemplatesBtn   = nullptr;
    PreviewCanvas*  m_previewView          = nullptr;
    ElidedLabel*    m_spriteNameFooterLabel = nullptr;
    QLabel*         m_spriteDimsLabel      = nullptr;
};
