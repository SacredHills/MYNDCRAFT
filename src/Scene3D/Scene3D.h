#pragma once
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QString>
#include <QUuid>
#include <QVector2D>
#include <QVector3D>
#include <map>
#include <memory>
#include <vector>

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
    bool operator==(const Material& o) const {
        return id == o.id && name == o.name && baseColor == o.baseColor && baseTexture == o.baseTexture && metallic == o.metallic &&
               roughness == o.roughness && emissive == o.emissive && doubleSided == o.doubleSided;
    }
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

struct Scene3D {
    std::map<QUuid, std::shared_ptr<const Mesh>> meshes;
    std::map<QUuid, QImage> textures;
    std::vector<Material> materials;
    std::vector<Node3D> nodes;           // parents are not necessarily before children
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
    QJsonObject toJson() const;                                    // structure only; blobs/images stored by the caller
    static bool fromJson(const QJsonObject& o, Scene3D& out, QString* err);
    // Validity checks used on load: unknown parents, cycles, missing mesh/material references (reported, not fatal).
    QStringList validate() const;
};

// Euler (degrees, same convention as Node3D) <-> matrix helpers.
QVector3D eulerFromMatrix(const QMatrix4x4& m);
void decomposeMatrix(const QMatrix4x4& m, QVector3D* t, QVector3D* eulerDeg, QVector3D* s);

// Primitive meshes so a scene can be built (and tested) without importing anything.
std::shared_ptr<Mesh> makeCube(double size = 1.0);
std::shared_ptr<Mesh> makeSphere(double radius = 0.5, int segments = 24, int rings = 12);
std::shared_ptr<Mesh> makePlane(double size = 1.0);
std::shared_ptr<Mesh> makeCylinder(double radius = 0.5, double height = 1.0, int segments = 24);

}  // namespace mynd
