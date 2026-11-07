#pragma once
#include <functional>
#include <QDialog>
#include <QPair>
#include <QSet>
#include <QStringList>
#include <QVector>
#include "PuppetModels.h"
#include "SpriteModels.h"

class NavigatorPanel;
class ProjectSession;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * @brief Modal "new puppet" window: its name at the top, its parts on the left and the
 *        atlas's sprites on the right.
 *
 * Sprites dragged onto a part (or added with "Add to Part") become its variants; sprites
 * dropped on empty space become one new part showing them all, named after the dragged
 * folder or what the sprites' names share (see partNameFor()). Right-clicking a part offers
 * to rename or delete it, and selected sprites can be taken out of their part. Parts are
 * placed at the puppet's origin; they are attached in the workspace. Nothing changes until Create:
 * puppet() is then the new puppet, which the workspace adds as one undo step.
 */
class NewPuppetDialog : public QDialog {
    Q_OBJECT
public:
    /// @param existing The atlas's puppets, whose names the new one can't take
    /// @param filter The sprites it may use (the active atlas's)
    NewPuppetDialog(const ProjectSession* session, const QVector<Puppet>& existing,
                    std::function<bool(const SpritePtr&)> filter, QWidget* parent = nullptr);

    /// The puppet as built: its name, and its parts with their sprites.
    Puppet puppet() const;

    QLineEdit* nameEdit() const { return m_nameEdit; }
    QTreeWidget* partsTree() const { return m_partsTree; }
    NavigatorPanel* sprites() const { return m_sprites; }

    /// Adds a part, named "part" (or the first free "part N"), and selects it.
    void addPart();
    /// Adds @p spritePaths to part @p partIndex; with -1, makes one part of them, named
    /// @p groupName or after the sprites.
    void addSprites(int partIndex, const QStringList& spritePaths, const QString& groupName = QString());
    /// Deletes the selected parts, and takes the selected sprites out of their parts.
    void removeSelected();
    /// The parts list's context menu, for its selection. The caller owns it.
    QMenu* createPartsMenu();

    /// A part's name for @p spritePaths: @p groupName when given, else what the sprites' names
    /// start with less a trailing frame number or separator (walk_01, walk_02 → walk), else
    /// the first sprite's name.
    static QString partNameFor(const QStringList& spritePaths, const QString& groupName = QString());

private:
    /// The selected parts, and the selected sprites (part, variant) of the other parts.
    struct Selection {
        QSet<int> parts;
        QVector<QPair<int, int>> sprites;
    };
    Selection selection() const;
    void rebuildParts(int selectPart, int selectVariant = -1);
    void checkName();
    void renamePart(QTreeWidgetItem* item);
    /// The part of the selected row (a part or one of its sprites), or -1.
    int selectedPart() const;
    void updateButtons();

    const QVector<Puppet> m_existing;
    Puppet m_puppet;
    bool m_rebuilding = false;

    QLineEdit* m_nameEdit = nullptr;
    QLabel* m_nameProblem = nullptr;
    QTreeWidget* m_partsTree = nullptr;
    QLabel* m_partsProblem = nullptr;
    QPushButton* m_removeButton = nullptr;
    NavigatorPanel* m_sprites = nullptr;
    QPushButton* m_addToPartButton = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
};
