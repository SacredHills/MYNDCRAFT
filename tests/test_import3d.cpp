#include <QtTest>
#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "TestEnv.h"
#include "AnimGltf.h"
#include "Scene3D/Anim3D.h"
#include "Scene3D/ModelImport.h"
#include "Scene3D/Render3D.h"

using namespace mynd;

namespace {

// ---------- a minimal binary FBX writer, enough to produce a file the importer must read ----------
struct FProp { char t; QByteArray bytes; };
struct FOut {
    QByteArray name; std::vector<FProp> props; std::vector<FOut> kids;
};
template <class T> QByteArray raw(T v) { return QByteArray(reinterpret_cast<const char*>(&v), sizeof(T)); }
FProp pI(qint32 v) { return {'I', raw(v)}; }
FProp pL(qint64 v) { return {'L', raw(v)}; }
FProp pD(double v) { return {'D', raw(v)}; }
FProp pS(const QByteArray& s) { return {'S', raw(quint32(s.size())) + s}; }
FProp pArrD(const std::vector<double>& v, bool compress) {
    QByteArray d; for (double x : v) d += raw(x);
    if (compress) { QByteArray c = qCompress(d).mid(4); return {'d', raw(quint32(v.size())) + raw(quint32(1)) + raw(quint32(c.size())) + c}; }
    return {'d', raw(quint32(v.size())) + raw(quint32(0)) + raw(quint32(d.size())) + d};
}
FProp pArrI(const std::vector<qint32>& v, bool compress) {
    QByteArray d; for (qint32 x : v) d += raw(x);
    if (compress) { QByteArray c = qCompress(d).mid(4); return {'i', raw(quint32(v.size())) + raw(quint32(1)) + raw(quint32(c.size())) + c}; }
    return {'i', raw(quint32(v.size())) + raw(quint32(0)) + raw(quint32(d.size())) + d};
}
FOut N(const char* name, std::vector<FProp> props = {}, std::vector<FOut> kids = {}) { return {name, std::move(props), std::move(kids)}; }
FOut P(const char* name, double a, double b, double c) { return N("P", {pS(name), pS("Lcl"), pS(""), pS("A"), pD(a), pD(b), pD(c)}); }

void writeNode(QByteArray& out, const FOut& n, bool wide) {
    const qint64 start = out.size();
    const int hdr = wide ? 24 : 12;                              // the three counters; the name length byte follows
    out += QByteArray(hdr, 0);
    out += raw(quint8(n.name.size())) + n.name;
    QByteArray props; for (const auto& p : n.props) props += QByteArray(1, p.t) + p.bytes;
    out += props;
    for (const auto& k : n.kids) writeNode(out, k, wide);
    if (!n.kids.empty()) out += QByteArray(hdr + 1, 0);           // null terminator record
    const quint64 end = quint64(out.size());
    QByteArray h;
    if (wide) h = raw(end) + raw(quint64(n.props.size())) + raw(quint64(props.size()));
    else h = raw(quint32(end)) + raw(quint32(n.props.size())) + raw(quint32(props.size()));
    out.replace(int(start), hdr, h);
}
QByteArray buildFbx(const std::vector<FOut>& top, quint32 version) {
    QByteArray out("Kaydara FBX Binary  ");
    out.append('\0'); out.append(char(0x1a)); out.append('\0');
    out += raw(version);
    const bool wide = version >= 7500;
    for (const auto& n : top) writeNode(out, n, wide);
    out += QByteArray(wide ? 25 : 13, 0);
    return out;
}

// A unit cube made of 6 quads, with per-polygon-vertex normals computed from the face.
QByteArray cubeFbx(quint32 version, bool compress) {
    std::vector<double> V = {-1,-1,-1, 1,-1,-1, 1,1,-1, -1,1,-1, -1,-1,1, 1,-1,1, 1,1,1, -1,1,1};
    std::vector<qint32> I = {0,3,2,~1, 4,5,6,~7, 0,1,5,~4, 3,7,6,~2, 0,4,7,~3, 1,2,6,~5};
    std::vector<double> normals;
    const double fn[6][3] = {{0,0,-1},{0,0,1},{0,-1,0},{0,1,0},{-1,0,0},{1,0,0}};
    for (int f = 0; f < 6; ++f) for (int k = 0; k < 4; ++k) { normals.push_back(fn[f][0]); normals.push_back(fn[f][1]); normals.push_back(fn[f][2]); }
    FOut geom = N("Geometry", {pL(100), pS(QByteArray("Cube\0\1Geometry", 14)), pS("Mesh")},
        {N("Vertices", {pArrD(V, compress)}), N("PolygonVertexIndex", {pArrI(I, compress)}),
         N("LayerElementNormal", {pI(0)}, {N("MappingInformationType", {pS("ByPolygonVertex")}), N("ReferenceInformationType", {pS("Direct")}), N("Normals", {pArrD(normals, compress)})}),
         N("LayerElementMaterial", {pI(0)}, {N("MappingInformationType", {pS("AllSame")}), N("Materials", {pArrI({0}, false)})})});
    FOut model = N("Model", {pL(200), pS(QByteArray("Box\0\1Model", 10)), pS("Mesh")}, {N("Properties70", {}, {P("Lcl Translation", 1, 2, 3), P("Lcl Scaling", 2, 2, 2)})});
    FOut mat = N("Material", {pL(300), pS(QByteArray("Red\0\1Material", 13)), pS("")}, {N("Properties70", {}, {P("DiffuseColor", 1, 0, 0)})});
    FOut objects = N("Objects", {}, {geom, model, mat});
    FOut conns = N("Connections", {}, {N("C", {pS("OO"), pL(100), pL(200)}), N("C", {pS("OO"), pL(300), pL(200)}), N("C", {pS("OO"), pL(200), pL(0)})});
    return buildFbx({N("GlobalSettings", {}, {N("Properties70", {}, {N("P", {pS("UpAxis"), pS("int"), pS("Integer"), pS(""), pI(1)})})}), objects, conns}, version);
}

// ---------- glTF helpers: one triangle (ushort indices) with a material ----------
QByteArray triangleBuffer() {
    QByteArray b;
    const float pos[9] = {0,0,0, 1,0,0, 0,1,0};
    const float uv[6] = {0,1, 1,1, 0,0};
    const quint16 idx[3] = {0, 1, 2};
    b.append(reinterpret_cast<const char*>(pos), 36);
    b.append(reinterpret_cast<const char*>(uv), 24);
    b.append(reinterpret_cast<const char*>(idx), 6);
    b.append(QByteArray(2, 0));
    return b;
}
QJsonObject triangleGltf(const QJsonObject& buffer) {
    QJsonObject o;
    o["asset"] = QJsonObject{{"version", "2.0"}};
    o["buffers"] = QJsonArray{buffer};
    o["bufferViews"] = QJsonArray{QJsonObject{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 36}}, QJsonObject{{"buffer", 0}, {"byteOffset", 36}, {"byteLength", 24}},
                                  QJsonObject{{"buffer", 0}, {"byteOffset", 60}, {"byteLength", 6}}};
    o["accessors"] = QJsonArray{QJsonObject{{"bufferView", 0}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}},
                                QJsonObject{{"bufferView", 1}, {"componentType", 5126}, {"count", 3}, {"type", "VEC2"}},
                                QJsonObject{{"bufferView", 2}, {"componentType", 5123}, {"count", 3}, {"type", "SCALAR"}}};
    o["materials"] = QJsonArray{QJsonObject{{"name", "Blue"}, {"pbrMetallicRoughness", QJsonObject{{"baseColorFactor", QJsonArray{0, 0, 1, 1}}, {"metallicFactor", 0.0}}}, {"doubleSided", true}}};
    o["meshes"] = QJsonArray{QJsonObject{{"name", "Tri"}, {"primitives", QJsonArray{QJsonObject{{"attributes", QJsonObject{{"POSITION", 0}, {"TEXCOORD_0", 1}}}, {"indices", 2}, {"material", 0}}}}}};
    o["nodes"] = QJsonArray{QJsonObject{{"name", "Parent"}, {"translation", QJsonArray{0, 5, 0}}, {"children", QJsonArray{1}}},
                            QJsonObject{{"name", "Child"}, {"mesh", 0}, {"rotation", QJsonArray{0, 0.7071068, 0, 0.7071068}}, {"scale", QJsonArray{2, 2, 2}}}};
    o["scenes"] = QJsonArray{QJsonObject{{"nodes", QJsonArray{0}}}};
    o["scene"] = 0;
    return o;
}
}  // namespace

class TestImport3D : public QObject {
    Q_OBJECT
    static const Node3D* find(const Scene3D& s, const QString& name) { for (const auto& n : s.nodes) if (n.name == name) return &n; return nullptr; }
private slots:
    void modelFilesOverOneGigabyteAreNotRejectedBySize() {
        QVERIFY(maxModelFileBytes() >= (qint64(3) << 30));
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = dir.filePath("big.glb");
        QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("glTF\2\0\0\0", 8);
        if (!f.resize((qint64(1) << 30) + (64 << 20))) QSKIP("cannot create a sparse file here");
        f.close();
        const ImportResult r = importModelFile(path);
        QVERIFY(!r.error.isEmpty());                                   // zeros are not a model...
        QVERIFY(!r.error.contains("larger than"));                     // ...but it is no longer refused for its size
    }

    void objWithMaterialsQuadsAndNegativeIndices() {
        const QByteArray mtl = "newmtl Red\nKd 1 0 0\nNs 50\n";
        QTemporaryDir dir; QFile f(dir.filePath("a.mtl")); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(mtl); f.close();
        const QByteArray obj = "mtllib a.mtl\no Quad\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\nusemtl Red\nf 1/1/1 2/2/1 3/3/1 4/4/1\n"
                               "o Tri\nv 0 0 1\nv 1 0 1\nv 0 1 1\nf -3 -2 -1\n";
        ImportResult r = importObjData(obj, dir.path(), "model");
        QVERIFY2(r, qPrintable(r.error));
        QCOMPARE(r.scene->meshes.size(), size_t(2));
        const Node3D* quad = find(*r.scene, "Quad"); QVERIFY(quad);
        const Mesh& m = *r.scene->meshes.at(quad->mesh);
        QCOMPARE(m.triangleCount(), 2);                                       // the quad was fan-triangulated
        QCOMPARE(m.normals.size(), m.positions.size());
        QCOMPARE(m.uvs.size(), m.positions.size());
        const Material* mat = r.scene->material(m.parts[0].material);
        QVERIFY(mat); QCOMPARE(mat->baseColor.red(), 255); QCOMPARE(mat->baseColor.green(), 0);
        const Mesh& t = *r.scene->meshes.at(find(*r.scene, "Tri")->mesh);
        QCOMPARE(t.triangleCount(), 1);                                       // negative indices are relative
        QCOMPARE(t.normals.size(), t.positions.size());                       // normals were generated
        QVERIFY(r.warnings.isEmpty());
    }

    void objRejectsGarbageGracefully() {
        ImportResult e = importObjData("hello world\n", ".", "x");
        QVERIFY(!e); QVERIFY(!e.error.isEmpty());
        ImportResult r = importObjData("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\nf 1 2 99\nf 0 1 2\nf a b c\n", ".", "x");
        QVERIFY2(r, qPrintable(r.error));
        QVERIFY(!r.warnings.isEmpty());                                       // skipped faces reported
        QCOMPARE(r.scene->meshes.begin()->second->triangleCount(), 1);
    }

    void gltfEmbeddedBufferHierarchyMaterialsAndTextureFlip() {
        QByteArray buf = triangleBuffer();
        QJsonObject g = triangleGltf(QJsonObject{{"byteLength", buf.size()}, {"uri", "data:application/octet-stream;base64," + QString::fromLatin1(buf.toBase64())}});
        ImportResult r = importGltfData(QJsonDocument(g).toJson(), ".", "tri");
        QVERIFY2(r, qPrintable(r.error));
        const Node3D* parent = find(*r.scene, "Parent"); const Node3D* child = find(*r.scene, "Child");
        QVERIFY(parent && child);
        QCOMPARE(child->parent, parent->id);
        QCOMPARE(parent->parent, r.root);
        QCOMPARE(parent->position, QVector3D(0, 5, 0));
        QCOMPARE(child->scale, QVector3D(2, 2, 2));
        QVERIFY(std::abs(child->rotation.y() - 90) < 0.01f);                  // quaternion became Euler degrees
        const Mesh& m = *r.scene->meshes.at(child->mesh);
        QCOMPARE(m.positions.size(), size_t(3));
        QCOMPARE(m.triangleCount(), 1);
        QCOMPARE(m.uvs[0].y(), 0.0f);                                         // glTF v=1 (bottom) becomes v=0 in our v-up convention
        QCOMPARE(m.normals.size(), size_t(3));                                // generated
        const Material* mat = r.scene->material(m.parts[0].material);
        QVERIFY(mat && mat->baseColor.blue() == 255 && mat->doubleSided);
        // world position of the child's origin goes through both parents
        QVERIFY((r.scene->worldMatrix(child->id).map(QVector3D(0, 0, 0)) - QVector3D(0, 5, 0)).length() < 1e-4);
    }

    void glbContainerAndDamagedInput() {
        QByteArray buf = triangleBuffer();
        QJsonObject g = triangleGltf(QJsonObject{{"byteLength", buf.size()}});
        QByteArray json = QJsonDocument(g).toJson(QJsonDocument::Compact);
        while (json.size() % 4) json.append(' ');
        QByteArray glb("glTF"); glb += raw(quint32(2)); glb += raw(quint32(12 + 8 + json.size() + 8 + buf.size()));
        glb += raw(quint32(json.size())) + raw(quint32(0x4E4F534A)) + json;
        glb += raw(quint32(buf.size())) + raw(quint32(0x004E4942)) + buf;
        ImportResult r = importGltfData(glb, ".", "tri");
        QVERIFY2(r, qPrintable(r.error));
        QCOMPARE(r.scene->meshes.size(), size_t(1));
        // truncated container
        QVERIFY(!importGltfData(glb.left(glb.size() - 20), ".", "x"));
        // an accessor that points past its buffer is skipped, not trusted
        QJsonObject bad = triangleGltf(QJsonObject{{"byteLength", 10}, {"uri", "data:application/octet-stream;base64," + QString::fromLatin1(buf.left(10).toBase64())}});
        ImportResult rb = importGltfData(QJsonDocument(bad).toJson(), ".", "x");
        QVERIFY(!rb);
        // out-of-range index
        QByteArray buf2 = buf; quint16 evil = 7; memcpy(buf2.data() + 60, &evil, 2);
        QJsonObject g2 = triangleGltf(QJsonObject{{"byteLength", buf2.size()}, {"uri", "data:application/octet-stream;base64," + QString::fromLatin1(buf2.toBase64())}});
        QVERIFY(!importGltfData(QJsonDocument(g2).toJson(), ".", "x"));
        QVERIFY(!importGltfData("{not json", ".", "x"));
        // refuses things it cannot do
        QJsonObject draco = g; draco["extensionsRequired"] = QJsonArray{"KHR_draco_mesh_compression"};
        ImportResult rd = importGltfData(QJsonDocument(draco).toJson(), ".", "x");
        QVERIFY(!rd && rd.error.contains("compression"));
    }

    void fbxBinaryRawAndCompressedBothVersions() {
        for (quint32 version : {7400u, 7500u}) for (bool compress : {false, true}) {
            ImportResult r = importFbxData(cubeFbx(version, compress), ".", "cube");
            QVERIFY2(r, qPrintable(QString("v%1 compress=%2: %3").arg(version).arg(compress).arg(r.error)));
            const Node3D* box = find(*r.scene, "Box"); QVERIFY(box);
            QCOMPARE(box->kind, NodeKind::Mesh);
            QCOMPARE(box->position, QVector3D(1, 2, 3));
            QCOMPARE(box->scale, QVector3D(2, 2, 2));
            const Mesh& m = *r.scene->meshes.at(box->mesh);
            QCOMPARE(m.triangleCount(), 12);                                   // 6 quads
            QCOMPARE(m.positions.size(), size_t(24));                          // split per face because normals differ per face
            const Material* mat = r.scene->material(m.parts[0].material);
            QVERIFY(mat && mat->baseColor.red() == 255 && mat->baseColor.green() == 0);
            // renders as a cube
            Scene3D s; mergeImport(s, *r.scene);
            Node3D cam; cam.kind = NodeKind::Camera; cam.position = {1, 2, 12}; s.addNode(cam);
            s.ambientIntensity = 1;
            QVERIFY(renderScene(s, sceneCamera(s, 1.0), QSize(80, 80)).pixelColor(40, 40).red() > 200);
        }
    }

    void fbxDamageIsRejectedNotTrusted() {
        const QByteArray good = cubeFbx(7400, true);
        QVERIFY(!importFbxData(good.left(good.size() / 2), ".", "x"));
        QVERIFY(!importFbxData(QByteArray("; FBX 6.1.0 project file\n"), ".", "x"));
        QVERIFY(importFbxData("junk", ".", "x").error.size() > 0);
        QByteArray evil = good;                                                // corrupt every 7th byte in the body
        for (int i = 60; i < evil.size(); i += 7) evil[i] = char(0xFF);
        ImportResult r = importFbxData(evil, ".", "x");                        // must not crash; success or a clean error are both fine
        QVERIFY(r || !r.error.isEmpty());
        QVERIFY(importFbxData(QByteArray("Kaydara FBX Binary  \0\x1a\0\0\0\0\0", 28), ".", "x").error.size() > 0);
        QVERIFY(importFbxData(QByteArray("Kaydara FBX Binary  \0\x1a\0", 23) + raw(quint32(7400)), ".", "x").error.size() > 0);
    }

    void mergingGivesFreshIdsAndKeepsTheSceneConsistent() {
        ImportResult r = importFbxData(cubeFbx(7400, false), ".", "cube");
        QVERIFY(r);
        Scene3D s;
        const QUuid a = mergeImport(s, *r.scene);
        const QUuid b = mergeImport(s, *r.scene);                              // same import twice -> two independent copies
        QVERIFY(!a.isNull() && !b.isNull() && a != b);
        QCOMPARE(s.meshes.size(), size_t(2));
        QCOMPARE(s.materials.size(), size_t(2));
        QVERIFY2(s.validate().isEmpty(), qPrintable(s.validate().join(";")));
        for (const auto& n : s.nodes) if (n.kind == NodeKind::Mesh) QVERIFY(s.meshes.count(n.mesh));
        s.removeNode(a);
        QCOMPARE(s.meshes.size(), size_t(1));                                  // its resources went with it
        QCOMPARE(s.materials.size(), size_t(1));
    }

    void randomlyDamagedFilesNeverCrashOrYieldInvalidScenes() {
        QByteArray gltfJson;
        { QByteArray buf = triangleBuffer(); gltfJson = QJsonDocument(triangleGltf(QJsonObject{{"byteLength", buf.size()}, {"uri", "data:application/octet-stream;base64," + QString::fromLatin1(buf.toBase64())}})).toJson(QJsonDocument::Compact); }
        QByteArray glb;
        { QByteArray buf = triangleBuffer(); QByteArray json = QJsonDocument(triangleGltf(QJsonObject{{"byteLength", buf.size()}})).toJson(QJsonDocument::Compact); while (json.size() % 4) json.append(' ');
          glb = QByteArray("glTF") + raw(quint32(2)) + raw(quint32(12 + 8 + json.size() + 8 + buf.size())) + raw(quint32(json.size())) + raw(quint32(0x4E4F534A)) + json + raw(quint32(buf.size())) + raw(quint32(0x004E4942)) + buf; }
        const QByteArray obj = "mtllib none.mtl\no A\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvn 0 0 1\nusemtl X\nf 1/1/1 2/2/1 3/3/1 4/1/1\nf -1 -2 -3\n";
        const std::vector<std::pair<int, QByteArray>> seeds = {{0, obj}, {1, gltfJson}, {2, glb}, {3, cubeFbx(7400, true)}, {3, cubeFbx(7500, false)}, {1, testmodels::animatedGltf()}, {1, testmodels::skinnedGltf()}};
        QRandomGenerator rng(12345);
        int accepted = 0;
        for (const auto& [kind, base] : seeds) for (int iter = 0; iter < 400; ++iter) {
            QByteArray d = base;
            const int edits = 1 + int(rng.bounded(8));
            for (int e = 0; e < edits && !d.isEmpty(); ++e) {
                switch (rng.bounded(4)) {
                case 0: d[int(rng.bounded(quint32(d.size())))] = char(rng.bounded(256)); break;
                case 1: d.truncate(int(rng.bounded(quint32(d.size()) + 1))); break;
                case 2: { const int at = int(rng.bounded(quint32(d.size()))); d.insert(at, QByteArray(int(rng.bounded(16)), char(rng.bounded(256)))); break; }
                default: { const int at = int(rng.bounded(quint32(d.size()))); d.remove(at, int(rng.bounded(8))); break; }
                }
            }
            ImportResult r = kind == 0 ? importObjData(d, ".", "f") : kind == 1 || kind == 2 ? importGltfData(d, ".", "f") : importFbxData(d, ".", "f");
            if (!r) { QVERIFY(!r.error.isEmpty()); continue; }
            ++accepted;
            QVERIFY2(r.scene->validate().isEmpty(), qPrintable(r.scene->validate().join(";")));
            for (const auto& [id, m] : r.scene->meshes) { QString why; QVERIFY2(m->validate(&why), qPrintable(why)); }
            Scene3D dst; mergeImport(dst, *r.scene);                                   // and the result must be usable
            QVERIFY(dst.validate().isEmpty());
            for (int f : {0, 7, 30}) renderScene(evaluateScene(dst, {}, f), frameScene(dst, 1.0), QSize(32, 32));      // animated and skinned poses too
        }
        QVERIFY(accepted > 100);                                                       // many mutations are harmless, so the importers are not just rejecting everything
    }

    void aRunningImportCanBeCancelledAndReportsProgress() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile o(dir.filePath("big.obj")); QVERIFY(o.open(QIODevice::WriteOnly));
        QByteArray body;
        for (int i = 0; i < 60000; ++i) body += "v " + QByteArray::number(i % 97) + " " + QByteArray::number(i % 89) + " 0\n";
        body += "f 1 2 3\n";
        o.write(body); o.close();
        ImportControl stop; stop.cancel = true;
        const ImportResult c = importModelFile(o.fileName(), 24.0, &stop);
        QVERIFY(!c);
        QVERIFY2(c.error.contains("cancelled"), qPrintable(c.error));
        ImportControl go;
        const ImportResult ok = importModelFile(o.fileName(), 24.0, &go);                // same file, not cancelled: imports
        QVERIFY2(ok, qPrintable(ok.error));
        QVERIFY(go.percent.load() > 0);                                                  // progress was reported
        QVERIFY(!importCancelled());                                                     // the control is released after the call
    }

    void dispatchByExtensionAndMissingFiles() {
        QVERIFY(!importModelFile("/nonexistent/thing.glb"));
        QTemporaryDir dir; QFile f(dir.filePath("x.stl")); f.open(QIODevice::WriteOnly); f.write("solid"); f.close();
        ImportResult r = importModelFile(f.fileName());
        QVERIFY(!r && r.error.contains("Unsupported"));
        QFile o(dir.filePath("t.obj")); o.open(QIODevice::WriteOnly); o.write("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"); o.close();
        QVERIFY(importModelFile(o.fileName()));
    }
};

QTEST_MAIN(TestImport3D)
#include "test_import3d.moc"
