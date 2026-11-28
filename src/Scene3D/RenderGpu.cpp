#include "Scene3D/RenderGpu.h"

#include <QCoreApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <unordered_map>

namespace mynd::gpu3d {

namespace {

const char* kVert = R"(#version 330 core
layout(location = 0) in vec3 aP;
layout(location = 1) in vec3 aN;
layout(location = 2) in vec2 aUv;
uniform mat4 uModel;
uniform mat4 uVP;
uniform mat3 uNormalMat;
uniform float uNFlip;
out vec3 vP;
out vec3 vN;
out vec2 vUv;
void main() {
    vec4 w = uModel * vec4(aP, 1.0);
    vP = w.xyz;
    vN = uNormalMat * aN * uNFlip;
    vUv = aUv;
    gl_Position = uVP * w;
})";

// Port of shade() in Render3D.cpp.
const char* kFrag = R"(#version 330 core
in vec3 vP;
in vec3 vN;
in vec2 vUv;
uniform vec3 uBase;
uniform float uAlpha;
uniform vec3 uEmissive;
uniform float uMetal;
uniform float uRough;
uniform int uUnlit;
uniform int uHasTex;
uniform int uHasUv;
uniform vec4 uTexXf;          // repeat u, repeat v, offset u, offset v
uniform sampler2D uTex;
uniform vec3 uEye;
uniform vec3 uAmbient;
uniform int uNumLights;
uniform int uLType[16];       // 0 directional, 1 point, 2 spot
uniform vec3 uLColor[16];
uniform vec3 uLPos[16];
uniform vec3 uLDir[16];
uniform float uLRange[16];
uniform float uLCosOuter[16];
out vec4 outColor;
void main() {
    vec3 base = uBase;
    float alpha = uAlpha;
    if (uHasTex != 0 && uHasUv != 0) {
        vec2 t = vec2(vUv.x * uTexXf.x + uTexXf.z, 1.0 - (vUv.y * uTexXf.y + uTexXf.w));
        vec4 tc = texture(uTex, t);
        base *= tc.rgb;
        alpha *= tc.a;
    }
    if (alpha <= 0.004) discard;
    vec3 result;
    if (uUnlit != 0) {
        result = clamp(base + uEmissive, 0.0, 1.0);
    } else {
        vec3 N = vN;
        if (!gl_FrontFacing) N = -N;
        N = normalize(N);
        vec3 V = normalize(uEye - vP);
        float metal = uMetal, rough = uRough;
        vec3 diffuseCol = base * (1.0 - metal);
        vec3 F0 = vec3(0.04) * (1.0 - metal) + base * metal;
        float a = rough * rough;
        float shin = clamp(2.0 / (a * a + 1e-4) - 2.0, 2.0, 512.0);
        float specAmt = 0.25 + 0.75 * (1.0 - rough);
        vec3 o = uEmissive + diffuseCol * uAmbient + F0 * uAmbient * (metal * 0.8);
        for (int i = 0; i < uNumLights; ++i) {
            vec3 L; float att = 1.0;
            if (uLType[i] == 0) L = -uLDir[i];
            else {
                vec3 d = uLPos[i] - vP;
                float dist = length(d);
                L = dist > 1e-6 ? d / dist : vec3(0.0, 1.0, 0.0);
                if (uLRange[i] > 0.0) { float t = max(0.0, 1.0 - dist / uLRange[i]); att = t * t; }
                if (uLType[i] == 2) {
                    float c = dot(-L, uLDir[i]);
                    att *= clamp((c - uLCosOuter[i]) / (0.15 * (1.0 - uLCosOuter[i]) + 1e-4), 0.0, 1.0);
                }
            }
            float ndl = dot(N, L);
            if (ndl <= 0.0 || att <= 0.0) continue;
            vec3 H = normalize(L + V);
            float ndh = max(0.0, dot(N, H));
            vec3 lc = uLColor[i] * att;
            o += diffuseCol * lc * ndl;
            o += F0 * lc * (pow(ndh, shin) * specAmt);
        }
        result = clamp(o, 0.0, 1.0);
    }
    outColor = vec4(result, alpha >= 0.996 ? 1.0 : alpha);
})";

struct Light {
    int type; QVector3D color, pos, dir; float range, cosOuter;
};

struct GpuMesh {
    std::weak_ptr<const Mesh> owner;
    GLuint vao = 0, vbo = 0, ebo = 0;
    int vcount = 0, icount = 0;
    bool flat = false;                  // no normals in the mesh: triangle-expanded vertices with flat normals, drawn with glDrawArrays
    size_t bytes = 0;
};

class Worker : public QObject {
public:
    explicit Worker(QOffscreenSurface* s) : m_surface(s) {}
    ~Worker() override { teardown(); }

    bool init() {
        QSurfaceFormat f; f.setVersion(3, 3); f.setProfile(QSurfaceFormat::CoreProfile);
        m_ctx = std::make_unique<QOpenGLContext>();
        m_ctx->setFormat(f);
        if (!m_ctx->create() || !m_surface->isValid() || !m_ctx->makeCurrent(m_surface)) { m_ctx.reset(); return false; }
        m_gl = std::make_unique<QOpenGLFunctions_3_3_Core>();
        if (!m_gl->initializeOpenGLFunctions()) { teardown(); return false; }
        m_gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTex);
        m_gl->glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &m_maxRb);
        m_renderer = QString::fromLatin1(reinterpret_cast<const char*>(m_gl->glGetString(GL_RENDERER)));
        const bool ok = buildProgram();
        m_ctx->doneCurrent();
        if (!ok) { teardown(); return false; }
        return true;
    }
    QString renderer() const { return m_renderer; }

    QImage render(const Scene3D& s, const RenderCamera& cam, const QSize& size, const RenderOptions& opt);

private:
    QOffscreenSurface* m_surface;
    std::unique_ptr<QOpenGLContext> m_ctx;
    std::unique_ptr<QOpenGLFunctions_3_3_Core> m_gl;
    QString m_renderer;
    GLint m_maxTex = 0, m_maxRb = 0;
    GLuint m_prog = 0, m_fbo = 0, m_rbColor = 0, m_rbDepth = 0;
    int m_fbW = 0, m_fbH = 0;
    std::unordered_map<std::string, GLint> m_loc;
    std::unordered_map<const Mesh*, GpuMesh> m_meshes;
    size_t m_meshBytes = 0;
    struct GpuTex { GLuint id = 0; size_t bytes = 0; bool hasAlpha = false; };
    std::map<qint64, GpuTex> m_texs;
    size_t m_texBytes = 0;
    bool m_error = false;

    void teardown() {
        if (m_ctx && m_gl && m_ctx->makeCurrent(m_surface)) {
            clearCaches();
            if (m_prog) m_gl->glDeleteProgram(m_prog);
            if (m_fbo) m_gl->glDeleteFramebuffers(1, &m_fbo);
            if (m_rbColor) m_gl->glDeleteRenderbuffers(1, &m_rbColor);
            if (m_rbDepth) m_gl->glDeleteRenderbuffers(1, &m_rbDepth);
            m_ctx->doneCurrent();
        }
        m_gl.reset(); m_ctx.reset(); m_prog = m_fbo = m_rbColor = m_rbDepth = 0;
    }
    void clearCaches() {
        for (auto& [k, g] : m_meshes) freeMesh(g);
        m_meshes.clear(); m_meshBytes = 0;
        for (auto& [k, t] : m_texs) m_gl->glDeleteTextures(1, &t.id);
        m_texs.clear(); m_texBytes = 0;
    }
    void freeMesh(GpuMesh& g) {
        if (g.vao) m_gl->glDeleteVertexArrays(1, &g.vao);
        if (g.vbo) m_gl->glDeleteBuffers(1, &g.vbo);
        if (g.ebo) m_gl->glDeleteBuffers(1, &g.ebo);
        g = GpuMesh();
    }
    GLuint compile(GLenum type, const char* src) {
        GLuint sh = m_gl->glCreateShader(type);
        m_gl->glShaderSource(sh, 1, &src, nullptr);
        m_gl->glCompileShader(sh);
        GLint ok = 0; m_gl->glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[2048]; m_gl->glGetShaderInfoLog(sh, sizeof log, nullptr, log); qWarning("GPU 3D: shader error: %s", log); m_gl->glDeleteShader(sh); return 0; }
        return sh;
    }
    bool buildProgram() {
        const GLuint v = compile(GL_VERTEX_SHADER, kVert), f = compile(GL_FRAGMENT_SHADER, kFrag);
        if (!v || !f) return false;
        m_prog = m_gl->glCreateProgram();
        m_gl->glAttachShader(m_prog, v); m_gl->glAttachShader(m_prog, f);
        m_gl->glLinkProgram(m_prog);
        m_gl->glDeleteShader(v); m_gl->glDeleteShader(f);
        GLint ok = 0; m_gl->glGetProgramiv(m_prog, GL_LINK_STATUS, &ok);
        if (!ok) { char log[2048]; m_gl->glGetProgramInfoLog(m_prog, sizeof log, nullptr, log); qWarning("GPU 3D: link error: %s", log); }
        return ok;
    }
    GLint loc(const char* name) {
        auto it = m_loc.find(name);
        if (it != m_loc.end()) return it->second;
        const GLint l = m_gl->glGetUniformLocation(m_prog, name);
        m_loc[name] = l;
        return l;
    }
    void u1i(const char* n, int v) { m_gl->glUniform1i(loc(n), v); }
    void u1f(const char* n, float v) { m_gl->glUniform1f(loc(n), v); }
    void u3f(const char* n, const QVector3D& v) { m_gl->glUniform3f(loc(n), v.x(), v.y(), v.z()); }
    void u4f(const char* n, float a, float b, float c, float d) { m_gl->glUniform4f(loc(n), a, b, c, d); }
    void umat4(const char* n, const QMatrix4x4& m) { m_gl->glUniformMatrix4fv(loc(n), 1, GL_FALSE, m.constData()); }
    void umat3(const char* n, const QMatrix3x3& m) { m_gl->glUniformMatrix3fv(loc(n), 1, GL_FALSE, m.constData()); }

    bool ensureFramebuffer(int w, int h) {
        if (w > m_maxRb || h > m_maxRb) return false;
        if (!m_fbo) { m_gl->glGenFramebuffers(1, &m_fbo); m_gl->glGenRenderbuffers(1, &m_rbColor); m_gl->glGenRenderbuffers(1, &m_rbDepth); }
        if (w != m_fbW || h != m_fbH) {
            m_gl->glBindRenderbuffer(GL_RENDERBUFFER, m_rbColor);
            m_gl->glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
            m_gl->glBindRenderbuffer(GL_RENDERBUFFER, m_rbDepth);
            m_gl->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, w, h);
            m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
            m_gl->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_rbColor);
            m_gl->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_rbDepth);
            m_fbW = w; m_fbH = h;
        }
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        return m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }

    // Texture of a scene image (straight alpha, repeat, bilinear: what the CPU sampler does). Cached by the image's identity.
    const GpuTex* textureFor(const QImage& img) {
        if (img.isNull() || img.width() > m_maxTex || img.height() > m_maxTex) return nullptr;
        const qint64 key = img.cacheKey();
        auto it = m_texs.find(key);
        if (it != m_texs.end()) return &it->second;
        const QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
        GpuTex t;
        t.bytes = size_t(rgba.sizeInBytes());
        if (m_texBytes + t.bytes > (size_t(768) << 20)) { for (auto& [k, x] : m_texs) m_gl->glDeleteTextures(1, &x.id); m_texs.clear(); m_texBytes = 0; }
        for (int y = 0; y < rgba.height() && !t.hasAlpha; ++y) {
            const uchar* row = rgba.constScanLine(y);
            for (int x = 0; x < rgba.width(); ++x) if (row[x * 4 + 3] != 255) { t.hasAlpha = true; break; }
        }
        m_gl->glGenTextures(1, &t.id);
        m_gl->glBindTexture(GL_TEXTURE_2D, t.id);
        m_gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba.width(), rgba.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.constBits());
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        m_texBytes += t.bytes;
        return &(m_texs[key] = t);
    }

    void setupVertexArray(GpuMesh& g) {
        m_gl->glGenVertexArrays(1, &g.vao);
        m_gl->glBindVertexArray(g.vao);
        m_gl->glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
        const GLsizei stride = 8 * sizeof(float);
        m_gl->glEnableVertexAttribArray(0); m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
        m_gl->glEnableVertexAttribArray(1); m_gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
        m_gl->glEnableVertexAttribArray(2); m_gl->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(6 * sizeof(float)));
        if (g.ebo) m_gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo);
    }

    // Local-space buffers of a mesh. `keep` = cache it (shared meshes); skinned meshes are rebuilt every frame and freed afterwards.
    GpuMesh* meshFor(const std::shared_ptr<const Mesh>& mp, bool keep, GpuMesh& temp) {
        const Mesh& m = *mp;
        if (keep) {
            auto it = m_meshes.find(&m);
            if (it != m_meshes.end()) { if (it->second.owner.lock() == mp) return &it->second; freeMesh(it->second); m_meshBytes -= std::min(m_meshBytes, it->second.bytes); m_meshes.erase(it); }
        }
        GpuMesh g;
        g.owner = mp;
        const bool hasN = m.normals.size() == m.positions.size(), hasUv = m.uvs.size() == m.positions.size();
        std::vector<float> data;
        if (hasN) {
            data.resize(m.positions.size() * 8);
            for (size_t i = 0; i < m.positions.size(); ++i) {
                float* d = &data[i * 8];
                d[0] = m.positions[i].x(); d[1] = m.positions[i].y(); d[2] = m.positions[i].z();
                d[3] = m.normals[i].x(); d[4] = m.normals[i].y(); d[5] = m.normals[i].z();
                d[6] = hasUv ? m.uvs[i].x() : 0.f; d[7] = hasUv ? m.uvs[i].y() : 0.f;
            }
            g.vcount = int(m.positions.size());
            g.icount = int(m.indices.size());
        } else {                                                    // flat shading: one vertex per triangle corner, each with the face normal
            g.flat = true;
            data.resize(m.indices.size() * 8);
            for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
                const QVector3D& a = m.positions[m.indices[i]]; const QVector3D& b = m.positions[m.indices[i + 1]]; const QVector3D& c = m.positions[m.indices[i + 2]];
                QVector3D fn = QVector3D::crossProduct(b - a, c - a); fn.normalize();
                for (int k = 0; k < 3; ++k) {
                    const uint32_t vi = m.indices[i + size_t(k)];
                    float* d = &data[(i + size_t(k)) * 8];
                    d[0] = m.positions[vi].x(); d[1] = m.positions[vi].y(); d[2] = m.positions[vi].z();
                    d[3] = fn.x(); d[4] = fn.y(); d[5] = fn.z();
                    d[6] = hasUv ? m.uvs[vi].x() : 0.f; d[7] = hasUv ? m.uvs[vi].y() : 0.f;
                }
            }
            g.vcount = int(m.indices.size());
        }
        m_gl->glGenBuffers(1, &g.vbo);
        m_gl->glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
        m_gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(data.size() * sizeof(float)), data.data(), GL_STATIC_DRAW);
        g.bytes = data.size() * sizeof(float);
        if (!g.flat) {
            m_gl->glGenBuffers(1, &g.ebo);
            m_gl->glBindVertexArray(0);
            m_gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo);
            m_gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(m.indices.size() * sizeof(uint32_t)), m.indices.data(), GL_STATIC_DRAW);
            g.bytes += m.indices.size() * sizeof(uint32_t);
        }
        setupVertexArray(g);
        if (!keep) { temp = g; return &temp; }
        if (m_meshBytes + g.bytes > (size_t(1) << 30)) { for (auto& [k, x] : m_meshes) freeMesh(x); m_meshes.clear(); m_meshBytes = 0; }
        m_meshBytes += g.bytes;
        return &(m_meshes[&m] = g);
    }
};

struct Frame {                      // everything one part draw needs
    QVector3D base; float alpha; QVector3D emissive; float metallic, roughness;
    bool doubleSided, unlit; const QImage* tex; float repU, repV, offU, offV;
};

QVector3D rgbOf(const QColor& c) { return QVector3D(float(c.redF()), float(c.greenF()), float(c.blueF())); }

QImage Worker::render(const Scene3D& s, const RenderCamera& cam, const QSize& size, const RenderOptions& opt) {
    if (!m_ctx || opt.distanceOut) return {};
    const int ss = std::clamp(opt.supersample, 1, 3);
    const int W = std::max(1, size.width() * ss), H = std::max(1, size.height() * ss);
    if (W > m_maxRb || H > m_maxRb) return {};
    if (!m_ctx->makeCurrent(m_surface)) return {};
    struct Done { Worker* w; ~Done() { w->m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0); w->m_gl->glBindVertexArray(0); w->m_ctx->doneCurrent(); } } done{this};
    m_error = false;
    while (m_gl->glGetError() != GL_NO_ERROR) {}

    // ---- lights (as the CPU renderer collects them) ----
    std::vector<Light> lights;
    for (const auto& n : s.nodes) {
        if (n.kind != NodeKind::Light || !s.effectivelyVisible(n.id)) continue;
        const QMatrix4x4 w = s.worldMatrix(n.id);
        Light l;
        l.type = n.lightType == LightType::Directional ? 0 : n.lightType == LightType::Point ? 1 : 2;
        l.color = rgbOf(n.lightColor) * float(n.intensity);
        l.pos = w.map(QVector3D(0, 0, 0));
        const QVector3D d = w.mapVector(QVector3D(0, 0, -1));
        l.dir = d.isNull() ? QVector3D(0, -1, 0) : d.normalized();
        l.range = float(n.range);
        l.cosOuter = float(std::cos(n.spotAngle * M_PI / 360.0));
        lights.push_back(l);
    }
    if (lights.size() > 16) return {};
    const QVector3D ambient = rgbOf(s.ambient) * float(s.ambientIntensity);

    // ---- classify nodes exactly like the CPU renderer ----
    auto translucent = [&](const Node3D& n) {
        if (!n.cardLayer.isNull()) return true;
        if (const Material* m = s.material(n.materialOverride)) return m->baseColor.alpha() < 255;
        auto it = s.meshes.find(n.mesh);
        if (it != s.meshes.end()) for (const MeshPart& p : it->second->parts) if (const Material* m = s.material(p.material)) if (m->baseColor.alpha() < 255) return true;
        return false;
    };
    std::vector<const Node3D*> opaqueNodes, late;
    size_t lateTriangles = 0;
    for (const auto& n : s.nodes) {
        if (!translucent(n)) { opaqueNodes.push_back(&n); continue; }
        late.push_back(&n);
        if (auto it = s.meshes.find(n.mesh); it != s.meshes.end()) lateTriangles += it->second->indices.size() / 3;
    }
    if (lateTriangles > 400000) return {};                          // the CPU renderer sorts per object beyond 2M; keep the GPU path simple
    std::sort(late.begin(), late.end(), [&](const Node3D* a, const Node3D* b) {
        return (s.worldMatrix(a->id).map(QVector3D(0, 0, 0)) - cam.eye).lengthSquared() > (s.worldMatrix(b->id).map(QVector3D(0, 0, 0)) - cam.eye).lengthSquared();
    });

    // ---- frame buffer, state ----
    if (!ensureFramebuffer(W, H)) return {};
    m_gl->glViewport(0, 0, W, H);
    QRgb bg = 0;
    if (opt.drawBackground) bg = s.background.alpha() < 255 ? qPremultiply(s.background.rgba()) : s.background.rgba();
    m_gl->glClearColor(qRed(bg) / 255.0f, qGreen(bg) / 255.0f, qBlue(bg) / 255.0f, qAlpha(bg) / 255.0f);
    m_gl->glClearDepth(1.0);
    m_gl->glDepthMask(GL_TRUE);
    m_gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_gl->glEnable(GL_DEPTH_TEST);
    m_gl->glDepthFunc(GL_LESS);
    m_gl->glDisable(GL_BLEND);
    m_gl->glEnable(GL_CULL_FACE);
    m_gl->glCullFace(GL_BACK);
    m_gl->glUseProgram(m_prog);
    const QMatrix4x4 vp = cam.proj * cam.view;
    umat4("uVP", vp);
    u3f("uEye", cam.eye);
    u3f("uAmbient", ambient);
    u1i("uNumLights", int(lights.size()));
    {
        std::vector<GLint> t(16, 0); std::vector<float> col(48, 0), pos(48, 0), dir(48, 0), rng(16, 0), co(16, 0);
        for (size_t i = 0; i < lights.size(); ++i) {
            t[i] = lights[i].type; rng[i] = lights[i].range; co[i] = lights[i].cosOuter;
            for (int k = 0; k < 3; ++k) { col[i * 3 + size_t(k)] = lights[i].color[k]; pos[i * 3 + size_t(k)] = lights[i].pos[k]; dir[i * 3 + size_t(k)] = lights[i].dir[k]; }
        }
        m_gl->glUniform1iv(loc("uLType"), 16, t.data());
        m_gl->glUniform3fv(loc("uLColor"), 16, col.data());
        m_gl->glUniform3fv(loc("uLPos"), 16, pos.data());
        m_gl->glUniform3fv(loc("uLDir"), 16, dir.data());
        m_gl->glUniform1fv(loc("uLRange"), 16, rng.data());
        m_gl->glUniform1fv(loc("uLCosOuter"), 16, co.data());
    }
    m_gl->glActiveTexture(GL_TEXTURE0);
    u1i("uTex", 0);

    auto frameOf = [&](const Material* mat, const Scene3D& sc) {
        Frame f{};
        if (mat) {
            f.base = rgbOf(mat->baseColor); f.alpha = float(mat->baseColor.alphaF()); f.emissive = rgbOf(mat->emissive);
            f.metallic = float(mat->metallic); f.roughness = float(mat->roughness); f.doubleSided = mat->doubleSided; f.unlit = mat->unlit;
            f.repU = float(mat->repeatU); f.repV = float(mat->repeatV); f.offU = float(mat->offsetU); f.offV = float(mat->offsetV);
            auto t = sc.textures.find(mat->baseTexture);
            f.tex = (t != sc.textures.end() && !t->second.isNull()) ? &t->second : nullptr;
        } else { f.base = QVector3D(0.78f, 0.78f, 0.78f); f.alpha = 1; f.metallic = 0; f.roughness = 0.6f; f.doubleSided = false; f.unlit = false; f.repU = f.repV = 1; }
        return f;
    };
    // Sets the material uniforms; false = this material needs something the GPU path does not do.
    auto applyFrame = [&](const Frame& f, bool hasUv, bool opaquePass) {
        u3f("uBase", f.base); u1f("uAlpha", f.alpha); u3f("uEmissive", f.emissive);
        u1f("uMetal", f.metallic); u1f("uRough", f.roughness); u1i("uUnlit", f.unlit ? 1 : 0);
        u1i("uHasUv", hasUv ? 1 : 0);
        u4f("uTexXf", f.repU, f.repV, f.offU, f.offV);
        bool hasTex = false;
        if (f.tex) {
            const GpuTex* t = textureFor(*f.tex);
            if (!t) return false;
            if (opaquePass && t->hasAlpha) return false;            // CPU blends such fragments without writing depth; not reproduced
            m_gl->glBindTexture(GL_TEXTURE_2D, t->id);
            hasTex = true;
        }
        u1i("uHasTex", hasTex ? 1 : 0);
        if (f.doubleSided) m_gl->glDisable(GL_CULL_FACE); else m_gl->glEnable(GL_CULL_FACE);
        return true;
    };

    // blend modes other than Normal need the pixel behind (a read of the target): CPU only
    for (const auto& n : s.nodes) {
        if (n.kind != NodeKind::Mesh || !s.effectivelyVisible(n.id)) continue;
        auto mit = s.meshes.find(n.mesh);
        if (mit == s.meshes.end()) continue;
        for (const MeshPart& part : mit->second->parts) {
            const Material* mat = s.material(n.materialOverride.isNull() ? part.material : n.materialOverride);
            if (mat && mat->blendMode != 0) return {};
        }
    }

    // ---- opaque pass ----
    std::vector<float> overlay;                                    // world-space vertices of the see-through triangles
    struct TriJob { int mat; bool hasUv; float key; size_t first; };
    std::vector<TriJob> jobs;
    std::vector<Frame> jobFrames;
    std::vector<float> triData;                                    // 3 vertices x 8 floats per triangle, in job order of creation
    auto buildVertices = [&](const Node3D& n, const Mesh& mesh, const QMatrix4x4& world, std::vector<QVector3D>& wp, std::vector<QVector3D>& wn) {
        const QMatrix3x3 nm = world.normalMatrix();
        const bool hasN = mesh.normals.size() == mesh.positions.size();
        wp.resize(mesh.positions.size()); wn.assign(mesh.positions.size(), QVector3D());
        for (size_t i = 0; i < wp.size(); ++i) {
            wp[i] = world.map(mesh.positions[i]);
            if (hasN) {
                const QVector3D& q = mesh.normals[i];
                wn[i] = QVector3D(nm(0, 0) * q.x() + nm(0, 1) * q.y() + nm(0, 2) * q.z(), nm(1, 0) * q.x() + nm(1, 1) * q.y() + nm(1, 2) * q.z(), nm(2, 0) * q.x() + nm(2, 1) * q.y() + nm(2, 2) * q.z());
            }
        }
        (void)n;
    };
    for (int pass = 0; pass < 2; ++pass) {
        const std::vector<const Node3D*>& list = pass == 0 ? opaqueNodes : late;
        for (const Node3D* np : list) {
            const Node3D& n = *np;
            if (n.kind != NodeKind::Mesh || !s.effectivelyVisible(n.id)) continue;
            auto mit = s.meshes.find(n.mesh);
            if (mit == s.meshes.end()) continue;
            const std::shared_ptr<const Mesh> skinnedHolder = skinnedMeshFor(s, n);
            const std::shared_ptr<const Mesh> meshPtr = skinnedHolder ? skinnedHolder : mit->second;
            const Mesh& mesh = *meshPtr;
            const QMatrix4x4 world = skinnedHolder ? QMatrix4x4() : s.worldMatrix(n.id);
            const bool mirrored = world.determinant() < 0;
            const bool hasN = mesh.normals.size() == mesh.positions.size(), hasUv = mesh.uvs.size() == mesh.positions.size();
            if (pass == 0) {
                GpuMesh temp;
                GpuMesh* g = meshFor(meshPtr, !skinnedHolder, temp);
                m_gl->glBindVertexArray(g->vao);
                umat4("uModel", world);
                umat3("uNormalMat", world.normalMatrix());
                u1f("uNFlip", (hasN && mirrored) ? -1.0f : 1.0f);
                m_gl->glFrontFace(mirrored ? GL_CW : GL_CCW);
                bool ok = true;
                for (const MeshPart& part : mesh.parts) {
                    const Frame f = frameOf(s.material(n.materialOverride.isNull() ? part.material : n.materialOverride), s);
                    if (!applyFrame(f, hasUv, true)) { ok = false; break; }
                    if (g->flat) m_gl->glDrawArrays(GL_TRIANGLES, part.firstIndex, part.indexCount);
                    else m_gl->glDrawElements(GL_TRIANGLES, part.indexCount, GL_UNSIGNED_INT, reinterpret_cast<void*>(size_t(part.firstIndex) * sizeof(uint32_t)));
                }
                if (temp.vao) freeMesh(temp);
                if (!ok) return {};
            } else {
                // see-through: each triangle in world space with the CPU's normal / winding handling, sorted globally below
                std::vector<QVector3D> wp, wn;
                buildVertices(n, mesh, world, wp, wn);
                for (const MeshPart& part : mesh.parts) {
                    jobFrames.push_back(frameOf(s.material(n.materialOverride.isNull() ? part.material : n.materialOverride), s));
                    const int mi = int(jobFrames.size()) - 1;
                    for (int i = part.firstIndex; i + 2 < part.firstIndex + part.indexCount; i += 3) {
                        const uint32_t ia = mesh.indices[size_t(i)], ib = mesh.indices[size_t(i) + 1], ic = mesh.indices[size_t(i) + 2];
                        uint32_t idx[3] = {ia, ib, ic};
                        QVector3D nr[3];
                        if (!hasN) {
                            QVector3D fn = QVector3D::crossProduct(wp[ib] - wp[ia], wp[ic] - wp[ia]);
                            if (mirrored) fn = -fn;
                            fn.normalize();
                            nr[0] = nr[1] = nr[2] = fn;
                        } else for (int k = 0; k < 3; ++k) nr[k] = mirrored ? -wn[idx[k]] : wn[idx[k]];
                        int order[3] = {0, 1, 2};
                        if (mirrored) std::swap(order[1], order[2]);       // keep the front face counter-clockwise
                        const QVector3D centre = (wp[ia] + wp[ib] + wp[ic]) / 3.0f;
                        TriJob j{mi, hasUv, float(-cam.view.map(centre).z()), triData.size()};
                        for (int k = 0; k < 3; ++k) {
                            const int c = order[k];
                            const uint32_t vi = idx[c];
                            triData.insert(triData.end(), {wp[vi].x(), wp[vi].y(), wp[vi].z(), nr[c].x(), nr[c].y(), nr[c].z(), hasUv ? mesh.uvs[vi].x() : 0.f, hasUv ? mesh.uvs[vi].y() : 0.f});
                        }
                        jobs.push_back(j);
                    }
                }
            }
        }
    }

    // ---- see-through triangles, far to near ----
    if (!jobs.empty()) {
        std::stable_sort(jobs.begin(), jobs.end(), [](const TriJob& x, const TriJob& y) { return x.key > y.key; });
        std::vector<float> sorted; sorted.reserve(triData.size());
        for (const TriJob& j : jobs) sorted.insert(sorted.end(), triData.begin() + long(j.first), triData.begin() + long(j.first) + 24);
        GpuMesh g;
        m_gl->glGenBuffers(1, &g.vbo);
        m_gl->glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
        m_gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(sorted.size() * sizeof(float)), sorted.data(), GL_STREAM_DRAW);
        setupVertexArray(g);
        m_gl->glBindVertexArray(g.vao);
        QMatrix4x4 identity;
        umat4("uModel", identity); umat3("uNormalMat", identity.normalMatrix()); u1f("uNFlip", 1.0f);
        m_gl->glFrontFace(GL_CCW);
        m_gl->glDepthMask(GL_FALSE);                               // see-through surfaces test against what is there but do not hide what is behind them
        m_gl->glEnable(GL_BLEND);
        m_gl->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        size_t i = 0;
        bool ok = true;
        while (i < jobs.size() && ok) {
            size_t k = i;
            while (k < jobs.size() && jobs[k].mat == jobs[i].mat && jobs[k].hasUv == jobs[i].hasUv) ++k;
            if (!applyFrame(jobFrames[size_t(jobs[i].mat)], jobs[i].hasUv, false)) { ok = false; break; }
            m_gl->glDrawArrays(GL_TRIANGLES, GLint(i * 3), GLsizei((k - i) * 3));
            i = k;
        }
        m_gl->glDepthMask(GL_TRUE);
        m_gl->glDisable(GL_BLEND);
        freeMesh(g);
        if (!ok) return {};
    }

    // ---- read back (GL rows run bottom to top) ----
    QImage raw(W, H, QImage::Format_RGBA8888_Premultiplied);
    m_gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
    m_gl->glReadBuffer(GL_COLOR_ATTACHMENT0);
    m_gl->glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, raw.bits());
    if (m_gl->glGetError() != GL_NO_ERROR) return {};
    QImage img = raw.mirrored(false, true).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (ss == 1) return img;
    return img.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

struct State {
    QOffscreenSurface* surface = nullptr;
    QThread* thread = nullptr;
    Worker* worker = nullptr;
    QString renderer;
    std::atomic<bool> ready{false}, enabled{false};
    std::atomic<int> done{0};
};
State& st() { static State s; return s; }

}  // namespace

bool initialize() {
    State& s = st();
    if (s.ready) return true;
    if (!QCoreApplication::instance()) return false;
    auto* surf = new QOffscreenSurface;
    QSurfaceFormat f; f.setVersion(3, 3); f.setProfile(QSurfaceFormat::CoreProfile);
    surf->setFormat(f);
    surf->create();                                              // GUI thread
    if (!surf->isValid()) { delete surf; return false; }
    auto* th = new QThread;
    auto* wk = new Worker(surf);
    wk->moveToThread(th);
    th->start();
    bool ok = false;
    QMetaObject::invokeMethod(wk, [&] { ok = wk->init(); }, Qt::BlockingQueuedConnection);
    if (!ok) {
        QMetaObject::invokeMethod(wk, [wk] { delete wk; }, Qt::BlockingQueuedConnection);
        th->quit(); th->wait(); delete th; delete surf;
        return false;
    }
    s.surface = surf; s.thread = th; s.worker = wk; s.renderer = wk->renderer(); s.ready = true;
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, [] { shutdown(); });
    return true;
}
bool available() { return st().ready; }
QString rendererName() { return st().ready ? st().renderer : QString(); }
void shutdown() {
    State& s = st();
    if (!s.ready) return;
    s.ready = false;
    QMetaObject::invokeMethod(s.worker, [w = s.worker] { delete w; }, Qt::BlockingQueuedConnection);
    s.thread->quit(); s.thread->wait();
    delete s.thread; delete s.surface;
    s.thread = nullptr; s.surface = nullptr; s.worker = nullptr;
}
void setEnabled(bool on) { st().enabled = on; }
bool enabled() { return st().enabled && st().ready; }
int rendersDone() { return st().done; }

QImage render(const Scene3D& sc, const RenderCamera& cam, const QSize& size, const RenderOptions& opt) {
    State& s = st();
    if (!s.ready || !s.worker) return {};
    QImage out;
    QMetaObject::invokeMethod(s.worker, [&] { out = s.worker->render(sc, cam, size, opt); }, Qt::BlockingQueuedConnection);
    if (!out.isNull()) ++s.done;
    return out;
}

}  // namespace mynd::gpu3d
