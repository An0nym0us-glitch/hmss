// AudioBridge DSP building blocks. Everything here operates on interleaved stereo
// float32 blocks, is allocation free after prepare(), and is used by the real-time
// capture/render threads.
#pragma once

#include <atomic>
#include <complex>
#include <cstddef>
#include <vector>

namespace ab {

// ---------------------------------------------------------------------------------
// One-pole parameter smoother (prevents zipper noise when faders move).
class SmoothedValue {
public:
    void prepare(double sampleRate, double timeMs = 15.0);
    void setImmediate(float v) { current_ = target_ = v; }
    void setTarget(float v) { target_ = v; }
    float next() { current_ += (target_ - current_) * coef_; return current_; }
    float current() const { return current_; }
    float target() const { return target_; }
    bool isSettled() const { float d = current_ - target_; return d < 1e-6f && d > -1e-6f; }
    // Applies the smoothed gain to an interleaved stereo block.
    void applyStereo(float* stereo, std::size_t frames);

private:
    float current_ = 0.0f;
    float target_ = 0.0f;
    float coef_ = 0.01f;
};

// ---------------------------------------------------------------------------------
// 2nd order Butterworth high-pass (rumble / handling noise / plosive removal).
class HighPassFilter {
public:
    void prepare(double sampleRate, double cutoffHz = 80.0);
    void reset();
    void processStereo(float* stereo, std::size_t frames);

private:
    double b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;
    double z1_[2] = {0, 0}, z2_[2] = {0, 0};
};

// ---------------------------------------------------------------------------------
// In-place iterative radix-2 complex FFT.
class FFT {
public:
    void prepare(int size);
    int size() const { return n_; }
    void forward(std::complex<float>* data) const { transform(data, false); }
    void inverse(std::complex<float>* data) const { transform(data, true); } // includes 1/N scaling

private:
    void transform(std::complex<float>* data, bool inverse) const;
    int n_ = 0;
    std::vector<std::complex<float>> twiddle_;
    std::vector<int> bitrev_;
};

// ---------------------------------------------------------------------------------
// Stationary noise suppressor: STFT (sqrt-Hann, 50% overlap) with per-bin noise floor
// tracking (minimum-statistics style) and a decision-directed Wiener gain
// (Ephraim-Malah). Reduces fans, hiss, hum and Bluetooth/HFP background noise.
// Adds a fixed latency of one FFT frame (~10.7 ms at 48 kHz).
class NoiseSuppressor {
public:
    void prepare(double sampleRate);
    void reset();
    // strength: 0..1, maps to the maximum attenuation (6..30 dB).
    void setStrength(float s) { strength_ = s; }
    void processStereo(float* stereo, std::size_t frames, bool monoInput);
    int latencyFrames() const { return n_; }

private:
    void processFrame(bool monoInput);

    int n_ = 512;
    int hop_ = 256;
    double sampleRate_ = 48000.0;
    float strength_ = 0.6f;
    FFT fft_;
    std::vector<float> window_;
    std::vector<float> inL_, inR_;     // analysis FIFOs (n_)
    std::vector<float> outL_, outR_;   // overlap-add accumulators (n_)
    std::vector<std::complex<float>> specL_, specR_;
    std::vector<float> noise_, prevGain_, prevSnr_, gain_, smoothPow_;
    int fill_ = 0;      // samples collected towards the next hop
    long framesSeen_ = 0;
};

// ---------------------------------------------------------------------------------
// Downward noise gate with hysteresis and hold.
class NoiseGate {
public:
    void prepare(double sampleRate);
    void reset();
    void setThresholdDb(float db) { thresholdDb_ = db; }
    void processStereo(float* stereo, std::size_t frames);
    bool isOpen() const { return open_; }

private:
    double sampleRate_ = 48000.0;
    float thresholdDb_ = -50.0f;
    float env_ = 0.0f;
    float gain_ = 1.0f;
    bool open_ = true;
    int holdLeft_ = 0;
    float envAttack_ = 0, envRelease_ = 0, gainAttack_ = 0, gainRelease_ = 0;
    int holdSamples_ = 0;
};

// ---------------------------------------------------------------------------------
// Peak limiter (clipping protection, -1 dBFS ceiling, zero-attack so the output can
// never exceed the ceiling) plus a feedback guard that trips when the signal is held
// far above full scale for a sustained period, which is characteristic of acoustic
// feedback (howling) or a runaway gain setting.
class Limiter {
public:
    void prepare(double sampleRate);
    void reset();
    void setEnabled(bool e) { enabled_ = e; }
    void setFeedbackGuard(bool e) { feedbackGuard_ = e; }
    // Returns true if the feedback guard tripped during this block.
    bool processStereo(float* stereo, std::size_t frames);
    float gainReductionDb() const;

private:
    bool enabled_ = true;
    bool feedbackGuard_ = true;
    float ceiling_ = 0.891f; // -1 dBFS
    float env_ = 0.0f;
    float release_ = 0.0f;
    float minGain_ = 1.0f;
    double hotSamples_ = 0.0;
    double tripSamples_ = 0.0;
};

// ---------------------------------------------------------------------------------
// Level meter shared between an audio thread (single writer) and the UI.
struct LevelMeter {
    std::atomic<float> peak{0.0f};   // max abs since last UI read (UI resets)
    std::atomic<float> rms{0.0f};    // smoothed RMS
    std::atomic<bool> clipped{false};// set when a sample reached full scale

    // Audio thread.
    void update(const float* stereo, std::size_t frames, double sampleRate);
    // UI thread: returns peak since last call and resets it.
    float takePeak() { return peak.exchange(0.0f, std::memory_order_relaxed); }
    bool takeClip() { return clipped.exchange(false, std::memory_order_relaxed); }
    void clear() { peak.store(0.0f); rms.store(0.0f); clipped.store(false); }

private:
    float rmsState_ = 0.0f; // writer-private
};

} // namespace ab
