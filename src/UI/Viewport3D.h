#pragma once
#include <QHash>
#include <QWidget>

#include "Scene3D/Render3D.h"
#include "UI/EditorSession.h"
#include "UI/Scene3DActions.h"

class QToolButton;

namespace mynd {

class ViewCanvas;

// Interactive 3D editing view of the active 3D scene layer: orbit / pan / zoom, click to select, move / rotate / scale gizmos,
// and a "camera view" that shows exactly what the composition will contain.
class Viewport3D : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Move, Rotate, Scale };
    enum class Handle { None, X, Y, Z, Center };
    Viewport3D(EditorSession* s, Scene3DActions* a, QWidget* parent = nullptr);

    Mode mode() const { return m_mode; }
    void setMode(Mode m);
    bool cameraView() const { return m_cameraView; }
    void setCameraView(bool on);
    void frame(bool selectionOnly);
    OrbitCamera& orbit();                                   // the editor camera of the active scene layer
    // The camera and pixel size used for the last paint (what screen positions refer to).
    RenderCamera currentCamera(QSize* size = nullptr) const;
    QWidget* canvas() const;
    // Screen position of a gizmo handle for the selected node (for tests and hit-testing); false when there is none.
    bool handlePosition(Handle h, QPointF* out) const;

private:
    friend class ViewCanvas;
    void updateButtons();
    EditorSession* m_s;
    Scene3DActions* m_a;
    ViewCanvas* m_canvas;
    Mode m_mode = Mode::Move;
    bool m_cameraView = false;
    QHash<QUuid, OrbitCamera> m_orbits;
    QToolButton *m_bMove, *m_bRotate, *m_bScale, *m_bCam;
};

}  // namespace mynd
