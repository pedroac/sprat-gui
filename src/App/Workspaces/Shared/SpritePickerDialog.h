#pragma once
#include <functional>
#include <QDialog>
#include <QStringList>
#include <QVector>
#include "SpriteModels.h"

class NavigatorPanel;
class ProjectSession;
class QDialogButtonBox;
class QLabel;
class QStackedWidget;
class QTreeWidgetItem;

/**
 * @class SpritePickerDialog
 * @brief Modal sprite chooser: the project's sprite tree with its text filter, and a preview
 *        of the highlighted sprite.
 *
 * With multi-select, sprites are picked by ticking them, as in the Sprites workspace: ticking
 * a folder ticks every sprite under it, and Shift+click ticks a range. Without it, the selected
 * sprite is picked. Double-clicking a sprite picks it and closes the dialog.
 *
 * With `pickGroups`, groups (the tree's folders) are picked instead: select one, or several with
 * Ctrl/Shift+click; sprites can't be selected. selectedGroups() returns every group, in or under
 * the selected ones, that directly holds sprites.
 */
class SpritePickerDialog : public QDialog {
    Q_OBJECT
public:
    struct Options {
        QString title;
        /// Text of the button that accepts the selection, e.g. "Add".
        QString acceptText;
        /// Shown instead of the tree when no sprite passes `filter`.
        QString emptyText;
        /// Lists only the sprites it accepts; an empty filter lists them all.
        std::function<bool(const SpritePtr&)> filter;
        bool multiSelect = true;
        /// Picks groups (folders) rather than sprites; `multiSelect` is then ignored.
        bool pickGroups = false;
        /// Lets the accept button work with nothing picked, e.g. to create an empty timeline.
        bool allowEmpty = false;
    };

    /// A group that directly holds sprites.
    struct Group {
        QString name;         ///< Its name in the tree, e.g. "run"
        QString path;         ///< Names from the top of the tree down, e.g. "Mascot.zip/run"
        QStringList spritePaths;  ///< Its own sprites (not its subgroups'), in tree order
    };

    SpritePickerDialog(const ProjectSession* session, const Options& options, QWidget* parent = nullptr);

    /// Paths of the picked sprites, in tree order: the ticked ones with multi-select, else the
    /// selected one.
    QStringList selectedSpritePaths() const;

    /// With `pickGroups`: the selected groups and the groups under them that directly hold
    /// sprites, in tree order, each once.
    QVector<Group> selectedGroups() const;

    /// Shows the dialog and returns the picked sprites' paths, or nothing if it was cancelled.
    static QStringList pick(QWidget* parent, const ProjectSession* session, const Options& options);
    /// Shows the dialog in group mode and returns the picked groups, or nothing if it was cancelled.
    static QVector<Group> pickGroups(QWidget* parent, const ProjectSession* session, Options options);

private:
    void updatePreview(QTreeWidgetItem* current);
    void updateAcceptButton();

    bool    m_multiSelect = true;
    bool    m_pickGroups  = false;
    bool    m_allowEmpty  = false;
    QString m_acceptText;
    QString m_doubleClickedPath;

    NavigatorPanel*   m_navigatorPanel = nullptr;
    QStackedWidget*   m_contentStack   = nullptr;
    QLabel*           m_previewImage   = nullptr;
    QLabel*           m_previewInfo    = nullptr;
    QDialogButtonBox* m_buttons        = nullptr;
};
