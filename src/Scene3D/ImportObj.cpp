// Wavefront OBJ (+ MTL) importer.
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QFile>
#include <algorithm>
#include <cmath>

#include "Scene3D/ModelImport.h"

namespace mynd {

namespace {

struct MtlEntry { Material mat; QString texPath; };

QStringList tokens(const QString& line) { return line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts); }

QString lastPathToken(const QStringList& t, int from) {      // map_Kd -s 1 1 1 file name.png
    QStringList rest;
    for (int i = from; i < t.size(); ++i) {
        if (t[i].startsWith('-')) {                           // skip an option and its numeric arguments
            while (i + 1 < t.size() && QRegularExpression("^[-+0-9.]+$").match(t[i + 1]).hasMatch()) ++i;
            continue;
        }
        rest << t[i];
    }
    return rest.join(' ');
}

QHash<QString, MtlEntry> parseMtl(const QByteArray& data) {
    QHash<QString, MtlEntry> out;
    QString cur;
    for (const QByteArray& raw : data.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty() || line[0] == '#') continue;
        const QStringList t = tokens(line);
        const QString k = t[0].toLower();
        if (k == "newmtl" && t.size() > 1) {
            cur = t.mid(1).join(' ');
            MtlEntry e; e.mat.id = QUuid::createUuid(); e.mat.name = cur; e.mat.roughness = 0.7;
            out[cur] = e;
        } else if (!cur.isEmpty()) {
            MtlEntry& e = out[cur];
            auto num = [&](int i, double d = 0) { bool ok; double v = i < t.size() ? t[i].toDouble(&ok) : d; return (i < t.size() && ok && std::isfinite(v)) ? v : d; };
            if (k == "kd" && t.size() >= 4) { QColor c = e.mat.baseColor; c.setRgbF(std::clamp(num(1), 0.0, 1.0), std::clamp(num(2), 0.0, 1.0), std::clamp(num(3), 0.0, 1.0), c.alphaF()); e.mat.baseColor = c; }
            else if (k == "ke" && t.size() >= 4) e.mat.emissive = QColor::fromRgbF(std::clamp(num(1), 0.0, 1.0), std::clamp(num(2), 0.0, 1.0), std::clamp(num(3), 0.0, 1.0));
            else if (k == "d") { QColor c = e.mat.baseColor; c.setAlphaF(std::clamp(num(1, 1), 0.0, 1.0)); e.mat.baseColor = c; }
            else if (k == "tr") { QColor c = e.mat.baseColor; c.setAlphaF(std::clamp(1 - num(1, 0), 0.0, 1.0)); e.mat.baseColor = c; }
            else if (k == "ns") e.mat.roughness = std::clamp(std::sqrt(2.0 / (std::clamp(num(1, 10), 0.0, 1000.0) + 2.0)), 0.05, 1.0);
            else if (k == "pr") e.mat.roughness = std::clamp(num(1, 0.6), 0.0, 1.0);
            else if (k == "pm") e.mat.metallic = std::clamp(num(1, 0), 0.0, 1.0);
            else if (k == "map_kd") e.texPath = lastPathToken(t, 1);
        }
    }
    return out;
}

struct Key {
    int v, t, n;
    bool operator==(const Key& o) const { return v == o.v && t == o.t && n == o.n; }
};
inline size_t qHash(const Key& k, size_t seed = 0) { return ::qHash(quint64(uint32_t(k.v)) * 1000003u ^ quint64(uint32_t(k.t)) * 7919u ^ quint64(uint32_t(k.n)), seed); }

struct ObjObject {
    QString name;
    std::vector<Key> keys;                       // vertex table: key -> position in mesh
    QHash<Key, uint32_t> lookup;
    std::vector<std::pair<QString, std::vector<uint32_t>>> byMaterial;   // material name -> triangle indices
    int addVertex(const Key& k) {
        auto it = lookup.constFind(k);
        if (it != lookup.constEnd()) return int(it.value());
        const uint32_t i = uint32_t(keys.size());
        keys.push_back(k); lookup.insert(k, i);
        return int(i);
    }
    std::vector<uint32_t>& tris(const QString& mat) {
        for (auto& p : byMaterial) if (p.first == mat) return p.second;
        byMaterial.push_back({mat, {}});
        return byMaterial.back().second;
    }
};

}  // namespace

ImportResult importObjData(const QByteArray& data, const QString& baseDir, const QString& name) {
    ImportResult r;
    std::vector<QVector3D> P; std::vector<QVector2D> T; std::vector<QVector3D> N;
    QHash<QString, MtlEntry> mtl;
    std::vector<ObjObject> objs;
    auto cur = [&]() -> ObjObject& { if (objs.empty()) objs.push_back(ObjObject{name, {}, {}, {}}); return objs.back(); };
    QString curMat;
    int badFaces = 0;
    auto fnum = [](const QString& s, float& out) { bool ok; double d = s.toDouble(&ok); out = float(d); return ok && std::isfinite(d); };

    QByteArray joined;                           // handle "\" line continuations
    const QList<QByteArray> rawLines = data.split('\n');
    QString pending;
    int lineNo = 0;
    for (const QByteArray& rawLine : rawLines) {
        ++lineNo;
        QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.endsWith('\\')) { pending += line.chopped(1) + ' '; continue; }
        line = pending + line; pending.clear();
        if (line.isEmpty() || line[0] == '#') continue;
        const QStringList t = tokens(line);
        const QString k = t[0];
        if (k == "v" && t.size() >= 4) {
            float x, y, z;
            if (!fnum(t[1], x) || !fnum(t[2], y) || !fnum(t[3], z)) { r.warnings << QObject::tr("Line %1: bad vertex, using the origin").arg(lineNo); x = y = z = 0; }
            P.push_back({x, y, z});
            if (P.size() > 50000000) { r.error = QObject::tr("The OBJ file has too many vertices."); return r; }
        } else if (k == "vt" && t.size() >= 3) {
            float u, v; if (!fnum(t[1], u) || !fnum(t[2], v)) u = v = 0;
            T.push_back({u, v});
        } else if (k == "vn" && t.size() >= 4) {
            float x, y, z; if (!fnum(t[1], x) || !fnum(t[2], y) || !fnum(t[3], z)) { x = 0; y = 1; z = 0; }
            N.push_back({x, y, z});
        } else if (k == "o" || k == "g") {
            const QString nm = t.size() > 1 ? t.mid(1).join(' ') : name;
            if (objs.empty() || !objs.back().keys.empty()) objs.push_back(ObjObject{nm, {}, {}, {}});
            else objs.back().name = nm;               // a name directly after another name replaces it
        } else if (k == "usemtl") {
            curMat = t.size() > 1 ? t.mid(1).join(' ') : QString();
        } else if (k == "mtllib" && t.size() > 1) {
            const QString file = t.mid(1).join(' ');
            QFile f(QDir(baseDir).filePath(file));
            if (f.open(QIODevice::ReadOnly)) { const auto m = parseMtl(f.readAll()); for (auto it = m.begin(); it != m.end(); ++it) mtl.insert(it.key(), it.value()); }
            else r.warnings << QObject::tr("Material library '%1' was not found next to the model; default materials are used.").arg(file);
        } else if (k == "f" && t.size() >= 4) {
            std::vector<int> idx;
            bool bad = false;
            for (int i = 1; i < t.size() && !bad; ++i) {
                const QStringList c = t[i].split('/');
                auto resolve = [&](const QString& s, size_t count, int& out) {
                    if (s.isEmpty()) { out = -1; return true; }
                    bool ok; int v = s.toInt(&ok);
                    if (!ok || v == 0) return false;
                    out = v > 0 ? v - 1 : int(count) + v;
                    return out >= 0 && size_t(out) < count;
                };
                Key key{-1, -1, -1};
                if (!resolve(c[0], P.size(), key.v) || key.v < 0) { bad = true; break; }
                if (c.size() > 1 && !resolve(c[1], T.size(), key.t)) { bad = true; break; }
                if (c.size() > 2 && !resolve(c[2], N.size(), key.n)) { bad = true; break; }
                idx.push_back(cur().addVertex(key));
            }
            if (bad || idx.size() < 3) { ++badFaces; continue; }
            auto& tri = cur().tris(curMat);
            for (size_t i = 1; i + 1 < idx.size(); ++i) { tri.push_back(uint32_t(idx[0])); tri.push_back(uint32_t(idx[i])); tri.push_back(uint32_t(idx[i + 1])); }
        }
    }
    if (badFaces) r.warnings << QObject::tr("%1 face(s) with invalid indices were skipped.").arg(badFaces);

    auto scene = std::make_shared<Scene3D>();
    Node3D root; root.kind = NodeKind::Group; root.name = name.isEmpty() ? QObject::tr("OBJ Model") : name;
    const QUuid rootId = scene->addNode(root);
    QHash<QString, QUuid> matIds;
    auto materialFor = [&](const QString& mname) -> QUuid {
        if (mname.isEmpty()) return QUuid();
        if (matIds.contains(mname)) return matIds[mname];
        Material m;
        auto it = mtl.find(mname);
        if (it != mtl.end()) {
            m = it->mat;
            if (!it->texPath.isEmpty()) {
                QString p = it->texPath; p.replace('\\', '/');
                QImage img(QDir(baseDir).filePath(p));
                if (!img.isNull()) {
                    const QUuid tid = QUuid::createUuid();
                    scene->textures[tid] = img.convertToFormat(QImage::Format_ARGB32);
                    m.baseTexture = tid;
                    if (m.baseColor.red() == 0 && m.baseColor.green() == 0 && m.baseColor.blue() == 0) m.baseColor = Qt::white;
                } else r.warnings << QObject::tr("Texture '%1' for material '%2' was not found.").arg(p, mname);
            }
        } else { m.name = mname; r.warnings << QObject::tr("Material '%1' is not defined in any library; using a default grey.").arg(mname); }
        m.id = QUuid::createUuid();
        m.doubleSided = true;                      // OBJ files rarely guarantee consistent winding
        scene->materials.push_back(m);
        matIds[mname] = m.id;
        return m.id;
    };

    int made = 0;
    for (auto& o : objs) {
        if (o.keys.empty()) continue;
        auto mesh = std::make_shared<Mesh>();
        mesh->id = QUuid::createUuid();
        mesh->name = o.name;
        bool allN = true, allT = true;
        for (const Key& k : o.keys) { if (k.n < 0) allN = false; if (k.t < 0) allT = false; }
        for (const Key& k : o.keys) {
            mesh->positions.push_back(P[size_t(k.v)]);
            if (allN) mesh->normals.push_back(N[size_t(k.n)].normalized());
            if (allT) mesh->uvs.push_back(T[size_t(k.t)]);
        }
        for (auto& [mn, tris] : o.byMaterial) {
            MeshPart part; part.firstIndex = int(mesh->indices.size()); part.indexCount = int(tris.size()); part.material = materialFor(mn);
            mesh->indices.insert(mesh->indices.end(), tris.begin(), tris.end());
            mesh->parts.push_back(part);
        }
        if (!allN) mesh->computeNormals();
        QString why;
        if (!mesh->validate(&why)) { r.warnings << QObject::tr("Object '%1' skipped: %2").arg(o.name, why); continue; }
        scene->meshes[mesh->id] = mesh;
        Node3D n; n.kind = NodeKind::Mesh; n.name = o.name; n.mesh = mesh->id; n.parent = rootId;
        scene->addNode(n);
        ++made;
    }
    if (!made) { r.error = QObject::tr("The OBJ file contains no faces."); return r; }
    r.scene = scene; r.root = rootId;
    return r;
}

}  // namespace mynd
