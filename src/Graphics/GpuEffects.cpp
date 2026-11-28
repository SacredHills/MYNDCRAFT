#include "Graphics/GpuEffects.h"

#include <QCoreApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QThread>
#include <atomic>
#include <cmath>
#include <memory>

namespace mynd::fx::gpu {

namespace {

const char* kVert = R"(#version 330 core
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";

const char* kCommon = R"(#version 330 core
uniform sampler2D uTex;
uniform sampler2D uTex2;
out vec4 outColor;
vec4 fetchZero(sampler2D t, ivec2 p) {
    ivec2 sz = textureSize(t, 0);
    if (p.x < 0 || p.y < 0 || p.x >= sz.x || p.y >= sz.y) return vec4(0.0);
    return texelFetch(t, p, 0);
}
ivec4 toInt(vec4 v) { return ivec4(v * 255.0 + 0.5); }
vec4 fromInt(ivec4 v) { return vec4(v) / 255.0; }
// Qt's qUnpremultiply / qPremultiply on 8-bit values
ivec3 unpremul(ivec4 c) { return c.a == 0 ? ivec3(0) : min(ivec3(255), (c.rgb * 255 + c.a / 2) / c.a); }
ivec3 premul(ivec3 c, int a) { ivec3 v = c * a + 128; return (v + (v >> 8)) >> 8; }
)";

const char* kBox = R"(
uniform int uRadius;
uniform ivec2 uDir;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    int win = 2 * uRadius + 1;
    ivec4 sum = ivec4(0);
    for (int i = -uRadius; i <= uRadius; ++i) sum += toInt(fetchZero(uTex, p + uDir * i));
    outColor = fromInt((sum + win / 2) / win);
})";

const char* kShadow = R"(
uniform ivec2 uOffset;
uniform vec3 uColor;     // 0..255
uniform float uOpacity;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    float a = float(toInt(fetchZero(uTex, p - uOffset)).a) / 255.0 * uOpacity;
    outColor = vec4(floor(uColor * a + 0.5), floor(a * 255.0 + 0.5)) / 255.0;
})";

const char* kOver = R"(
void main() {   // uTex over uTex2 (both premultiplied)
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec4 top = toInt(texelFetch(uTex, p, 0)), bot = toInt(texelFetch(uTex2, p, 0));
    ivec4 inv = ivec4(255 - top.a);
    ivec4 v = bot * inv + 128;
    outColor = fromInt(min(ivec4(255), top + ((v + (v >> 8)) >> 8)));
})";

const char* kThreshold = R"(
uniform int uThr;
void main() {
    ivec4 c = toInt(texelFetch(uTex, ivec2(gl_FragCoord.xy), 0));
    int l = 0;
    if (c.a != 0) { ivec3 u = unpremul(c); l = (u.r * 299 + u.g * 587 + u.b * 114) / 1000; }
    outColor = l < uThr ? vec4(0.0) : texelFetch(uTex, ivec2(gl_FragCoord.xy), 0);
})";

const char* kGlowAdd = R"(
uniform float uK;
void main() {   // uTex = layer, uTex2 = blurred bright parts
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec4 d = toInt(texelFetch(uTex, p, 0)), s = toInt(texelFetch(uTex2, p, 0));
    int a = min(255, d.a + int(float(s.a) * uK));
    ivec3 add = ivec3(vec3(s.rgb) * uK);
    outColor = fromInt(ivec4(min(ivec3(a), d.rgb + add), a));
})";

const char* kColor = R"(
uniform int uMode;       // 0 brightness/contrast, 1 hue/sat/light, 2 invert
uniform vec3 uP;
float hue2(float h) { return h; }
void rgbToHsl(vec3 c, out float h, out float s, out float l) {
    float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b)), d = mx - mn;
    l = (mx + mn) * 0.5; h = 0.0; s = 0.0;
    if (d < 1e-6) return;
    s = d / (1.0 - abs(2.0 * l - 1.0) + 1e-9);
    if (mx == c.r) h = mod((c.g - c.b) / d + 6.0, 6.0);
    else if (mx == c.g) h = (c.b - c.r) / d + 2.0; else h = (c.r - c.g) / d + 4.0;
    h *= 60.0;
}
vec3 hslToRgb(float h, float s, float l) {
    float c = (1.0 - abs(2.0 * l - 1.0)) * s;
    float hp = mod(h < 0.0 ? h + 360.0 : h, 360.0) / 60.0;
    float x = c * (1.0 - abs(mod(hp, 2.0) - 1.0)), m = l - c / 2.0;
    vec3 r = vec3(0.0);
    if (hp < 1.0) r = vec3(c, x, 0.0); else if (hp < 2.0) r = vec3(x, c, 0.0); else if (hp < 3.0) r = vec3(0.0, c, x);
    else if (hp < 4.0) r = vec3(0.0, x, c); else if (hp < 5.0) r = vec3(x, 0.0, c); else r = vec3(c, 0.0, x);
    return r + m;
}
void main() {
    ivec4 c = toInt(texelFetch(uTex, ivec2(gl_FragCoord.xy), 0));
    if (c.a == 0) { outColor = vec4(0.0); return; }
    vec3 v = vec3(unpremul(c)) / 255.0;
    if (uMode == 0) {
        float br = uP.x, ct = uP.y;
        float k = ct >= 0.0 ? 1.0 / max(1e-3, 1.0 - ct * 0.99) : 1.0 + ct;
        v = (v - 0.5) * k + 0.5 + br;
    } else if (uMode == 1) {
        float h, s, l; rgbToHsl(v, h, s, l);
        h += uP.x;
        s = uP.y >= 0.0 ? s + (1.0 - s) * uP.y : s * (1.0 + uP.y);
        l = uP.z >= 0.0 ? l + (1.0 - l) * uP.z : l * (1.0 + uP.z);
        v = hslToRgb(h, clamp(s, 0.0, 1.0), clamp(l, 0.0, 1.0));
    } else v = 1.0 - v;
    ivec3 u = ivec3(clamp(v, 0.0, 1.0) * 255.0 + 0.5);
    outColor = fromInt(ivec4(premul(u, c.a), c.a));
})";

struct Program { GLuint id = 0; };

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
        m_renderer = QString::fromLatin1(reinterpret_cast<const char*>(m_gl->glGetString(GL_RENDERER)));
        m_gl->glGenVertexArrays(1, &m_vao);
        m_gl->glGenFramebuffers(1, &m_fbo);
        const bool ok = build(m_box, kBox) && build(m_shadow, kShadow) && build(m_over, kOver) && build(m_thresh, kThreshold) && build(m_glow, kGlowAdd) && build(m_color, kColor);
        m_ctx->doneCurrent();
        if (!ok) { teardown(); return false; }
        return true;
    }
    QString renderer() const { return m_renderer; }

    bool run(QImage& img, const QVector<Effect>& effects) {
        if (!m_ctx || img.isNull() || img.width() > m_maxTex || img.height() > m_maxTex) return false;
        if (!m_ctx->makeCurrent(m_surface)) return false;
        m_failed = false;
        const QImage src = img.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        const int w = src.width(), h = src.height();
        GLuint cur = makeTex(w, h, src.constBits());
        GLuint scratch = makeTex(w, h, nullptr);
        std::vector<GLuint> pool;                                   // temporaries freed at the end
        auto fresh = [&] { GLuint t = makeTex(w, h, nullptr); pool.push_back(t); return t; };
        pool.push_back(cur); pool.push_back(scratch);

        auto blurTex = [&](GLuint& tex, double radius) {
            if (radius < 0.5) return;
            const double sigma = radius / 3.0;
            const double ideal = std::sqrt(12.0 * sigma * sigma / 3 + 1);
            int wl = int(std::floor(ideal)); if (wl % 2 == 0) --wl;
            const int wu = wl + 2;
            const double mIdeal = (12 * sigma * sigma - 3.0 * wl * wl - 4.0 * 3 * wl - 3 * 3) / (-4.0 * wl - 4);
            const int m = int(std::lround(mIdeal));
            for (int pass = 0; pass < 3; ++pass) {
                const int r = std::max(((pass < m ? wl : wu) - 1) / 2, 0);
                if (r <= 0) continue;
                for (int dir = 0; dir < 2; ++dir) {
                    GLuint out = fresh();
                    use(m_box); bind(0, tex, "uTex"); set1i(m_box, "uRadius", r); set2i(m_box, "uDir", dir == 0 ? 1 : 0, dir == 0 ? 0 : 1);
                    draw(out, w, h);
                    tex = out;
                }
            }
        };

        for (const Effect& e : effects) {
            if (!e.enabled) continue;
            switch (e.type) {
            case EffectType::Blur: blurTex(cur, e.p[0]); break;
            case EffectType::DropShadow: {
                GLuint sh = fresh();
                use(m_shadow); bind(0, cur, "uTex");
                set2i(m_shadow, "uOffset", int(std::lround(e.p[0])), int(std::lround(e.p[1])));
                setf3(m_shadow, "uColor", e.color.red(), e.color.green(), e.color.blue());
                setf(m_shadow, "uOpacity", float(e.p[3] / 100.0));
                draw(sh, w, h);
                blurTex(sh, e.p[2]);
                GLuint out = fresh();
                use(m_over); bind(0, cur, "uTex"); bind(1, sh, "uTex2");
                draw(out, w, h);
                cur = out;
                break;
            }
            case EffectType::Glow: {
                GLuint bright = fresh();
                use(m_thresh); bind(0, cur, "uTex"); set1i(m_thresh, "uThr", int(std::lround(e.p[2] / 100.0 * 255)));
                draw(bright, w, h);
                blurTex(bright, e.p[0]);
                GLuint out = fresh();
                use(m_glow); bind(0, cur, "uTex"); bind(1, bright, "uTex2"); setf(m_glow, "uK", float(e.p[1] / 100.0));
                draw(out, w, h);
                cur = out;
                break;
            }
            case EffectType::BrightnessContrast: colorPass(cur, w, h, fresh(), 0, float(e.p[0] / 100.0), float(e.p[1] / 100.0), 0); break;
            case EffectType::HueSaturation: colorPass(cur, w, h, fresh(), 1, float(e.p[0]), float(e.p[1] / 100.0), float(e.p[2] / 100.0)); break;
            case EffectType::Invert: colorPass(cur, w, h, fresh(), 2, 0, 0, 0); break;
            }
        }
        QImage result(w, h, QImage::Format_RGBA8888_Premultiplied);
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, cur, 0);
        m_gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
        m_gl->glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, result.bits());
        const bool glOk = m_gl->glGetError() == GL_NO_ERROR && !m_failed;
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        for (GLuint t : pool) m_gl->glDeleteTextures(1, &t);
        m_ctx->doneCurrent();
        if (!glOk) return false;
        img = result.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        return true;
    }

private:
    QOffscreenSurface* m_surface;
    std::unique_ptr<QOpenGLContext> m_ctx;
    std::unique_ptr<QOpenGLFunctions_3_3_Core> m_gl;
    GLuint m_vao = 0, m_fbo = 0;
    GLint m_maxTex = 0;
    Program m_box, m_shadow, m_over, m_thresh, m_glow, m_color;
    GLuint m_cur = 0;
    bool m_failed = false;
    QString m_renderer;

    void teardown() {
        if (m_ctx && m_gl && m_ctx->makeCurrent(m_surface)) {
            for (Program* p : {&m_box, &m_shadow, &m_over, &m_thresh, &m_glow, &m_color}) if (p->id) m_gl->glDeleteProgram(p->id);
            if (m_vao) m_gl->glDeleteVertexArrays(1, &m_vao);
            if (m_fbo) m_gl->glDeleteFramebuffers(1, &m_fbo);
            m_ctx->doneCurrent();
        }
        m_gl.reset(); m_ctx.reset(); m_vao = m_fbo = 0;
        for (Program* p : {&m_box, &m_shadow, &m_over, &m_thresh, &m_glow, &m_color}) p->id = 0;
    }
    GLuint compile(GLenum type, const QByteArray& src) {
        GLuint s = m_gl->glCreateShader(type);
        const char* c = src.constData();
        m_gl->glShaderSource(s, 1, &c, nullptr);
        m_gl->glCompileShader(s);
        GLint ok = 0; m_gl->glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[1024]; m_gl->glGetShaderInfoLog(s, sizeof log, nullptr, log); qWarning("GPU effects: shader error: %s", log); m_gl->glDeleteShader(s); return 0; }
        return s;
    }
    bool build(Program& p, const char* frag) {
        const GLuint v = compile(GL_VERTEX_SHADER, kVert);
        const GLuint f = compile(GL_FRAGMENT_SHADER, QByteArray(kCommon) + frag);
        if (!v || !f) return false;
        p.id = m_gl->glCreateProgram();
        m_gl->glAttachShader(p.id, v); m_gl->glAttachShader(p.id, f);
        m_gl->glLinkProgram(p.id);
        m_gl->glDeleteShader(v); m_gl->glDeleteShader(f);
        GLint ok = 0; m_gl->glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
        return ok;
    }
    GLuint makeTex(int w, int h, const uchar* data) {
        GLuint t = 0;
        m_gl->glGenTextures(1, &t);
        m_gl->glBindTexture(GL_TEXTURE_2D, t);
        m_gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        return t;
    }
    void use(const Program& p) { m_gl->glUseProgram(p.id); m_prog = p.id; }
    void bind(int unit, GLuint tex, const char* name) {
        m_gl->glActiveTexture(GL_TEXTURE0 + unit);
        m_gl->glBindTexture(GL_TEXTURE_2D, tex);
        m_gl->glUniform1i(m_gl->glGetUniformLocation(m_prog, name), unit);
    }
    void set1i(const Program&, const char* n, int v) { m_gl->glUniform1i(m_gl->glGetUniformLocation(m_prog, n), v); }
    void set2i(const Program&, const char* n, int a, int b) { m_gl->glUniform2i(m_gl->glGetUniformLocation(m_prog, n), a, b); }
    void setf(const Program&, const char* n, float v) { m_gl->glUniform1f(m_gl->glGetUniformLocation(m_prog, n), v); }
    void setf3(const Program&, const char* n, float a, float b, float c) { m_gl->glUniform3f(m_gl->glGetUniformLocation(m_prog, n), a, b, c); }
    void draw(GLuint dst, int w, int h) {
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst, 0);
        if (m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { m_failed = true; return; }
        m_gl->glViewport(0, 0, w, h);
        m_gl->glDisable(GL_BLEND);
        m_gl->glBindVertexArray(m_vao);
        m_gl->glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    void colorPass(GLuint& cur, int w, int h, GLuint out, int mode, float a, float b, float c) {
        use(m_color); bind(0, cur, "uTex"); set1i(m_color, "uMode", mode);
        m_gl->glUniform3f(m_gl->glGetUniformLocation(m_prog, "uP"), a, b, c);
        draw(out, w, h);
        cur = out;
    }
    GLuint m_prog = 0;
};

struct State {
    QOffscreenSurface* surface = nullptr;
    QThread* thread = nullptr;
    Worker* worker = nullptr;
    QString renderer;
    std::atomic<bool> ready{false};
    std::atomic<bool> enabled{false};
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
    surf->create();                                              // must happen on the GUI thread
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

bool applyEffects(QImage& img, const QVector<Effect>& effects) {
    State& s = st();
    if (!s.ready || !s.worker || img.isNull() || effects.isEmpty()) return false;
    bool ok = false;
    QMetaObject::invokeMethod(s.worker, [&] { ok = s.worker->run(img, effects); }, Qt::BlockingQueuedConnection);
    return ok;
}

}  // namespace mynd::fx::gpu
