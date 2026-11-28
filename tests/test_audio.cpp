#include <QtTest>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <cmath>

#include "TestEnv.h"
#include "Audio/AudioClip.h"
#include "Audio/AudioMixer.h"
#include "Core/ProjectIO.h"
#include "Graphics/Compositor.h"
#include "Scene/Document.h"

using namespace mynd;

// Phase 10: audio clips, decoding, mixing, project storage.
class TestAudio : public QObject {
    Q_OBJECT
    static QByteArray wav(int rate, int ch, int bits, const std::vector<double>& s, bool isFloat = false) {   // s: interleaved, -1..1
        const int bps = bits / 8;
        QByteArray body;
        for (double v : s) {
            if (isFloat) { if (bits == 32) { float f = float(v); body.append(reinterpret_cast<const char*>(&f), 4); } else { body.append(reinterpret_cast<const char*>(&v), 8); } }
            else if (bits == 8) body.append(char(quint8(std::lround(v * 127) + 128)));
            else { qint32 q = qint32(std::lround(v * (bits == 16 ? 32767.0 : bits == 24 ? 8388607.0 : 2147483647.0))); body.append(reinterpret_cast<const char*>(&q), bps); }
        }
        QByteArray h;
        auto le32 = [&](quint32 x) { h.append(reinterpret_cast<const char*>(&x), 4); };
        auto le16 = [&](quint16 x) { h.append(reinterpret_cast<const char*>(&x), 2); };
        h += "RIFF"; le32(36 + body.size()); h += "WAVEfmt "; le32(16); le16(isFloat ? 3 : 1); le16(ch); le32(rate); le32(rate * ch * bps); le16(ch * bps); le16(bits);
        h += "data"; le32(body.size());
        return h + body;
    }
    // A clip of `seconds` of a constant value on both channels.
    static AudioClipPtr constClip(double seconds, double v, const QString& name = "tone.wav") {
        std::vector<double> s(size_t(seconds * kAudioRate) * 2, v);
        QString err;
        auto c = decodeAudio(wav(kAudioRate, 2, 16, s), name, &err);
        return c;
    }
    static float at(const std::vector<float>& m, double sec, int ch) { return m[size_t(std::llround(sec * kAudioRate)) * 2 + size_t(ch)]; }
    static std::unique_ptr<Document> doc(int frames = 48) {
        CompositionSettings cs; cs.width = 32; cs.height = 32; cs.durationFrames = frames; cs.fps = {24, 1};
        return std::make_unique<Document>(cs);
    }

private slots:
    void wavDecodeResamplesAndUpmixes() {
        std::vector<double> s(4410, 0.5);                                   // 0.1 s mono at 44.1 kHz
        QString err;
        auto c = decodeAudio(wav(44100, 1, 16, s), "m.wav", &err);
        QVERIFY2(c, qPrintable(err));
        QCOMPARE(c->frames(), qint64(4800));
        QCOMPARE(c->format, QString("wav"));
        QCOMPARE(c->name, QString("m"));
        QVERIFY(std::fabs(c->pcm[1000] - 0.5f) < 0.001f);                   // both channels carry the mono signal
        QVERIFY(std::fabs(c->pcm[1001] - 0.5f) < 0.001f);
    }
    void wavSampleFormats() {
        const std::vector<double> s = {0.5, -0.5, 0.25, -0.25};            // 2 stereo frames
        for (int bits : {8, 16, 24, 32}) {
            auto c = decodeWav(wav(kAudioRate, 2, bits, s));
            QVERIFY2(c, qPrintable(QString::number(bits)));
            QCOMPARE(c->frames(), qint64(2));
            const float tol = bits == 8 ? 0.02f : 0.001f;
            QVERIFY2(std::fabs(c->pcm[0] - 0.5f) < tol && std::fabs(c->pcm[1] + 0.5f) < tol, qPrintable(QString::number(bits)));
        }
        for (int bits : {32, 64}) {
            auto c = decodeWav(wav(kAudioRate, 2, bits, s, true));
            QVERIFY(c);
            QVERIFY(std::fabs(c->pcm[2] - 0.25f) < 1e-6f);
        }
    }
    void badInputIsRejectedWithAMessage() {
        QString err;
        QVERIFY(!decodeWav("RIFFxxxxWAVE", &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!decodeAudio(QByteArray(), "x.wav", &err));
        QVERIFY(!decodeWav(wav(kAudioRate, 2, 16, {}), &err));              // no samples
        QByteArray trunc = wav(kAudioRate, 2, 16, std::vector<double>(100, 0.1)); trunc.chop(51);   // cut in the middle of a sample
        QVERIFY(decodeWav(trunc));                                           // damaged tail: what is there still plays
    }
    void encodeWavRoundTrips() {
        std::vector<float> v;
        for (int i = 0; i < 1000; ++i) { v.push_back(std::sin(i * 0.05f) * 0.8f); v.push_back(std::cos(i * 0.05f) * 0.6f); }
        for (bool f32 : {false, true}) {
            auto c = decodeWav(encodeWav(v, f32));
            QVERIFY(c);
            QCOMPARE(c->frames(), qint64(1000));
            for (size_t i = 0; i < v.size(); i += 37) QVERIFY(std::fabs(c->pcm[i] - v[i]) < (f32 ? 1e-6f : 1e-4f));
        }
        std::vector<float> loud = {2.0f, -2.0f};
        auto c = decodeWav(encodeWav(loud));
        QVERIFY(std::fabs(c->pcm[0] - 1.0f) < 0.001f && std::fabs(c->pcm[1] + 1.0f) < 0.001f);   // clipped, not wrapped
    }
    void peaksFollowTheSignal() {
        std::vector<double> s;
        for (int i = 0; i < kAudioRate; ++i) { const double a = i < kAudioRate / 2 ? 0.1 : 0.9; s.push_back(a); s.push_back(a); }
        auto c = decodeWav(wav(kAudioRate, 2, 16, s));
        QVERIFY(c);
        QCOMPARE(qint64(c->peaks.size()), (c->frames() + AudioClip::kPeakBlock - 1) / AudioClip::kPeakBlock);
        QVERIFY(std::fabs(c->peaks.front() - 0.1f) < 0.01f);
        QVERIFY(std::fabs(c->peaks.back() - 0.9f) < 0.01f);
    }
    void mixerPlacesTrimsAndLoopsClips() {
        auto d = doc();
        auto clip = constClip(1.0, 0.5);
        QVERIFY(clip);
        const QUuid id = d->addAudioLayer(clip, "a", 24);                    // starts at 1 s
        AudioParams p = d->findLayer(id)->audioParams;
        p.trimIn = 0.25; p.trimOut = 0.75;                                   // 0.5 s plays
        d->setAudioParams(id, p);
        auto m = renderAudioMix(audioSceneOf(*d->sceneRoot(), d->comp().fps, 96));   // 4 s
        QCOMPARE(qint64(m.size()), qint64(4 * kAudioRate * 2));
        QCOMPARE(at(m, 0.5, 0), 0.0f);                                       // before the start
        QVERIFY(std::fabs(at(m, 1.2, 0) - 0.5f) < 0.001f);
        QCOMPARE(at(m, 1.6, 0), 0.0f);                                       // trimmed part (0.5 s) is over
        p.loop = true;
        d->setAudioParams(id, p);
        m = renderAudioMix(audioSceneOf(*d->sceneRoot(), d->comp().fps, 96));
        QVERIFY(std::fabs(at(m, 1.6, 1) - 0.5f) < 0.001f);                   // loops
        QVERIFY(std::fabs(at(m, 3.9, 1) - 0.5f) < 0.001f);
        QCOMPARE(at(m, 0.9, 1), 0.0f);
    }
    void volumeAndPanAreKeyframable() {
        auto d = doc();
        const QUuid id = d->addAudioLayer(constClip(4.0, 0.5), "a", 0);
        Layer* l = d->findLayer(id);
        setValueAt(l->tracks, "audio.volume", 0, PropValue::of(0), true);     // fade 0 % -> 200 % over frames 0..48 (2 s)
        setValueAt(l->tracks, "audio.volume", 48, PropValue::of(200), true);
        auto m = renderAudioMix(audioSceneOf(*d->sceneRoot(), d->comp().fps, 96));
        QVERIFY(std::fabs(at(m, 0.0, 0)) < 0.01f);
        QVERIFY(std::fabs(at(m, 1.0, 0) - 0.5f) < 0.02f);                    // halfway: 100 %
        QVERIFY(std::fabs(at(m, 2.0, 0) - 1.0f) < 0.02f);                    // 200 %
        QVERIFY(std::fabs(at(m, 3.0, 0) - 1.0f) < 0.02f);                    // holds after the last key
        l->tracks.clear();
        setValueAt(l->tracks, "audio.pan", 0, PropValue::of(-100), false);    // hard left (static)
        m = renderAudioMix(audioSceneOf(*d->sceneRoot(), d->comp().fps, 96));
        QVERIFY(std::fabs(at(m, 1.0, 0) - 0.5f) < 0.001f);
        QCOMPARE(at(m, 1.0, 1), 0.0f);
        setValueAt(l->tracks, "audio.pan", 0, PropValue::of(50), false);
        m = renderAudioMix(audioSceneOf(*d->sceneRoot(), d->comp().fps, 96));
        QVERIFY(std::fabs(at(m, 1.0, 0) - 0.25f) < 0.001f);                  // panned right: the left channel is attenuated
        QVERIFY(std::fabs(at(m, 1.0, 1) - 0.5f) < 0.001f);
    }
    void mutedLayersAndGroupsAreSilentAndClipsSum() {
        auto d = doc();
        const QUuid a = d->addAudioLayer(constClip(2.0, 0.25), "a", 0);
        const QUuid b = d->addAudioLayer(constClip(2.0, 0.25), "b", 0);
        auto mix = [&] { return renderAudioMix(audioSceneOf(*d->sceneRoot(), d->comp().fps, 48)); };
        QVERIFY(std::fabs(at(mix(), 0.5, 0) - 0.5f) < 0.001f);                // two layers add
        LayerProps pr = d->findLayer(b)->props; pr.visible = false; d->setLayerProps(b, pr);
        QVERIFY(std::fabs(at(mix(), 0.5, 0) - 0.25f) < 0.001f);               // muted
        QVERIFY(hasAudio(*d->sceneRoot()));
        pr = d->findLayer(a)->props; pr.visible = false; d->setLayerProps(a, pr);
        QCOMPARE(at(mix(), 0.5, 0), 0.0f);
    }
    void parametersAreClampedAndUndoable() {
        auto d = doc();
        const QUuid id = d->addAudioLayer(constClip(1.0, 0.5), "a", -5);
        QCOMPARE(d->findLayer(id)->audioParams.startFrame, 0);
        AudioParams p = d->findLayer(id)->audioParams;
        p.trimIn = 5; p.trimOut = 0.2; p.startFrame = -3;
        d->setAudioParams(id, p);
        const AudioParams q = d->findLayer(id)->audioParams;
        QCOMPARE(q.startFrame, 0);
        QVERIFY(q.trimIn <= 1.0 && (q.trimOut == 0.0 || q.trimOut >= q.trimIn));
        d->undoStack()->undo();
        QCOMPARE(d->findLayer(id)->audioParams.trimIn, 0.0);
        // dragging merges into a single undo step
        const int before = d->undoStack()->index();
        for (int f : {1, 2, 3, 4}) { AudioParams r = d->findLayer(id)->audioParams; r.startFrame = f; d->setAudioParams(id, r, "Move Audio", "move"); }
        QCOMPARE(d->undoStack()->index(), before + 1);
        d->undoStack()->undo();
        QCOMPARE(d->findLayer(id)->audioParams.startFrame, 0);
        d->undoStack()->undo();                                               // the add itself
        QVERIFY(!d->findLayer(id));
        d->undoStack()->redo(); d->undoStack()->redo();
        QCOMPARE(d->findLayer(id)->audioParams.startFrame, 4);
    }
    void audioLayersDrawNothing() {
        auto d = doc();
        d->addAudioLayer(constClip(1.0, 0.5), "a", 0);
        Compositor::Options o; o.frame = 0; o.includeBackground = false;
        const QImage img = Compositor::render(*d, d->canvasRect(), o);
        for (int y = 0; y < img.height(); ++y) for (int x = 0; x < img.width(); ++x) QCOMPARE(qAlpha(img.pixel(x, y)), 0);
    }
    void projectRoundTripKeepsClipParamsAndKeyframes() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        auto d = doc();
        auto clip = constClip(1.5, 0.4, "voice.wav");
        const QUuid id = d->addAudioLayer(clip, "Voice", 12);
        AudioParams p = d->findLayer(id)->audioParams; p.trimIn = 0.1; p.trimOut = 1.2; p.loop = true;
        d->setAudioParams(id, p);
        Layer* l = d->findLayer(id);
        setValueAt(l->tracks, "audio.volume", 0, PropValue::of(50), true);
        setValueAt(l->tracks, "audio.volume", 24, PropValue::of(150), true);
        const QString path = dir.filePath("a.myndcraft");
        QString err;
        QVERIFY2(ProjectIO::save(*d, path, &err), qPrintable(err));
        LoadResult lr = ProjectIO::load(path);
        QVERIFY2(lr.doc, qPrintable(lr.error));
        QVERIFY2(lr.warnings.isEmpty(), qPrintable(lr.warnings.join("; ")));
        auto& r = lr.doc;
        const Layer* rl = r->sceneRoot()->children.back().get();
        QCOMPARE(int(rl->type), int(LayerType::Audio));
        QVERIFY(rl->audio);
        QCOMPARE(rl->audio->frames(), clip->frames());
        QCOMPARE(rl->audio->name, QString("voice"));
        QCOMPARE(rl->audioParams.startFrame, 12);
        QCOMPARE(rl->audioParams.trimIn, 0.1);
        QCOMPARE(rl->audioParams.trimOut, 1.2);
        QVERIFY(rl->audioParams.loop);
        const auto a = renderAudioMix(audioSceneOf(*d->sceneRoot(), d->comp().fps, 48));
        const auto b = renderAudioMix(audioSceneOf(*r->sceneRoot(), r->comp().fps, 48));
        QCOMPARE(a.size(), b.size());
        QVERIFY(a == b);                                                       // identical sound after save/load
    }
    void compressedFormatsDecodeWhenADecoderExists() {
        const QString ff = QStandardPaths::findExecutable("ffmpeg");
        if (ff.isEmpty() || !audioDecodersAvailable()) QSKIP("no ffmpeg / Qt Multimedia decoder here");
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString src = dir.filePath("t.wav"), mp3 = dir.filePath("t.mp3");
        { QFile f(src); QVERIFY(f.open(QIODevice::WriteOnly)); std::vector<double> s; for (int i = 0; i < 44100; ++i) { double v = 0.5 * std::sin(i * 2 * M_PI * 440 / 44100); s.push_back(v); s.push_back(v); } f.write(wav(44100, 2, 16, s)); }
        QProcess p; p.start(ff, {"-v", "error", "-y", "-i", src, mp3});
        QVERIFY(p.waitForFinished(30000));
        if (!QFile::exists(mp3) || p.exitCode() != 0) QSKIP("this ffmpeg cannot encode MP3");
        QString err;
        auto c = decodeAudioFile(mp3, &err);
        QVERIFY2(c, qPrintable(err));
        QCOMPARE(c->format, QString("mp3"));
        QVERIFY(std::fabs(c->duration() - 1.0) < 0.15);
        float peak = 0; for (float v : c->pcm) peak = std::max(peak, std::fabs(v));
        QVERIFY(peak > 0.3f && peak < 0.8f);
    }
};

QTEST_MAIN(TestAudio)
#include "test_audio.moc"
