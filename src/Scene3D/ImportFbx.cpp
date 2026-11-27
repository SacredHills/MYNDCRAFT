// Autodesk FBX importer: binary FBX 7.x (geometry, hierarchy, transforms, materials, diffuse textures).
// ASCII FBX, cameras, lights, skeletons and animation are not imported.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <set>

#include "Scene3D/ModelImport.h"

namespace mynd {

namespace {

struct Prop {
    char type = 0;
    qint64 i = 0;
    double d = 0;
    QByteArray s;
    std::vector<double> arrD;
    std::vector<qint64> arrI;
};
struct FNode {
    QByteArray name;
    std::vector<Prop> props;
    std::vector<FNode> kids;
    const FNode* find(const char* n) const { for (const auto& k : kids) if (k.name == n) return &k; return nullptr; }
    qint64 id() const { return props.empty() ? 0 : props[0].i; }
    QString label() const {                      // "Name\x00\x01Class" -> Name
        if (props.size() < 2) return {};
        const QByteArray& s = props[1].s;
        const int z = s.indexOf('\0');
        return QString::fromUtf8(z >= 0 ? s.left(z) : s);
    }
    QByteArray str(size_t i) const { return props.size() > i ? props[i].s : QByteArray(); }
    double num(size_t i, double def = 0) const { return props.size() > i ? props[i].d : def; }
};

struct Reader {
    const QByteArray& b;
    qint64 pos = 0;
    bool ok = true;
    qint64 nodes = 0;
    explicit Reader(const QByteArray& d) : b(d) {}
    bool need(qint64 n) { if (n < 0 || pos + n > b.size()) { ok = false; return false; } return true; }
    template <class T> T get() { T v{}; if (!need(sizeof(T))) return v; std::memcpy(&v, b.constData() + pos, sizeof(T)); pos += sizeof(T); return v; }
};

bool readProp(Reader& r, Prop& p, QString* err) {
    p.type = r.get<char>();
    if (!r.ok) { *err = "truncated"; return false; }
    switch (p.type) {
    case 'Y': p.i = r.get<qint16>(); p.d = double(p.i); break;
    case 'C': p.i = r.get<qint8>(); p.d = double(p.i); break;
    case 'I': p.i = r.get<qint32>(); p.d = double(p.i); break;
    case 'L': p.i = r.get<qint64>(); p.d = double(p.i); break;
    case 'F': p.d = double(r.get<float>()); p.i = qint64(p.d); break;
    case 'D': p.d = r.get<double>(); p.i = qint64(p.d); break;
    case 'S': case 'R': {
        const quint32 len = r.get<quint32>();
        if (!r.need(len)) { *err = "truncated string"; return false; }
        p.s = QByteArray(r.b.constData() + r.pos, int(len)); r.pos += len; break;
    }
    case 'f': case 'd': case 'l': case 'i': case 'b': {
        const quint32 len = r.get<quint32>(), enc = r.get<quint32>(), clen = r.get<quint32>();
        const int es = (p.type == 'd' || p.type == 'l') ? 8 : (p.type == 'f' || p.type == 'i') ? 4 : 1;
        if (!r.ok || len > 100000000u || !r.need(clen)) { *err = "bad array"; return false; }
        QByteArray raw;
        const quint64 outLen = quint64(len) * quint64(es);
        if (enc == 0) {
            if (quint64(clen) != outLen) { *err = "array size mismatch"; return false; }
            raw = QByteArray(r.b.constData() + r.pos, int(clen));
        } else if (enc == 1) {
            if (outLen > (1ull << 31)) { *err = "array too large"; return false; }
            QByteArray z(4, 0);                                   // qUncompress wants a big-endian size prefix
            z[0] = char(outLen >> 24); z[1] = char(outLen >> 16); z[2] = char(outLen >> 8); z[3] = char(outLen);
            z.append(r.b.constData() + r.pos, int(clen));
            raw = qUncompress(z);
            if (quint64(raw.size()) != outLen) { *err = "corrupt compressed array"; return false; }
        } else { *err = "unknown array encoding"; return false; }
        r.pos += clen;
        const char* d = raw.constData();
        for (quint32 i = 0; i < len; ++i) {
            switch (p.type) {
            case 'f': { float v; std::memcpy(&v, d + qint64(i) * 4, 4); p.arrD.push_back(v); break; }
            case 'd': { double v; std::memcpy(&v, d + qint64(i) * 8, 8); p.arrD.push_back(v); break; }
            case 'i': { qint32 v; std::memcpy(&v, d + qint64(i) * 4, 4); p.arrI.push_back(v); break; }
            case 'l': { qint64 v; std::memcpy(&v, d + qint64(i) * 8, 8); p.arrI.push_back(v); break; }
            default: p.arrI.push_back(d[i] ? 1 : 0); break;
            }
        }
        break;
    }
    default: *err = QString("unknown property type 0x%1").arg(int(uchar(p.type)), 0, 16); return false;
    }
    if (!r.ok) { *err = "truncated"; return false; }
    return true;
}

bool readNode(Reader& r, bool wide, FNode& out, bool* isNull, int depth, QString* err) {
    if (depth > 64) { *err = "nodes nested too deeply"; return false; }
    if (++r.nodes > 5000000) { *err = "too many nodes"; return false; }
    quint64 endOff, nProps, plen;
    if (wide) { endOff = r.get<quint64>(); nProps = r.get<quint64>(); plen = r.get<quint64>(); }
    else { endOff = r.get<quint32>(); nProps = r.get<quint32>(); plen = r.get<quint32>(); }
    const quint8 nameLen = r.get<quint8>();
    if (!r.ok) { *err = "truncated"; return false; }
    if (endOff == 0 && nProps == 0 && plen == 0 && nameLen == 0) { *isNull = true; return true; }
    *isNull = false;
    if (!r.need(nameLen)) { *err = "truncated"; return false; }
    out.name = QByteArray(r.b.constData() + r.pos, nameLen); r.pos += nameLen;
    if (plen > quint64(r.b.size()) || nProps > plen) { *err = QString("bad property list size in '%1' (props %2, bytes %3)").arg(QString::fromLatin1(out.name)).arg(nProps).arg(plen); return false; }
    const qint64 propsEnd = r.pos + qint64(plen);
    if (propsEnd > r.b.size() || endOff > quint64(r.b.size())) { *err = "node extends past the end of the file"; return false; }
    for (quint64 k = 0; k < nProps; ++k) {
        Prop p;
        if (!readProp(r, p, err)) return false;
        if (r.pos > propsEnd) { *err = "property data overruns its list"; return false; }
        out.props.push_back(std::move(p));
    }
    r.pos = propsEnd;
    if (qint64(endOff) < r.pos) { *err = "invalid node end offset"; return false; }
    while (r.pos < qint64(endOff)) {
        FNode kid; bool nul = false;
        if (!readNode(r, wide, kid, &nul, depth + 1, err)) return false;
        if (nul) break;
        out.kids.push_back(std::move(kid));
    }
    r.pos = qint64(endOff);
    return true;
}

// Reads the numeric values of a "P" property entry of Properties70: [name, type, label, flags, v1, v2, v3...]
const FNode* findP(const FNode& obj, const char* name) {
    const FNode* props = obj.find("Properties70");
    if (!props) return nullptr;
    for (const auto& p : props->kids) if (p.name == "P" && p.str(0) == name) return &p;
    return nullptr;
}
QVector3D vec3P(const FNode& obj, const char* name, QVector3D def) {
    const FNode* p = findP(obj, name);
    if (!p || p->props.size() < 7) return def;
    return QVector3D(float(p->num(4)), float(p->num(5)), float(p->num(6)));
}

QMatrix4x4 rotMatrix(const QVector3D& deg) {
    QMatrix4x4 m;
    m.rotate(deg.z(), 0, 0, 1); m.rotate(deg.y(), 0, 1, 0); m.rotate(deg.x(), 1, 0, 0);
    return m;
}

}  // namespace

ImportResult importFbxData(const QByteArray& data, const QString& baseDir, const QString& name) {
    ImportResult r;
    static const char kMagic[] = "Kaydara FBX Binary  ";             // followed by NUL (21 bytes), then 0x1A 0x00
    if (data.size() < 27 || std::memcmp(data.constData(), kMagic, 20) != 0 || data[20] != 0) {
        if (data.left(64).contains("FBX") || data.trimmed().startsWith(';')) r.error = QObject::tr("This is an ASCII FBX file. Only binary FBX is supported; re-export it as binary FBX or as glTF.");
        else r.error = QObject::tr("Not a valid FBX file.");
        return r;
    }
    quint32 version; std::memcpy(&version, data.constData() + 23, 4);
    if (version < 7100 || version > 7800) { r.error = QObject::tr("Unsupported FBX version %1 (supported: 7.1 - 7.7).").arg(version); return r; }
    const bool wide = version >= 7500;

    Reader rd(data);
    rd.pos = 27;
    FNode top;
    QString perr;
    while (rd.pos < data.size()) {
        FNode n; bool nul = false;
        if (!readNode(rd, wide, n, &nul, 0, &perr)) { r.error = QObject::tr("The FBX file is damaged: %1").arg(perr); return r; }
        if (nul) break;
        top.kids.push_back(std::move(n));
        if (importCancelled()) { r.error = QObject::tr("Import cancelled."); return r; }
        importProgress(int(60.0 * double(rd.pos) / double(std::max<qint64>(1, data.size()))));
    }
    const FNode* objects = top.find("Objects");
    const FNode* conns = top.find("Connections");
    if (!objects) { r.error = QObject::tr("The FBX file has no objects."); return r; }

    std::map<qint64, const FNode*> byId;
    for (const auto& o : objects->kids) byId[o.id()] = &o;
    struct Conn { qint64 child, parent; QByteArray prop; bool property; };
    std::vector<Conn> cons;
    if (conns) for (const auto& c : conns->kids) {
        if (c.name != "C" || c.props.size() < 3) continue;
        cons.push_back({c.props[1].i, c.props[2].i, c.str(3), c.str(0) == "OP"});
    }
    auto type = [&](qint64 id) -> QByteArray { auto it = byId.find(id); return it == byId.end() ? QByteArray() : it->second->name; };

    auto scene = std::make_shared<Scene3D>();
    Node3D rootN; rootN.kind = NodeKind::Group; rootN.name = name.isEmpty() ? QObject::tr("FBX Model") : name;
    // coordinate system: FBX files from Z-up tools declare it
    if (const FNode* gs = top.find("GlobalSettings"))
        if (const FNode* up = findP(*gs, "UpAxis")) if (up->props.size() > 4 && up->props[4].i == 2) rootN.rotation = QVector3D(-90, 0, 0);
    const QUuid rootId = scene->addNode(rootN);

    // materials
    std::map<qint64, QUuid> matIds;
    int objDone = 0;
    for (const auto& o : objects->kids) {
        if ((++objDone & 0xFF) == 0) {
            if (importCancelled()) { r.error = QObject::tr("Import cancelled."); return r; }
            importProgress(60 + int(35.0 * objDone / std::max<size_t>(1, objects->kids.size())));
        }
        if (o.name != "Material") continue;
        Material m; m.id = QUuid::createUuid(); m.name = o.label().isEmpty() ? "Material" : o.label();
        const QVector3D dc = vec3P(o, "DiffuseColor", vec3P(o, "Diffuse", QVector3D(0.8f, 0.8f, 0.8f)));
        double alpha = 1.0;
        if (const FNode* p = findP(o, "Opacity")) alpha = p->num(4, 1);
        else if (const FNode* t = findP(o, "TransparencyFactor")) alpha = 1.0 - t->num(4, 0);
        m.baseColor = QColor::fromRgbF(std::clamp<double>(dc.x(), 0, 1), std::clamp<double>(dc.y(), 0, 1), std::clamp<double>(dc.z(), 0, 1), std::clamp(alpha, 0.0, 1.0));
        const QVector3D em = vec3P(o, "EmissiveColor", QVector3D(0, 0, 0));
        if (const FNode* ef = findP(o, "EmissiveFactor")) { const float f = float(ef->num(4, 1)); m.emissive = QColor::fromRgbF(std::clamp(em.x() * f, 0.f, 1.f), std::clamp(em.y() * f, 0.f, 1.f), std::clamp(em.z() * f, 0.f, 1.f)); }
        if (const FNode* sh = findP(o, "ShininessExponent")) m.roughness = std::clamp(std::sqrt(2.0 / (std::clamp(sh->num(4, 20), 0.0, 1000.0) + 2.0)), 0.05, 1.0);
        m.doubleSided = true;
        scene->materials.push_back(m);
        matIds[o.id()] = m.id;
    }
    // textures attached to a material's diffuse/base colour
    int missingTex = 0;
    for (const auto& c : cons) {
        if (!c.property || type(c.child) != "Texture" || !matIds.count(c.parent)) continue;
        const QString prop = QString::fromUtf8(c.prop).toLower();
        if (!prop.contains("diffuse") && !prop.contains("base")) continue;
        const FNode* tex = byId[c.child];
        QString file;
        if (const FNode* rf = tex->find("RelativeFilename")) file = QString::fromUtf8(rf->str(0));
        if (file.isEmpty()) if (const FNode* fn = tex->find("FileName")) file = QString::fromUtf8(fn->str(0));
        file.replace('\\', '/');
        const QString leaf = file.section('/', -1);
        QImage img;
        for (const QString& cand : {QDir(baseDir).filePath(file), QDir(baseDir).filePath(leaf), QDir(baseDir).filePath("textures/" + leaf)}) {
            if (QFileInfo(cand).isFile()) { img = QImage(cand); if (!img.isNull()) break; }
        }
        Material* m = scene->material(matIds[c.parent]);
        if (img.isNull()) { ++missingTex; continue; }
        const QUuid tid = QUuid::createUuid();
        scene->textures[tid] = img.convertToFormat(QImage::Format_ARGB32);
        m->baseTexture = tid;
        m->baseColor = QColor(255, 255, 255, m->baseColor.alpha());
    }
    if (missingTex) r.warnings << QObject::tr("%1 texture file(s) were not found next to the model; those surfaces are drawn untextured.").arg(missingTex);

    // hierarchy
    std::map<qint64, std::vector<qint64>> modelKids, modelGeoms, modelMats;
    std::set<qint64> hasParent;
    for (const auto& c : cons) {
        if (c.property) continue;
        const QByteArray ct = type(c.child), pt = type(c.parent);
        if (ct == "Model" && pt == "Model") { modelKids[c.parent].push_back(c.child); hasParent.insert(c.child); }
        else if (ct == "Geometry" && pt == "Model") modelGeoms[c.parent].push_back(c.child);
        else if (ct == "Material" && pt == "Model") modelMats[c.parent].push_back(c.child);
    }

    int skippedGeom = 0, unsupported = 0;
    std::set<qint64> visited;
    auto buildMesh = [&](const FNode& g, const std::vector<qint64>& mats, const QString& mname) -> std::shared_ptr<Mesh> {
        const FNode* vn = g.find("Vertices");
        const FNode* pn = g.find("PolygonVertexIndex");
        if (!vn || !pn || vn->props.empty() || pn->props.empty()) return nullptr;
        const std::vector<double>& V = vn->props[0].arrD;
        const std::vector<qint64>& PI = pn->props[0].arrI;
        if (V.size() < 3 || V.size() % 3 || PI.empty()) return nullptr;
        const size_t vcount = V.size() / 3;
        auto layer = [&](const char* node, const char* data, const char* index, std::string& mapping, std::string& ref, const std::vector<double>** d, const std::vector<qint64>** idx) {
            const FNode* ln = g.find(node);
            if (!ln) return false;
            const FNode* dn = ln->find(data);
            if (!dn || dn->props.empty()) return false;
            *d = &dn->props[0].arrD;
            *idx = nullptr;
            if (const FNode* in = ln->find(index)) if (!in->props.empty()) *idx = &in->props[0].arrI;
            const FNode* mp = ln->find("MappingInformationType"); const FNode* rf = ln->find("ReferenceInformationType");
            mapping = mp ? mp->str(0).toStdString() : "ByPolygonVertex"; ref = rf ? rf->str(0).toStdString() : "Direct";
            return true;
        };
        std::string nMap, nRef, uMap, uRef;
        const std::vector<double>* ND = nullptr; const std::vector<qint64>* NI = nullptr;
        const std::vector<double>* UD = nullptr; const std::vector<qint64>* UI = nullptr;
        const bool hasN = layer("LayerElementNormal", "Normals", "NormalsIndex", nMap, nRef, &ND, &NI);
        const bool hasU = layer("LayerElementUV", "UV", "UVIndex", uMap, uRef, &UD, &UI);
        std::vector<qint64> polyMat; std::string mMap = "AllSame";
        if (const FNode* ml = g.find("LayerElementMaterial")) {
            if (const FNode* mn = ml->find("Materials")) if (!mn->props.empty()) polyMat = mn->props[0].arrI;
            if (const FNode* mp = ml->find("MappingInformationType")) mMap = mp->str(0).toStdString();
        }
        // resolves which entry of the layer's data a corner uses
        auto resolve = [&](const std::string& mapping, const std::string& ref, const std::vector<qint64>* idx, size_t dataCount, size_t pvi, size_t vert, size_t poly) -> qint64 {
            size_t k;
            if (mapping == "ByPolygonVertex") k = pvi;
            else if (mapping == "ByVertice" || mapping == "ByVertex") k = vert;
            else if (mapping == "ByPolygon") k = poly;
            else k = 0;
            if (ref == "IndexToDirect" || ref == "Index") { if (!idx || k >= idx->size()) return -1; const qint64 v = (*idx)[k]; return (v >= 0 && size_t(v) < dataCount) ? v : -1; }
            return k < dataCount ? qint64(k) : -1;
        };
        auto mesh = std::make_shared<Mesh>();
        mesh->id = QUuid::createUuid(); mesh->name = mname;
        struct Key { qint64 v, n, u; bool operator<(const Key& o) const { return std::tie(v, n, u) < std::tie(o.v, o.n, o.u); } };
        std::map<Key, uint32_t> lookup;
        std::map<int, std::vector<uint32_t>> bySlot;
        std::vector<uint32_t> poly;
        size_t polyIndex = 0;
        bool badIndex = false;
        const size_t nCount = ND ? ND->size() / 3 : 0, uCount = UD ? UD->size() / 2 : 0;
        for (size_t pvi = 0; pvi < PI.size(); ++pvi) {
            const qint64 raw = PI[pvi];
            const bool end = raw < 0;
            const qint64 vi = end ? ~raw : raw;
            if (vi < 0 || size_t(vi) >= vcount) { badIndex = true; poly.clear(); if (end) ++polyIndex; continue; }
            Key k{vi, -1, -1};
            if (hasN) k.n = resolve(nMap, nRef, NI, nCount, pvi, size_t(vi), polyIndex);
            if (hasU) k.u = resolve(uMap, uRef, UI, uCount, pvi, size_t(vi), polyIndex);
            auto it = lookup.find(k);
            uint32_t vid;
            if (it == lookup.end()) {
                vid = uint32_t(mesh->positions.size());
                mesh->positions.push_back(QVector3D(float(V[size_t(vi) * 3]), float(V[size_t(vi) * 3 + 1]), float(V[size_t(vi) * 3 + 2])));
                if (hasN) mesh->normals.push_back(k.n >= 0 ? QVector3D(float((*ND)[size_t(k.n) * 3]), float((*ND)[size_t(k.n) * 3 + 1]), float((*ND)[size_t(k.n) * 3 + 2])).normalized() : QVector3D(0, 1, 0));
                if (hasU) mesh->uvs.push_back(k.u >= 0 ? QVector2D(float((*UD)[size_t(k.u) * 2]), float((*UD)[size_t(k.u) * 2 + 1])) : QVector2D(0, 0));
                lookup[k] = vid;
            } else vid = it->second;
            poly.push_back(vid);
            if (end) {
                int slot = 0;
                if (mMap == "ByPolygon" && polyIndex < polyMat.size()) slot = int(std::clamp<qint64>(polyMat[polyIndex], 0, 1000000));
                for (size_t i = 1; i + 1 < poly.size(); ++i) { auto& t = bySlot[slot]; t.push_back(poly[0]); t.push_back(poly[i]); t.push_back(poly[i + 1]); }
                poly.clear();
                ++polyIndex;
            }
        }
        if (badIndex) r.warnings << QObject::tr("Mesh '%1': some polygons referenced vertices that do not exist and were skipped.").arg(mname);
        for (auto& [slot, tris] : bySlot) {
            MeshPart part; part.firstIndex = int(mesh->indices.size()); part.indexCount = int(tris.size());
            if (size_t(slot) < mats.size() && matIds.count(mats[size_t(slot)])) part.material = matIds[mats[size_t(slot)]];
            mesh->indices.insert(mesh->indices.end(), tris.begin(), tris.end());
            mesh->parts.push_back(part);
        }
        if (mesh->indices.empty()) return nullptr;
        if (!hasN) mesh->computeNormals();
        QString why;
        if (!mesh->validate(&why)) { r.warnings << QObject::tr("Mesh '%1' skipped: %2").arg(mname, why); return nullptr; }
        return mesh;
    };

    std::function<void(qint64, const QUuid&, int)> buildModel = [&](qint64 id, const QUuid& parent, int depth) {
        if (depth > 128 || visited.count(id)) return;
        visited.insert(id);
        const FNode& m = *byId[id];
        Node3D n;
        n.name = m.label().isEmpty() ? QString("Model") : m.label();
        n.parent = parent;
        n.position = vec3P(m, "Lcl Translation", QVector3D(0, 0, 0));
        const QVector3D rot = vec3P(m, "Lcl Rotation", QVector3D(0, 0, 0));
        n.scale = vec3P(m, "Lcl Scaling", QVector3D(1, 1, 1));
        if (const FNode* ro = findP(m, "RotationOrder")) if (ro->props.size() > 4 && ro->props[4].i != 0) r.warnings << QObject::tr("'%1' uses a non-default rotation order; its orientation may differ.").arg(n.name);
        QMatrix4x4 rm = rotMatrix(rot);
        if (findP(m, "PreRotation")) rm = rotMatrix(vec3P(m, "PreRotation", QVector3D(0, 0, 0))) * rm;
        n.rotation = eulerFromMatrix(rm);
        for (int k = 0; k < 3; ++k) {
            if (!std::isfinite(n.position[k])) n.position[k] = 0;
            if (!std::isfinite(n.scale[k]) || n.scale[k] == 0) n.scale[k] = 1;
        }
        const QByteArray mtype = m.str(2);
        auto g = modelGeoms.find(id);
        if (g != modelGeoms.end() && !g->second.empty() && byId.count(g->second[0])) {
            if (auto mesh = buildMesh(*byId[g->second[0]], modelMats[id], n.name)) {
                scene->meshes[mesh->id] = mesh;
                n.kind = NodeKind::Mesh; n.mesh = mesh->id;
            } else ++skippedGeom;
        } else if (mtype == "Camera" || mtype == "Light") ++unsupported;
        const QUuid me = scene->addNode(n);
        for (qint64 k : modelKids[id]) buildModel(k, me, depth + 1);
    };
    for (const auto& o : objects->kids) if (o.name == "Model" && !hasParent.count(o.id())) buildModel(o.id(), rootId, 0);
    if (skippedGeom) r.warnings << QObject::tr("%1 object(s) had unusable geometry and were left out.").arg(skippedGeom);
    if (unsupported) r.warnings << QObject::tr("FBX cameras and lights are not imported; add them from the 3D panel.");
    if (objects->find("AnimationStack") || objects->find("Deformer")) r.warnings << QObject::tr("Animation and skeleton data in the FBX file are not imported (the model is shown in its rest pose).");
    int meshNodes = 0;
    for (const auto& n : scene->nodes) if (n.kind == NodeKind::Mesh) ++meshNodes;
    if (!meshNodes) { r.error = QObject::tr("The FBX file contains no meshes this importer can read."); return r; }
    r.scene = scene; r.root = rootId;
    return r;
}

}  // namespace mynd
