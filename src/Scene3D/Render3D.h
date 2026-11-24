#pragma once
#include <QImage>
#include <QPointF>
#include <QSize>

#include "Scene3D/Scene3D.h"

namespace mynd {

// A CPU renderer for Scene3D: perspective camera, z-buffer, per-pixel Blinn-Phong lighting with a metallic/roughness
// style material, bilinear base-colour textures, optional 2x2 supersampling. It runs identically on every platform
// and headless, so previews, exports and tests all agree. It does not do shadows, reflections, normal maps,
// transparency sorting or skinning.

// The mesh of a skinned mesh node deformed by its skeleton's current pose, in world space (so it is drawn with an identity model
// matrix, as glTF specifies). Null when the node has no usable skin.
std::shared_ptr<const Mesh> skinnedMeshFor(const Scene3D& s, const Node3D& n);

struct RenderCamera {
    QMatrix4x4 view, proj;
    QVector3D eye;
    double nearPlane = 0.1, farPlane = 1000;
};

struct RenderOptions {
    int supersample = 1;            // 1 = fastest (editing), 2 = anti-aliased (export / final preview)
    bool drawBackground = true;     // fill with Scene3D::background first
};

// The camera of a Camera node, or the scene's active camera when `cameraNode` is null; a default framing camera when
// the scene has none.
RenderCamera sceneCamera(const Scene3D& s, double aspect, const QUuid& cameraNode = {});
RenderCamera frameScene(const Scene3D& s, double aspect);

// Editor camera orbiting a target point.
struct OrbitCamera {
    QVector3D target{0, 0, 0};
    double distance = 6, yawDeg = 35, pitchDeg = 25, fov = 45;
    RenderCamera toCamera(double aspect) const;
    QVector3D eye() const;
    void frame(const QVector3D& mn, const QVector3D& mx);
};

QImage renderScene(const Scene3D& s, const RenderCamera& cam, const QSize& size, const RenderOptions& o = {});

// Screen <-> world helpers for gizmos and picking. Screen space is pixels with the origin at the top-left.
bool projectToScreen(const RenderCamera& cam, const QVector3D& world, const QSize& size, QPointF* out, double* depth = nullptr);
struct Ray { QVector3D origin, dir; };
Ray screenRay(const RenderCamera& cam, const QSize& size, const QPointF& p);

struct PickHit { QUuid node; double distance = 0; };
// Nearest visible mesh triangle under the point, or a light/camera marker. Null node when nothing is hit.
PickHit pickNode(const Scene3D& s, const RenderCamera& cam, const QSize& size, const QPointF& p);

}  // namespace mynd
