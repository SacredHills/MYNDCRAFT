#include "Scene3D/ModelImport.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>

namespace mynd {

QStringList supportedModelExtensions() { return {"glb", "gltf", "obj", "fbx"}; }
QString modelFileFilter() {
    return QObject::tr("3D models (*.glb *.gltf *.obj *.fbx);;glTF / GLB (*.glb *.gltf);;Wavefront OBJ (*.obj);;Autodesk FBX (*.fbx)");
}

ImportResult importModelFile(const QString& path) {
    ImportResult r;
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) { r.error = QObject::tr("File not found: %1").arg(path); return r; }
    if (fi.size() > (1LL << 30)) { r.error = QObject::tr("The file is larger than 1 GB, which this importer does not accept."); return r; }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { r.error = QObject::tr("Cannot read %1: %2").arg(path, f.errorString()); return r; }
    const QByteArray data = f.readAll();
    const QString ext = fi.suffix().toLower(), base = fi.completeBaseName(), dir = fi.absolutePath();
    if (ext == "obj") return importObjData(data, dir, base);
    if (ext == "glb" || ext == "gltf") return importGltfData(data, dir, base);
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
    QUuid root;
    for (const auto& n : src.nodes) {
        Node3D c = n;
        c.id = nodeMap[n.id];
        if (nodeMap.contains(n.parent)) c.parent = nodeMap[n.parent];
        else { c.parent = parent; if (root.isNull()) root = c.id; }
        if (meshMap.contains(n.mesh)) c.mesh = meshMap[n.mesh];
        if (matMap.contains(n.materialOverride)) c.materialOverride = matMap[n.materialOverride];
        c.name = n.name;
        dst.nodes.push_back(c);
        if (c.kind == NodeKind::Camera && dst.activeCamera.isNull()) dst.activeCamera = c.id;
    }
    return root;
}

}  // namespace mynd
