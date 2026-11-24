// glTF 2.0 importer (.gltf with embedded or external buffers, and binary .glb).
#include <memory>
#include <QDir>
#include <functional>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QUrl>
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstring>

#include "Scene3D/ModelImport.h"
#include "Scene3D/Anim3D.h"

namespace mynd {

namespace {

constexpr qint64 kMaxBuffer = 64LL << 30;

struct Ctx {
    QJsonObject root;
    QString baseDir;
    std::vector<QByteArray> buffers;
    QByteArray glbBin;
    bool hasGlbBin = false;
    QStringList* warnings;
};

QByteArray loadUri(const QString& uri, const QString& baseDir, QString* err) {
    static thread_local std::vector<std::unique_ptr<QFile>> keepMapped;   // mapped external buffers stay mapped for the rest of this thread's imports (small)
    if (keepMapped.size() > 64) keepMapped.clear();
    if (uri.startsWith("data:")) {
        const int comma = uri.indexOf(',');
        if (comma < 0) { *err = "malformed data URI"; return {}; }
        if (!uri.left(comma).contains(";base64")) { *err = "only base64 data URIs are supported"; return {}; }
        return QByteArray::fromBase64(uri.mid(comma + 1).toLatin1());
    }
    const QString rel = QUrl::fromPercentEncoding(uri.toUtf8());
    if (QDir::isAbsolutePath(rel) || rel.contains("..")) { *err = "external file '" + uri + "' is outside the model folder"; return {}; }
    QFile f(QDir(baseDir).filePath(rel));
    if (!f.open(QIODevice::ReadOnly)) { *err = "cannot open external file '" + uri + "'"; return {}; }
    if (f.size() > kMaxBuffer) { *err = "external file too large"; return {}; }
    auto file = std::make_unique<QFile>(f.fileName());
    if (file->open(QIODevice::ReadOnly)) if (uchar* m = file->map(0, file->size())) { QByteArray v = QByteArray::fromRawData(reinterpret_cast<const char*>(m), file->size()); keepMapped.push_back(std::move(file)); return v; }
    return f.readAll();
}

int compSize(int ct) {
    switch (ct) { case 5120: case 5121: return 1; case 5122: case 5123: return 2; case 5125: case 5126: return 4; }
    return 0;
}
int typeCount(const QString& t) {
    if (t == "SCALAR") return 1;
    if (t == "VEC2") return 2;
    if (t == "VEC3") return 3;
    if (t == "VEC4") return 4;
    if (t == "MAT2") return 4;
    if (t == "MAT3") return 9;
    if (t == "MAT4") return 16;
    return 0;
}

// Reads accessor `index` as floats (or raw integers when asIndex). Returns false with a reason when anything is out of range.
bool readAccessor(const Ctx& c, int index, std::vector<double>& out, int* comps, QString* why) {
    const QJsonArray accs = c.root["accessors"].toArray();
    if (index < 0 || index >= accs.size()) { *why = "accessor index out of range"; return false; }
    const QJsonObject a = accs[index].toObject();
    const int ct = a["componentType"].toInt(), n = typeCount(a["type"].toString()), cs = compSize(ct);
    const qint64 count = qint64(a["count"].toDouble(-1));
    if (!cs || !n || count < 0 || count > 100000000) { *why = "invalid accessor"; return false; }
    *comps = n;
    out.assign(size_t(count) * size_t(n), 0.0);
    if (a.contains("sparse")) { *why = "sparse accessors are not supported"; return false; }
    if (!a.contains("bufferView")) return true;                       // all zeros by spec
    const QJsonArray views = c.root["bufferViews"].toArray();
    const int vi = a["bufferView"].toInt(-1);
    if (vi < 0 || vi >= views.size()) { *why = "bufferView index out of range"; return false; }
    const QJsonObject v = views[vi].toObject();
    const int bi = v["buffer"].toInt(-1);
    if (bi < 0 || size_t(bi) >= c.buffers.size()) { *why = "buffer index out of range"; return false; }
    const QByteArray& buf = c.buffers[size_t(bi)];
    const qint64 viewOff = qint64(v["byteOffset"].toDouble(0)), viewLen = qint64(v["byteLength"].toDouble(0));
    qint64 stride = qint64(v["byteStride"].toDouble(0));
    const qint64 elem = qint64(cs) * n;
    if (stride == 0) stride = elem;
    if (stride < elem || viewOff < 0 || viewLen < 0 || viewOff + viewLen > buf.size()) { *why = "bufferView exceeds its buffer"; return false; }
    const qint64 accOff = qint64(a["byteOffset"].toDouble(0));
    if (accOff < 0) { *why = "negative accessor offset"; return false; }
    if (count > 0 && accOff + (count - 1) * stride + elem > viewLen) { *why = "accessor exceeds its bufferView"; return false; }
    const bool norm = a["normalized"].toBool();
    const char* base = buf.constData() + viewOff + accOff;
    for (qint64 i = 0; i < count; ++i) {
        const char* p = base + i * stride;
        for (int k = 0; k < n; ++k) {
            const char* q = p + qint64(k) * cs;
            double v2 = 0;
            switch (ct) {
            case 5120: { qint8 x; std::memcpy(&x, q, 1); v2 = norm ? std::max(x / 127.0, -1.0) : x; break; }
            case 5121: { quint8 x; std::memcpy(&x, q, 1); v2 = norm ? x / 255.0 : x; break; }
            case 5122: { qint16 x; std::memcpy(&x, q, 2); v2 = norm ? std::max(x / 32767.0, -1.0) : x; break; }
            case 5123: { quint16 x; std::memcpy(&x, q, 2); v2 = norm ? x / 65535.0 : x; break; }
            case 5125: { quint32 x; std::memcpy(&x, q, 4); v2 = x; break; }
            case 5126: { float x; std::memcpy(&x, q, 4); v2 = x; break; }
            }
            out[size_t(i) * size_t(n) + size_t(k)] = v2;
        }
    }
    return true;
}

double toSrgb(double c) { c = std::clamp(c, 0.0, 1.0); return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1 / 2.4) - 0.055; }

}  // namespace

ImportResult importGltfData(const QByteArray& data, const QString& baseDir, const QString& name, double fps) {
    ImportResult r;
    Ctx c; c.baseDir = baseDir; c.warnings = &r.warnings;
    QByteArray jsonData = data;
    if (data.size() >= 12 && data.left(4) == "glTF") {                          // binary container
        quint32 version, total;
        std::memcpy(&version, data.constData() + 4, 4); std::memcpy(&total, data.constData() + 8, 4);
        if (version != 2) { r.error = QObject::tr("Only glTF 2.0 files are supported (this is version %1).").arg(version); return r; }
        qint64 pos = 12;
        bool haveJson = false;
        while (pos + 8 <= data.size()) {
            quint32 len, type;
            std::memcpy(&len, data.constData() + pos, 4); std::memcpy(&type, data.constData() + pos + 4, 4);
            pos += 8;
            if (qint64(len) > data.size() - pos) { r.error = QObject::tr("The GLB file is truncated."); return r; }
            if (type == 0x4E4F534A && !haveJson) { jsonData = QByteArray::fromRawData(data.constData() + pos, len); haveJson = true; }
            else if (type == 0x004E4942 && !c.hasGlbBin) { c.glbBin = QByteArray::fromRawData(data.constData() + pos, len); c.hasGlbBin = true; }
            pos += (len + 3) & ~3u;
        }
        if (!haveJson) { r.error = QObject::tr("The GLB file has no JSON chunk."); return r; }
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &pe);
    if (doc.isNull() || !doc.isObject()) { r.error = QObject::tr("Not a valid glTF file: %1").arg(pe.errorString()); return r; }
    c.root = doc.object();
    if (!c.root["asset"].toObject()["version"].toString().startsWith("2")) { r.error = QObject::tr("Only glTF 2.0 files are supported."); return r; }
    for (const auto& e : c.root["extensionsRequired"].toArray()) {
        const QString ext = e.toString();
        if (ext == "KHR_draco_mesh_compression" || ext == "EXT_meshopt_compression") {
            r.error = QObject::tr("This model uses %1 compression, which is not supported. Re-export it without compression.").arg(ext);
            return r;
        }
    }

    // buffers
    int bi = 0;
    for (const auto& bv : c.root["buffers"].toArray()) {
        const QJsonObject b = bv.toObject();
        QByteArray bytes; QString err;
        if (b.contains("uri")) bytes = loadUri(b["uri"].toString(), baseDir, &err);
        else if (bi == 0 && c.hasGlbBin) bytes = c.glbBin;
        else err = "no data";
        if (!err.isEmpty()) { r.error = QObject::tr("Buffer %1: %2").arg(bi).arg(err); return r; }
        if (bytes.size() < qint64(b["byteLength"].toDouble(0))) { r.error = QObject::tr("Buffer %1 is shorter than declared.").arg(bi); return r; }
        c.buffers.push_back(bytes);
        ++bi;
    }

    auto scene = std::make_shared<Scene3D>();

    // images -> textures
    const QJsonArray images = c.root["images"].toArray(), texs = c.root["textures"].toArray();
    std::vector<QUuid> imageTex(size_t(images.size()));
    std::vector<bool> imageTried(size_t(images.size()), false);
    auto textureFor = [&](int texIndex) -> QUuid {
        if (texIndex < 0 || texIndex >= texs.size()) return {};
        const int src = texs[texIndex].toObject()["source"].toInt(-1);
        if (src < 0 || src >= images.size()) return {};
        if (imageTried[size_t(src)]) return imageTex[size_t(src)];
        imageTried[size_t(src)] = true;
        const QJsonObject io = images[src].toObject();
        QByteArray bytes; QString err;
        if (io.contains("uri")) bytes = loadUri(io["uri"].toString(), baseDir, &err);
        else if (io.contains("bufferView")) {
            const QJsonArray views = c.root["bufferViews"].toArray();
            const int vi = io["bufferView"].toInt(-1);
            if (vi < 0 || vi >= views.size()) err = "bad bufferView";
            else {
                const QJsonObject v = views[vi].toObject();
                const int b = v["buffer"].toInt(-1);
                const qint64 off = qint64(v["byteOffset"].toDouble(0)), len = qint64(v["byteLength"].toDouble(0));
                if (b < 0 || size_t(b) >= c.buffers.size() || off < 0 || len < 0 || off + len > c.buffers[size_t(b)].size()) err = "bufferView out of range";
                else bytes = c.buffers[size_t(b)].mid(off, len);
            }
        } else err = "no data";
        QImage img;
        if (err.isEmpty() && !img.loadFromData(bytes)) err = "unsupported image format";
        if (!err.isEmpty() || img.isNull()) { r.warnings << QObject::tr("Texture %1 could not be loaded (%2); the surface is drawn untextured.").arg(src).arg(err); return {}; }
        const QUuid tid = QUuid::createUuid();
        scene->textures[tid] = img.convertToFormat(QImage::Format_ARGB32);
        imageTex[size_t(src)] = tid;
        return tid;
    };

    // materials
    std::vector<QUuid> matIds;
    for (const auto& mv : c.root["materials"].toArray()) {
        const QJsonObject mo = mv.toObject();
        const QJsonObject pbr = mo["pbrMetallicRoughness"].toObject();
        Material m;
        m.id = QUuid::createUuid();
        m.name = mo["name"].toString("Material");
        const QJsonArray bc = pbr["baseColorFactor"].toArray();
        if (bc.size() == 4) m.baseColor = QColor::fromRgbF(toSrgb(bc[0].toDouble(1)), toSrgb(bc[1].toDouble(1)), toSrgb(bc[2].toDouble(1)), std::clamp(bc[3].toDouble(1), 0.0, 1.0));
        else m.baseColor = Qt::white;
        m.metallic = std::clamp(pbr["metallicFactor"].toDouble(1.0), 0.0, 1.0);
        m.roughness = std::clamp(pbr["roughnessFactor"].toDouble(1.0), 0.0, 1.0);
        const QJsonObject bt = pbr["baseColorTexture"].toObject();
        if (bt.contains("index")) m.baseTexture = textureFor(bt["index"].toInt(-1));
        const QJsonArray em = mo["emissiveFactor"].toArray();
        if (em.size() == 3) m.emissive = QColor::fromRgbF(toSrgb(em[0].toDouble()), toSrgb(em[1].toDouble()), toSrgb(em[2].toDouble()));
        m.doubleSided = mo["doubleSided"].toBool(false);
        const QString am = mo["alphaMode"].toString("OPAQUE");
        if (am == "OPAQUE") { QColor col = m.baseColor; col.setAlphaF(1.0); m.baseColor = col; }
        if (mo.contains("normalTexture") || mo.contains("occlusionTexture") || pbr.contains("metallicRoughnessTexture") || mo.contains("emissiveTexture"))
            r.warnings << QObject::tr("Material '%1': only base colour textures are used (normal, metal/roughness, occlusion and emissive maps are ignored).").arg(m.name);
        scene->materials.push_back(m);
        matIds.push_back(m.id);
    }
    auto matId = [&](int i) -> QUuid { return i >= 0 && size_t(i) < matIds.size() ? matIds[size_t(i)] : QUuid(); };

    // meshes
    const QJsonArray meshes = c.root["meshes"].toArray();
    std::vector<QUuid> meshIds(size_t(meshes.size()));
    int skippedPrims = 0;
    for (int mi = 0; mi < meshes.size(); ++mi) {
        const QJsonObject mo = meshes[mi].toObject();
        auto mesh = std::make_shared<Mesh>();
        mesh->id = QUuid::createUuid();
        mesh->name = mo["name"].toString(QString("Mesh %1").arg(mi));
        struct PrimOut { std::vector<QVector3D> p, n; std::vector<QVector2D> t; std::vector<uint32_t> idx; bool hasN, hasT; bool hasS = false; std::vector<std::array<uint16_t, 4>> j; std::vector<std::array<float, 4>> w; QUuid mat; };
        std::vector<PrimOut> prims;
        for (const auto& pv : mo["primitives"].toArray()) {
            const QJsonObject po = pv.toObject();
            const int mode = po["mode"].toInt(4);
            if (mode != 4 && mode != 5 && mode != 6) { ++skippedPrims; continue; }
            const QJsonObject at = po["attributes"].toObject();
            if (!at.contains("POSITION")) { ++skippedPrims; continue; }
            std::vector<double> pos, nor, uv, ind; int pc = 0, nc = 0, uc = 0, ic = 0; QString why;
            if (!readAccessor(c, at["POSITION"].toInt(-1), pos, &pc, &why) || pc != 3) { r.warnings << QObject::tr("Mesh '%1': %2").arg(mesh->name, why.isEmpty() ? "bad positions" : why); ++skippedPrims; continue; }
            PrimOut o;
            o.hasN = at.contains("NORMAL") && readAccessor(c, at["NORMAL"].toInt(-1), nor, &nc, &why) && nc == 3 && nor.size() == pos.size();
            o.hasT = at.contains("TEXCOORD_0") && readAccessor(c, at["TEXCOORD_0"].toInt(-1), uv, &uc, &why) && uc == 2 && uv.size() / 2 == pos.size() / 3;
            const size_t vc = pos.size() / 3;
            if (at.contains("JOINTS_0") && at.contains("WEIGHTS_0")) {
                std::vector<double> jv, wv; int jc = 0, wc = 0; QString why2;
                if (readAccessor(c, at["JOINTS_0"].toInt(-1), jv, &jc, &why2) && jc == 4 && jv.size() == vc * 4 &&
                    readAccessor(c, at["WEIGHTS_0"].toInt(-1), wv, &wc, &why2) && wc == 4 && wv.size() == vc * 4) {
                    bool ok = true;
                    o.j.resize(vc); o.w.resize(vc);
                    for (size_t i = 0; i < vc && ok; ++i) {
                        float sum = 0;
                        for (size_t k = 0; k < 4; ++k) {
                            const double jj = jv[i * 4 + k], ww = wv[i * 4 + k];
                            if (!(jj >= 0 && jj <= 65535)) { ok = false; break; }
                            o.j[i][k] = uint16_t(jj);
                            o.w[i][k] = std::isfinite(ww) ? float(std::clamp(ww, 0.0, 1.0)) : 0.0f;
                            sum += o.w[i][k];
                        }
                        if (sum > 1e-6f) for (size_t k = 0; k < 4; ++k) o.w[i][k] /= sum; else { o.j[i] = {0, 0, 0, 0}; o.w[i] = {1, 0, 0, 0}; }
                    }
                    o.hasS = ok;
                }
                if (!o.hasS) r.warnings << QObject::tr("Mesh '%1': skin weights could not be read; it is shown undeformed.").arg(mesh->name);
            }
            for (size_t i = 0; i < vc; ++i) o.p.push_back({float(pos[i * 3]), float(pos[i * 3 + 1]), float(pos[i * 3 + 2])});
            if (o.hasN) for (size_t i = 0; i < vc; ++i) o.n.push_back(QVector3D(float(nor[i * 3]), float(nor[i * 3 + 1]), float(nor[i * 3 + 2])).normalized());
            if (o.hasT) for (size_t i = 0; i < vc; ++i) o.t.push_back({float(uv[i * 2]), float(1.0 - uv[i * 2 + 1])});     // glTF v is down, ours is up
            std::vector<uint32_t> raw;
            if (po.contains("indices")) {
                if (!readAccessor(c, po["indices"].toInt(-1), ind, &ic, &why) || ic != 1) { r.warnings << QObject::tr("Mesh '%1': %2").arg(mesh->name, why.isEmpty() ? "bad indices" : why); ++skippedPrims; continue; }
                for (double d : ind) raw.push_back(uint32_t(d));
            } else for (size_t i = 0; i < vc; ++i) raw.push_back(uint32_t(i));
            bool bad = false;
            for (uint32_t i : raw) if (i >= vc) { bad = true; break; }
            if (bad) { r.warnings << QObject::tr("Mesh '%1': a primitive has indices outside its vertex range and was skipped.").arg(mesh->name); ++skippedPrims; continue; }
            if (mode == 4) { raw.resize(raw.size() / 3 * 3); o.idx = raw; }
            else for (size_t i = 2; i < raw.size(); ++i) {
                if (mode == 5) { if (i % 2 == 0) o.idx.insert(o.idx.end(), {raw[i - 2], raw[i - 1], raw[i]}); else o.idx.insert(o.idx.end(), {raw[i - 1], raw[i - 2], raw[i]}); }
                else o.idx.insert(o.idx.end(), {raw[0], raw[i - 1], raw[i]});
            }
            o.mat = matId(po["material"].toInt(-1));
            prims.push_back(std::move(o));
        }
        bool anyT = false, anyS = false;
        for (const auto& p : prims) { anyT |= p.hasT; anyS |= p.hasS; }
        for (auto& p : prims) {
            const uint32_t base = uint32_t(mesh->positions.size());
            Mesh tmp; tmp.positions = p.p; tmp.indices = p.idx;
            if (!p.hasN) { tmp.computeNormals(); p.n = tmp.normals; }
            mesh->positions.insert(mesh->positions.end(), p.p.begin(), p.p.end());
            mesh->normals.insert(mesh->normals.end(), p.n.begin(), p.n.end());
            if (anyS) {
                if (p.hasS) { mesh->joints.insert(mesh->joints.end(), p.j.begin(), p.j.end()); mesh->weights.insert(mesh->weights.end(), p.w.begin(), p.w.end()); }
                else { mesh->joints.insert(mesh->joints.end(), p.p.size(), std::array<uint16_t, 4>{0, 0, 0, 0}); mesh->weights.insert(mesh->weights.end(), p.p.size(), std::array<float, 4>{1, 0, 0, 0}); }
            }
            if (anyT) { if (p.hasT) mesh->uvs.insert(mesh->uvs.end(), p.t.begin(), p.t.end()); else mesh->uvs.insert(mesh->uvs.end(), p.p.size(), QVector2D(0, 0)); }
            MeshPart part; part.firstIndex = int(mesh->indices.size()); part.indexCount = int(p.idx.size()); part.material = p.mat;
            for (uint32_t i : p.idx) mesh->indices.push_back(base + i);
            mesh->parts.push_back(part);
        }
        QString why;
        if (mesh->positions.empty() || !mesh->validate(&why)) continue;      // leaves meshIds[mi] null
        scene->meshes[mesh->id] = mesh;
        meshIds[size_t(mi)] = mesh->id;
    }
    if (skippedPrims) r.warnings << QObject::tr("%1 mesh primitive(s) were skipped (points, lines, unsupported or damaged data).").arg(skippedPrims);

    // lights (KHR_lights_punctual)
    const QJsonArray lights = c.root["extensions"].toObject()["KHR_lights_punctual"].toObject()["lights"].toArray();
    bool lightWarned = false;

    // nodes
    const QJsonArray nodes = c.root["nodes"].toArray();
    Node3D rootNode; rootNode.kind = NodeKind::Group; rootNode.name = name.isEmpty() ? QObject::tr("glTF Model") : name;
    const QUuid rootId = scene->addNode(rootNode);
    std::vector<QUuid> nodeIds(size_t(nodes.size()));
    std::vector<bool> isChild(size_t(nodes.size()), false);
    for (int i = 0; i < nodes.size(); ++i) for (const auto& cv : nodes[i].toObject()["children"].toArray()) { const int ci = cv.toInt(-1); if (ci >= 0 && ci < nodes.size()) isChild[size_t(ci)] = true; }
    std::vector<int> visiting(size_t(nodes.size()), 0);
    std::function<void(int, const QUuid&, int)> build = [&](int i, const QUuid& parent, int depth) {
        if (i < 0 || i >= nodes.size() || depth > 128) return;
        if (visiting[size_t(i)]) { r.warnings << QObject::tr("A node is referenced twice (or loops); the repeat was ignored."); return; }
        visiting[size_t(i)] = 1;
        const QJsonObject no = nodes[i].toObject();
        Node3D n;
        n.name = no["name"].toString(QString("Node %1").arg(i));
        n.parent = parent;
        QMatrix4x4 m;
        const QJsonArray ma = no["matrix"].toArray();
        if (ma.size() == 16) {
            float v[16]; for (int k = 0; k < 16; ++k) v[k] = float(ma[k].toDouble());
            m = QMatrix4x4(v).transposed();               // glTF is column-major, QMatrix4x4(float*) is row-major
            decomposeMatrix(m, &n.position, &n.rotation, &n.scale);
        } else {
            const QJsonArray t = no["translation"].toArray(), q = no["rotation"].toArray(), s = no["scale"].toArray();
            if (t.size() == 3) n.position = QVector3D(float(t[0].toDouble()), float(t[1].toDouble()), float(t[2].toDouble()));
            if (s.size() == 3) n.scale = QVector3D(float(s[0].toDouble(1)), float(s[1].toDouble(1)), float(s[2].toDouble(1)));
            if (q.size() == 4) {
                QQuaternion qq(float(q[3].toDouble(1)), float(q[0].toDouble()), float(q[1].toDouble()), float(q[2].toDouble()));
                qq.normalize();
                QMatrix4x4 rm; rm.rotate(qq);
                n.rotation = eulerFromMatrix(rm);
            }
        }
        for (int k = 0; k < 3; ++k) {
            if (!std::isfinite(n.position[k])) n.position[k] = 0;
            if (!std::isfinite(n.rotation[k])) n.rotation[k] = 0;
            if (!std::isfinite(n.scale[k]) || n.scale[k] == 0) n.scale[k] = 1;
        }
        const int mi = no["mesh"].toInt(-1);
        const int ci = no["camera"].toInt(-1);
        const QJsonObject lo = no["extensions"].toObject()["KHR_lights_punctual"].toObject();
        if (mi >= 0 && mi < meshes.size() && !meshIds[size_t(mi)].isNull()) { n.kind = NodeKind::Mesh; n.mesh = meshIds[size_t(mi)]; }
        else if (ci >= 0 && ci < c.root["cameras"].toArray().size()) {
            const QJsonObject co = c.root["cameras"].toArray()[ci].toObject();
            if (co["type"].toString() == "perspective") {
                const QJsonObject po = co["perspective"].toObject();
                n.kind = NodeKind::Camera;
                n.fov = std::clamp(po["yfov"].toDouble(0.8) * 180.0 / M_PI, 1.0, 170.0);
                n.nearPlane = std::max(1e-4, po["znear"].toDouble(0.1));
                n.farPlane = std::max(n.nearPlane * 1.01, po["zfar"].toDouble(1000));
            } else r.warnings << QObject::tr("Camera '%1' is orthographic, which is not supported; skipped.").arg(n.name);
        } else if (lo.contains("light")) {
            const int li = lo["light"].toInt(-1);
            if (li >= 0 && li < lights.size()) {
                const QJsonObject l = lights[li].toObject();
                n.kind = NodeKind::Light;
                lightTypeFromKey(l["type"].toString("point"), n.lightType);
                const QJsonArray col = l["color"].toArray();
                if (col.size() == 3) n.lightColor = QColor::fromRgbF(toSrgb(col[0].toDouble(1)), toSrgb(col[1].toDouble(1)), toSrgb(col[2].toDouble(1)));
                n.intensity = 1.0;
                n.range = std::max(0.0, l["range"].toDouble(0));
                if (l["type"].toString() == "spot") n.spotAngle = std::clamp(l["spot"].toObject()["outerConeAngle"].toDouble(M_PI / 4) * 360.0 / M_PI, 1.0, 179.0);
                if (!lightWarned) { r.warnings << QObject::tr("Light intensities use physical units in glTF; lights were imported at intensity 1.0. Adjust them in the 3D panel."); lightWarned = true; }
            }
        }
        nodeIds[size_t(i)] = scene->addNode(n);
        const QUuid me = nodeIds[size_t(i)];
        for (const auto& cv : no["children"].toArray()) build(cv.toInt(-1), me, depth + 1);
    };
    int sceneIndex = c.root["scene"].toInt(0);
    const QJsonArray scenes = c.root["scenes"].toArray();
    if (!scenes.isEmpty() && sceneIndex >= 0 && sceneIndex < scenes.size()) {
        for (const auto& nv : scenes[sceneIndex].toObject()["nodes"].toArray()) build(nv.toInt(-1), rootId, 0);
    } else {
        for (int i = 0; i < nodes.size(); ++i) if (!isChild[size_t(i)]) build(i, rootId, 0);
    }
    // ---- animations: translation / rotation / scale channels of nodes become keyframed clips ----
    {
        const QJsonArray anims = c.root["animations"].toArray();
        if (!(fps > 0 && fps <= 1000)) fps = 24.0;
        bool weightsWarned = false, cubicWarned = false;
        int imported = 0, skipped = 0;
        for (int ai = 0; ai < anims.size() && ai < 256; ++ai) {
            const QJsonObject ao = anims[ai].toObject();
            const QJsonArray samplers = ao["samplers"].toArray();
            AnimClip clip;
            clip.id = QUuid::createUuid();
            clip.name = ao["name"].toString();
            if (clip.name.isEmpty()) clip.name = QObject::tr("Clip %1").arg(ai + 1);
            const QJsonArray channels = ao["channels"].toArray();
            for (int ci = 0; ci < channels.size() && ci < 100000; ++ci) {
                const QJsonObject ch = channels[ci].toObject();
                const QJsonObject tg = ch["target"].toObject();
                const int ni = tg["node"].toInt(-1), si = ch["sampler"].toInt(-1);
                const QString path = tg["path"].toString();
                if (path == "weights") { if (!weightsWarned) { r.warnings << QObject::tr("Morph-target (blend shape) animation is not supported and was skipped."); weightsWarned = true; } continue; }
                if (path != "translation" && path != "rotation" && path != "scale") { ++skipped; continue; }
                if (ni < 0 || ni >= int(nodeIds.size()) || nodeIds[size_t(ni)].isNull() || si < 0 || si >= samplers.size()) { ++skipped; continue; }
                const QJsonObject so = samplers[si].toObject();
                std::vector<double> times, vals;
                int tc = 0, vc = 0;
                QString why;
                if (!readAccessor(c, so["input"].toInt(-1), times, &tc, &why) || tc != 1 || times.empty()) { ++skipped; continue; }
                if (!readAccessor(c, so["output"].toInt(-1), vals, &vc, &why)) { ++skipped; continue; }
                const int comps = path == "rotation" ? 4 : 3;
                const QString interp = so["interpolation"].toString("LINEAR");
                const bool cubic = interp == "CUBICSPLINE";
                if (cubic && !cubicWarned) { r.warnings << QObject::tr("Cubic-spline animation was imported as linear keyframes."); cubicWarned = true; }
                if (vc != comps || vals.size() != times.size() * size_t(comps) * (cubic ? 3 : 1)) { ++skipped; continue; }
                const bool hold = interp == "STEP";
                const QString id = nodePropId(nodeIds[size_t(ni)], path == "translation" ? "pos" : path == "rotation" ? "rot" : "scl");
                const PropertyDef* def = findPropertyDef(id);
                if (!def) { ++skipped; continue; }
                auto valueAt = [&](size_t k, int comp) { return cubic ? vals[(k * 3 + 1) * size_t(comps) + size_t(comp)] : vals[k * size_t(comps) + size_t(comp)]; };
                PropertyTrack track;
                track.base = def->def;
                int lastFrame = INT_MIN;
                QQuaternion prevQ;
                QVector3D prevE;
                bool havePrev = false;
                // Two Euler triples describe the same rotation, (x,y,z) and (x+180, 180-y, z+180); pick the one (and the 360° multiples)
                // nearest to the previous key so that interpolating the angles follows the real turn.
                auto nearestEuler = [](QVector3D e, const QVector3D& prev) {
                    auto unwrap = [&](QVector3D v) {
                        for (int a = 0; a < 3; ++a) { while (v[a] - prev[a] > 180) v[a] -= 360; while (v[a] - prev[a] < -180) v[a] += 360; }
                        return v;
                    };
                    const QVector3D a = unwrap(e), b = unwrap(QVector3D(e.x() + 180, 180 - e.y(), e.z() + 180));
                    return (a - prev).lengthSquared() <= (b - prev).lengthSquared() ? a : b;
                };
                auto addKey = [&](int frame, PropValue v, bool holdOut) {
                    track.setKey(*def, frame, v);
                    const int ix = track.indexOfKey(frame);
                    if (ix >= 0 && holdOut) track.keys[ix].out = Interp::Hold;
                };
                bool okTimes = true;
                for (size_t k = 0; k < times.size() && okTimes; ++k) {
                    if (!std::isfinite(times[k]) || times[k] < 0 || times[k] > 1e6) { okTimes = false; break; }
                    const int frame = int(std::lround(times[k] * fps));
                    if (frame <= lastFrame) continue;                        // closer together than one frame: the first one wins
                    if (track.keys.size() >= 20000) break;
                    if (path == "rotation") {
                        QQuaternion q(float(valueAt(k, 3)), float(valueAt(k, 0)), float(valueAt(k, 1)), float(valueAt(k, 2)));   // glTF stores x,y,z,w
                        if (q.isNull() || !std::isfinite(q.lengthSquared())) q = QQuaternion();
                        q.normalize();
                        if (havePrev) {
                            if (QQuaternion::dotProduct(prevQ, q) < 0) q = QQuaternion(-q.scalar(), -q.vector());       // shortest way round
                            // Euler angles interpolate linearly between keys, so long turns get intermediate keys
                            const double ang = 2.0 * std::acos(std::min(1.0, double(std::abs(QQuaternion::dotProduct(prevQ, q))))) * 180.0 / M_PI;
                            const int f0 = lastFrame;
                            const int steps = std::min(frame - f0, int(std::ceil(ang / 15.0 - 1e-6)));
                            for (int st = 1; st < steps; ++st) {
                                const float u = float(st) / float(steps);
                                const int f = f0 + int(std::lround(double(frame - f0) * u));
                                if (f <= lastFrame || f >= frame) continue;
                                QMatrix4x4 m; m.rotate(QQuaternion::slerp(prevQ, q, u));
                                QVector3D e = eulerFromMatrix(m);
                                e = nearestEuler(e, prevE);
                                addKey(f, PropValue::of(e.x(), e.y(), e.z()), hold);
                                prevE = e; lastFrame = f;
                            }
                        }
                        QMatrix4x4 m; m.rotate(q);
                        QVector3D e = eulerFromMatrix(m);
                        if (havePrev) e = nearestEuler(e, prevE);
                        addKey(frame, PropValue::of(e.x(), e.y(), e.z()), hold);
                        prevQ = q; prevE = e; havePrev = true;
                    } else {
                        addKey(frame, PropValue::of(valueAt(k, 0), valueAt(k, 1), valueAt(k, 2)), hold);
                    }
                    lastFrame = frame;
                }
                if (!okTimes || track.keys.isEmpty()) { ++skipped; continue; }
                clip.length = std::max(clip.length, track.keys.last().frame);
                clip.tracks.insert(id, track);
            }
            if (clip.tracks.isEmpty() || clip.length <= 0) { ++skipped; continue; }
            scene->clips.push_back(std::move(clip));
            ++imported;
        }
        if (anims.size() > 256) r.warnings << QObject::tr("Only the first 256 animations were read.");
        if (!scene->clips.empty()) {
            ClipPlacement p;
            p.id = QUuid::createUuid(); p.clip = scene->clips.front().id; p.start = 0; p.loop = true;
            scene->placements.push_back(p);
            r.warnings << QObject::tr("Imported %1 animation clip(s); \"%2\" is placed on the timeline and loops. Others are in the Animation Clips list.").arg(imported).arg(scene->clips.front().name);
        } else if (!anims.isEmpty()) {
            r.warnings << QObject::tr("The model's animations contain nothing that could be imported (only node translation/rotation/scale is supported).");
        }
        if (skipped) r.warnings << QObject::tr("%1 animation channel(s)/clip(s) could not be read and were skipped.").arg(skipped);
    }
    // ---- skins ----
    {
        const QJsonArray skinsJ = c.root["skins"].toArray();
        std::vector<QUuid> skinIds(size_t(skinsJ.size()));
        int bad = 0;
        for (int si = 0; si < skinsJ.size() && si < 4096; ++si) {
            const QJsonObject so = skinsJ[si].toObject();
            Skin k;
            k.id = QUuid::createUuid();
            bool ok = true;
            for (const auto& jv : so["joints"].toArray()) {
                const int ji = jv.toInt(-1);
                if (ji < 0 || ji >= int(nodeIds.size()) || nodeIds[size_t(ji)].isNull() || k.joints.size() >= 65535) { ok = false; break; }
                k.joints.push_back(nodeIds[size_t(ji)]);
            }
            if (!ok || k.joints.empty()) { ++bad; continue; }
            std::vector<double> ibm; int ic = 0; QString why;
            if (so.contains("inverseBindMatrices")) {
                if (!readAccessor(c, so["inverseBindMatrices"].toInt(-1), ibm, &ic, &why) || ic != 16 || ibm.size() != k.joints.size() * 16) { ++bad; continue; }
            }
            for (size_t j = 0; j < k.joints.size(); ++j) {
                QMatrix4x4 m;
                if (!ibm.empty()) { float* d = m.data(); for (int i = 0; i < 16; ++i) { const double x = ibm[j * 16 + size_t(i)]; d[i] = std::isfinite(x) ? float(x) : 0.0f; } }
                k.inverseBind.push_back(m);
            }
            skinIds[size_t(si)] = k.id;
            scene->skins.push_back(std::move(k));
        }
        int skinnedNodes = 0;
        for (int i = 0; i < nodes.size(); ++i) {
            const QJsonObject no = nodes[i].toObject();
            const int si = no["skin"].toInt(-1);
            if (si < 0 || si >= int(skinIds.size()) || skinIds[size_t(si)].isNull() || nodeIds[size_t(i)].isNull()) continue;
            Node3D* n = scene->node(nodeIds[size_t(i)]);
            if (!n || n->kind != NodeKind::Mesh) continue;
            auto mit = scene->meshes.find(n->mesh);
            if (mit == scene->meshes.end() || !mit->second->skinned()) continue;
            n->skin = skinIds[size_t(si)];
            ++skinnedNodes;
        }
        if (bad) r.warnings << QObject::tr("%1 skin(s) were damaged and ignored; their meshes are shown in the bind pose.").arg(bad);
        if (skinnedNodes) r.warnings << QObject::tr("Skeletal deformation uses up to 4 joints per vertex, no morph targets (blend shapes).");
    }
    int meshNodes = 0;
    for (const auto& n : scene->nodes) if (n.kind == NodeKind::Mesh) ++meshNodes;
    if (!meshNodes) { r.error = QObject::tr("The glTF file contains no drawable meshes."); return r; }
    scene->activeCamera = QUuid();             // an imported camera is available but does not become the composition camera automatically
    r.scene = scene; r.root = rootId;
    return r;
}

}  // namespace mynd
