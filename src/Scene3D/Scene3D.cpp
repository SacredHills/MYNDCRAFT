#include "Scene3D/Scene3D.h"

#include <limits>

#include <QBuffer>
#include <QDataStream>
#include <QJsonArray>
#include <QSet>
#include <atomic>
#include <cmath>
#include <functional>

namespace mynd {

quint64 nextSceneRevision() {
    static std::atomic<quint64> counter{1};
    return counter.fetch_add(1);
}

// ============================================================ keys
QString nodeKindKey(NodeKind k) {
    switch (k) {
    case NodeKind::Group: return "group";
    case NodeKind::Mesh: return "mesh";
    case NodeKind::Light: return "light";
    case NodeKind::Camera: return "camera";
    }
    return "group";
}
bool nodeKindFromKey(const QString& s, NodeKind& out) {
    for (NodeKind k : {NodeKind::Group, NodeKind::Mesh, NodeKind::Light, NodeKind::Camera})
        if (nodeKindKey(k) == s) { out = k; return true; }
    return false;
}
QString lightTypeKey(LightType t) {
    switch (t) {
    case LightType::Directional: return "directional";
    case LightType::Point: return "point";
    case LightType::Spot: return "spot";
    }
    return "point";
}
bool lightTypeFromKey(const QString& s, LightType& out) {
    for (LightType k : {LightType::Directional, LightType::Point, LightType::Spot})
        if (lightTypeKey(k) == s) { out = k; return true; }
    return false;
}

// ============================================================ math helpers
QMatrix4x4 Node3D::localMatrix() const {
    QMatrix4x4 m;
    m.translate(position);
    m.rotate(float(rotation.z()), 0, 0, 1);
    m.rotate(float(rotation.y()), 0, 1, 0);
    m.rotate(float(rotation.x()), 1, 0, 0);
    m.scale(scale);
    return m;
}

bool Node3D::operator==(const Node3D& o) const {
    return id == o.id && parent == o.parent && name == o.name && kind == o.kind && visible == o.visible && position == o.position &&
           rotation == o.rotation && scale == o.scale && mesh == o.mesh && materialOverride == o.materialOverride && skin == o.skin &&
           cardLayer == o.cardLayer && cardLit == o.cardLit &&
           lightType == o.lightType && lightColor == o.lightColor && intensity == o.intensity && range == o.range &&
           spotAngle == o.spotAngle && fov == o.fov && nearPlane == o.nearPlane && farPlane == o.farPlane;
}

QVector3D eulerFromMatrix(const QMatrix4x4& m) {
    // m = Rz * Ry * Rx (rotation only, unit columns)
    const double m20 = m(2, 0), m21 = m(2, 1), m22 = m(2, 2), m10 = m(1, 0), m00 = m(0, 0), m11 = m(1, 1), m12 = m(1, 2);
    const double y = std::asin(std::clamp(-m20, -1.0, 1.0));
    double x, z;
    if (std::abs(m20) < 0.999999) { x = std::atan2(m21, m22); z = std::atan2(m10, m00); }
    else { x = std::atan2(-m12, m11); z = 0; }
    const double d = 180.0 / M_PI;
    return QVector3D(float(x * d), float(y * d), float(z * d));
}

QVector3D eulerNear(const QVector3D& e, const QVector3D& ref) {
    auto unwrap = [](float v, float r) { return v + 360.0f * std::round((r - v) / 360.0f); };
    const QVector3D alts[2] = {e, QVector3D(e.x() + 180.0f, 180.0f - e.y(), e.z() + 180.0f)};
    QVector3D best = e;
    float bestD = std::numeric_limits<float>::max();
    for (const QVector3D& a : alts) {
        const QVector3D c(unwrap(a.x(), ref.x()), unwrap(a.y(), ref.y()), unwrap(a.z(), ref.z()));
        const float d = (c - ref).lengthSquared();
        if (d < bestD) { bestD = d; best = c; }
    }
    return best;
}

void decomposeMatrix(const QMatrix4x4& m, QVector3D* t, QVector3D* eulerDeg, QVector3D* s) {
    const QVector3D c0(m(0, 0), m(1, 0), m(2, 0)), c1(m(0, 1), m(1, 1), m(2, 1)), c2(m(0, 2), m(1, 2), m(2, 2));
    QVector3D sc(c0.length(), c1.length(), c2.length());
    const float det = QVector3D::dotProduct(QVector3D::crossProduct(c0, c1), c2);
    if (det < 0) sc.setX(-sc.x());
    if (t) *t = QVector3D(m(0, 3), m(1, 3), m(2, 3));
    if (s) *s = sc;
    if (eulerDeg) {
        QMatrix4x4 r;
        const QVector3D n0 = sc.x() != 0 ? c0 / sc.x() : QVector3D(1, 0, 0);
        const QVector3D n1 = sc.y() != 0 ? c1 / sc.y() : QVector3D(0, 1, 0);
        const QVector3D n2 = sc.z() != 0 ? c2 / sc.z() : QVector3D(0, 0, 1);
        r.setColumn(0, QVector4D(n0, 0)); r.setColumn(1, QVector4D(n1, 0)); r.setColumn(2, QVector4D(n2, 0));
        *eulerDeg = eulerFromMatrix(r);
    }
}

// ============================================================ Mesh
void Mesh::computeNormals() {
    normals.assign(positions.size(), QVector3D(0, 0, 0));
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
        if (a >= positions.size() || b >= positions.size() || c >= positions.size()) continue;
        const QVector3D n = QVector3D::crossProduct(positions[b] - positions[a], positions[c] - positions[a]);   // area weighted
        normals[a] += n; normals[b] += n; normals[c] += n;
    }
    for (auto& n : normals) { n.normalize(); if (n.isNull()) n = QVector3D(0, 1, 0); }
}

bool Mesh::validate(QString* why) const {
    auto fail = [&](const QString& s) { if (why) *why = s; return false; };
    if (positions.empty()) return fail("mesh has no vertices");
    if (indices.size() % 3) return fail("index count is not a multiple of 3");
    if (!normals.empty() && normals.size() != positions.size()) return fail("normal count mismatch");
    if (!uvs.empty() && uvs.size() != positions.size()) return fail("uv count mismatch");
    for (uint32_t i : indices) if (i >= positions.size()) return fail("index out of range");
    int cover = 0;
    for (const auto& p : parts) {
        if (p.firstIndex < 0 || p.indexCount < 0 || size_t(p.firstIndex) + size_t(p.indexCount) > indices.size() || p.indexCount % 3)
            return fail("bad part range");
        cover += p.indexCount;
    }
    (void)cover;
    if (!joints.empty() || !weights.empty()) {
        if (joints.size() != positions.size() || weights.size() != positions.size()) return fail("skin weight count mismatch");
        for (const auto& w : weights) for (float f : w) if (!std::isfinite(f) || f < 0 || f > 1.0001f) return fail("invalid skin weight");
    }
    for (const auto& v : positions) if (!std::isfinite(v.x()) || !std::isfinite(v.y()) || !std::isfinite(v.z())) return fail("non-finite vertex");
    return true;
}

void Mesh::bounds(QVector3D* mn, QVector3D* mx) const {
    QVector3D lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
    for (const auto& p : positions) {
        lo = QVector3D(std::min(lo.x(), p.x()), std::min(lo.y(), p.y()), std::min(lo.z(), p.z()));
        hi = QVector3D(std::max(hi.x(), p.x()), std::max(hi.y(), p.y()), std::max(hi.z(), p.z()));
    }
    if (positions.empty()) lo = hi = QVector3D();
    *mn = lo; *mx = hi;
}

QJsonObject Mesh::toJson() const {
    QJsonObject o;
    o["id"] = id.toString(QUuid::WithoutBraces);
    o["name"] = name;
    o["vertices"] = int(positions.size());
    o["indices"] = int(indices.size());
    o["normals"] = !normals.empty();
    o["uvs"] = !uvs.empty();
    if (skinned()) o["skinned"] = true;
    QJsonArray pa;
    for (const auto& p : parts) {
        QJsonObject po; po["first"] = p.firstIndex; po["count"] = p.indexCount; po["material"] = p.material.toString(QUuid::WithoutBraces);
        pa.append(po);
    }
    o["parts"] = pa;
    return o;
}

QByteArray Mesh::geometryBlob() const {
    QByteArray b;
    QDataStream s(&b, QIODevice::WriteOnly);
    s.setByteOrder(QDataStream::LittleEndian);
    s.setFloatingPointPrecision(QDataStream::SinglePrecision);
    for (const auto& p : positions) s << p.x() << p.y() << p.z();
    for (const auto& n : normals) s << n.x() << n.y() << n.z();
    for (const auto& t : uvs) s << t.x() << t.y();
    for (uint32_t i : indices) s << quint32(i);
    if (skinned()) {
        for (const auto& j : joints) for (uint16_t v : j) s << quint16(v);
        for (const auto& w : weights) for (float v : w) s << v;
    }
    return b;
}

std::shared_ptr<Mesh> Mesh::fromJson(const QJsonObject& o, const QByteArray& blob, QString* err) {
    auto fail = [&](const QString& s) -> std::shared_ptr<Mesh> { if (err) *err = s; return nullptr; };
    auto m = std::make_shared<Mesh>();
    m->id = QUuid(o["id"].toString());
    if (m->id.isNull()) return fail("mesh without valid id");
    m->name = o["name"].toString();
    const qint64 vc = o["vertices"].toInt(-1), ic = o["indices"].toInt(-1);
    const bool hn = o["normals"].toBool(), hu = o["uvs"].toBool();
    if (vc <= 0 || ic < 0 || vc > 50000000 || ic > 200000000) return fail("mesh '" + m->name + "' has an invalid size");
    const bool hs = o["skinned"].toBool();
    const qint64 need = vc * 12 + (hn ? vc * 12 : 0) + (hu ? vc * 8 : 0) + ic * 4 + (hs ? vc * 24 : 0);
    if (blob.size() != need) return fail("mesh '" + m->name + "' geometry is truncated or the wrong size");
    QDataStream s(blob);
    s.setByteOrder(QDataStream::LittleEndian);
    s.setFloatingPointPrecision(QDataStream::SinglePrecision);
    m->positions.resize(size_t(vc));
    for (auto& p : m->positions) { float x, y, z; s >> x >> y >> z; p = QVector3D(x, y, z); }
    if (hn) { m->normals.resize(size_t(vc)); for (auto& n : m->normals) { float x, y, z; s >> x >> y >> z; n = QVector3D(x, y, z); } }
    if (hu) { m->uvs.resize(size_t(vc)); for (auto& t : m->uvs) { float x, y; s >> x >> y; t = QVector2D(x, y); } }
    m->indices.resize(size_t(ic));
    for (auto& i : m->indices) { quint32 v; s >> v; i = v; }
    if (hs) {
        m->joints.resize(size_t(vc)); m->weights.resize(size_t(vc));
        for (auto& j : m->joints) for (auto& v : j) { quint16 x; s >> x; v = x; }
        for (auto& w : m->weights) for (auto& v : w) { float x; s >> x; v = std::isfinite(x) ? std::clamp(x, 0.0f, 1.0f) : 0.0f; }
    }
    for (const auto& pv : o["parts"].toArray()) {
        const QJsonObject po = pv.toObject();
        m->parts.push_back({po["first"].toInt(), po["count"].toInt(), QUuid(po["material"].toString())});
    }
    if (m->parts.empty()) m->parts.push_back({0, int(m->indices.size()), QUuid()});
    QString why;
    if (!m->validate(&why)) return fail("mesh '" + m->name + "': " + why);
    return m;
}

// ============================================================ Scene3D
Node3D* Scene3D::node(const QUuid& id) {
    for (auto& n : nodes) if (n.id == id) return &n;
    return nullptr;
}
const Node3D* Scene3D::node(const QUuid& id) const {
    for (const auto& n : nodes) if (n.id == id) return &n;
    return nullptr;
}
Material* Scene3D::material(const QUuid& id) {
    for (auto& m : materials) if (m.id == id) return &m;
    return nullptr;
}
const Material* Scene3D::material(const QUuid& id) const {
    for (const auto& m : materials) if (m.id == id) return &m;
    return nullptr;
}
QVector<QUuid> Scene3D::childrenOf(const QUuid& parent) const {
    QVector<QUuid> r;
    for (const auto& n : nodes) if (n.parent == parent) r.append(n.id);
    return r;
}
QMatrix4x4 Scene3D::worldMatrix(const QUuid& id) const {
    QMatrix4x4 m;
    QUuid cur = id;
    for (int guard = 0; !cur.isNull() && guard < 256; ++guard) {
        const Node3D* n = node(cur);
        if (!n) break;
        m = n->localMatrix() * m;
        cur = n->parent;
    }
    return m;
}
bool Scene3D::effectivelyVisible(const QUuid& id) const {
    QUuid cur = id;
    for (int guard = 0; !cur.isNull() && guard < 256; ++guard) {
        const Node3D* n = node(cur);
        if (!n) return true;
        if (!n->visible) return false;
        cur = n->parent;
    }
    return true;
}
bool Scene3D::isAncestor(const QUuid& anc, const QUuid& of) const {
    QUuid cur = of;
    for (int guard = 0; !cur.isNull() && guard < 256; ++guard) {
        const Node3D* n = node(cur);
        if (!n) return false;
        if (n->parent == anc) return true;
        cur = n->parent;
    }
    return false;
}
bool Scene3D::sameAs(const Scene3D& o) const {
    if (nodes.size() != o.nodes.size() || materials.size() != o.materials.size() || meshes != o.meshes || textures.size() != o.textures.size()) return false;
    if (skins.size() != o.skins.size()) return false;
    for (size_t i = 0; i < skins.size(); ++i) if (!(skins[i] == o.skins[i])) return false;
    if (clips.size() != o.clips.size() || placements.size() != o.placements.size()) return false;
    for (size_t i = 0; i < clips.size(); ++i) if (!(clips[i] == o.clips[i])) return false;
    for (size_t i = 0; i < placements.size(); ++i) if (!(placements[i] == o.placements[i])) return false;
    if (activeCamera != o.activeCamera || ambient != o.ambient || ambientIntensity != o.ambientIntensity || background != o.background) return false;
    for (size_t i = 0; i < nodes.size(); ++i) if (!(nodes[i] == o.nodes[i])) return false;
    for (size_t i = 0; i < materials.size(); ++i) if (!(materials[i] == o.materials[i])) return false;
    for (const auto& [k, img] : textures) { auto it = o.textures.find(k); if (it == o.textures.end() || it->second.cacheKey() != img.cacheKey()) return false; }
    return true;
}

const Skin* Scene3D::skinById(const QUuid& id) const {
    for (const Skin& k : skins) if (k.id == id) return &k;
    return nullptr;
}

const AnimClip* Scene3D::clip(const QUuid& id) const {
    for (const AnimClip& c : clips) if (c.id == id) return &c;
    return nullptr;
}

QString Scene3D::uniqueName(const QString& base) const {
    auto used = [&](const QString& s) { for (const auto& n : nodes) if (n.name == s) return true; return false; };
    if (!used(base)) return base;
    for (int i = 2;; ++i) { const QString s = QString("%1 %2").arg(base).arg(i); if (!used(s)) return s; }
}
QUuid Scene3D::addNode(Node3D n) {
    if (n.id.isNull()) n.id = QUuid::createUuid();
    if (n.name.isEmpty()) n.name = nodeKindKey(n.kind);
    nodes.push_back(n);
    if (n.kind == NodeKind::Camera && activeCamera.isNull()) activeCamera = n.id;
    return n.id;
}
void Scene3D::removeNode(const QUuid& id) {
    QSet<QUuid> kill;
    std::function<void(const QUuid&)> walk = [&](const QUuid& i) { kill.insert(i); for (const QUuid& c : childrenOf(i)) walk(c); };
    walk(id);
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const Node3D& n) { return kill.contains(n.id); }), nodes.end());
    if (kill.contains(activeCamera)) activeCamera = firstCamera();
    pruneUnused();
}
bool Scene3D::reparent(const QUuid& id, const QUuid& newParent) {
    Node3D* n = node(id);
    if (!n || id == newParent) return false;
    if (!newParent.isNull() && (!node(newParent) || isAncestor(id, newParent))) return false;
    const QMatrix4x4 world = worldMatrix(id);
    const QMatrix4x4 pw = newParent.isNull() ? QMatrix4x4() : worldMatrix(newParent);
    const QMatrix4x4 local = pw.inverted() * world;
    n->parent = newParent;
    decomposeMatrix(local, &n->position, &n->rotation, &n->scale);
    return true;
}
bool Scene3D::moveNode(const QUuid& id, const QUuid& newParent, const QUuid& ref, bool after) {
    if (id == ref || !node(id)) return false;
    if (!ref.isNull()) { const Node3D* r = node(ref); if (!r || r->parent != newParent || isAncestor(id, ref)) return false; }
    if (node(id)->parent != newParent && !reparent(id, newParent)) return false;
    auto at = [&](const QUuid& u) { return std::find_if(nodes.begin(), nodes.end(), [&](const Node3D& n) { return n.id == u; }); };
    const Node3D moved = *at(id);
    nodes.erase(at(id));
    if (ref.isNull()) { nodes.push_back(moved); return true; }
    auto it = at(ref);
    nodes.insert(after ? it + 1 : it, moved);
    return true;
}
QUuid Scene3D::firstCamera() const {
    for (const auto& n : nodes) if (n.kind == NodeKind::Camera) return n.id;
    return {};
}
bool Scene3D::worldBounds(QVector3D* mn, QVector3D* mx) const {
    QVector3D lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
    bool any = false;
    for (const auto& n : nodes) {
        if (n.kind != NodeKind::Mesh || !effectivelyVisible(n.id)) continue;
        auto it = meshes.find(n.mesh);
        if (it == meshes.end()) continue;
        QVector3D a, b; it->second->bounds(&a, &b);
        const QMatrix4x4 w = worldMatrix(n.id);
        for (int i = 0; i < 8; ++i) {
            const QVector3D c = w.map(QVector3D(i & 1 ? b.x() : a.x(), i & 2 ? b.y() : a.y(), i & 4 ? b.z() : a.z()));
            lo = QVector3D(std::min(lo.x(), c.x()), std::min(lo.y(), c.y()), std::min(lo.z(), c.z()));
            hi = QVector3D(std::max(hi.x(), c.x()), std::max(hi.y(), c.y()), std::max(hi.z(), c.z()));
            any = true;
        }
    }
    if (!any) return false;
    *mn = lo; *mx = hi;
    return true;
}
void Scene3D::pruneUnused() {
    QSet<QUuid> usedMesh, usedMat, usedTex;
    for (const auto& n : nodes) {
        if (n.kind == NodeKind::Mesh) usedMesh.insert(n.mesh);
        if (!n.materialOverride.isNull()) usedMat.insert(n.materialOverride);
    }
    for (auto it = meshes.begin(); it != meshes.end();) {
        if (!usedMesh.contains(it->first)) { it = meshes.erase(it); continue; }
        for (const auto& p : it->second->parts) usedMat.insert(p.material);
        ++it;
    }
    materials.erase(std::remove_if(materials.begin(), materials.end(), [&](const Material& m) { return !usedMat.contains(m.id); }), materials.end());
    for (const auto& m : materials) if (!m.baseTexture.isNull()) usedTex.insert(m.baseTexture);
    for (auto it = textures.begin(); it != textures.end();) it = usedTex.contains(it->first) ? std::next(it) : textures.erase(it);
}

QStringList Scene3D::validate() const {
    QStringList w;
    for (const auto& n : nodes) if (!n.skin.isNull() && !skinById(n.skin)) w << QString("node '%1' refers to a missing skin").arg(n.name);
    for (const ClipPlacement& p : placements) if (!clip(p.clip)) w << QString("a placed animation clip refers to a missing clip");
    QSet<QUuid> ids;
    for (const auto& n : nodes) { if (ids.contains(n.id)) w << QString("duplicate node id for '%1'").arg(n.name); ids.insert(n.id); }
    for (const auto& n : nodes) {
        if (!n.parent.isNull() && !ids.contains(n.parent)) w << QString("node '%1' has a missing parent").arg(n.name);
        if (n.kind == NodeKind::Mesh && !meshes.count(n.mesh)) w << QString("node '%1' refers to a missing mesh").arg(n.name);
        if (!n.materialOverride.isNull() && !material(n.materialOverride)) w << QString("node '%1' refers to a missing material").arg(n.name);
    }
    for (const auto& n : nodes) {              // cycles
        QUuid cur = n.parent; int guard = 0;
        while (!cur.isNull() && guard++ < 1024) { const Node3D* p = node(cur); if (!p) break; if (p->id == n.id) { w << QString("node '%1' is its own ancestor").arg(n.name); break; } cur = p->parent; }
    }
    if (!activeCamera.isNull() && !node(activeCamera)) w << "active camera does not exist";
    return w;
}

// ============================================================ JSON
namespace {
QJsonArray vec3(const QVector3D& v) { return {v.x(), v.y(), v.z()}; }
bool readVec3(const QJsonValue& v, QVector3D& out) {
    const QJsonArray a = v.toArray();
    if (a.size() != 3) return false;
    out = QVector3D(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()));
    return std::isfinite(out.x()) && std::isfinite(out.y()) && std::isfinite(out.z());
}
QString colorStr(const QColor& c) { return c.name(QColor::HexArgb); }
QColor colorFrom(const QString& s, const QColor& def) { QColor c(s); return c.isValid() ? c : def; }
}  // namespace

QJsonObject Scene3D::toJson() const {
    QJsonObject o;
    QJsonArray na;
    for (const auto& n : nodes) {
        QJsonObject j;
        j["id"] = n.id.toString(QUuid::WithoutBraces);
        if (!n.parent.isNull()) j["parent"] = n.parent.toString(QUuid::WithoutBraces);
        j["name"] = n.name; j["kind"] = nodeKindKey(n.kind); j["visible"] = n.visible;
        j["position"] = vec3(n.position); j["rotation"] = vec3(n.rotation); j["scale"] = vec3(n.scale);
        if (n.kind == NodeKind::Mesh) {
            j["mesh"] = n.mesh.toString(QUuid::WithoutBraces);
            if (!n.materialOverride.isNull()) j["material"] = n.materialOverride.toString(QUuid::WithoutBraces);
            if (!n.skin.isNull()) j["skin"] = n.skin.toString(QUuid::WithoutBraces);
            if (!n.cardLayer.isNull()) { j["cardLayer"] = n.cardLayer.toString(QUuid::WithoutBraces); j["cardLit"] = n.cardLit; }
        } else if (n.kind == NodeKind::Light) {
            j["lightType"] = lightTypeKey(n.lightType); j["color"] = colorStr(n.lightColor); j["intensity"] = n.intensity;
            j["range"] = n.range; j["spotAngle"] = n.spotAngle;
        } else if (n.kind == NodeKind::Camera) {
            j["fov"] = n.fov; j["near"] = n.nearPlane; j["far"] = n.farPlane;
        }
        na.append(j);
    }
    o["nodes"] = na;
    QJsonArray ma;
    for (const auto& m : materials) {
        QJsonObject j;
        j["id"] = m.id.toString(QUuid::WithoutBraces); j["name"] = m.name; j["baseColor"] = colorStr(m.baseColor);
        if (!m.baseTexture.isNull()) j["texture"] = m.baseTexture.toString(QUuid::WithoutBraces);
        j["metallic"] = m.metallic; j["roughness"] = m.roughness; j["emissive"] = colorStr(m.emissive); j["doubleSided"] = m.doubleSided; if (m.unlit) j["unlit"] = true;
        if (m.repeatU != 1.0 || m.repeatV != 1.0) { j["repeatU"] = m.repeatU; j["repeatV"] = m.repeatV; }
        if (m.offsetU != 0.0 || m.offsetV != 0.0) { j["offsetU"] = m.offsetU; j["offsetV"] = m.offsetV; }
        ma.append(j);
    }
    o["materials"] = ma;
    QJsonArray me;
    for (const auto& [id, m] : meshes) me.append(m->toJson());
    o["meshes"] = me;
    QJsonArray te;
    for (const auto& [id, img] : textures) te.append(id.toString(QUuid::WithoutBraces));
    o["textures"] = te;
    if (!activeCamera.isNull()) o["activeCamera"] = activeCamera.toString(QUuid::WithoutBraces);
    if (!skins.empty()) {
        QJsonArray ka;
        for (const Skin& k : skins) {
            QJsonObject j;
            j["id"] = k.id.toString(QUuid::WithoutBraces);
            QJsonArray ja, ma;
            for (const QUuid& u : k.joints) ja.append(u.toString(QUuid::WithoutBraces));
            for (const QMatrix4x4& m : k.inverseBind) for (int i = 0; i < 16; ++i) ma.append(double(m.constData()[i]));
            j["joints"] = ja; j["inverseBind"] = ma;
            ka.append(j);
        }
        o["skins"] = ka;
    }
    if (!clips.empty()) {
        QJsonArray ca;
        for (const AnimClip& c : clips) {
            QJsonObject j;
            j["id"] = c.id.toString(QUuid::WithoutBraces); j["name"] = c.name; j["length"] = c.length; j["tracks"] = tracksToJson(c.tracks);
            ca.append(j);
        }
        o["clips"] = ca;
    }
    if (!placements.empty()) {
        QJsonArray pa;
        for (const ClipPlacement& p : placements) {
            QJsonObject j;
            j["id"] = p.id.toString(QUuid::WithoutBraces); j["clip"] = p.clip.toString(QUuid::WithoutBraces);
            j["start"] = p.start; j["speed"] = p.speed; j["offset"] = p.offset; j["loop"] = p.loop; j["enabled"] = p.enabled;
            pa.append(j);
        }
        o["placements"] = pa;
    }
    o["ambient"] = colorStr(ambient); o["ambientIntensity"] = ambientIntensity; o["background"] = colorStr(background);
    return o;
}

bool Scene3D::fromJson(const QJsonObject& o, Scene3D& out, QString* err) {
    auto fail = [&](const QString& s) { if (err) *err = s; return false; };
    out.nodes.clear(); out.materials.clear();
    for (const auto& v : o["nodes"].toArray()) {
        const QJsonObject j = v.toObject();
        Node3D n;
        n.id = QUuid(j["id"].toString());
        if (n.id.isNull()) return fail("3D node without a valid id");
        n.parent = QUuid(j["parent"].toString());
        n.name = j["name"].toString();
        if (!nodeKindFromKey(j["kind"].toString(), n.kind)) return fail("unknown 3D node kind '" + j["kind"].toString() + "'");
        n.visible = j["visible"].toBool(true);
        if (!readVec3(j["position"], n.position) || !readVec3(j["rotation"], n.rotation) || !readVec3(j["scale"], n.scale))
            return fail("3D node '" + n.name + "' has an invalid transform");
        if (n.kind == NodeKind::Mesh) { n.mesh = QUuid(j["mesh"].toString()); n.materialOverride = QUuid(j["material"].toString()); n.skin = QUuid(j["skin"].toString()); n.cardLayer = QUuid(j["cardLayer"].toString()); n.cardLit = j["cardLit"].toBool(false); }
        else if (n.kind == NodeKind::Light) {
            if (!lightTypeFromKey(j["lightType"].toString("point"), n.lightType)) return fail("unknown light type");
            n.lightColor = colorFrom(j["color"].toString(), Qt::white);
            n.intensity = std::clamp(j["intensity"].toDouble(1.0), 0.0, 1000.0);
            n.range = std::max(0.0, j["range"].toDouble(0));
            n.spotAngle = std::clamp(j["spotAngle"].toDouble(45), 1.0, 179.0);
        } else if (n.kind == NodeKind::Camera) {
            n.fov = std::clamp(j["fov"].toDouble(45), 1.0, 170.0);
            n.nearPlane = std::max(1e-4, j["near"].toDouble(0.1));
            n.farPlane = std::max(n.nearPlane * 1.01, j["far"].toDouble(1000));
        }
        out.nodes.push_back(n);
    }
    for (const auto& v : o["materials"].toArray()) {
        const QJsonObject j = v.toObject();
        Material m;
        m.id = QUuid(j["id"].toString());
        if (m.id.isNull()) return fail("material without a valid id");
        m.name = j["name"].toString("Material");
        m.baseColor = colorFrom(j["baseColor"].toString(), QColor(200, 200, 200));
        m.baseTexture = QUuid(j["texture"].toString());
        m.metallic = std::clamp(j["metallic"].toDouble(0), 0.0, 1.0);
        m.roughness = std::clamp(j["roughness"].toDouble(0.6), 0.0, 1.0);
        m.emissive = colorFrom(j["emissive"].toString(), Qt::black);
        m.doubleSided = j["doubleSided"].toBool(false);
        m.unlit = j["unlit"].toBool(false);
        auto finite = [](double v, double def, double lo, double hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : def; };
        m.repeatU = finite(j["repeatU"].toDouble(1.0), 1.0, -10000.0, 10000.0);
        m.repeatV = finite(j["repeatV"].toDouble(1.0), 1.0, -10000.0, 10000.0);
        m.offsetU = finite(j["offsetU"].toDouble(0.0), 0.0, -10000.0, 10000.0);
        m.offsetV = finite(j["offsetV"].toDouble(0.0), 0.0, -10000.0, 10000.0);
        out.materials.push_back(m);
    }
    out.clips.clear(); out.placements.clear(); out.skins.clear();
    {
        const QJsonArray ka = o["skins"].toArray();
        if (ka.size() > 4096) return fail("too many skins");
        for (const auto& v : ka) {
            const QJsonObject j = v.toObject();
            Skin k;
            k.id = QUuid(j["id"].toString());
            if (k.id.isNull()) return fail("skin without a valid id");
            const QJsonArray ja = j["joints"].toArray(), ma = j["inverseBind"].toArray();
            if (ja.size() > 65535 || ma.size() != ja.size() * 16) return fail("skin has inconsistent joint data");
            for (const auto& u : ja) k.joints.push_back(QUuid(u.toString()));
            for (int mi = 0; mi < ja.size(); ++mi) {
                QMatrix4x4 m;
                float* d = m.data();
                for (int i = 0; i < 16; ++i) { const double x = ma[mi * 16 + i].toDouble(i % 5 == 0 ? 1 : 0); d[i] = std::isfinite(x) ? float(x) : (i % 5 == 0 ? 1.0f : 0.0f); }
                k.inverseBind.push_back(m);
            }
            out.skins.push_back(std::move(k));
        }
    }
    {
        const QJsonArray ca = o["clips"].toArray();
        if (ca.size() > 1024) return fail("too many animation clips");
        for (const auto& v : ca) {
            const QJsonObject j = v.toObject();
            AnimClip c;
            c.id = QUuid(j["id"].toString());
            if (c.id.isNull()) return fail("animation clip without a valid id");
            c.name = j["name"].toString();
            c.length = std::clamp(j["length"].toInt(0), 0, 10000000);
            QString terr;
            if (!tracksFromJson(j["tracks"].toObject(), c.tracks, &terr)) return fail("animation clip '" + c.name + "': " + terr);
            out.clips.push_back(std::move(c));
        }
        const QJsonArray pa = o["placements"].toArray();
        if (pa.size() > 100000) return fail("too many animation clip placements");
        for (const auto& v : pa) {
            const QJsonObject j = v.toObject();
            ClipPlacement p;
            p.id = QUuid(j["id"].toString());
            p.clip = QUuid(j["clip"].toString());
            if (p.id.isNull()) p.id = QUuid::createUuid();
            p.start = std::clamp(j["start"].toInt(0), -10000000, 10000000);
            const double sp = j["speed"].toDouble(1.0);
            p.speed = std::isfinite(sp) ? std::clamp(sp, 0.01, 100.0) : 1.0;
            p.offset = std::clamp(j["offset"].toInt(0), -10000000, 10000000);
            p.loop = j["loop"].toBool(false);
            p.enabled = j["enabled"].toBool(true);
            out.placements.push_back(p);
        }
    }
    out.activeCamera = QUuid(o["activeCamera"].toString());
    out.ambient = colorFrom(o["ambient"].toString(), Qt::white);
    out.ambientIntensity = std::clamp(o["ambientIntensity"].toDouble(0.25), 0.0, 10.0);
    out.background = colorFrom(o["background"].toString(), QColor(0, 0, 0, 0));
    out.touch();
    return true;
}

// ============================================================ primitives
namespace {
std::shared_ptr<Mesh> finish(std::shared_ptr<Mesh> m, const QString& name) {
    m->id = QUuid::createUuid();
    m->name = name;
    m->parts = {{0, int(m->indices.size()), QUuid()}};
    return m;
}
}  // namespace

std::shared_ptr<Mesh> makeCube(double size) {
    auto m = std::make_shared<Mesh>();
    const float h = float(size / 2);
    struct Face { QVector3D n, u, v; };
    const Face faces[6] = {{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}, {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
                           {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}};
    for (const Face& f : faces) {
        const uint32_t b = uint32_t(m->positions.size());
        const QVector3D c = f.n * h;
        m->positions.push_back(c - f.u * h - f.v * h); m->uvs.push_back({0, 0});
        m->positions.push_back(c + f.u * h - f.v * h); m->uvs.push_back({1, 0});
        m->positions.push_back(c + f.u * h + f.v * h); m->uvs.push_back({1, 1});
        m->positions.push_back(c - f.u * h + f.v * h); m->uvs.push_back({0, 1});
        for (int i = 0; i < 4; ++i) m->normals.push_back(f.n);
        for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) m->indices.push_back(b + i);
    }
    return finish(m, "Cube");
}

std::shared_ptr<Mesh> makeSphere(double radius, int segments, int rings) {
    auto m = std::make_shared<Mesh>();
    segments = std::max(3, segments); rings = std::max(2, rings);
    for (int r = 0; r <= rings; ++r) {
        const double v = double(r) / rings, phi = v * M_PI;
        for (int s = 0; s <= segments; ++s) {
            const double u = double(s) / segments, th = u * 2 * M_PI;
            const QVector3D n(float(std::sin(phi) * std::cos(th)), float(std::cos(phi)), float(-std::sin(phi) * std::sin(th)));
            m->positions.push_back(n * float(radius)); m->normals.push_back(n); m->uvs.push_back({float(u), float(1 - v)});
        }
    }
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < segments; ++s) {
            const uint32_t a = uint32_t(r * (segments + 1) + s), b = a + uint32_t(segments + 1);
            for (uint32_t i : {a, a + 1, b, a + 1, b + 1, b}) m->indices.push_back(i);
        }
    // winding: make triangles face outwards
    for (size_t i = 0; i + 2 < m->indices.size(); i += 3) {
        const QVector3D n = QVector3D::crossProduct(m->positions[m->indices[i + 1]] - m->positions[m->indices[i]], m->positions[m->indices[i + 2]] - m->positions[m->indices[i]]);
        if (QVector3D::dotProduct(n, m->normals[m->indices[i]]) < 0) std::swap(m->indices[i + 1], m->indices[i + 2]);
    }
    return finish(m, "Sphere");
}

std::shared_ptr<Mesh> makePlane(double size) {
    auto m = std::make_shared<Mesh>();
    const float h = float(size / 2);
    m->positions = {{-h, 0, h}, {h, 0, h}, {h, 0, -h}, {-h, 0, -h}};
    m->normals.assign(4, QVector3D(0, 1, 0));
    m->uvs = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    m->indices = {0, 1, 2, 0, 2, 3};
    return finish(m, "Plane");
}

std::shared_ptr<Mesh> makeCardPlane(double width, double height) {
    auto m = std::make_shared<Mesh>();
    const float w = float(width / 2), h = float(height / 2);
    m->positions = {{-w, -h, 0}, {w, -h, 0}, {w, h, 0}, {-w, h, 0}};
    m->normals.assign(4, QVector3D(0, 0, 1));
    m->uvs = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    m->indices = {0, 1, 2, 0, 2, 3};
    return finish(m, "Card");
}

std::shared_ptr<Mesh> makeCylinder(double radius, double height, int segments) {
    auto m = std::make_shared<Mesh>();
    segments = std::max(3, segments);
    const float h = float(height / 2), r = float(radius);
    for (int s = 0; s <= segments; ++s) {         // side
        const double u = double(s) / segments, th = u * 2 * M_PI;
        const QVector3D n(float(std::cos(th)), 0, float(-std::sin(th)));
        m->positions.push_back(n * r + QVector3D(0, -h, 0)); m->normals.push_back(n); m->uvs.push_back({float(u), 0});
        m->positions.push_back(n * r + QVector3D(0, h, 0)); m->normals.push_back(n); m->uvs.push_back({float(u), 1});
    }
    for (int s = 0; s < segments; ++s) {
        const uint32_t a = uint32_t(s * 2);
        for (uint32_t i : {a, a + 2, a + 1, a + 1, a + 2, a + 3}) m->indices.push_back(i);
    }
    for (int cap = 0; cap < 2; ++cap) {           // caps
        const float y = cap ? h : -h;
        const QVector3D n(0, cap ? 1.f : -1.f, 0);
        const uint32_t c = uint32_t(m->positions.size());
        m->positions.push_back(QVector3D(0, y, 0)); m->normals.push_back(n); m->uvs.push_back({0.5f, 0.5f});
        for (int s = 0; s <= segments; ++s) {
            const double th = double(s) / segments * 2 * M_PI;
            m->positions.push_back(QVector3D(float(std::cos(th)) * r, y, float(-std::sin(th)) * r)); m->normals.push_back(n);
            m->uvs.push_back({float(0.5 + 0.5 * std::cos(th)), float(0.5 + 0.5 * std::sin(th))});
        }
        for (int s = 0; s < segments; ++s) {
            if (cap) { m->indices.push_back(c); m->indices.push_back(c + uint32_t(s) + 2); m->indices.push_back(c + uint32_t(s) + 1); }
            else { m->indices.push_back(c); m->indices.push_back(c + uint32_t(s) + 1); m->indices.push_back(c + uint32_t(s) + 2); }
        }
    }
    for (size_t i = 0; i + 2 < m->indices.size(); i += 3) {    // outward winding
        const QVector3D n = QVector3D::crossProduct(m->positions[m->indices[i + 1]] - m->positions[m->indices[i]], m->positions[m->indices[i + 2]] - m->positions[m->indices[i]]);
        if (QVector3D::dotProduct(n, m->normals[m->indices[i]]) < 0) std::swap(m->indices[i + 1], m->indices[i + 2]);
    }
    return finish(m, "Cylinder");
}

}  // namespace mynd
