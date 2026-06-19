#pragma once
class QWidget;

/**
 * @interface IWorkspace
 * @brief Lifecycle interface for application workspaces.
 *
 * Each concrete workspace (Atlas, FrameAnimation, Exportation, AtlasesManagement)
 * implements this interface so MainWindow can drive enter/leave via a single
 * switchWorkspace() method rather than four hand-rolled switch functions.
 */
class IWorkspace {
public:
    virtual ~IWorkspace() = default;

    /// Called after the dock/stack layout for this workspace has been applied.
    virtual void enter() = 0;

    /// Called before switching away from this workspace.
    virtual void leave() = 0;

    /// Returns the widget to show as the QMainWindow central-area page,
    /// or nullptr for dock-only workspaces (Atlas, FrameAnimation).
    virtual QWidget* widget() = 0;
};
