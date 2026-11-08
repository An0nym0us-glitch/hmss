// The real-time audio graph: state shared between the control thread (engine / UI)
// and the audio threads (one per capture device, one per playback device).
//
//   capture thread i ──► InputSlot[i].ring ──┬──► route(i,o) reader ─► mix bus of output o ─► playback thread o
//                                            └──► route(i,p) reader ─► mix bus of output p ─► playback thread p
//
// All slots are pre-allocated. Parameters are atomics, so fader moves, mutes, solos and
// routing-matrix changes are applied by the audio threads on their next cycle without
// any locks and without freeing memory an audio thread might still be using.
#pragma once

#include "AudioProcessing.h"
#include "AudioTypes.h"
#include "BroadcastRing.h"

#include <atomic>
#include <cstdint>

namespace ab {

struct InputParams {
    std::atomic<bool> enabled{true};
    std::atomic<float> gainDb{0.0f};      // microphone gain (pre-fader), -24..+36 dB
    std::atomic<float> volume{1.0f};      // channel fader, linear 0..2 (+6 dB)
    std::atomic<bool> mute{false};
    std::atomic<bool> solo{false};
    std::atomic<bool> monoMix{true};      // sum capture channels to mono
    std::atomic<bool> highPass{false};    // 80 Hz rumble filter
    std::atomic<bool> noiseSuppression{false};
    std::atomic<float> noiseSuppressionStrength{0.6f};
    std::atomic<bool> noiseGate{false};
    std::atomic<float> gateThresholdDb{-50.0f};
};

struct InputSlot {
    std::atomic<bool> active{false};             // a capture stream currently feeds this slot
    std::atomic<std::uint32_t> generation{0};    // bumped on every (re)start of the stream
    std::atomic<std::uint32_t> maxBurstFrames{0};// largest block written (engine-rate frames)
    BroadcastRing ring;                          // processed, post-fader audio at engine rate
    InputParams params;
    LevelMeter meter;                            // post-gain, pre-fader
    std::atomic<std::uint64_t> framesCaptured{0};
    std::atomic<std::uint64_t> glitches{0};      // capture discontinuities reported by the device
};

struct OutputParams {
    std::atomic<bool> enabled{true};
    std::atomic<float> volume{1.0f};             // linear 0..2
    std::atomic<bool> mute{false};
    std::atomic<float> extraDelayMs{0.0f};       // manual alignment delay (e.g. wired vs Bluetooth)
};

struct OutputSlot {
    std::atomic<bool> active{false};
    OutputParams params;
    LevelMeter meter;                            // post-fader, post-limiter (what the device receives)
    std::atomic<bool> feedbackTripped{false};    // latched by the feedback guard; cleared by the user
    std::atomic<std::uint64_t> underruns{0};     // route buffer starvation events
    std::atomic<std::uint64_t> resyncs{0};       // route buffer overflow -> latency resynchronisation
    std::atomic<float> routeLatencyMs{0.0f};     // current route buffering (max over routes)
    std::atomic<float> deviceLatencyMs{0.0f};    // WASAPI buffer + stream latency
    std::atomic<float> driftPpm{0.0f};           // clock correction of the first active route
    std::atomic<float> limiterReductionDb{0.0f};
    std::atomic<int> testToneRequests{0};        // incremented by the control thread: play the test chime
};

struct RouteCell {
    std::atomic<bool> enabled{false};
    std::atomic<float> gain{1.0f};               // per-route level, linear 0..2
    LevelMeter meter;                            // written by the output thread that owns the route
};

class AudioGraph {
public:
    AudioGraph() = default;
    AudioGraph(const AudioGraph&) = delete;
    AudioGraph& operator=(const AudioGraph&) = delete;

    InputSlot inputs[kMaxInputs];
    OutputSlot outputs[kMaxOutputs];
    RouteCell routes[kMaxInputs][kMaxOutputs];

    std::atomic<float> masterVolume{0.8f};
    std::atomic<bool> globalMute{false};
    std::atomic<bool> paused{false};
    std::atomic<int> soloCount{0};               // number of enabled inputs with solo
    std::atomic<bool> limiterEnabled{true};
    std::atomic<bool> feedbackGuard{true};

    // Engine (internal) sample rate. Changed only while all streams are stopped.
    std::atomic<int> engineRate{48000};
    // Requested route buffering in engine frames (from the "buffer size" setting).
    std::atomic<int> targetLatencyFrames{960};

    // Ring capacity per input (frames). ~2.7 s at 48 kHz: room for latency targets,
    // manual delays and badly bursting Bluetooth devices.
    static constexpr std::size_t kRingFrames = 1u << 17;

    // Control thread: make sure the ring of an input slot exists (allocates once).
    void ensureInputRing(int slot)
    {
        if (!inputs[slot].ring.isAllocated())
            inputs[slot].ring.allocate(kRingFrames, kEngineChannels);
    }
};

} // namespace ab
