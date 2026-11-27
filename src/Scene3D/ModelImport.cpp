#include "Scene3D/ModelImport.h"
#include "Scene3D/Anim3D.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <algorithm>

namespace mynd {

QStringList supportedModelExtensions() { return {"glb", "gltf", "obj", "fbx"}; }
QString modelFileFilter() {
    return QObject::tr("3D models (*.glb *.gltf *.obj *.fbx);;glTF / GLB (*.glb *.gltf);;Wavefront OBJ (*.obj);;Autodesk FBX (*.fbx)");
}

qint64 maxModelFileBytes() { return qint64(64) << 30; }   // sanity limit only; the file is memory-mapped, so what matters is free memory for the decoded model

static thread_local ImportControl* t_control = nullptr;
void setImportControl(ImportControl* c) { t_control = c; }
bool importCancelled() { return t_control && t_control->cancel.load(std::memory_order_relaxed); }
void importProgress(int percent) { if (t_control) t_control->percent.store(std::clamp(percent, 0, 100), std::memory_order_relaxed); }

namespace {
struct ControlScope { explicit ControlScope(ImportControl* c) { setImportControl(c); } ~ControlScope() { setImportControl(nullptr); } };
}

ImportResult importModelFile(const QString& path, double fps, ImportControl* control) {
    ControlScope scope(control);
    ImportResult r;
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) { r.error = QObject::tr("File not found: %1").arg(path); return r; }
    if (fi.size() > maxModelFileBytes()) { r.error = QObject::tr("The file is larger than 64 GB, which this importer does not accept."); return r; }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { r.error = QObject::tr("Cannot read %1: %2").arg(path, f.errorString()); return r; }
    // Memory-map the file instead of copying it into a QByteArray: no second copy, and the OS pages it in as the importer reads.
    QByteArray data;
    if (uchar* mapped = f.map(0, f.size())) data = QByteArray::fromRawData(reinterpret_cast<const char*>(mapped), f.size());
    else data = f.readAll();                                  // (some file systems cannot be mapped)
    const QString ext = fi.suffix().toLower(), base = fi.completeBaseName(), dir = fi.absolutePath();
    if (ext == "obj") return importObjData(data, dir, base);
    if (ext == "glb" || ext == "gltf") return importGltfData(data, dir, base, fps);
    if (ext == "fbx") return importFbxData(data, dir, base);
    r.error = QObject::tr("Unsupported model format '.%1'. Supported: glTF/GLB, OBJ, FBX.").arg(ext);
    return r;
}

QUuid mergeImport(Scene3D& dst, const Scene3D& src, const QUuid& parent) {
    QHash<QUuid, QUuid> nodeMap, meshMap, matMap, texMap;
    for (const auto& [id, img] : src.textures) { const QUuid n = QUuid::createUuid(); texMap[id] = n; dst.textures[n] = img; }
    for (const auto& m : src.materials) {
        Material c = m;
        c.id = QUuid::createUuid();
        if (texMap.contains(m.baseTexture)) c.baseTexture = texMap[m.baseTexture];
        matMap[m.id] = c.id;
        dst.materials.push_back(c);
    }
    for (const auto& [id, m] : src.meshes) {
        auto copy = std::make_shared<Mesh>(*m);
        copy->id = QUuid::createUuid();
        for (auto& p : copy->parts) if (matMap.contains(p.material)) p.material = matMap[p.material];
        meshMap[id] = copy->id;
        dst.meshes[copy->id] = copy;
    }
    for (const auto& n : src.nodes) nodeMap[n.id] = QUuid::createUuid();
    // animation clips follow the renumbered nodes; every clip is kept, the first one is placed looping from frame 0
    QHash<QUuid, QUuid> clipMap;
    for (const AnimClip& ac : src.clips) {
        AnimClip c = ac;
        c.id = QUuid::createUuid();
        c.tracks.clear();
        for (auto it = ac.tracks.begin(); it != ac.tracks.end(); ++it) {
            PropTarget t;
            if (!parseScene3DPropId(it.key(), &t) || t.material || !nodeMap.contains(t.target)) continue;
            c.tracks.insert(nodePropId(nodeMap[t.target], t.what), it.value());
        }
        clipMap[ac.id] = c.id;
        dst.clips.push_back(c);
    }
    QHash<QUuid, QUuid> skinMap;
    for (const Skin& sk : src.skins) {
        Skin c = sk;
        c.id = QUuid::createUuid();
        for (QUuid& j : c.joints) if (nodeMap.contains(j)) j = nodeMap[j];
        skinMap[sk.id] = c.id;
        dst.skins.push_back(std::move(c));
    }
    bool placed = false;
    for (const ClipPlacement& sp : src.placements) {
        if (!clipMap.contains(sp.clip)) continue;
        ClipPlacement p = sp;
        p.id = QUuid::createUuid();
        p.clip = clipMap[sp.clip];
        dst.placements.push_back(p);
        placed = true;
    }
    Q_UNUSED(placed);
    QUuid root;
    for (const auto& n : src.nodes) {
        Node3D c = n;
        c.id = nodeMap[n.id];
        if (nodeMap.contains(n.parent)) c.parent = nodeMap[n.parent];
        else { c.parent = parent; if (root.isNull()) root = c.id; }
        if (meshMap.contains(n.mesh)) c.mesh = meshMap[n.mesh];
        if (matMap.contains(n.materialOverride)) c.materialOverride = matMap[n.materialOverride];
        if (skinMap.contains(n.skin)) c.skin = skinMap[n.skin]; else c.skin = QUuid();
        c.name = n.name;
        dst.nodes.push_back(c);
        if (c.kind == NodeKind::Camera && dst.activeCamera.isNull()) dst.activeCamera = c.id;
    }
    return root;
}

}  // namespace mynd
