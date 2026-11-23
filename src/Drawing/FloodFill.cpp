#include "Drawing/FloodFill.h"

#include <QPainter>
#include <cstdlib>
#include <vector>

namespace mynd {

namespace {
inline bool similar(quint32 a, quint32 b, int tol) {
    return std::abs(int(a >> 24) - int(b >> 24)) <= tol && std::abs(int((a >> 16) & 255) - int((b >> 16) & 255)) <= tol &&
           std::abs(int((a >> 8) & 255) - int((b >> 8) & 255)) <= tol && std::abs(int(a & 255) - int(b & 255)) <= tol;
}
}  // namespace

QRect floodFill(QImage& img, const QPoint& seed, const QColor& color, int tolerance, bool contiguous) {
    if (img.format() != QImage::Format_ARGB32_Premultiplied || !img.rect().contains(seed)) return {};
    const int w = img.width(), h = img.height();
    const quint32 target = reinterpret_cast<const quint32*>(img.constScanLine(seed.y()))[seed.x()];
    QColor c = color.toRgb();
    const int a = c.alpha();
    const quint32 fill = (quint32(a) << 24) | (quint32(c.red() * a / 255) << 16) |
                         (quint32(c.green() * a / 255) << 8) | quint32(c.blue() * a / 255);
    tolerance = std::clamp(tolerance, 0, 255);

    std::vector<uint8_t> mask(size_t(w) * h, 0);
    QRect changed;
    auto matches = [&](int x, int y) {
        return similar(reinterpret_cast<const quint32*>(img.constScanLine(y))[x], target, tolerance);
    };

    if (!contiguous) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) if (matches(x, y)) mask[size_t(y) * w + x] = 1;
    } else {
        std::vector<QPoint> stack{seed};
        while (!stack.empty()) {
            QPoint p = stack.back(); stack.pop_back();
            if (mask[size_t(p.y()) * w + p.x()] || !matches(p.x(), p.y())) continue;
            int x0 = p.x(), x1 = p.x();
            while (x0 > 0 && !mask[size_t(p.y()) * w + x0 - 1] && matches(x0 - 1, p.y())) --x0;
            while (x1 < w - 1 && !mask[size_t(p.y()) * w + x1 + 1] && matches(x1 + 1, p.y())) ++x1;
            for (int x = x0; x <= x1; ++x) mask[size_t(p.y()) * w + x] = 1;
            for (int dy : {-1, 1}) {
                const int ny = p.y() + dy;
                if (ny < 0 || ny >= h) continue;
                bool inSpan = false;
                for (int x = x0; x <= x1; ++x) {
                    const bool ok = !mask[size_t(ny) * w + x] && matches(x, ny);
                    if (ok && !inSpan) stack.emplace_back(x, ny);
                    inSpan = ok;
                }
            }
        }
    }
    for (int y = 0; y < h; ++y) {
        auto* row = reinterpret_cast<quint32*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            if (!mask[size_t(y) * w + x] || row[x] == fill) continue;
            row[x] = fill;
            changed = changed.isNull() ? QRect(x, y, 1, 1) : changed.united(QRect(x, y, 1, 1));
        }
    }
    return changed;
}

}  // namespace mynd
