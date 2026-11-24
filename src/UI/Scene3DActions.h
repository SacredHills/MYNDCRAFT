#pragma once
#include <QAction>
#include <QMenu>
#include <QObject>

#include "Scene3D/ModelImport.h"
#include "UI/EditorSession.h"

namespace mynd {

enum class Primitive { Cube, Sphere, Plane, Cylinder };

// 3D scene commands (menu, panel, viewport). The dialog-free helpers do the work so tests can drive them directly.
class Scene3DActions : public QObject {
    Q_OBJECT
public:
    Scene3DActions(EditorSession* s, QWidget* dialogParent);
    QAction *newScene, *importModel, *addCube, *addSphere, *addPlane, *addCylinder;
    QAction *addPoint, *addDirectional, *addSpot, *addCamera, *addGroup;
    QAction *duplicateNode, *deleteNode, *useAsSceneCamera, *frameSelection;
    void fillMenu(QMenu* menu);

    // Active 3D layer, or a new one when the active layer is not a 3D scene.
    QUuid ensureSceneLayer();
    QUuid createPrimitive(Primitive p);
    QUuid createLight(LightType t);
    QUuid createCamera();                      // placed so that it frames what is in the scene
    QUuid createGroup();
    // Imports a model file into the active (or a new) 3D layer as one undo step. Adds a light and a camera when the scene has
    // none, so the model is visible in the composition straight away. Returns the new root node (null on failure).
    QUuid importModelFile(const QString& path, QString* error, QStringList* warnings = nullptr);
    bool deleteSelected();
    QUuid duplicateSelected();
    bool setSceneCamera(const QUuid& node);
    bool reparentNode(const QUuid& node, const QUuid& newParent);
    void refresh();
signals:
    void sceneLayerActivated();      // a 3D layer was created or activated: the main window shows the 3D panels

private:
    EditorSession* m_s;
    QWidget* m_parent;
    QUuid add(const Node3D& n, const QString& text, std::shared_ptr<const Mesh> mesh = nullptr);
};

}  // namespace mynd
