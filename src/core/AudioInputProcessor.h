// Runs on a capture thread. Converts raw device packets to the engine bus format
// (stereo float at the engine sample rate), applies the input channel strip
// (gain, high-pass, noise suppression, gate, fader, mute/solo) and publishes the
// result into the input slot's broadcast ring for every route reader.
#pragma once

#include "AudioGraph.h"
#include "AudioProcessing.h"
#include "AudioResampler.h"

#include <vector>

namespace ab {

class AudioInputProcessor {
public:
    static constexpr std::size_t kChunk = 2048; // device frames processed per inner iteration

    // Engine (control thread): attach to an input slot of the graph.
    void bind(AudioGraph* graph, int slot) { graph_ = graph; slot_ = slot; }

    // Backend (control thread), once the device mix format is known and before the
    // capture thread starts. Allocates everything the real-time path needs.
    void prepare(const StreamFormat& deviceFormat);

    // Capture thread. `data` may be null when `silent` is set (AUDCLNT_BUFFERFLAGS_SILENT).
    void process(const void* data, std::size_t frames, bool silent);

    // Capture thread: device reported a gap in the stream.
    void reportDiscontinuity();

    const StreamFormat& deviceFormat() const { return format_; }
    int slot() const { return slot_; }

private:
    void processChunk(const std::uint8_t* data, std::size_t frames, bool silent);

    AudioGraph* graph_ = nullptr;
    int slot_ = -1;
    StreamFormat format_;
    double engineRate_ = 48000.0;

    AudioResampler resampler_;
    HighPassFilter hpf_;
    NoiseSuppressor ns_;
    NoiseGate gate_;
    SmoothedValue gain_;
    SmoothedValue fader_;
    bool hpfOn_ = false, nsOn_ = false, gateOn_ = false;

    std::vector<float> devBuf_;
    std::vector<float> stereoBuf_;
    std::vector<float> resampled_;
};

} // namespace ab
