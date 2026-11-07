// Runs on a playback thread: pulls every routed input from its broadcast ring
// (each route with its own read cursor, latency target, drift controller and
// resampler), mixes them with per-route gains, applies output volume, master volume,
// mutes, the limiter / feedback guard, then converts to the device format.
#pragma once

#include "AudioGraph.h"
#include "AudioResampler.h"
#include "DriftController.h"

#include <vector>

namespace ab {

class AudioOutputMixer {
public:
    static constexpr std::size_t kChunk = 2048; // device frames rendered per inner iteration

    // Engine (control thread): attach to an output slot of the graph.
    void bind(AudioGraph* graph, int slot) { graph_ = graph; slot_ = slot; }

    // Backend (control thread), once the device mix format is known and before the
    // playback thread starts. Allocates everything the real-time path needs.
    void prepare(const StreamFormat& deviceFormat);

    // Playback thread: fills `dst` (device format) with `frames` frames.
    void render(void* dst, std::size_t frames);

    // Playback thread: fills `dst` with silence while still consuming routes (used
    // while the device is warming up).
    void renderSilence(void* dst, std::size_t frames);

    const StreamFormat& deviceFormat() const { return format_; }
    int slot() const { return slot_; }

private:
    enum class RouteState { Idle, Priming, Running };

    struct RouteReader {
        RouteState state = RouteState::Idle;
        std::uint32_t generation = 0;
        std::uint64_t readIndex = 0;
        AudioResampler resampler;
        DriftController drift;
        SmoothedValue gain;
        double adaptiveMarginFrames = 0.0; // grows after underruns (jitter buffer)
        double lastFill = 0.0;
        std::uint64_t cleanCallbacks = 0;
    };

    void renderChunk(std::uint8_t* dst, std::size_t frames);
    void mixRoute(int input, std::size_t frames);
    double targetFillFrames(const RouteReader& r, const InputSlot& in) const;

    AudioGraph* graph_ = nullptr;
    int slot_ = -1;
    StreamFormat format_;
    double engineRate_ = 48000.0;
    double nominalRatio_ = 1.0; // engine frames per device frame
    RouteReader readers_[kMaxInputs];
    SmoothedValue outGain_;
    Limiter limiter_;
    std::size_t maxBlockSeen_ = 0;

    std::vector<float> mix_;
    std::vector<float> routeIn_;
    std::vector<float> routeOut_;
    std::vector<float> devBuf_;
};

} // namespace ab
