#pragma once
#include <QAction>
#include <QActionGroup>
#include <QComboBox>
#include <QLabel>
#include <QMainWindow>
class QDragEnterEvent;
class QDropEvent;
#include <memory>

#include "Core/Autosave.h"
#include "Core/ProjectIO.h"
#include "UI/CanvasView.h"
#include "UI/AnimationActions.h"
#include "UI/SymbolActions.h"
#include "UI/Scene3DActions.h"
#include "UI/Scene3DPanel.h"
#include "UI/Viewport3D.h"
#include "UI/EditorSession.h"
#include "UI/Panels.h"
#include "UI/Workspaces.h"

namespace mynd {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

    Document* document() const { return m_doc.get(); }
    EditorSession* session() const { return m_session; }
    CanvasView* canvas() const { return m_canvas; }
    QDockWidget* canvasDock() const { return m_canvasDock; }
    CanvasView* openCanvasWindow();    // another view of the document in its own movable window (any screen); the main canvas stays in every workspace
    int canvasWindowCount() const { return int(m_extraCanvases.size()); }
    LayerPanel* layerPanel() const { return m_layers; }
    WorkspaceManager* workspaces() const { return m_workspaces; }
    AnimationActions* animation() const { return m_anim; }
    SymbolActions* symbols() const { return m_symbolActions; }
    Scene3DActions* scene3d() const { return m_scene3dActions; }
    Viewport3D* viewport3d() const { return m_viewport3d; }
    Scene3DPanel* scene3dPanel() const { return m_scene3dPanel; }
    void show3DPanels();
    Autosaver* autosaver() const { return m_autosave; }

    // Non-interactive API (used by menu handlers after dialogs, by tests and by command line).
    void newDocument(const CompositionSettings& s, bool withLayer = true);
    bool openFile(const QString& path, QString* error = nullptr);
    bool importImage(const QString& path, QString* error = nullptr);
    bool saveFile(const QString& path, QString* error = nullptr);
    bool exportImage(const QString& path, QString* error = nullptr);
    void adoptDocument(std::unique_ptr<Document> d);
    // Interactive helpers
    bool maybeSave();
    void offerRecovery();

protected:
    void closeEvent(QCloseEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    void buildActions();
    void buildMenus();
    void buildToolbars();
    void buildDocks();
    void bindUndo();
    void updateTitle();
    void updateActionStates();
    void showLayerContextMenu(const QPoint& globalPos);
    void actNew();
    void actOpen();
    bool actSave();
    bool actSaveAs();
    void actExport();
    void actExportAnimation();
    void actImport();
    void actPreferences();
    void actAbout();
    void rebuildWorkspaceMenu();
    void mutateShapes(const QString& text, const std::function<void(ShapeList&, const QSet<QUuid>&)>& fn);
    void updateRecentMenu();
    void adjustBrushSize(double factor);

    std::unique_ptr<Document> m_doc;
    EditorSession* m_session;
    CanvasView* m_canvas;
    QDockWidget* m_canvasDock = nullptr;      // the canvas is a panel inside the central area: drag its title bar to float it, drag it back to dock it
    bool m_applyingCanvas = false;
    void rememberCanvasPlacement();
    void applyCanvasPlacement();
    QList<CanvasView*> m_extraCanvases;
    QAction* aDetachCanvas = nullptr;
    LayerPanel* m_layers;
    WorkspaceManager* m_workspaces = nullptr;
    AnimationActions* m_anim = nullptr;
    SymbolActions* m_symbolActions = nullptr;
    Scene3DActions* m_scene3dActions = nullptr;
    Viewport3D* m_viewport3d = nullptr;
    Scene3DPanel* m_scene3dPanel = nullptr;
    QLabel* m_crumb = nullptr;
    QToolButton* m_crumbExit = nullptr;
    QAction* m_crumbExitAction = nullptr;
    QAction* m_crumbAction = nullptr;
    void updateBreadcrumb();
    Autosaver* m_autosave;
    QMap<QString, QDockWidget*> m_docks;
    QActionGroup* m_toolGroup;
    QMap<ToolId, QAction*> m_toolActions;
    QAction *aNew, *aOpen, *aSave, *aSaveAs, *aExport, *aExportAnim, *aImport, *aPrefs, *aQuit;
    QAction *aUndo, *aRedo, *aSelAll, *aDeselect, *aDeleteSel;
    QAction *aZoomIn, *aZoomOut, *aFit, *aActual, *aRotL, *aRotR, *aRotReset, *aGrid, *aRulers, *aGuides, *aSnap, *aClearGuides;
    QAction *aGroupShapes, *aUngroupShapes, *aFront, *aForward, *aBackward, *aBack;
    QAction *aNewRaster, *aNewVector, *aNewGroup, *aDup, *aDelLayer, *aGroupLayer, *aUngroupLayer, *aUp, *aDown, *aToggleVis, *aToggleLock, *aCopyLayer, *aCutLayer, *aPasteLayer, *aRenameLayer, *aTopLayer, *aBottomLayer;
    QMenu *m_recentMenu, *m_workspaceMenu;
    QComboBox* m_wsCombo;
    QLabel *m_zoomLabel, *m_posLabel, *m_layerLabel;
    QList<QMetaObject::Connection> m_undoConns;
};

}  // namespace mynd
