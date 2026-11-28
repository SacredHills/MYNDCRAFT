#include "Audio/AudioClip.h"

#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef MYND_HAS_MULTIMEDIA
#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QUrl>
#endif

namespace mynd {

void AudioClip::buildPeaks() {
    peaks.clear();
    const qint64 n = frames();
    peaks.reserve(size_t(n / kPeakBlock + 1));
    for (qint64 i = 0; i < n; i += kPeakBlock) {
        float m = 0;
        const qint64 e = std::min<qint64>(n, i + kPeakBlock);
        for (qint64 k = i * 2; k < e * 2; ++k) m = std::max(m, std::fabs(pcm[size_t(k)]));
        peaks.push_back(m);
    }
}

namespace {

// Linear resample + channel conversion of interleaved float samples to 48 kHz stereo.
std::vector<float> toStereo48k(const std::vector<float>& in, int channels, int rate) {
    std::vector<float> out;
    if (channels < 1 || rate < 1 || in.empty()) return out;
    const qint64 inFrames = qint64(in.size() / size_t(channels));
    auto sample = [&](qint64 f, int side) -> float {      // side 0 = left, 1 = right
        f = std::clamp<qint64>(f, 0, inFrames - 1);
        const float* p = &in[size_t(f) * size_t(channels)];
        if (channels == 1) return p[0];
        return p[std::min(side, channels - 1)];
    };
    const qint64 outFrames = rate == kAudioRate ? inFrames : qint64(std::llround(double(inFrames) * kAudioRate / rate));
    out.resize(size_t(outFrames) * 2);
    for (qint64 i = 0; i < outFrames; ++i) {
        if (rate == kAudioRate) {
            out[size_t(i) * 2] = sample(i, 0); out[size_t(i) * 2 + 1] = sample(i, 1);
        } else {
            const double pos = double(i) * rate / kAudioRate;
            const qint64 a = qint64(pos);
            const float t = float(pos - double(a));
            for (int s = 0; s < 2; ++s) out[size_t(i) * 2 + size_t(s)] = sample(a, s) * (1 - t) + sample(a + 1, s) * t;
        }
    }
    return out;
}

AudioClipPtr finish(std::vector<float>&& pcm, const QByteArray& bytes, const QString& fileName) {
    auto c = std::make_shared<AudioClip>();
    c->name = QFileInfo(fileName).completeBaseName();
    if (c->name.isEmpty()) c->name = "Audio";
    c->format = QFileInfo(fileName).suffix().toLower();
    c->original = bytes;
    c->pcm = std::move(pcm);
    c->buildPeaks();
    return c;
}

quint32 rd32(const uchar* p) { return quint32(p[0]) | quint32(p[1]) << 8 | quint32(p[2]) << 16 | quint32(p[3]) << 24; }
quint16 rd16(const uchar* p) { return quint16(p[0] | p[1] << 8); }

#ifdef MYND_HAS_MULTIMEDIA
bool decodeWithQt(const QString& path, std::vector<float>* out, QString* error) {
    QAudioDecoder dec;
    QAudioFormat fmt;
    fmt.setSampleRate(kAudioRate);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Float);
    dec.setAudioFormat(fmt);
    dec.setSource(QUrl::fromLocalFile(path));
    QEventLoop loop;
    bool failed = false;
    QString msg;
    QObject::connect(&dec, &QAudioDecoder::bufferReady, &dec, [&] {
        const QAudioBuffer b = dec.read();
        if (!b.isValid() || b.format().sampleFormat() != QAudioFormat::Float) return;
        const int ch = b.format().channelCount();
        const float* d = b.constData<float>();
        const qint64 n = b.frameCount();
        const size_t base = out->size();
        out->resize(base + size_t(n) * 2);
        for (qint64 i = 0; i < n; ++i) {
            (*out)[base + size_t(i) * 2] = d[i * ch];
            (*out)[base + size_t(i) * 2 + 1] = d[i * ch + (ch > 1 ? 1 : 0)];
        }
    });
    QObject::connect(&dec, &QAudioDecoder::finished, &loop, &QEventLoop::quit);
    QObject::connect(&dec, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), &loop, [&](QAudioDecoder::Error) {
        failed = true; msg = dec.errorString(); loop.quit();
    });
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, [&] { failed = true; msg = "decoder timed out"; loop.quit(); });
    guard.start(120000);
    dec.start();
    if (dec.isDecoding() || !failed) loop.exec();
    if (failed || out->empty()) { if (error) *error = msg.isEmpty() ? "no audio decoded" : msg; return false; }
    return true;
}
#endif

QString findFfmpeg() { return QStandardPaths::findExecutable("ffmpeg"); }

bool decodeWithFfmpeg(const QString& path, std::vector<float>* out, QString* error) {
    const QString exe = findFfmpeg();
    if (exe.isEmpty()) { if (error) *error = "no ffmpeg on PATH"; return false; }
    QProcess p;
    p.start(exe, {"-v", "error", "-i", path, "-vn", "-f", "f32le", "-ac", "2", "-ar", QString::number(kAudioRate), "-"});
    if (!p.waitForStarted(5000)) { if (error) *error = "ffmpeg did not start"; return false; }
    QByteArray all;
    while (p.state() != QProcess::NotRunning) { p.waitForReadyRead(200); all += p.readAllStandardOutput(); }
    all += p.readAllStandardOutput();
    if (p.exitCode() != 0 || all.size() < 8) { if (error) *error = QString::fromUtf8(p.readAllStandardError()).trimmed(); return false; }
    out->resize(size_t(all.size() / 4));
    std::memcpy(out->data(), all.constData(), out->size() * 4);
    if (out->size() % 2) out->pop_back();
    return true;
}

}  // namespace

AudioClipPtr decodeWav(const QByteArray& bytes, QString* error) {
    auto fail = [&](const char* m) -> AudioClipPtr { if (error) *error = m; return nullptr; };
    const uchar* d = reinterpret_cast<const uchar*>(bytes.constData());
    const qint64 n = bytes.size();
    if (n < 12 || std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "WAVE", 4) != 0) return fail("not a WAV file");
    int fmtTag = 0, ch = 0, rate = 0, bits = 0;
    const uchar* data = nullptr; qint64 dataLen = 0;
    qint64 pos = 12;
    while (pos + 8 <= n) {
        const quint32 len = rd32(d + pos + 4);
        const uchar* body = d + pos + 8;
        const qint64 avail = std::min<qint64>(len, n - pos - 8);
        if (std::memcmp(d + pos, "fmt ", 4) == 0 && avail >= 16) {
            fmtTag = rd16(body); ch = rd16(body + 2); rate = int(rd32(body + 4)); bits = rd16(body + 14);
            if (fmtTag == 0xFFFE && avail >= 26) fmtTag = rd16(body + 24);        // WAVE_FORMAT_EXTENSIBLE: sub-format
        } else if (std::memcmp(d + pos, "data", 4) == 0) {
            data = body; dataLen = avail;
        }
        pos += 8 + qint64(len) + (len & 1);
    }
    if (!ch || !data) return fail("WAV has no fmt/data chunk");
    if (ch > 8 || rate < 1000 || rate > 400000) return fail("unsupported WAV layout");
    const bool isFloat = fmtTag == 3, isPcm = fmtTag == 1;
    if (!(isPcm && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) && !(isFloat && (bits == 32 || bits == 64))) return fail("unsupported WAV sample format");
    const int bps = bits / 8;
    const qint64 count = dataLen / bps;
    std::vector<float> in(size_t(count - count % ch));
    for (size_t i = 0; i < in.size(); ++i) {
        const uchar* s = data + i * size_t(bps);
        float v = 0;
        if (isFloat) { if (bits == 32) { float f; std::memcpy(&f, s, 4); v = f; } else { double f; std::memcpy(&f, s, 8); v = float(f); } }
        else if (bits == 8) v = (int(s[0]) - 128) / 128.0f;
        else if (bits == 16) v = qint16(rd16(s)) / 32768.0f;
        else if (bits == 24) { qint32 x = qint32(s[0] << 8 | s[1] << 16 | quint32(s[2]) << 24) >> 8; v = x / 8388608.0f; }
        else v = qint32(rd32(s)) / 2147483648.0f;
        in[i] = std::isfinite(v) ? v : 0.0f;
    }
    std::vector<float> pcm = toStereo48k(in, ch, rate);
    if (pcm.empty()) return fail("WAV contains no samples");
    return finish(std::move(pcm), bytes, "audio.wav");
}

QByteArray encodeWav(const std::vector<float>& s, bool f32) {
    const int bps = f32 ? 4 : 2;
    const quint32 dataLen = quint32(s.size()) * quint32(bps);
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.writeRawData("RIFF", 4); ds << quint32(36 + dataLen); ds.writeRawData("WAVE", 4);
    ds.writeRawData("fmt ", 4); ds << quint32(16) << quint16(f32 ? 3 : 1) << quint16(2) << quint32(kAudioRate)
       << quint32(kAudioRate * 2 * bps) << quint16(2 * bps) << quint16(bps * 8);
    ds.writeRawData("data", 4); ds << dataLen;
    QByteArray body(int(dataLen), 0);
    char* p = body.data();
    for (float v : s) {
        if (f32) { std::memcpy(p, &v, 4); p += 4; }
        else { const qint16 q = qint16(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)); std::memcpy(p, &q, 2); p += 2; }
    }
    out.append(body);
    return out;
}

bool audioDecodersAvailable() {
#ifdef MYND_HAS_MULTIMEDIA
    return true;
#else
    return !findFfmpeg().isEmpty();
#endif
}

AudioClipPtr decodeAudio(const QByteArray& bytes, const QString& fileName, QString* error) {
    if (bytes.isEmpty()) { if (error) *error = "empty file"; return nullptr; }
    QString werr;
    if (bytes.size() > 12 && bytes.startsWith("RIFF")) {
        if (auto c = decodeWav(bytes, &werr)) {
            auto m = std::make_shared<AudioClip>(*c);
            m->name = QFileInfo(fileName).completeBaseName().isEmpty() ? "Audio" : QFileInfo(fileName).completeBaseName();
            m->format = "wav";
            return m;
        }
    }
    // Other formats: the decoders want a real file.
    QTemporaryDir dir;
    if (!dir.isValid()) { if (error) *error = "cannot create a temporary folder"; return nullptr; }
    QString suffix = QFileInfo(fileName).suffix().toLower();
    if (suffix.isEmpty()) suffix = "bin";
    const QString tmp = dir.filePath("in." + suffix);
    { QFile f(tmp); if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) { if (error) *error = "cannot write temporary file"; return nullptr; } }
    std::vector<float> pcm;
    QString qerr, ferr;
    bool ok = false;
#ifdef MYND_HAS_MULTIMEDIA
    ok = decodeWithQt(tmp, &pcm, &qerr);
#endif
    if (!ok) { pcm.clear(); ok = decodeWithFfmpeg(tmp, &pcm, &ferr); }
    if (!ok) {
        if (error) *error = QString("Cannot decode '%1': %2").arg(QFileInfo(fileName).fileName(), !qerr.isEmpty() ? qerr : (!ferr.isEmpty() ? ferr : werr));
        return nullptr;
    }
    return finish(std::move(pcm), bytes, fileName);
}

AudioClipPtr decodeAudioFile(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = "Cannot open " + path; return nullptr; }
    return decodeAudio(f.readAll(), path, error);
}

}  // namespace mynd
