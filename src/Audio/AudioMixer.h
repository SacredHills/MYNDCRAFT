#pragma once
#include <vector>

#include "Audio/AudioClip.h"
#include "Scene/Document.h"

namespace mynd {

// Everything the mixer needs from one audio layer, copied out of the document so playback can mix on the audio thread
// while the user keeps editing (the engine swaps in a fresh list after every change).
struct AudioSource {
    AudioClipPtr clip;
    AudioParams params;
    PropertyTracks tracks;       // audio.volume / audio.pan keyframes
};
struct AudioScene {
    std::vector<AudioSource> sources;
    FrameRate fps{24, 1};
    int durationFrames = 0;
};

// The audible layers of a composition tree (visible layers inside visible groups; muted layers are skipped).
AudioScene audioSceneOf(const Layer& root, FrameRate fps, int durationFrames);
bool hasAudio(const Layer& root);

// Adds `frames` stereo frames of the mix, starting at composition time `startSample` / kAudioRate seconds, into `out`
// (interleaved, `out` must hold frames*2 floats and is overwritten). Volume/pan keyframes are evaluated at the
// composition frame the sample falls in (smoothed across 32-sample blocks), clip trim and loop are applied.
void mixAudio(const AudioScene& scene, qint64 startSample, int frames, float* out);
// Renders the whole composition (0 .. duration) to interleaved stereo floats.
std::vector<float> renderAudioMix(const AudioScene& scene);

}  // namespace mynd
