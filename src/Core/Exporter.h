#pragma once
#include <QByteArray>
#include <QImage>
#include <QString>
#include <functional>

#include "Scene/Document.h"

namespace mynd::exporter {

// Renders animation frames out of a Document. Pure engine code (no widgets). Exports never include the
// onion skin or any editor overlay.
struct ExportOptions {
    int firstFrame = 0;
    int lastFrame = 0;               // inclusive
    bool transparentBackground = false;
    int loopCount = 0;               // GIF only: 0 = loop forever
    // Called after each frame with (framesDone, framesTotal); return false to cancel.
    std::function<bool(int, int)> progress;
};

// Palette-quantised image: indices into `palette` (RGB triples), `transparentIndex` -1 when unused.
struct Indexed {
    int width = 0, height = 0;
    QByteArray pixels;               // width*height palette indices
    QByteArray palette;              // 3 bytes per entry, entries <= 256
    int transparentIndex = -1;
};

// Median-cut quantiser. Images with <= maxColors distinct colours are reproduced exactly.
Indexed quantize(const QImage& img, int maxColors = 256, bool transparency = false);
// GIF89a LZW image-data stream (min code size byte, sub-blocks, terminator) for one indexed frame.
QByteArray lzwEncode(const QByteArray& indices, int minCodeSize);

// Frame delay in centiseconds for each frame of a run starting at `firstFrame`; the rounding error is
// carried forward so the total length is exact to within one centisecond.
QVector<int> gifDelaysCentiseconds(FrameRate fps, int count);

// Whole-animation GIF. Returns false and fills *error on failure or cancel.
bool writeGif(const Document& doc, const QString& path, const ExportOptions& opt, QString* error = nullptr);
// base_0001.png, base_0002.png ... into `dir`. Numbering starts at 1 for the first exported frame.
bool writePngSequence(const Document& doc, const QString& dir, const QString& baseName, const ExportOptions& opt,
                      QStringList* written = nullptr, QString* error = nullptr);

}  // namespace mynd::exporter
