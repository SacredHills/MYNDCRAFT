#include "Audio/AudioMixer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mynd {

static void collect(const Layer& g, AudioScene& s) {
    for (const auto& c : g.children) {
        if (!c->props.visible) continue;
        if (c->type == LayerType::Audio && c->audio) s.sources.push_back({c->audio, c->audioParams, c->tracks});
        if (c->type == LayerType::Group) collect(*c, s);
    }
}
AudioScene audioSceneOf(const Layer& root, FrameRate fps, int durationFrames) {
    AudioScene s; s.fps = fps; s.durationFrames = durationFrames;
    collect(root, s);
    return s;
}
static bool anyAudio(const Layer& g) {
    for (const auto& c : g.children) if ((c->type == LayerType::Audio && c->audio) || anyAudio(*c)) return true;
    return false;
}
bool hasAudio(const Layer& root) { return anyAudio(root); }

void mixAudio(const AudioScene& scene, qint64 startSample, int frames, float* out) {
    std::memset(out, 0, size_t(frames) * 2 * sizeof(float));
    if (frames <= 0) return;
    const double fps = scene.fps.den > 0 ? double(scene.fps.num) / scene.fps.den : 24.0;
    const PropertyDef* vd = findPropertyDef("audio.volume");
    const PropertyDef* pd = findPropertyDef("audio.pan");
    constexpr int kBlock = 32;
    for (const AudioSource& src : scene.sources) {
        const qint64 clipFrames = src.clip->frames();
        if (clipFrames <= 0) continue;
        const double dur = src.clip->duration();
        const qint64 inS = qint64(std::llround(std::clamp(src.params.trimIn, 0.0, dur) * kAudioRate));
        qint64 outS = src.params.trimOut > 0 ? qint64(std::llround(std::clamp(src.params.trimOut, 0.0, dur) * kAudioRate)) : clipFrames;
        outS = std::clamp<qint64>(outS, inS, clipFrames);
        const qint64 len = outS - inS;
        if (len <= 0) continue;
        const qint64 startAt = qint64(std::llround(double(src.params.startFrame) / fps * kAudioRate));
        const float* pcm = src.clip->pcm.data();
        for (int b = 0; b < frames; b += kBlock) {
            const int n = std::min(kBlock, frames - b);
            const qint64 t0 = startSample + b - startAt;
            if (t0 + n <= 0) continue;
            if (!src.params.loop && t0 >= len) continue;
            const double frame = double(startSample + b + n / 2) / kAudioRate * fps;     // composition frame at the block centre
            double vol = vd ? valueOf(src.tracks, *vd, frame).c[0] / 100.0 : 1.0;
            const double pan = pd ? valueOf(src.tracks, *pd, frame).c[0] / 100.0 : 0.0;
            const float gl = float(vol * (pan > 0 ? 1.0 - pan : 1.0)), gr = float(vol * (pan < 0 ? 1.0 + pan : 1.0));
            for (int i = 0; i < n; ++i) {
                qint64 t = t0 + i;
                if (t < 0) continue;
                if (src.params.loop) t %= len; else if (t >= len) break;
                const float* sp = pcm + (inS + t) * 2;
                out[(b + i) * 2] += sp[0] * gl;
                out[(b + i) * 2 + 1] += sp[1] * gr;
            }
        }
    }
}

std::vector<float> renderAudioMix(const AudioScene& scene) {
    const double fps = scene.fps.den > 0 ? double(scene.fps.num) / scene.fps.den : 24.0;
    const qint64 total = qint64(std::llround(double(scene.durationFrames) / fps * kAudioRate));
    std::vector<float> out(size_t(std::max<qint64>(0, total)) * 2);
    constexpr int kChunk = 4096;
    for (qint64 s = 0; s < total; s += kChunk) mixAudio(scene, s, int(std::min<qint64>(kChunk, total - s)), out.data() + s * 2);
    return out;
}

}  // namespace mynd
