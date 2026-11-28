#include "Graphics/Effects.h"

#include <QPainter>
#include <algorithm>
#include <cmath>
#include <vector>

namespace mynd::fx {

namespace {

// One box-blur pass along a line of `n` samples (`channels` interleaved bytes per sample, `step` bytes between samples).
void boxLine(const uchar* in, uchar* out, int n, int step, int outStep, int channels, int r) {
    const int win = 2 * r + 1;
    for (int c = 0; c < channels; ++c) {
        int sum = 0;
        for (int i = 0; i <= r && i < n; ++i) sum += in[i * step + c];
        for (int x = 0; x < n; ++x) {
            out[x * outStep + c] = uchar((sum + win / 2) / win);
            const int add = x + r + 1, sub = x - r;
            if (add < n) sum += in[add * step + c];
            if (sub >= 0) sum -= in[sub * step + c];
        }
    }
}

void boxBlurPass(QImage& img, int r, bool horizontal) {
    if (r <= 0) return;
    const int channels = img.format() == QImage::Format_Alpha8 ? 1 : 4;
    const int w = img.width(), h = img.height();
    const qsizetype bpl = img.bytesPerLine();
    std::vector<uchar> tmp(size_t(std::max(w, h)) * channels);
    if (horizontal) {
        for (int y = 0; y < h; ++y) {
            uchar* row = img.scanLine(y);
            boxLine(row, tmp.data(), w, channels, channels, channels, r);
            std::copy(tmp.begin(), tmp.begin() + size_t(w) * channels, row);
        }
    } else {
        for (int x = 0; x < w; ++x) {
            uchar* base = img.bits() + size_t(x) * channels;
            boxLine(base, tmp.data(), h, int(bpl), channels, channels, r);
            for (int y = 0; y < h; ++y) for (int c = 0; c < channels; ++c) base[size_t(y) * bpl + c] = tmp[size_t(y) * channels + c];
        }
    }
}

inline int luma255(QRgb c) { return (qRed(c) * 299 + qGreen(c) * 587 + qBlue(c) * 114) / 1000; }

void rgbToHsl(float r, float g, float b, float& h, float& s, float& l) {
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    l = (mx + mn) * 0.5f; h = 0; s = 0;
    if (d < 1e-6f) return;
    s = d / (1 - std::abs(2 * l - 1) + 1e-9f);
    if (mx == r) h = std::fmod((g - b) / d + 6.0f, 6.0f);
    else if (mx == g) h = (b - r) / d + 2; else h = (r - g) / d + 4;
    h *= 60;
}
void hslToRgb(float h, float s, float l, float& r, float& g, float& b) {
    const float c = (1 - std::abs(2 * l - 1)) * s, hp = std::fmod(h < 0 ? h + 360 : h, 360.0f) / 60.0f;
    const float x = c * (1 - std::abs(std::fmod(hp, 2.0f) - 1)), m = l - c / 2;
    float R = 0, G = 0, B = 0;
    if (hp < 1) { R = c; G = x; } else if (hp < 2) { R = x; G = c; } else if (hp < 3) { G = c; B = x; }
    else if (hp < 4) { G = x; B = c; } else if (hp < 5) { R = x; B = c; } else { R = c; B = x; }
    r = R + m; g = G + m; b = B + m;
}

template <typename F> void perPixelStraight(QImage& img, F f) {            // f(r,g,b in 0..1) -> modified in place; alpha untouched
    for (int y = 0; y < img.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int a = qAlpha(row[x]);
            if (a == 0) continue;
            const QRgb u = qUnpremultiply(row[x]);
            float r = qRed(u) / 255.0f, g = qGreen(u) / 255.0f, b = qBlue(u) / 255.0f;
            f(r, g, b);
            row[x] = qPremultiply(qRgba(int(std::clamp(r, 0.0f, 1.0f) * 255 + 0.5f), int(std::clamp(g, 0.0f, 1.0f) * 255 + 0.5f), int(std::clamp(b, 0.0f, 1.0f) * 255 + 0.5f), a));
        }
    }
}

QImage shifted(const QImage& src, int dx, int dy) {
    QImage out(src.size(), src.format()); out.fill(0);
    QPainter p(&out); p.setCompositionMode(QPainter::CompositionMode_Source); p.drawImage(dx, dy, src);
    return out;
}

}  // namespace

void blur(QImage& img, double radius) {
    if (img.isNull() || radius < 0.5) return;
    if (img.format() != QImage::Format_Alpha8 && img.format() != QImage::Format_ARGB32_Premultiplied) img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // three box passes approximate a gaussian with sigma = radius / 3 (so the visible extent is about `radius`)
    const double sigma = radius / 3.0;
    const double ideal = std::sqrt(12.0 * sigma * sigma / 3 + 1);
    int wl = int(std::floor(ideal)); if (wl % 2 == 0) --wl;
    const int wu = wl + 2;
    const double mIdeal = (12 * sigma * sigma - 3.0 * wl * wl - 4.0 * 3 * wl - 3 * 3) / (-4.0 * wl - 4);
    const int m = int(std::lround(mIdeal));
    for (int pass = 0; pass < 3; ++pass) {
        const int r = ((pass < m ? wl : wu) - 1) / 2;
        boxBlurPass(img, std::max(r, 0), true);
        boxBlurPass(img, std::max(r, 0), false);
    }
}

int effectMargin(const QVector<Effect>& effects) {
    int m = 0;
    for (const Effect& e : effects) {
        if (!e.enabled) continue;
        switch (e.type) {
        case EffectType::Blur: case EffectType::Glow: m += int(std::ceil(e.p[0])) + 2; break;
        case EffectType::DropShadow: m += int(std::ceil(std::max(std::abs(e.p[0]), std::abs(e.p[1])) + e.p[2])) + 2; break;
        default: break;
        }
    }
    return std::min(m, 2000);
}

void applyEffects(QImage& img, const QVector<Effect>& effects) {
    if (img.isNull()) return;
    if (img.format() != QImage::Format_ARGB32_Premultiplied) img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (const Effect& e : effects) {
        if (!e.enabled) continue;
        switch (e.type) {
        case EffectType::Blur:
            blur(img, e.p[0]);
            break;
        case EffectType::DropShadow: {
            QImage sh(img.size(), QImage::Format_ARGB32_Premultiplied);
            const QColor c = e.color; const double op = e.p[3] / 100.0;
            for (int y = 0; y < img.height(); ++y) {
                const QRgb* s = reinterpret_cast<const QRgb*>(img.constScanLine(y)); QRgb* d = reinterpret_cast<QRgb*>(sh.scanLine(y));
                for (int x = 0; x < img.width(); ++x) {
                    const double a = qAlpha(s[x]) / 255.0 * op;
                    d[x] = qRgba(int(c.red() * a + 0.5), int(c.green() * a + 0.5), int(c.blue() * a + 0.5), int(a * 255 + 0.5));
                }
            }
            sh = shifted(sh, int(std::lround(e.p[0])), int(std::lround(e.p[1])));
            blur(sh, e.p[2]);
            QPainter p(&sh); p.drawImage(0, 0, img); p.end();             // the layer over its shadow
            img = sh;
            break;
        }
        case EffectType::Glow: {
            QImage g = img.copy();
            const int thr = int(std::lround(e.p[2] / 100.0 * 255));
            for (int y = 0; y < g.height(); ++y) {
                QRgb* d = reinterpret_cast<QRgb*>(g.scanLine(y));
                for (int x = 0; x < g.width(); ++x) {
                    const int a = qAlpha(d[x]);
                    const int l = a ? luma255(qUnpremultiply(d[x])) : 0;
                    if (l < thr) d[x] = 0;
                }
            }
            blur(g, e.p[0]);
            const double k = e.p[1] / 100.0;
            for (int y = 0; y < img.height(); ++y) {
                QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y)); const QRgb* s = reinterpret_cast<const QRgb*>(g.constScanLine(y));
                for (int x = 0; x < img.width(); ++x) {
                    const int a = std::min(255, qAlpha(d[x]) + int(qAlpha(s[x]) * k));
                    d[x] = qRgba(std::min(a, qRed(d[x]) + int(qRed(s[x]) * k)), std::min(a, qGreen(d[x]) + int(qGreen(s[x]) * k)), std::min(a, qBlue(d[x]) + int(qBlue(s[x]) * k)), a);   // additive, kept valid (premultiplied)
                }
            }
            break;
        }
        case EffectType::BrightnessContrast: {
            const float br = float(e.p[0] / 100.0), ct = float(e.p[1] / 100.0);
            const float k = ct >= 0 ? 1.0f / std::max(1e-3f, 1.0f - ct * 0.99f) : 1.0f + ct;
            perPixelStraight(img, [&](float& r, float& g, float& b) { r = (r - 0.5f) * k + 0.5f + br; g = (g - 0.5f) * k + 0.5f + br; b = (b - 0.5f) * k + 0.5f + br; });
            break;
        }
        case EffectType::HueSaturation: {
            const float dh = float(e.p[0]), ds = float(e.p[1] / 100.0), dl = float(e.p[2] / 100.0);
            perPixelStraight(img, [&](float& r, float& g, float& b) {
                float h, s, l; rgbToHsl(r, g, b, h, s, l);
                h += dh;
                s = ds >= 0 ? s + (1 - s) * ds : s * (1 + ds);
                l = dl >= 0 ? l + (1 - l) * dl : l * (1 + dl);
                hslToRgb(h, std::clamp(s, 0.0f, 1.0f), std::clamp(l, 0.0f, 1.0f), r, g, b);
            });
            break;
        }
        case EffectType::Invert:
            perPixelStraight(img, [](float& r, float& g, float& b) { r = 1 - r; g = 1 - g; b = 1 - b; });
            break;
        }
    }
}

QImage maskCoverage(const QVector<Mask>& masks, const QSize& size, const QPoint& origin) {
    QImage result(size, QImage::Format_Alpha8);
    const bool baseFull = !masks.isEmpty() && masks.first().mode != MaskMode::Add;
    result.fill(baseFull ? 255 : 0);
    for (const Mask& m : masks) {
        QImage cov(size, QImage::Format_Alpha8); cov.fill(0);
        {
            QPainter p(&cov);
            p.setRenderHint(QPainter::Antialiasing);
            p.translate(-origin);
            p.setPen(Qt::NoPen); p.setBrush(Qt::white);
            p.drawPath(m.shape.path());
        }
        if (m.feather > 0) blur(cov, m.feather);
        const double op = m.opacity / 100.0;
        for (int y = 0; y < size.height(); ++y) {
            uchar* r = result.scanLine(y); const uchar* c = cov.constScanLine(y);
            for (int x = 0; x < size.width(); ++x) {
                double v = c[x] / 255.0;
                if (m.invert) v = 1 - v;
                v *= op;
                const double cur = r[x] / 255.0;
                double out = cur;
                switch (m.mode) {
                case MaskMode::Add: out = cur + v - cur * v; break;
                case MaskMode::Subtract: out = cur * (1 - v); break;
                case MaskMode::Intersect: out = cur * v; break;
                }
                r[x] = uchar(std::clamp(out, 0.0, 1.0) * 255 + 0.5);
            }
        }
    }
    return result;
}

void applyCoverage(QImage& img, const QImage& coverage) {
    if (img.isNull() || coverage.size() != img.size()) return;
    for (int y = 0; y < img.height(); ++y) {
        QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y)); const uchar* c = coverage.constScanLine(y);
        for (int x = 0; x < img.width(); ++x) {
            const int k = c[x];
            if (k == 255) continue;
            d[x] = qRgba((qRed(d[x]) * k + 127) / 255, (qGreen(d[x]) * k + 127) / 255, (qBlue(d[x]) * k + 127) / 255, (qAlpha(d[x]) * k + 127) / 255);
        }
    }
}

void applyMatte(QImage& img, const QImage& matte, MatteMode mode) {
    if (img.isNull() || matte.size() != img.size() || mode == MatteMode::None) return;
    const bool inverted = mode == MatteMode::InvertedAlpha || mode == MatteMode::InvertedLuma;
    const bool useLuma = mode == MatteMode::Luma || mode == MatteMode::InvertedLuma;
    for (int y = 0; y < img.height(); ++y) {
        QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y)); const QRgb* m = reinterpret_cast<const QRgb*>(matte.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            int k = useLuma ? luma255(m[x]) : qAlpha(m[x]);       // luma of premultiplied colour = luminance * alpha
            if (inverted) k = 255 - k;
            if (k == 255) continue;
            d[x] = qRgba((qRed(d[x]) * k + 127) / 255, (qGreen(d[x]) * k + 127) / 255, (qBlue(d[x]) * k + 127) / 255, (qAlpha(d[x]) * k + 127) / 255);
        }
    }
}

}  // namespace mynd::fx
