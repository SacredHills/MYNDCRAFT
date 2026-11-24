#pragma once
#include <QSet>
#include <QString>
#include <QUuid>
#include <QVector>
#include <memory>

#include "Scene/Property.h"
#include "Scene3D/Scene3D.h"

namespace mynd {

// ---------------------------------------------------------------------------------------------
// Keyframe animation of 3D scene properties. Animated 3D properties live in the owning layer's ordinary
// PropertyTracks under ids of the form
//     "3d:n:<node uuid>:<what>"      what = pos | rot | scl | fov | lint | lcol | lrange | lspot
//     "3d:m:<material uuid>:<what>"  what = base | emis | rough | metal
// so the Timeline, the Graph Editor, key selection/move/copy/delete and the easing presets handle
// them exactly like the 2D properties. The scene itself keeps the static values; evaluating a scene at a
// frame overlays the animated ones on a copy.
// ---------------------------------------------------------------------------------------------

QString nodePropId(const QUuid& node, const QString& what);
QString materialPropId(const QUuid& material, const QString& what);
bool isScene3DPropId(const QString& id);

struct PropTarget {
    bool material = false;
    QUuid target;
    QString what;
};
bool parseScene3DPropId(const QString& id, PropTarget* out);

// Definition for a 3D property id (type, range, label); null for malformed ids. Pointers stay valid for the process lifetime.
const PropertyDef* scene3DPropertyDef(const QString& id);

// Ids of every property of the scene that can be animated, in display order.
QVector<QString> animatableProps(const Scene3D& s);
// Does the scene contain this property (node exists and has the right kind / material exists)?
bool scenePropExists(const Scene3D& s, const QString& id);
// Reads/writes the static value of a property in the scene. Writing clamps through the property definition.
bool readSceneProp(const Scene3D& s, const QString& id, PropValue* out);
bool writeSceneProp(Scene3D& s, const QString& id, PropValue v);
// "Cube › Position"
QString describeSceneProp(const Scene3D& s, const QString& id);

// Where a placed clip is at composition frame `frame`: its own time in 0..length, or false when the placement has not started yet
// (or is disabled / refers to a missing clip). Looping placements wrap; others hold the last pose.
bool clipTimeAt(const ClipPlacement& p, const AnimClip& c, double frame, double* clipTime);
// Properties of the scene that currently follow some animation: layer keyframes or an enabled clip placement.
QSet<QString> drivenProps(const Scene3D& s, const PropertyTracks& t);

// True when any track of `t` targets this scene and has keyframes.
bool hasSceneAnimation(const Scene3D& s, const PropertyTracks& t);
// The scene as it looks at `frame`: a copy with every animated property overlaid. The revision is derived from the base revision
// and the evaluated values, so equal (scene, frame values) pairs share render-cache entries. Tracks for missing targets are ignored.
Scene3D evaluateScene(const Scene3D& base, const PropertyTracks& t, double frame);
// Tracks that no longer refer to anything in the scene (deleted nodes) removed; for saving.
PropertyTracks pruneSceneTracks(const Scene3D& s, const PropertyTracks& t);
// Track ids in `t` that belong to the scene (exist in it and are animated), in animatableProps order.
QVector<QString> animatedSceneProps(const Scene3D& s, const PropertyTracks& t);

}  // namespace mynd
