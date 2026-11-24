#pragma once
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QString>
#include <QUuid>
#include <QVector2D>
#include <QVector3D>
#include <array>
#include <map>
#include <memory>
#include <vector>

#include "Scene/Property.h"

namespace mynd {

// ---- 3D scene data model -------------------------------------------------------------------------------------
// A Scene3D layer owns one of these. Everything is plain data with value semantics (meshes are shared immutably,
// so copying a scene for an undo snapshot is cheap). Units are arbitrary scene units; +Y is up, cameras look down
// their local -Z axis (glTF conventions).

struct MeshPart {                  // a run of triangles that share a material
    int firstIndex = 0;
    int indexCount = 0;
    QUuid material;                // looked up in Scene3D::materials; null = default grey
};

struct Mesh {
    QUuid id;
    QString name;
    std::vector<QVector3D> positions;
    std::vector<QVector3D> normals;      // empty or same size as positions
    std::vector<QVector2D> uvs;          // empty or same size as positions
    std::vector<uint32_t> indices;       // triangle list
    std::vector<MeshPart> parts;
    // Skinning (optional; both empty or both the size of positions): up to four joints per vertex, indices into the Skin used by
    // the mesh node. Weights sum to 1.
    std::vector<std::array<uint16_t, 4>> joints;
    std::vector<std::array<float, 4>> weights;
    bool skinned() const { return !joints.empty() && joints.size() == positions.size() && weights.size() == positions.size(); }
    int triangleCount() const { return int(indices.size() / 3); }
    void computeNormals();               // smooth normals from the triangles (when the source had none)
    bool validate(QString* why = nullptr) const;
    void bounds(QVector3D* mn, QVector3D* mx) const;
    QJsonObject toJson() const;                       // metadata + parts only; geometry goes to a binary blob
    QByteArray geometryBlob() const;
    static std::shared_ptr<Mesh> fromJson(const QJsonObject& o, const QByteArray& blob, QString* err);
};

struct Material {
    QUuid id;
    QString name = "Material";
    QColor baseColor{200, 200, 200};
    QUuid baseTexture;                   // key into Scene3D::textures; null = none
    double metallic = 0.0;               // 0..1
    double roughness = 0.6;              // 0..1
    QColor emissive{0, 0, 0};
    bool doubleSided = false;
    bool unlit = false;                  // flat: base colour (times texture) is shown as is, lights are ignored
    bool operator==(const Material& o) const {
        return id == o.id && name == o.name && baseColor == o.baseColor && baseTexture == o.baseTexture && metallic == o.metallic &&
               roughness == o.roughness && emissive == o.emissive && doubleSided == o.doubleSided && unlit == o.unlit;
    }
};

// Joints of a skeleton: nodes of the scene plus the inverse of each joint's world transform in the bind pose.
struct Skin {
    QUuid id;
    std::vector<QUuid> joints;                  // node ids
    std::vector<QMatrix4x4> inverseBind;        // same size as joints
    bool operator==(const Skin& o) const { return id == o.id && joints == o.joints && inverseBind == o.inverseBind; }
};

enum class NodeKind { Group, Mesh, Light, Camera };
enum class LightType { Directional, Point, Spot };
QString nodeKindKey(NodeKind k);
bool nodeKindFromKey(const QString& s, NodeKind& out);
QString lightTypeKey(LightType t);
bool lightTypeFromKey(const QString& s, LightType& out);

struct Node3D {
    QUuid id;
    QUuid parent;                        // null = scene root
    QString name;
    NodeKind kind = NodeKind::Group;
    bool visible = true;
    QVector3D position{0, 0, 0};
    QVector3D rotation{0, 0, 0};         // Euler degrees; the matrix is Rz * Ry * Rx (X applied first)
    QVector3D scale{1, 1, 1};
    // Mesh nodes
    QUuid mesh;                          // key into Scene3D::meshes
    QUuid materialOverride;              // replaces every part's material when set
    // A "card": this mesh node shows the rendered content of a 2D layer of the composition (the layer is then drawn only through the card).
    QUuid cardLayer;
    bool cardLit = false;                // flat by default (looks exactly like the 2D layer); lit cards react to the scene's lights
    QUuid skin;                          // key into Scene3D::skins; the mesh then follows the skin's joints and this node's own transform is ignored
    // Light nodes
    LightType lightType = LightType::Point;
    QColor lightColor{255, 255, 255};
    double intensity = 1.0;
    double range = 0;                    // point/spot: 0 = unlimited, else linear falloff to 0 at this distance
    double spotAngle = 45;               // full cone angle in degrees
    // Camera nodes
    double fov = 45;                     // vertical field of view in degrees
    double nearPlane = 0.1, farPlane = 1000;
    QMatrix4x4 localMatrix() const;
    bool operator==(const Node3D& o) const;
};

// A named piece of keyframe animation of this scene's properties (ids as in Anim3D.h), e.g. imported from a model file. Times are in
// composition frames at the time of import. Clips are played by placing them on the layer's timeline (ClipPlacement).
struct AnimClip {
    QUuid id;
    QString name;
    int length = 0;                      // frames; the clip's time runs 0..length
    PropertyTracks tracks;
    bool operator==(const AnimClip& o) const { return id == o.id && name == o.name && length == o.length && tracks == o.tracks; }
};
// A clip placed on the timeline: it starts at composition frame `start`, runs at `speed`, begins `offset` frames into the clip, and
// either loops or holds its last pose.
struct ClipPlacement {
    QUuid id;
    QUuid clip;
    int start = 0;
    double speed = 1.0;                  // 1 = as authored, 2 = twice as fast; always > 0
    int offset = 0;                      // frames into the clip at `start`
    bool loop = false;
    bool enabled = true;
    bool operator==(const ClipPlacement& o) const {
        return id == o.id && clip == o.clip && start == o.start && speed == o.speed && offset == o.offset && loop == o.loop && enabled == o.enabled;
    }
};

// Identifies a state of a scene for render caching. Every edit that goes through the undo system takes a new one; code that
// changes a scene directly must call Scene3D::touch().
quint64 nextSceneRevision();

struct Scene3D {
    quint64 revision = nextSceneRevision();
    void touch() { revision = nextSceneRevision(); }
    std::map<QUuid, std::shared_ptr<const Mesh>> meshes;
    std::map<QUuid, QImage> textures;
    std::vector<Material> materials;
    std::vector<Node3D> nodes;           // parents are not necessarily before children
    std::vector<Skin> skins;
    std::vector<AnimClip> clips;
    std::vector<ClipPlacement> placements;       // applied in order, before the layer's own keyframes
    QUuid activeCamera;
    QColor ambient{255, 255, 255};
    double ambientIntensity = 0.25;
    QColor background{0, 0, 0, 0};       // transparent by default so the 3D layer sits over the 2D artwork

    Node3D* node(const QUuid& id);
    const Node3D* node(const QUuid& id) const;
    Material* material(const QUuid& id);
    const Material* material(const QUuid& id) const;
    QVector<QUuid> childrenOf(const QUuid& parent) const;      // in list order
    QMatrix4x4 worldMatrix(const QUuid& id) const;
    bool effectivelyVisible(const QUuid& id) const;
    bool isAncestor(const QUuid& maybeAncestor, const QUuid& of) const;
    // Adds a node (uuid assigned if null) and returns its id.
    QUuid addNode(Node3D n);
    // Removes the node and its descendants; unused meshes/materials/textures are dropped. Clears activeCamera if needed.
    void removeNode(const QUuid& id);
    // Re-parents keeping the world transform; refuses cycles.
    bool reparent(const QUuid& id, const QUuid& newParent);
    // World-space bounds of all visible meshes. Returns false for an empty scene.
    bool worldBounds(QVector3D* mn, QVector3D* mx) const;
    QUuid firstCamera() const;
    // Drops resources nothing refers to.
    void pruneUnused();
    QString uniqueName(const QString& base) const;
    const AnimClip* clip(const QUuid& id) const;
    const Skin* skinById(const QUuid& id) const;
    QJsonObject toJson() const;                                    // structure only; blobs/images stored by the caller
    static bool fromJson(const QJsonObject& o, Scene3D& out, QString* err);
    // Validity checks used on load: unknown parents, cycles, missing mesh/material references (reported, not fatal).
    QStringList validate() const;
    // Same content (ignoring the render revision); images compare by identity.
    bool sameAs(const Scene3D& o) const;
};

// Euler (degrees, same convention as Node3D) <-> matrix helpers.
QVector3D eulerFromMatrix(const QMatrix4x4& m);
void decomposeMatrix(const QMatrix4x4& m, QVector3D* t, QVector3D* eulerDeg, QVector3D* s);

// Primitive meshes so a scene can be built (and tested) without importing anything.
std::shared_ptr<Mesh> makeCube(double size = 1.0);
std::shared_ptr<Mesh> makeSphere(double radius = 0.5, int segments = 24, int rings = 12);
std::shared_ptr<Mesh> makePlane(double size = 1.0);
std::shared_ptr<Mesh> makeCardPlane(double width, double height);   // upright (XY) plane facing +Z, UV v up

std::shared_ptr<Mesh> makeCylinder(double radius = 0.5, double height = 1.0, int segments = 24);

}  // namespace mynd
