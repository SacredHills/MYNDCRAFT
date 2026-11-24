#include <QtTest>

#include "TestEnv.h"
#include "Core/Exporter.h"

using namespace mynd;

namespace {

// Independent GIF decoder used only to verify what the encoder wrote.
struct GifFrame { QImage img; int delayCs = 0; int transparent = -1; };
struct GifFile { int w = 0, h = 0; int loops = -1; QVector<GifFrame> frames; bool ok = false; };

QByteArray lzwDecode(const QByteArray& data, int minCode, int pixels) {
    QByteArray out;
    const int clear = 1 << minCode, eoi = clear + 1;
    std::vector<int> prefix(4096), suffix(4096), len(4096);
    int next = eoi + 1, width = minCode + 1, prev = -1;
    uint32_t buf = 0; int bits = 0; qsizetype pos = 0;
    auto expand = [&](int code, QByteArray& dst) {
        QByteArray tmp;
        while (code >= clear) { tmp.append(char(suffix[code])); code = prefix[code]; }
        tmp.append(char(code));
        std::reverse(tmp.begin(), tmp.end());
        dst = tmp;
    };
    while (out.size() < pixels) {
        while (bits < width && pos < data.size()) { buf |= uint32_t(uint8_t(data[pos++])) << bits; bits += 8; }
        if (bits < width) break;
        const int code = int(buf & ((1u << width) - 1)); buf >>= width; bits -= width;
        if (code == clear) { next = eoi + 1; width = minCode + 1; prev = -1; continue; }
        if (code == eoi) break;
        QByteArray s;
        if (prev < 0) { s = QByteArray(1, char(code)); }
        else if (code < next) {
            expand(code, s);
            if (next < 4096) { prefix[next] = prev; suffix[next] = uint8_t(s[0]); ++next; }
        } else {
            QByteArray p; expand(prev, p);
            s = p + p[0];
            if (next < 4096) { prefix[next] = prev; suffix[next] = uint8_t(p[0]); ++next; }
        }
        out += s;
        prev = code;
        if (next == (1 << width) && width < 12) ++width;
    }
    return out;
}

GifFile decodeGif(const QByteArray& d) {
    GifFile g;
    if (!d.startsWith("GIF89a")) return g;
    auto u8 = [&](qsizetype i) { return uint8_t(d[i]); };
    auto u16 = [&](qsizetype i) { return u8(i) | (u8(i + 1) << 8); };
    g.w = u16(6); g.h = u16(8);
    qsizetype p = 13;
    if (u8(10) & 0x80) p += 3 * (1 << ((u8(10) & 7) + 1));
    int delay = 0, trans = -1;
    while (p < d.size()) {
        const uint8_t t = u8(p++);
        if (t == 0x3B) { g.ok = true; break; }
        if (t == 0x21) {
            const uint8_t label = u8(p++);
            if (label == 0xF9) { delay = u16(p + 2); trans = (u8(p + 1) & 1) ? u8(p + 4) : -1; }
            if (label == 0xFF && d.mid(p + 1, 11) == "NETSCAPE2.0") g.loops = u16(p + 14);
            while (u8(p) != 0) p += u8(p) + 1;
            ++p;
        } else if (t == 0x2C) {
            const int fw = u16(p + 4), fh = u16(p + 6);
            const uint8_t flags = u8(p + 8); p += 9;
            QByteArray pal;
            if (flags & 0x80) { const int n = 3 * (1 << ((flags & 7) + 1)); pal = d.mid(p, n); p += n; }
            const int minCode = u8(p++);
            QByteArray stream;
            while (u8(p) != 0) { stream += d.mid(p + 1, u8(p)); p += u8(p) + 1; }
            ++p;
            const QByteArray idx = lzwDecode(stream, minCode, fw * fh);
            GifFrame f; f.delayCs = delay; f.transparent = trans;
            f.img = QImage(fw, fh, QImage::Format_ARGB32);
            for (int y = 0; y < fh; ++y) for (int x = 0; x < fw; ++x) {
                const int i = qsizetype(y) * fw + x < idx.size() ? uint8_t(idx[qsizetype(y) * fw + x]) : 0;
                if (i == trans) f.img.setPixel(x, y, 0);
                else f.img.setPixel(x, y, qRgb(uint8_t(pal[i * 3]), uint8_t(pal[i * 3 + 1]), uint8_t(pal[i * 3 + 2])));
            }
            g.frames.push_back(f);
        } else return g;
    }
    return g;
}

}  // namespace

class TestExport : public QObject {
    Q_OBJECT
    // 3 exposures on one layer: red (2 frames), blue (1 frame), gap (1 frame).
    static std::unique_ptr<Document> make(FrameRate fps = {24, 1}, bool transparentBg = false) {
        CompositionSettings s; s.width = 40; s.height = 30; s.fps = fps; s.durationFrames = 4;
        if (transparentBg) s.background = Qt::transparent;
        auto d = std::make_unique<Document>(s);
        const QUuid id = d->addAnimationLayer("A");
        const QUuid red = d->createDrawing(id, 0, false);
        d->findLayer(id)->drawingImages[red].fill(Qt::red);
        AnimationState a = d->findLayer(id)->anim;
        a.extendExposure(0, 1);                                           // red holds frames 0-1
        d->editAnimation(id, a, "t");
        const QUuid blue = d->createDrawing(id, 2, false);
        d->findLayer(id)->drawingImages[blue].fill(Qt::blue);
        return d;
    }
private slots:
    void delaysCarryTheRoundingErrorSoLengthIsExact() {
        auto sum = [](FrameRate f, int n) { int t = 0; for (int v : exporter::gifDelaysCentiseconds(f, n)) t += v; return t; };
        QCOMPARE(sum({24, 1}, 24), 100);
        QCOMPARE(sum({30, 1}, 30), 100);
        QCOMPARE(sum({25, 1}, 50), 200);
        QCOMPARE(sum({24000, 1001}, 240), 1001);                     // 240 frames @23.976 = 10.01 s
        for (int v : exporter::gifDelaysCentiseconds({24, 1}, 24)) QVERIFY(v == 4 || v == 5);
        QVERIFY(exporter::gifDelaysCentiseconds({0, 1}, 5).isEmpty());
    }

    void lzwRoundTripsAcrossTableResetsAndEveryCodeSize() {
        QRandomGenerator rng(12345);
        for (int bits = 2; bits <= 8; ++bits) {
            for (int kind = 0; kind < 3; ++kind) {
                QByteArray in;
                const int n = 60000;
                for (int i = 0; i < n; ++i) {
                    int v = kind == 0 ? int(rng.bounded(1u << bits))            // noise: fills the table quickly
                          : kind == 1 ? (i / 7) % (1 << bits)                   // long runs
                                      : (i % 13 < 9 ? 0 : (i * 31) % (1 << bits));
                    in.append(char(v));
                }
                const QByteArray enc = exporter::lzwEncode(in, bits);
                QCOMPARE(int(uint8_t(enc[0])), bits);
                QByteArray stream; qsizetype p = 1;
                while (uint8_t(enc[p]) != 0) { stream += enc.mid(p + 1, uint8_t(enc[p])); p += uint8_t(enc[p]) + 1; }
                QCOMPARE(p, enc.size() - 1);
                QVERIFY2(lzwDecode(stream, bits, n) == in, qPrintable(QString("bits=%1 kind=%2").arg(bits).arg(kind)));
            }
        }
        // tiny inputs
        for (int n : {0, 1, 2}) {
            QByteArray in(n, char(1));
            const QByteArray enc = exporter::lzwEncode(in, 2);
            QByteArray stream; qsizetype p = 1;
            while (uint8_t(enc[p]) != 0) { stream += enc.mid(p + 1, uint8_t(enc[p])); p += uint8_t(enc[p]) + 1; }
            QCOMPARE(lzwDecode(stream, 2, n), in);
        }
    }

    void quantizerIsExactForFewColoursAndCloseForGradients() {
        QImage few(16, 16, QImage::Format_ARGB32);
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) few.setPixel(x, y, qRgb(x * 16, y * 16, (x + y) % 3 * 100));
        auto q = exporter::quantize(few);
        QCOMPARE(q.palette.size() / 3, 256);                           // 256 distinct colours fit exactly
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
            const int i = uint8_t(q.pixels[y * 16 + x]);
            QCOMPARE(QColor(uint8_t(q.palette[i * 3]), uint8_t(q.palette[i * 3 + 1]), uint8_t(q.palette[i * 3 + 2])), QColor(few.pixel(x, y)));
        }
        QImage grad(256, 64, QImage::Format_ARGB32);
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 256; ++x) grad.setPixel(x, y, qRgb(x, y * 4, 255 - x));
        q = exporter::quantize(grad);
        QVERIFY(q.palette.size() / 3 <= 256);
        int worst = 0;
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 256; ++x) {
            const int i = uint8_t(q.pixels[y * 256 + x]);
            const QRgb c = grad.pixel(x, y);
            worst = qMax(worst, qAbs(uint8_t(q.palette[i * 3]) - qRed(c)) + qAbs(uint8_t(q.palette[i * 3 + 1]) - qGreen(c)) + qAbs(uint8_t(q.palette[i * 3 + 2]) - qBlue(c)));
        }
        QVERIFY2(worst < 60, qPrintable(QString::number(worst)));
        // transparency reserves an index
        QImage t(4, 4, QImage::Format_ARGB32); t.fill(Qt::transparent);
        t.setPixelColor(1, 1, Qt::red);
        auto qt = exporter::quantize(t, 256, true);
        QVERIFY(qt.transparentIndex >= 0);
        QCOMPARE(int(uint8_t(qt.pixels[0])), qt.transparentIndex);
        QVERIFY(int(uint8_t(qt.pixels[5])) != qt.transparentIndex);
    }

    void gifHasTheRightFramesTimingLoopAndPixels() {
        auto d = make();
        QTemporaryDir dir;
        const QString path = dir.path() + "/a.gif";
        exporter::ExportOptions o; o.firstFrame = 0; o.lastFrame = 3;
        int calls = 0; o.progress = [&](int done, int total) { ++calls; return done <= total; };
        QString err;
        QVERIFY2(exporter::writeGif(*d, path, o, &err), qPrintable(err));
        QCOMPARE(calls, 4);
        QFile f(path); QVERIFY(f.open(QIODevice::ReadOnly));
        const GifFile g = decodeGif(f.readAll());
        QVERIFY(g.ok);
        QCOMPARE(g.w, 40); QCOMPARE(g.h, 30);
        QCOMPARE(g.loops, 0);
        QCOMPARE(g.frames.size(), 4);
        int total = 0; for (auto& fr : g.frames) total += fr.delayCs;
        QCOMPARE(total, 17);                                              // 4 frames @24fps = 16.67 cs*... rounds to 17
        const QColor want[4] = {Qt::red, Qt::red, Qt::blue, Qt::white};   // frame 3 is a gap: background
        for (int i = 0; i < 4; ++i) QCOMPARE(QColor(g.frames[i].img.pixel(20, 15)), want[i]);
        // a sub-range
        exporter::ExportOptions o2; o2.firstFrame = 2; o2.lastFrame = 2; o2.loopCount = 3;
        QVERIFY(exporter::writeGif(*d, dir.path() + "/b.gif", o2, &err));
        QFile f2(dir.path() + "/b.gif"); QVERIFY(f2.open(QIODevice::ReadOnly));
        const GifFile g2 = decodeGif(f2.readAll());
        QCOMPARE(g2.frames.size(), 1); QCOMPARE(g2.loops, 3);
        QCOMPARE(QColor(g2.frames[0].img.pixel(1, 1)), QColor(Qt::blue));
    }

    void gifSupportsTransparencyAndCancel() {
        auto d = make({24, 1}, true);
        QTemporaryDir dir;
        exporter::ExportOptions o; o.lastFrame = 3; o.transparentBackground = true;
        QString err;
        QVERIFY2(exporter::writeGif(*d, dir.path() + "/t.gif", o, &err), qPrintable(err));
        QFile f(dir.path() + "/t.gif"); QVERIFY(f.open(QIODevice::ReadOnly));
        const GifFile g = decodeGif(f.readAll());
        QCOMPARE(g.frames.size(), 4);
        QCOMPARE(QColor(g.frames[0].img.pixel(5, 5)), QColor(Qt::red));
        QCOMPARE(qAlpha(g.frames[3].img.pixel(5, 5)), 0);                  // gap frame is fully transparent
        QVERIFY(g.frames[3].transparent >= 0);
        // cancelling leaves no file behind
        exporter::ExportOptions c; c.lastFrame = 3; int n = 0; c.progress = [&](int, int) { return ++n < 2; };
        QVERIFY(!exporter::writeGif(*d, dir.path() + "/c.gif", c, &err));
        QVERIFY(err.contains("cancel", Qt::CaseInsensitive));
        QVERIFY(!QFile::exists(dir.path() + "/c.gif"));
        // bad range / bad path
        exporter::ExportOptions bad; bad.firstFrame = 5; bad.lastFrame = 2;
        QVERIFY(!exporter::writeGif(*d, dir.path() + "/x.gif", bad, &err));
        QVERIFY(!exporter::writeGif(*d, dir.path() + "/nope/x.gif", o, &err));
    }

    void pngSequenceNumbersFilesAndMatchesTheRender() {
        auto d = make();
        QTemporaryDir dir;
        exporter::ExportOptions o; o.firstFrame = 1; o.lastFrame = 3;
        QStringList files; QString err;
        QVERIFY2(exporter::writePngSequence(*d, dir.path() + "/out", "shot", o, &files, &err), qPrintable(err));
        QCOMPARE(files.size(), 3);
        QVERIFY(files[0].endsWith("shot_0001.png") && files[2].endsWith("shot_0003.png"));
        QCOMPARE(QImage(files[0]).pixelColor(3, 3), QColor(Qt::red));      // source frame 1 (held red)
        QCOMPARE(QImage(files[1]).pixelColor(3, 3), QColor(Qt::blue));     // source frame 2
        QCOMPARE(QImage(files[2]).pixelColor(3, 3), QColor(Qt::white));
        exporter::ExportOptions c; c.lastFrame = 3; int n = 0; c.progress = [&](int, int) { return ++n < 2; };
        QVERIFY(!exporter::writePngSequence(*d, dir.path() + "/o2", "s", c, nullptr, &err));
    }

    // Optional: MYND_GIF_OUT=/path/x.gif writes a gradient animation so an external decoder can check the file.
    void zzWriteSampleForExternalValidation() {
        const QString out = qEnvironmentVariable("MYND_GIF_OUT");
        if (out.isEmpty()) QSKIP("MYND_GIF_OUT not set");
        CompositionSettings s; s.width = 120; s.height = 80; s.durationFrames = 6; s.fps = {12, 1};
        Document d(s);
        const QUuid id = d.addAnimationLayer("A");
        for (int f = 0; f < 6; ++f) {
            const QUuid dr = d.createDrawing(id, f, false);
            QImage& im = d.findLayer(id)->drawingImages[dr];
            QPainter p(&im);
            QLinearGradient g(0, 0, 120, 80); g.setColorAt(0, QColor::fromHsv(f * 40, 255, 255)); g.setColorAt(1, Qt::black);
            p.fillRect(im.rect(), g);
            p.setPen(Qt::white); p.drawEllipse(10 + f * 15, 20, 30, 30);
        }
        exporter::ExportOptions o; o.lastFrame = 5;
        QString err; QVERIFY2(exporter::writeGif(d, out, o, &err), qPrintable(err));
    }
};

QTEST_MAIN(TestExport)
#include "test_export.moc"
