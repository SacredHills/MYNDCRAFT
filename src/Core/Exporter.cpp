#include "Core/Exporter.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>

#include "Graphics/Compositor.h"

namespace mynd::exporter {

namespace {

struct Box {
    std::vector<std::pair<uint32_t, int>> colors;   // 0xRRGGBB, count
    int minc[3], maxc[3];
    long long total = 0;
    void measure() {
        for (int c = 0; c < 3; ++c) { minc[c] = 255; maxc[c] = 0; }
        total = 0;
        for (auto& [rgb, n] : colors) {
            const int ch[3] = {int(rgb >> 16 & 255), int(rgb >> 8 & 255), int(rgb & 255)};
            for (int c = 0; c < 3; ++c) { minc[c] = std::min(minc[c], ch[c]); maxc[c] = std::max(maxc[c], ch[c]); }
            total += n;
        }
    }
    int widestChannel() const {
        int best = 0, span = -1;
        for (int c = 0; c < 3; ++c) if (maxc[c] - minc[c] > span) { span = maxc[c] - minc[c]; best = c; }
        return best;
    }
};

inline int chan(uint32_t rgb, int c) { return int(rgb >> (16 - 8 * c) & 255); }

}  // namespace

Indexed quantize(const QImage& src, int maxColors, bool transparency) {
    Indexed out;
    const QImage img = src.convertToFormat(QImage::Format_ARGB32);
    out.width = img.width(); out.height = img.height();
    maxColors = std::clamp(maxColors, 2, 256);
    const int limit = transparency ? maxColors - 1 : maxColors;

    std::unordered_map<uint32_t, int> hist;
    bool anyTransparent = false;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (transparency && qAlpha(row[x]) < 128) { anyTransparent = true; continue; }
            ++hist[row[x] & 0xFFFFFF];
        }
    }
    const bool reserve = transparency && anyTransparent;
    const int cap = reserve ? limit : maxColors;

    std::vector<uint32_t> palette;                         // 0xRRGGBB
    std::unordered_map<uint32_t, uint8_t> exact;           // exact colour -> index (when no reduction is needed)
    const bool needReduce = int(hist.size()) > cap;
    if (!needReduce) {
        for (auto& [rgb, n] : hist) palette.push_back(rgb);
        std::sort(palette.begin(), palette.end());
        for (size_t i = 0; i < palette.size(); ++i) exact[palette[i]] = uint8_t(i);
    } else {
        // Median cut over the 5-bit-per-channel histogram (keeps the search small on photographic frames).
        std::unordered_map<uint32_t, int> h5;
        for (auto& [rgb, n] : hist) {
            const uint32_t k = ((rgb >> 19 & 31) << 10) | ((rgb >> 11 & 31) << 5) | (rgb >> 3 & 31);
            h5[k] += n;
        }
        Box all;
        for (auto& [k, n] : h5) {
            const uint32_t r = (k >> 10 & 31) << 3 | (k >> 12 & 7), g = (k >> 5 & 31) << 3 | (k >> 7 & 7), b = (k & 31) << 3 | (k >> 2 & 7);
            all.colors.push_back({(r << 16) | (g << 8) | b, n});
        }
        all.measure();
        std::vector<Box> boxes{std::move(all)};
        while (int(boxes.size()) < cap) {
            int pick = -1; long long best = 0;
            for (size_t i = 0; i < boxes.size(); ++i)
                if (boxes[i].colors.size() > 1 && boxes[i].total > best) { best = boxes[i].total; pick = int(i); }
            if (pick < 0) break;
            Box b = std::move(boxes[pick]);
            const int c = b.widestChannel();
            std::sort(b.colors.begin(), b.colors.end(), [c](auto& l, auto& r) { return chan(l.first, c) < chan(r.first, c); });
            long long acc = 0; size_t cut = 1;
            for (size_t i = 0; i < b.colors.size(); ++i) { acc += b.colors[i].second; if (acc * 2 >= b.total) { cut = i + 1; break; } }
            cut = std::clamp<size_t>(cut, 1, b.colors.size() - 1);
            Box lo, hi;
            lo.colors.assign(b.colors.begin(), b.colors.begin() + cut);
            hi.colors.assign(b.colors.begin() + cut, b.colors.end());
            lo.measure(); hi.measure();
            boxes[pick] = std::move(lo);
            boxes.push_back(std::move(hi));
        }
        for (const Box& b : boxes) {
            double s[3] = {0, 0, 0};
            for (auto& [rgb, n] : b.colors) for (int c = 0; c < 3; ++c) s[c] += double(chan(rgb, c)) * n;
            const double t = std::max<long long>(1, b.total);
            palette.push_back((uint32_t(s[0] / t + 0.5) << 16) | (uint32_t(s[1] / t + 0.5) << 8) | uint32_t(s[2] / t + 0.5));
        }
    }

    out.transparentIndex = reserve ? int(palette.size()) : -1;
    for (uint32_t rgb : palette) { out.palette.append(char(rgb >> 16 & 255)); out.palette.append(char(rgb >> 8 & 255)); out.palette.append(char(rgb & 255)); }
    if (reserve) out.palette.append(3, '\0');

    std::vector<int16_t> cache(1 << 15, -1);               // 15-bit colour -> nearest palette index
    auto nearest = [&](uint32_t rgb) -> uint8_t {
        const uint32_t k = ((rgb >> 19 & 31) << 10) | ((rgb >> 11 & 31) << 5) | (rgb >> 3 & 31);
        if (cache[k] >= 0) return uint8_t(cache[k]);
        int best = 0; long long bd = -1;
        for (size_t i = 0; i < palette.size(); ++i) {
            long long d = 0;
            for (int c = 0; c < 3; ++c) { const int e = chan(rgb, c) - chan(palette[i], c); d += e * e; }
            if (bd < 0 || d < bd) { bd = d; best = int(i); }
        }
        cache[k] = int16_t(best);
        return uint8_t(best);
    };

    out.pixels.resize(qsizetype(img.width()) * img.height());
    char* dst = out.pixels.data();
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (reserve && qAlpha(row[x]) < 128) { *dst++ = char(out.transparentIndex); continue; }
            const uint32_t rgb = row[x] & 0xFFFFFF;
            if (!needReduce) *dst++ = char(exact[rgb]);
            else *dst++ = char(nearest(rgb));
        }
    }
    if (out.palette.isEmpty()) out.palette = QByteArray(6, '\0');   // fully transparent frame
    return out;
}

QByteArray lzwEncode(const QByteArray& indices, int minCodeSize) {
    QByteArray bytes;                                       // raw code stream, packed LSB first
    uint32_t bitBuf = 0; int bitCnt = 0;
    auto put = [&](uint32_t code, int width) {
        bitBuf |= code << bitCnt; bitCnt += width;
        while (bitCnt >= 8) { bytes.append(char(bitBuf & 255)); bitBuf >>= 8; bitCnt -= 8; }
    };
    const int clear = 1 << minCodeSize, eoi = clear + 1;
    int next = eoi + 1, width = minCodeSize + 1;
    std::unordered_map<uint32_t, uint16_t> dict;
    dict.reserve(8192);
    put(clear, width);
    if (!indices.isEmpty()) {
        uint32_t prefix = uint8_t(indices[0]);
        for (qsizetype i = 1; i < indices.size(); ++i) {
            const uint32_t c = uint8_t(indices[i]);
            const uint32_t key = (prefix << 8) | c;
            auto it = dict.find(key);
            if (it != dict.end()) { prefix = it->second; continue; }
            put(prefix, width);
            if (next < 4096) {
                dict[key] = uint16_t(next++);
                if (next - 1 == (1 << width) && width < 12) ++width;   // code just added needs the wider width
            } else {
                put(clear, width);
                dict.clear(); next = eoi + 1; width = minCodeSize + 1;
            }
            prefix = c;
        }
        put(prefix, width);
        // The decoder adds an entry after this code too, which can widen its read size; mirror that.
        if (next < 4096 && next == (1 << width) && width < 12) ++width;   // (no code follows except EOI)
    }
    put(eoi, width);
    if (bitCnt > 0) bytes.append(char(bitBuf & 255));

    QByteArray out;
    out.append(char(minCodeSize));
    for (qsizetype i = 0; i < bytes.size(); i += 255) {
        const qsizetype n = std::min<qsizetype>(255, bytes.size() - i);
        out.append(char(n)); out.append(bytes.constData() + i, n);
    }
    out.append('\0');
    return out;
}

QVector<int> gifDelaysCentiseconds(FrameRate fps, int count) {
    QVector<int> d;
    if (!fps.isValid() || count <= 0) return d;
    // frame i starts at i*den*100/num centiseconds; delay = difference of rounded starts.
    auto start = [&](qint64 i) { return (i * fps.den * 100 * 2 + fps.num) / (2 * qint64(fps.num)); };
    for (int i = 0; i < count; ++i) d.push_back(int(start(i + 1) - start(i)));
    return d;
}

static void le16(QByteArray& b, int v) { b.append(char(v & 255)); b.append(char(v >> 8 & 255)); }

bool writeGif(const Document& doc, const QString& path, const ExportOptions& opt, QString* error) {
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };
    const int first = opt.firstFrame, last = opt.lastFrame;
    if (first < 0 || last < first) return fail("Invalid frame range.");
    if (doc.comp().width > 65535 || doc.comp().height > 65535) return fail("GIF images are limited to 65535 pixels per side.");
    const int count = last - first + 1;
    const QVector<int> delays = gifDelaysCentiseconds(doc.comp().fps, count);

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return fail("Could not open " + path + " for writing.");
    QByteArray head("GIF89a");
    le16(head, doc.comp().width); le16(head, doc.comp().height);
    head.append(char(0x70)); head.append('\0'); head.append('\0');            // no global table, 8-bit colour resolution
    head.append("\x21\xFF\x0BNETSCAPE2.0\x03\x01", 16);
    le16(head, std::clamp(opt.loopCount, 0, 65535));
    head.append('\0');
    f.write(head);

    for (int i = 0; i < count; ++i) {
        Compositor::Options co;
        co.includeBackground = !opt.transparentBackground;
        co.frame = first + i;
        QImage img = Compositor::render(doc, doc.canvasRect(), co);
        if (!opt.transparentBackground) {                       // flatten onto opaque white in case the background itself is transparent
            QImage flat(img.size(), QImage::Format_RGB32);
            flat.fill(Qt::white);
            QPainter p(&flat); p.drawImage(0, 0, img); p.end();
            img = flat;
        }
        const Indexed q = quantize(img, 256, opt.transparentBackground);
        int bits = 1;
        while ((1 << bits) < q.palette.size() / 3) ++bits;
        QByteArray fr;
        fr.append("\x21\xF9\x04", 3);
        fr.append(char((q.transparentIndex >= 0 ? (2 << 2) | 1 : (1 << 2))));
        le16(fr, delays[i]);
        fr.append(char(q.transparentIndex >= 0 ? q.transparentIndex : 0));
        fr.append('\0');
        fr.append('\x2C');
        le16(fr, 0); le16(fr, 0); le16(fr, q.width); le16(fr, q.height);
        fr.append(char(0x80 | (bits - 1)));
        QByteArray pal = q.palette;
        pal.append(QByteArray((1 << bits) * 3 - pal.size(), '\0'));
        fr.append(pal);
        fr.append(lzwEncode(q.pixels, std::max(2, bits)));
        f.write(fr);
        if (opt.progress && !opt.progress(i + 1, count)) { f.cancelWriting(); return fail("Export cancelled."); }
    }
    f.write(";", 1);
    if (!f.commit()) return fail("Could not finish writing " + path + ".");
    return true;
}

bool writePngSequence(const Document& doc, const QString& dir, const QString& baseName, const ExportOptions& opt,
                      QStringList* written, QString* error) {
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };
    if (opt.firstFrame < 0 || opt.lastFrame < opt.firstFrame) return fail("Invalid frame range.");
    if (!QDir().mkpath(dir)) return fail("Could not create " + dir);
    const int count = opt.lastFrame - opt.firstFrame + 1;
    const int digits = std::max<int>(4, QString::number(count).size());
    for (int i = 0; i < count; ++i) {
        Compositor::Options co;
        co.includeBackground = !opt.transparentBackground;
        co.frame = opt.firstFrame + i;
        const QImage img = Compositor::render(doc, doc.canvasRect(), co);
        const QString file = QDir(dir).filePath(QString("%1_%2.png").arg(baseName).arg(i + 1, digits, 10, QChar('0')));
        if (!img.save(file, "PNG")) return fail("Could not write " + file);
        if (written) written->append(file);
        if (opt.progress && !opt.progress(i + 1, count)) return fail("Export cancelled.");
    }
    return true;
}

}  // namespace mynd::exporter
