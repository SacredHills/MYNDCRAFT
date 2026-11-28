#pragma once
#include <QByteArray>
#include <QString>
#include <memory>
#include <vector>

namespace mynd {

// Decoded audio the engine works with: 48 kHz, stereo, interleaved 32-bit float. `original` keeps the imported
// file's bytes so the project can store them untouched (and re-decode on load).
constexpr int kAudioRate = 48000;

struct AudioClip {
    QString name;                  // shown in the timeline
    QString format;                // "wav", "mp3", ... (file extension of `original`)
    QByteArray original;
    std::vector<float> pcm;        // L R L R ...
    qint64 frames() const { return qint64(pcm.size() / 2); }
    double duration() const { return double(frames()) / kAudioRate; }
    // Peak (max |sample|, both channels) of each block of `kPeakBlock` frames, for drawing waveforms.
    static constexpr int kPeakBlock = 256;
    std::vector<float> peaks;
    void buildPeaks();
};
using AudioClipPtr = std::shared_ptr<const AudioClip>;

// Decodes a file's bytes. WAV (8/16/24/32-bit PCM and 32/64-bit float, any rate/channel count) is read natively; other
// formats (MP3, OGG, FLAC, AAC, ...) go through Qt Multimedia when the build has it, then through an `ffmpeg` found on
// PATH. Returns null and sets *error when nothing could decode it.
AudioClipPtr decodeAudio(const QByteArray& bytes, const QString& fileName, QString* error = nullptr);
AudioClipPtr decodeAudioFile(const QString& path, QString* error = nullptr);
// Native WAV only (exposed for tests and the mixdown reader).
AudioClipPtr decodeWav(const QByteArray& bytes, QString* error = nullptr);
// 16-bit (or 32-bit float) stereo WAV file bytes from interleaved float samples at kAudioRate.
QByteArray encodeWav(const std::vector<float>& interleavedStereo, bool float32 = false);
// True when this build can decode formats other than WAV through Qt Multimedia.
bool audioDecodersAvailable();

}  // namespace mynd
