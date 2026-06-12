#pragma once
#include <QAbstractItemView>
#include <QWidget>
#include "models.h"

class NavigatorTreeWidget;
class ProjectSession;
class QLineEdit;
class QLabel;
class QCheckBox;
class QComboBox;
class QTreeWidgetItem;

/**
 * @class NavigatorPanel
 * @brief Self-contained widget: sprite tree + filter edit + atlas combo (optional).
 *
 * Handles tree building and filtering. Context menus are forwarded via Qt's
 * standard customContextMenuRequested signal (from the internal tree widget).
 */
class NavigatorPanel : public QWidget {
    Q_OBJECT
public:
    struct Config {
        bool atlasCombo    = false;
        bool showHidden    = false;
        bool checkboxes    = false;
        bool filterBar     = true;
        QAbstractItemView::SelectionMode selectionMode = QAbstractItemView::ExtendedSelection;
    };

    explicit NavigatorPanel(QWidget* parent = nullptr);

    void configure(const Config& config);

    void setAtlasComboVisible(bool visible);
    void setShowHiddenVisible(bool visible);
    /** Enable or disable checkboxes on tree items. Default: true (Sprites workspace). */
    void setCheckboxesEnabled(bool enabled);
    /** Forward selection mode to the internal tree widget. */
    void setSelectionMode(QAbstractItemView::SelectionMode mode);

    /**
     * @brief Rebuild the tree.
     * @param session       Active project session.
     * @param showHidden    Whether to show hidden/excluded items.
     * @param atlasFilter   -1 = show all non-excluded atlases;
     *                      >=0 = show only that atlas's spritePaths.
     */
    void refresh(const ProjectSession* session, bool showHidden, int atlasFilter = -1);

    /**
     * @brief Populate/update the atlas combo (for Frame Animation).
     * @param atlases           All atlas entries.
     * @param activeSessionIndex The currently active atlas index in the session.
     */
    void updateAtlasCombo(const QVector<AtlasEntry>& atlases, int activeSessionIndex);

    QString filterText() const;
    void clearFilter();

    NavigatorTreeWidget* tree()            const { return m_spriteTree; }

    // Accessors to internal widgets (used by MainWindow to keep its existing pointer members valid)
    QLineEdit*           filterEdit()      const { return m_filterEdit; }
    QLabel*              filterResultLabel() const { return m_filterResult; }
    QCheckBox*           showHiddenCheckBox() const { return m_showHidden; }
    QWidget*             atlasRow()        const { return m_atlasRow; }
    QComboBox*           atlasCombo()      const { return m_atlasCombo; }

    /** Returns paths of all checked leaf sprites. */
    QStringList checkedPaths() const;

    /** Apply a text filter to the tree without rebuilding it. */
    void applyFilter(const QString& text);

    /** Enable or disable grouping of similar (animation-sequence) sprites under a parent node. */
    void setGroupSimilar(bool group);

signals:
    /** Emitted when the user selects a different atlas in the combo. */
    void atlasIndexChanged(int sessionAtlasIndex);
    /** Emitted when the show-hidden checkbox changes. */
    void showHiddenChanged(bool show);
    /** Forwarded from the tree's excludeRequested signal (DEL key). */
    void excludeKeyPressed(QTreeWidgetItem* item);

private slots:
    void onAtlasComboChanged(int comboIdx);
    void onFilterTextChanged(const QString& text);

private:
    void buildTree(const ProjectSession* session, bool showHidden, int atlasFilter);

    NavigatorTreeWidget* m_spriteTree        = nullptr;
    QLineEdit*           m_filterEdit        = nullptr;
    QLabel*              m_filterResult      = nullptr;
    QCheckBox*           m_showHidden        = nullptr;
    QWidget*             m_atlasRow          = nullptr;
    QComboBox*           m_atlasCombo        = nullptr;
    bool                 m_checkboxesEnabled = true;
    bool                 m_groupSimilar      = true;
};
