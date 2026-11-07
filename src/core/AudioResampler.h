// Streaming band-limited (windowed-sinc, polyphase) resampler with a continuously
// variable ratio. Used for
//   * fixed sample-rate conversion (e.g. 16 kHz Bluetooth HFP mic -> 48 kHz engine), and
//   * clock-drift compensation between devices that run on independent crystals
//     (ratio is nudged by a few hundred ppm by the DriftController).
// All buffers are allocated in configure(); process calls never allocate.
#pragma once

#include <cstddef>
#include <vector>

namespace ab {

class AudioResampler {
public:
    // halfTaps: filter half length in input samples (quality); phases: kernel table resolution.
    void configure(int channels, double inputRate, double outputRate, std::size_t maxOutputFrames,
                   int halfTaps = 24, int phases = 256);

    // Input frames consumed per output frame. Nominal = inputRate / outputRate.
    void setRatio(double inputPerOutput);
    double ratio() const { return ratio_; }
    double nominalRatio() const { return nominalRatio_; }

    // True when input and output rates are identical and no drift correction is
    // requested: callers may bypass processing entirely.
    bool isIdentity() const { return identity_; }

    void reset();

    // Pull mode: how many new input frames must be supplied so that exactly
    // `outFrames` output frames can be produced with the current ratio.
    std::size_t requiredInput(std::size_t outFrames) const;

    // Pull mode: consumes `inFrames` (as returned by requiredInput) and writes exactly
    // `outFrames` interleaved frames.
    void processPull(const float* in, std::size_t inFrames, float* out, std::size_t outFrames);

    // Push mode: consumes all `inFrames` and writes as many output frames as can be
    // produced (at most maxOut). Returns the number of frames written.
    std::size_t processPush(const float* in, std::size_t inFrames, float* out, std::size_t maxOut);

    // Upper bound for output frames produced from `inFrames` input frames in push mode.
    std::size_t maxOutputFor(std::size_t inFrames) const;

    // Group delay in input frames.
    int latencyFrames() const { return halfTaps_; }

private:
    void append(const float* in, std::size_t frames);
    std::size_t produce(float* out, std::size_t maxOut);
    void compact();

    int channels_ = 2;
    int halfTaps_ = 16;
    int phases_ = 512;
    double nominalRatio_ = 1.0;
    double ratio_ = 1.0;
    bool identity_ = true;
    std::vector<float> table_;  // (phases_ + 1) * (2 * halfTaps_)
    std::vector<float> buf_;    // interleaved history + pending input
    std::size_t bufFrames_ = 0;
    std::size_t bufCapacity_ = 0;
    double pos_ = 0.0;          // position (in buf_ frames) of the next output sample
};

} // namespace ab
