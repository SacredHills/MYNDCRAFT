#pragma once
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace testmodels {
// A triangle under Parent/Child with glTF animation: Child translates along +X and turns 180° about Y over one second.
inline QByteArray animatedGltf(bool cubic = false) {
    QByteArray b;
    auto f32 = [&](std::initializer_list<float> v) { for (float x : v) b.append(reinterpret_cast<const char*>(&x), 4); };
    f32({0,0,0, 1,0,0, 0,1,0});                         // 0..36 positions
    const quint16 idx[3] = {0, 1, 2};
    b.append(reinterpret_cast<const char*>(idx), 6);    // 36..42
    b.append(QByteArray(2, 0));                         // pad to 44
    f32({0, 0.5f, 1});                                  // 44..56 times
    f32({0,0,0, 1,0,0, 2,0,0});                         // 56..92 translations
    const float h = 0.70710678f;
    f32({0,0,0,1, 0,h,0,h, 0,1,0,0});                   // 92..140 rotations (0, 90, 180 degrees about Y)
    QJsonObject o;
    o["asset"] = QJsonObject{{"version", "2.0"}};
    o["buffers"] = QJsonArray{QJsonObject{{"byteLength", b.size()}, {"uri", "data:application/octet-stream;base64," + QString::fromLatin1(b.toBase64())}}};
    o["bufferViews"] = QJsonArray{QJsonObject{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 36}}, QJsonObject{{"buffer", 0}, {"byteOffset", 36}, {"byteLength", 6}},
                                  QJsonObject{{"buffer", 0}, {"byteOffset", 44}, {"byteLength", 12}}, QJsonObject{{"buffer", 0}, {"byteOffset", 56}, {"byteLength", 36}},
                                  QJsonObject{{"buffer", 0}, {"byteOffset", 92}, {"byteLength", 48}}};
    o["accessors"] = QJsonArray{QJsonObject{{"bufferView", 0}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}},
                                QJsonObject{{"bufferView", 1}, {"componentType", 5123}, {"count", 3}, {"type", "SCALAR"}},
                                QJsonObject{{"bufferView", 2}, {"componentType", 5126}, {"count", 3}, {"type", "SCALAR"}},
                                QJsonObject{{"bufferView", 3}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}},
                                QJsonObject{{"bufferView", 4}, {"componentType", 5126}, {"count", 3}, {"type", "VEC4"}}};
    o["meshes"] = QJsonArray{QJsonObject{{"name", "Tri"}, {"primitives", QJsonArray{QJsonObject{{"attributes", QJsonObject{{"POSITION", 0}}}, {"indices", 1}}}}}};
    o["nodes"] = QJsonArray{QJsonObject{{"name", "Parent"}, {"children", QJsonArray{1}}},
                            QJsonObject{{"name", "Child"}, {"mesh", 0}, {"scale", QJsonArray{2, 2, 2}}}};
    o["scenes"] = QJsonArray{QJsonObject{{"nodes", QJsonArray{0}}}};
    o["scene"] = 0;
    o["animations"] = QJsonArray{QJsonObject{{"name", "Spin"},
        {"samplers", QJsonArray{QJsonObject{{"input", 2}, {"output", 3}, {"interpolation", "LINEAR"}}, QJsonObject{{"input", 2}, {"output", 4}, {"interpolation", "LINEAR"}}}},
        {"channels", QJsonArray{QJsonObject{{"sampler", 0}, {"target", QJsonObject{{"node", 1}, {"path", "translation"}}}},
                                QJsonObject{{"sampler", 1}, {"target", QJsonObject{{"node", 1}, {"path", "rotation"}}}}}}}};
    (void)cubic;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
// A triangle skinned to two joints (J0 fixed, J1 child of J0) with J1 sliding +2 along X over one second.
inline QByteArray skinnedGltf() {
    QByteArray b;
    auto f32 = [&](std::initializer_list<float> v) { for (float x : v) b.append(reinterpret_cast<const char*>(&x), 4); };
    f32({0,0,0, 1,0,0, 0,1,0});                         // 0..36
    const quint16 idx[3] = {0, 1, 2};
    b.append(reinterpret_cast<const char*>(idx), 6); b.append(QByteArray(2, 0));        // 36..44
    const quint8 joints[12] = {0,0,0,0,  1,0,0,0,  0,1,0,0};
    b.append(reinterpret_cast<const char*>(joints), 12);                                // 44..56
    f32({1,0,0,0,  1,0,0,0,  0.5f,0.5f,0,0});                                           // 56..104 weights
    f32({1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1,  1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1});    // 104..232 inverse bind (identity)
    f32({0, 0.5f, 1});                                                                  // 232..244 times
    f32({0,0,0, 1,0,0, 2,0,0});                                                         // 244..280 translations
    QJsonObject o;
    o["asset"] = QJsonObject{{"version", "2.0"}};
    o["buffers"] = QJsonArray{QJsonObject{{"byteLength", b.size()}, {"uri", "data:application/octet-stream;base64," + QString::fromLatin1(b.toBase64())}}};
    auto view = [](int off, int len) { return QJsonObject{{"buffer", 0}, {"byteOffset", off}, {"byteLength", len}}; };
    o["bufferViews"] = QJsonArray{view(0, 36), view(36, 6), view(44, 12), view(56, 48), view(104, 128), view(232, 12), view(244, 36)};
    o["accessors"] = QJsonArray{QJsonObject{{"bufferView", 0}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}},
                                QJsonObject{{"bufferView", 1}, {"componentType", 5123}, {"count", 3}, {"type", "SCALAR"}},
                                QJsonObject{{"bufferView", 2}, {"componentType", 5121}, {"count", 3}, {"type", "VEC4"}},
                                QJsonObject{{"bufferView", 3}, {"componentType", 5126}, {"count", 3}, {"type", "VEC4"}},
                                QJsonObject{{"bufferView", 4}, {"componentType", 5126}, {"count", 2}, {"type", "MAT4"}},
                                QJsonObject{{"bufferView", 5}, {"componentType", 5126}, {"count", 3}, {"type", "SCALAR"}},
                                QJsonObject{{"bufferView", 6}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}}};
    o["meshes"] = QJsonArray{QJsonObject{{"name", "Tri"}, {"primitives", QJsonArray{QJsonObject{{"attributes", QJsonObject{{"POSITION", 0}, {"JOINTS_0", 2}, {"WEIGHTS_0", 3}}}, {"indices", 1}}}}}};
    o["skins"] = QJsonArray{QJsonObject{{"joints", QJsonArray{1, 2}}, {"inverseBindMatrices", 4}}};
    o["nodes"] = QJsonArray{QJsonObject{{"name", "Skinned"}, {"mesh", 0}, {"skin", 0}},
                            QJsonObject{{"name", "J0"}, {"children", QJsonArray{2}}},
                            QJsonObject{{"name", "J1"}}};
    o["scenes"] = QJsonArray{QJsonObject{{"nodes", QJsonArray{0, 1}}}};
    o["scene"] = 0;
    o["animations"] = QJsonArray{QJsonObject{{"name", "Slide"},
        {"samplers", QJsonArray{QJsonObject{{"input", 5}, {"output", 6}, {"interpolation", "LINEAR"}}}},
        {"channels", QJsonArray{QJsonObject{{"sampler", 0}, {"target", QJsonObject{{"node", 2}, {"path", "translation"}}}}}}}};
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
}  // namespace testmodels