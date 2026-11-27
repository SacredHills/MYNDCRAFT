#include "Scene3D/Render3D.h"

#include <QThread>
#include <QtConcurrent>

#include <QPainter>
#include <algorithm>
#include <cmath>

namespace mynd {

std::shared_ptr<const Mesh> skinnedMeshFor(const Scene3D& s, const Node3D& n) {
    if (n.skin.isNull() || n.kind != NodeKind::Mesh) return nullptr;
    auto mit = s.meshes.find(n.mesh);
    if (mit == s.meshes.end() || !mit->second->skinned()) return nullptr;
    const Skin* k = s.skinById(n.skin);
    if (!k || k->joints.empty() || k->inverseBind.size() != k->joints.size()) return nullptr;
    const Mesh& m = *mit->second;
    std::vector<QMatrix4x4> M(k->joints.size());
    for (size_t j = 0; j < M.size(); ++j) M[j] = s.node(k->joints[j]) ? s.worldMatrix(k->joints[j]) * k->inverseBind[j] : QMatrix4x4();
    auto out = std::make_shared<Mesh>(m);
    out->joints.clear(); out->weights.clear();
    const bool hasN = m.normals.size() == m.positions.size();
    for (size_t v = 0; v < m.positions.size(); ++v) {
        QVector3D p, nr;
        float sum = 0;
        for (int i = 0; i < 4; ++i) {
            const float w = m.weights[v][size_t(i)];
            if (w <= 0) continue;
            const size_t j = m.joints[v][size_t(i)];
            if (j >= M.size()) continue;
            p += M[j].map(m.positions[v]) * w;
            if (hasN) nr += M[j].mapVector(m.normals[v]) * w;
            sum += w;
        }
        if (sum <= 1e-6f) { p = m.positions[v]; nr = hasN ? m.normals[v] : QVector3D(); sum = 1; }
        out->positions[v] = p / sum;
        if (hasN) out->normals[v] = nr.isNull() ? m.normals[v] : nr.normalized();
    }
    return out;
}


// ============================================================ cameras
QVector3D OrbitCamera::eye() const {
    const double y = yawDeg * M_PI / 180, p = pitchDeg * M_PI / 180;
    return target + QVector3D(float(std::cos(p) * std::sin(y)), float(std::sin(p)), float(std::cos(p) * std::cos(y))) * float(distance);
}
RenderCamera OrbitCamera::toCamera(double aspect) const {
    RenderCamera c;
    c.eye = eye();
    c.nearPlane = std::max(1e-3, distance * 0.01);
    c.farPlane = std::max(100.0, distance * 200);
    c.view.setToIdentity();
    c.view.lookAt(c.eye, target, QVector3D(0, 1, 0));
    c.proj.setToIdentity();
    c.proj.perspective(float(fov), float(aspect), float(c.nearPlane), float(c.farPlane));
    return c;
}
void OrbitCamera::frame(const QVector3D& mn, const QVector3D& mx) {
    target = (mn + mx) * 0.5f;
    const double radius = std::max(0.05, double((mx - mn).length()) * 0.5);
    distance = radius / std::sin(fov * M_PI / 360.0) * 1.1;
}

RenderCamera sceneCamera(const Scene3D& s, double aspect, const QUuid& cameraNode) {
    const Node3D* n = s.node(cameraNode.isNull() ? s.activeCamera : cameraNode);
    if (!n || n->kind != NodeKind::Camera) return frameScene(s, aspect);
    RenderCamera c;
    const QMatrix4x4 w = s.worldMatrix(n->id);
    QMatrix4x4 rigid = w;                      // remove scale so a scaled parent does not skew the view
    QVector3D t, e, sc;
    decomposeMatrix(w, &t, &e, &sc);
    rigid.setToIdentity();
    rigid.translate(t);
    rigid.rotate(e.z(), 0, 0, 1); rigid.rotate(e.y(), 0, 1, 0); rigid.rotate(e.x(), 1, 0, 0);
    c.view = rigid.inverted();
    c.eye = t;
    c.nearPlane = n->nearPlane; c.farPlane = n->farPlane;
    c.proj.setToIdentity();
    c.proj.perspective(float(n->fov), float(aspect), float(n->nearPlane), float(n->farPlane));
    return c;
}
RenderCamera frameScene(const Scene3D& s, double aspect) {
    OrbitCamera o;
    QVector3D mn, mx;
    if (s.worldBounds(&mn, &mx)) o.frame(mn, mx);
    return o.toCamera(aspect);
}

// ============================================================ projection helpers
bool projectToScreen(const RenderCamera& cam, const QVector3D& world, const QSize& size, QPointF* out, double* depth) {
    const QVector4D c = cam.proj * cam.view * QVector4D(world, 1);
    if (c.w() <= 1e-6f) return false;
    if (out) *out = QPointF((c.x() / c.w() * 0.5 + 0.5) * size.width(), (1 - (c.y() / c.w() * 0.5 + 0.5)) * size.height());
    if (depth) *depth = c.w();
    return true;
}
Ray screenRay(const RenderCamera& cam, const QSize& size, const QPointF& p) {
    const QMatrix4x4 inv = (cam.proj * cam.view).inverted();
    const float nx = float(p.x() / size.width() * 2 - 1), ny = float(1 - p.y() / size.height() * 2);
    auto unp = [&](float z) { const QVector4D v = inv * QVector4D(nx, ny, z, 1); return QVector3D(v.x(), v.y(), v.z()) / v.w(); };
    const QVector3D a = unp(-1), b = unp(1);
    return {cam.eye, (b - a).normalized()};
}

// ============================================================ shading
namespace {

struct Light {
    LightType type;
    QVector3D color;       // colour * intensity, 0..n
    QVector3D pos, dir;    // dir = direction the light travels
    double range, cosOuter;
};

struct ShadeMat {
    QVector3D base; float alpha;
    QVector3D emissive;
    float metallic, roughness;
    bool doubleSided;
    bool unlit = false;
    const QImage* tex = nullptr;
};

QVector3D rgb(const QColor& c) { return QVector3D(float(c.redF()), float(c.greenF()), float(c.blueF())); }

// Bilinear sample, repeat wrapping, v up. Returns straight (non-premultiplied) rgba in 0..1.
QVector4D sampleTex(const QImage& img, float u, float v) {
    const int w = img.width(), h = img.height();
    float fx = u * w - 0.5f, fy = (1 - v) * h - 0.5f;
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    const float tx = fx - x0, ty = fy - y0;
    auto texel = [&](int x, int y) {
        x = ((x % w) + w) % w; y = ((y % h) + h) % h;
        const QRgb c = reinterpret_cast<const QRgb*>(img.constScanLine(y))[x];
        return QVector4D(qRed(c) / 255.f, qGreen(c) / 255.f, qBlue(c) / 255.f, qAlpha(c) / 255.f);
    };
    return (texel(x0, y0) * (1 - tx) + texel(x0 + 1, y0) * tx) * (1 - ty) + (texel(x0, y0 + 1) * (1 - tx) + texel(x0 + 1, y0 + 1) * tx) * ty;
}

struct Frag { QVector3D color; float alpha; };

Frag shade(const ShadeMat& m, QVector3D P, QVector3D N, const QVector2D& uv, bool hasUv, bool backFacing, const QVector3D& eye,
           const std::vector<Light>& lights, const QVector3D& ambient) {
    QVector3D base = m.base;
    float alpha = m.alpha;
    if (m.tex && hasUv) {
        const QVector4D t = sampleTex(*m.tex, uv.x(), uv.y());
        base = QVector3D(base.x() * t.x(), base.y() * t.y(), base.z() * t.z());
        alpha *= t.w();
    }
    if (m.unlit) return {QVector3D(std::clamp(base.x() + m.emissive.x(), 0.f, 1.f), std::clamp(base.y() + m.emissive.y(), 0.f, 1.f), std::clamp(base.z() + m.emissive.z(), 0.f, 1.f)), alpha};
    if (backFacing) N = -N;
    N.normalize();
    const QVector3D V = (eye - P).normalized();
    const float metal = m.metallic, rough = m.roughness;
    const QVector3D diffuseCol = base * (1 - metal);
    const QVector3D F0 = QVector3D(0.04f, 0.04f, 0.04f) * (1 - metal) + base * metal;
    const float a = rough * rough;
    const float shin = std::clamp(2.0f / (a * a + 1e-4f) - 2.0f, 2.0f, 512.0f);
    const float specAmt = 0.25f + 0.75f * (1 - rough);
    QVector3D out = m.emissive + diffuseCol * ambient + F0 * ambient * (metal * 0.8f);
    for (const Light& l : lights) {
        QVector3D L; float att = 1;
        if (l.type == LightType::Directional) L = -l.dir;
        else {
            QVector3D d = l.pos - P;
            const float dist = d.length();
            L = dist > 1e-6f ? d / dist : QVector3D(0, 1, 0);
            if (l.range > 0) { const float t = std::max(0.0f, 1 - dist / float(l.range)); att = t * t; }
            if (l.type == LightType::Spot) {
                const float c = QVector3D::dotProduct(-L, l.dir);
                att *= std::clamp(float((c - l.cosOuter) / (0.15 * (1 - l.cosOuter) + 1e-4)), 0.0f, 1.0f);
            }
        }
        const float ndl = QVector3D::dotProduct(N, L);
        if (ndl <= 0 || att <= 0) continue;
        const QVector3D H = (L + V).normalized();
        const float ndh = std::max(0.0f, QVector3D::dotProduct(N, H));
        const QVector3D lc = l.color * att;
        out += QVector3D(diffuseCol.x() * lc.x(), diffuseCol.y() * lc.y(), diffuseCol.z() * lc.z()) * ndl;
        out += QVector3D(F0.x() * lc.x(), F0.y() * lc.y(), F0.z() * lc.z()) * (std::pow(ndh, shin) * specAmt);
    }
    return {QVector3D(std::clamp(out.x(), 0.f, 1.f), std::clamp(out.y(), 0.f, 1.f), std::clamp(out.z(), 0.f, 1.f)), alpha};
}

// ---- rasteriser ----
struct Vtx {
    QVector4D clip;
    QVector3D wpos, nrm;
    QVector2D uv;
};

// A render target restricted to the rows [y0, y1]. Bands never overlap, so threads can each rasterise into their own band of the same
// image and depth buffer, and every pixel still sees the triangles in the same order as in a single-threaded render (identical result).
struct Target {
    QImage* img; std::vector<float>* depth; int w, h;
    int y0 = 0, y1 = -1;     // inclusive; y1 < y0 means the whole image
    int rowMin() const { return y1 < y0 ? 0 : y0; }
    int rowMax() const { return y1 < y0 ? h - 1 : y1; }
};

Vtx lerp(const Vtx& a, const Vtx& b, float t) {
    return {a.clip * (1 - t) + b.clip * t, a.wpos * (1 - t) + b.wpos * t, a.nrm * (1 - t) + b.nrm * t, a.uv * (1 - t) + b.uv * t};
}

void rasterTriangle(Target& T, const Vtx& v0, const Vtx& v1, const Vtx& v2, const ShadeMat& mat, bool hasUv, const QVector3D& eye,
                    const std::vector<Light>& lights, const QVector3D& ambient) {
    const Vtx* v[3] = {&v0, &v1, &v2};
    float sx[3], sy[3], sz[3], iw[3];
    float nx[3], ny[3];
    for (int i = 0; i < 3; ++i) {
        const float w = v[i]->clip.w();
        iw[i] = 1.0f / w;
        nx[i] = v[i]->clip.x() * iw[i]; ny[i] = v[i]->clip.y() * iw[i];
        sz[i] = v[i]->clip.z() * iw[i];
        sx[i] = (nx[i] * 0.5f + 0.5f) * T.w;
        sy[i] = (1 - (ny[i] * 0.5f + 0.5f)) * T.h;
    }
    const float ndcArea = (nx[1] - nx[0]) * (ny[2] - ny[0]) - (ny[1] - ny[0]) * (nx[2] - nx[0]);
    if (ndcArea == 0) return;
    const bool front = ndcArea > 0;
    if (!front && !mat.doubleSided) return;
    const float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sy[1] - sy[0]) * (sx[2] - sx[0]);
    if (std::abs(area) < 1e-9f) return;
    const int minX = std::max(0, int(std::floor(std::min({sx[0], sx[1], sx[2]})))), maxX = std::min(T.w - 1, int(std::ceil(std::max({sx[0], sx[1], sx[2]}))));
    const int minY = std::max(T.rowMin(), int(std::floor(std::min({sy[0], sy[1], sy[2]})))), maxY = std::min(T.rowMax(), int(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
    const float invArea = 1.0f / area;
    for (int y = minY; y <= maxY; ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(T.img->scanLine(y));
        float* drow = T.depth->data() + size_t(y) * T.w;
        for (int x = minX; x <= maxX; ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            const float b0 = ((sx[1] - px) * (sy[2] - py) - (sy[1] - py) * (sx[2] - px)) * invArea;
            const float b1 = ((sx[2] - px) * (sy[0] - py) - (sy[2] - py) * (sx[0] - px)) * invArea;
            const float b2 = 1.0f - b0 - b1;
            if (b0 < -1e-5f || b1 < -1e-5f || b2 < -1e-5f) continue;
            const float z = b0 * sz[0] + b1 * sz[1] + b2 * sz[2];
            if (z < -1.0f || z > 1.0f || z >= drow[x]) continue;
            const float p0 = b0 * iw[0], p1 = b1 * iw[1], p2 = b2 * iw[2];
            const float ps = p0 + p1 + p2;
            if (ps <= 0) continue;
            const float q0 = p0 / ps, q1 = p1 / ps, q2 = p2 / ps;
            const QVector3D P = v0.wpos * q0 + v1.wpos * q1 + v2.wpos * q2;
            const QVector3D N = v0.nrm * q0 + v1.nrm * q1 + v2.nrm * q2;
            const QVector2D uv = v0.uv * q0 + v1.uv * q1 + v2.uv * q2;
            const Frag f = shade(mat, P, N, uv, hasUv, !front, eye, lights, ambient);
            if (f.alpha <= 0.004f) continue;
            const int r = int(f.color.x() * 255 + 0.5f), g = int(f.color.y() * 255 + 0.5f), b = int(f.color.z() * 255 + 0.5f);
            if (f.alpha >= 0.996f) {
                row[x] = qRgba(r, g, b, 255);
                drow[x] = z;
            } else {                                         // blended over what is there; does not write depth
                const QRgb d = row[x];
                const float a = f.alpha, ia = 1 - a;
                row[x] = qRgba(int(r * a + qRed(d) * ia + 0.5f), int(g * a + qGreen(d) * ia + 0.5f), int(b * a + qBlue(d) * ia + 0.5f),
                               int(255 * a + qAlpha(d) * ia + 0.5f));
            }
        }
    }
}

void drawClipped(Target& T, const Vtx& a, const Vtx& b, const Vtx& c, const ShadeMat& mat, bool hasUv, const QVector3D& eye,
                 const std::vector<Light>& lights, const QVector3D& ambient) {
    // clip against the near plane (z >= -w); far plane handled by the depth range test
    Vtx in[3] = {a, b, c};
    Vtx poly[4];
    int n = 0;
    for (int i = 0; i < 3; ++i) {
        const Vtx& p = in[i]; const Vtx& q = in[(i + 1) % 3];
        const float dp = p.clip.z() + p.clip.w(), dq = q.clip.z() + q.clip.w();
        if (dp >= 0) poly[n++] = p;
        if ((dp >= 0) != (dq >= 0)) poly[n++] = lerp(p, q, dp / (dp - dq));
    }
    for (int i = 1; i + 1 < n; ++i) {
        if (poly[0].clip.w() <= 1e-7f || poly[i].clip.w() <= 1e-7f || poly[i + 1].clip.w() <= 1e-7f) continue;
        rasterTriangle(T, poly[0], poly[i], poly[i + 1], mat, hasUv, eye, lights, ambient);
    }
}

}  // namespace

QImage renderScene(const Scene3D& s, const RenderCamera& cam, const QSize& size, const RenderOptions& opt) {
    const int ss = std::clamp(opt.supersample, 1, 3);
    const int W = std::max(1, size.width() * ss), H = std::max(1, size.height() * ss);
    QImage img(W, H, QImage::Format_ARGB32_Premultiplied);
    img.fill(opt.drawBackground ? s.background.rgba() : 0);
    if (opt.drawBackground && s.background.alpha() < 255) {
        QColor bg = s.background; img.fill(qPremultiply(bg.rgba()));
    }
    std::vector<float> depth(size_t(W) * H, 2.0f);
    Target T{&img, &depth, W, H};
    // Horizontal bands, a few per thread so uneven scenes still balance. Small images are not worth the hand-over.
    const int threads = opt.threads > 0 ? opt.threads : QThread::idealThreadCount();
    std::vector<Target> bands;
    if (threads > 1 && qint64(W) * H >= 40000) {
        const int n = std::min(threads * 4, std::max(1, H / 16));
        for (int i = 0; i < n; ++i) { Target b = T; b.y0 = H * i / n; b.y1 = H * (i + 1) / n - 1; if (b.y1 >= b.y0) bands.push_back(b); }
    }
    if (bands.size() < 2) bands.assign(1, T);

    // lights
    std::vector<Light> lights;
    for (const auto& n : s.nodes) {
        if (n.kind != NodeKind::Light || !s.effectivelyVisible(n.id)) continue;
        const QMatrix4x4 w = s.worldMatrix(n.id);
        Light l;
        l.type = n.lightType;
        l.color = rgb(n.lightColor) * float(n.intensity);
        l.pos = w.map(QVector3D(0, 0, 0));
        QVector3D d = w.mapVector(QVector3D(0, 0, -1));
        l.dir = d.isNull() ? QVector3D(0, -1, 0) : d.normalized();
        l.range = n.range;
        l.cosOuter = std::cos(n.spotAngle * M_PI / 360.0);
        lights.push_back(l);
    }
    const QVector3D ambient = rgb(s.ambient) * float(s.ambientIntensity);

    // textures as straight-alpha ARGB32 so sampling needs no unpremultiply
    std::map<QUuid, QImage> tex;
    for (const auto& [id, im] : s.textures) if (!im.isNull()) tex[id] = im.convertToFormat(QImage::Format_ARGB32);

    const QMatrix4x4 vp = cam.proj * cam.view;
    // Opaque meshes first, then see-through ones (cards, translucent materials) from far to near, so overlapping translucent
    // surfaces blend in a sensible order. (Per node, not per triangle: intersecting translucent meshes can still blend wrongly.)
    auto translucent = [&](const Node3D& n) {
        if (!n.cardLayer.isNull()) return true;
        if (const Material* m = s.material(n.materialOverride)) return m->baseColor.alpha() < 255;
        auto it = s.meshes.find(n.mesh);
        if (it != s.meshes.end()) for (const MeshPart& p : it->second->parts) if (const Material* m = s.material(p.material)) if (m->baseColor.alpha() < 255) return true;
        return false;
    };
    std::vector<const Node3D*> order, late;
    for (const auto& n : s.nodes) (translucent(n) ? late : order).push_back(&n);
    std::sort(late.begin(), late.end(), [&](const Node3D* a, const Node3D* b) {
        return (s.worldMatrix(a->id).map(QVector3D(0, 0, 0)) - cam.eye).lengthSquared() > (s.worldMatrix(b->id).map(QVector3D(0, 0, 0)) - cam.eye).lengthSquared();
    });
    order.insert(order.end(), late.begin(), late.end());
    for (const Node3D* np : order) {
        const Node3D& n = *np;
        if (n.kind != NodeKind::Mesh || !s.effectivelyVisible(n.id)) continue;
        auto mit = s.meshes.find(n.mesh);
        if (mit == s.meshes.end()) continue;
        const std::shared_ptr<const Mesh> skinnedHolder = skinnedMeshFor(s, n);
        const Mesh& mesh = skinnedHolder ? *skinnedHolder : *mit->second;
        const QMatrix4x4 world = skinnedHolder ? QMatrix4x4() : s.worldMatrix(n.id);
        const QMatrix4x4 mvp = vp * world;
        const QMatrix3x3 nm = world.normalMatrix();
        const bool hasN = mesh.normals.size() == mesh.positions.size(), hasUv = mesh.uvs.size() == mesh.positions.size();
        std::vector<Vtx> vs(mesh.positions.size());
        for (size_t i = 0; i < vs.size(); ++i) {
            const QVector3D& p = mesh.positions[i];
            vs[i].clip = mvp * QVector4D(p, 1);
            vs[i].wpos = world.map(p);
            if (hasN) {
                const QVector3D& q = mesh.normals[i];
                vs[i].nrm = QVector3D(nm(0, 0) * q.x() + nm(0, 1) * q.y() + nm(0, 2) * q.z(), nm(1, 0) * q.x() + nm(1, 1) * q.y() + nm(1, 2) * q.z(),
                                      nm(2, 0) * q.x() + nm(2, 1) * q.y() + nm(2, 2) * q.z());
            }
            if (hasUv) vs[i].uv = mesh.uvs[i];
        }
        const bool mirrored = world.determinant() < 0;
        for (const MeshPart& part : mesh.parts) {
            const Material* mat = s.material(n.materialOverride.isNull() ? part.material : n.materialOverride);
            ShadeMat sm;
            if (mat) {
                sm.base = rgb(mat->baseColor); sm.alpha = float(mat->baseColor.alphaF()); sm.emissive = rgb(mat->emissive);
                sm.metallic = float(mat->metallic); sm.roughness = float(mat->roughness); sm.doubleSided = mat->doubleSided; sm.unlit = mat->unlit;
                auto t = tex.find(mat->baseTexture);
                if (t != tex.end()) sm.tex = &t->second;
            } else { sm.base = QVector3D(0.78f, 0.78f, 0.78f); sm.alpha = 1; sm.metallic = 0; sm.roughness = 0.6f; sm.doubleSided = false; }
            auto drawTriangles = [&](Target& B) {
            for (int i = part.firstIndex; i + 2 < part.firstIndex + part.indexCount; i += 3) {
                const uint32_t i0 = mesh.indices[size_t(i)], i1 = mesh.indices[size_t(i) + 1], i2 = mesh.indices[size_t(i) + 2];
                {   // skip triangles that lie wholly outside this band (only when no near-plane clipping is needed)
                    const QVector4D &c0 = vs[i0].clip, &c1 = vs[i1].clip, &c2 = vs[i2].clip;
                    if (c0.w() > 1e-7f && c1.w() > 1e-7f && c2.w() > 1e-7f) {
                        const float y0s = (1 - (c0.y() / c0.w() * 0.5f + 0.5f)) * B.h, y1s = (1 - (c1.y() / c1.w() * 0.5f + 0.5f)) * B.h, y2s = (1 - (c2.y() / c2.w() * 0.5f + 0.5f)) * B.h;
                        if (std::max({y0s, y1s, y2s}) < B.rowMin() - 1 || std::min({y0s, y1s, y2s}) > B.rowMax() + 1) continue;
                    }
                }
                Vtx a = vs[i0], b = vs[i1], c = vs[i2];
                if (!hasN) {                                        // flat normal
                    QVector3D fn = QVector3D::crossProduct(b.wpos - a.wpos, c.wpos - a.wpos);
                    if (mirrored) fn = -fn;
                    fn.normalize();
                    a.nrm = b.nrm = c.nrm = fn;
                } else if (mirrored) { a.nrm = -a.nrm; b.nrm = -b.nrm; c.nrm = -c.nrm; }
                if (mirrored) std::swap(b, c);                       // keep the front face counter-clockwise
                drawClipped(B, a, b, c, sm, hasUv, cam.eye, lights, ambient);
            }
            };
            if (bands.size() == 1) drawTriangles(bands[0]);
            else QtConcurrent::blockingMap(bands, drawTriangles);
        }
    }
    if (ss == 1) return img;
    return img.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

// ============================================================ picking
namespace {
bool rayTriangle(const QVector3D& o, const QVector3D& d, const QVector3D& a, const QVector3D& b, const QVector3D& c, float* t) {
    const QVector3D e1 = b - a, e2 = c - a, p = QVector3D::crossProduct(d, e2);
    const float det = QVector3D::dotProduct(e1, p);
    if (std::abs(det) < 1e-12f) return false;
    const float inv = 1 / det;
    const QVector3D s = o - a;
    const float u = QVector3D::dotProduct(s, p) * inv;
    if (u < 0 || u > 1) return false;
    const QVector3D q = QVector3D::crossProduct(s, e1);
    const float v = QVector3D::dotProduct(d, q) * inv;
    if (v < 0 || u + v > 1) return false;
    const float tt = QVector3D::dotProduct(e2, q) * inv;
    if (tt <= 0) return false;
    *t = tt;
    return true;
}
}  // namespace

PickHit pickNode(const Scene3D& s, const RenderCamera& cam, const QSize& size, const QPointF& p) {
    const Ray ray = screenRay(cam, size, p);
    PickHit best;
    best.distance = 1e30;
    for (const auto& n : s.nodes) {
        if (!s.effectivelyVisible(n.id)) continue;
        const QMatrix4x4 world = s.worldMatrix(n.id);
        if (n.kind == NodeKind::Mesh) {
            auto mit = s.meshes.find(n.mesh);
            if (mit == s.meshes.end()) continue;
            const std::shared_ptr<const Mesh> skinnedHolder = skinnedMeshFor(s, n);
            const QMatrix4x4 modelM = skinnedHolder ? QMatrix4x4() : world;
            const QMatrix4x4 inv = modelM.inverted();
            const QVector3D o = inv.map(ray.origin), d = inv.mapVector(ray.dir);
            const Mesh& m = skinnedHolder ? *skinnedHolder : *mit->second;
            for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
                float t;
                if (!rayTriangle(o, d, m.positions[m.indices[i]], m.positions[m.indices[i + 1]], m.positions[m.indices[i + 2]], &t)) continue;
                const double dist = double((modelM.map(o + d * t) - ray.origin).length());
                if (dist < best.distance) { best.distance = dist; best.node = n.id; }
            }
        } else if (n.kind == NodeKind::Light || n.kind == NodeKind::Camera) {
            const QVector3D c = world.map(QVector3D(0, 0, 0));
            const QVector3D to = c - ray.origin;
            const float along = QVector3D::dotProduct(to, ray.dir);
            if (along <= 0) continue;
            const float off = (to - ray.dir * along).length();
            if (off < 0.06f * along + 0.05f && along < best.distance) { best.distance = along; best.node = n.id; }
        }
    }
    if (best.node.isNull()) best.distance = 0;
    return best;
}

}  // namespace mynd
