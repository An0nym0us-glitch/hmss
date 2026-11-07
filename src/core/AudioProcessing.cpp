#include "AudioProcessing.h"

#include "AudioTypes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ab {

// ---------------------------------------------------------------------------------
void SmoothedValue::prepare(double sampleRate, double timeMs)
{
    const double samples = std::max(1.0, sampleRate * timeMs / 1000.0);
    coef_ = static_cast<float>(1.0 - std::exp(-1.0 / samples));
}

void SmoothedValue::applyStereo(float* stereo, std::size_t frames)
{
    if (isSettled()) {
        current_ = target_;
        const float g = current_;
        if (g == 1.0f)
            return;
        for (std::size_t i = 0; i < frames * 2; ++i)
            stereo[i] *= g;
        return;
    }
    for (std::size_t f = 0; f < frames; ++f) {
        const float g = next();
        stereo[2 * f] *= g;
        stereo[2 * f + 1] *= g;
    }
}

// ---------------------------------------------------------------------------------
void HighPassFilter::prepare(double sampleRate, double cutoffHz)
{
    const double w0 = 2.0 * kPi * cutoffHz / sampleRate;
    const double cs = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * 0.7071067811865476);
    const double a0 = 1.0 + alpha;
    b0_ = (1.0 + cs) / 2.0 / a0;
    b1_ = -(1.0 + cs) / a0;
    b2_ = (1.0 + cs) / 2.0 / a0;
    a1_ = -2.0 * cs / a0;
    a2_ = (1.0 - alpha) / a0;
    reset();
}

void HighPassFilter::reset()
{
    z1_[0] = z1_[1] = z2_[0] = z2_[1] = 0.0;
}

void HighPassFilter::processStereo(float* stereo, std::size_t frames)
{
    for (int c = 0; c < 2; ++c) {
        double z1 = z1_[c], z2 = z2_[c];
        for (std::size_t f = 0; f < frames; ++f) {
            const double x = stereo[2 * f + c];
            const double y = b0_ * x + z1;
            z1 = b1_ * x - a1_ * y + z2;
            z2 = b2_ * x - a2_ * y;
            stereo[2 * f + c] = static_cast<float>(y);
        }
        // Flush denormals.
        if (std::fabs(z1) < 1e-20) z1 = 0.0;
        if (std::fabs(z2) < 1e-20) z2 = 0.0;
        z1_[c] = z1;
        z2_[c] = z2;
    }
}

// ---------------------------------------------------------------------------------
void FFT::prepare(int size)
{
    n_ = size;
    twiddle_.resize(static_cast<std::size_t>(size / 2));
    for (int i = 0; i < size / 2; ++i)
        twiddle_[i] = std::polar(1.0f, static_cast<float>(-2.0 * kPi * i / size));
    bitrev_.resize(static_cast<std::size_t>(size));
    int bits = 0;
    while ((1 << bits) < size)
        ++bits;
    for (int i = 0; i < size; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b))
                r |= 1 << (bits - 1 - b);
        bitrev_[i] = r;
    }
}

void FFT::transform(std::complex<float>* data, bool inverse) const
{
    const int n = n_;
    for (int i = 0; i < n; ++i)
        if (i < bitrev_[i])
            std::swap(data[i], data[bitrev_[i]]);
    for (int len = 2; len <= n; len <<= 1) {
        const int half = len >> 1;
        const int step = n / len;
        for (int i = 0; i < n; i += len) {
            for (int j = 0; j < half; ++j) {
                std::complex<float> w = twiddle_[static_cast<std::size_t>(j * step)];
                if (inverse)
                    w = std::conj(w);
                const std::complex<float> u = data[i + j];
                const std::complex<float> v = data[i + j + half] * w;
                data[i + j] = u + v;
                data[i + j + half] = u - v;
            }
        }
    }
    if (inverse) {
        const float s = 1.0f / static_cast<float>(n);
        for (int i = 0; i < n; ++i)
            data[i] *= s;
    }
}

// ---------------------------------------------------------------------------------
void NoiseSuppressor::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    n_ = sampleRate > 64000.0 ? 1024 : 512;
    hop_ = n_ / 2;
    fft_.prepare(n_);
    window_.resize(static_cast<std::size_t>(n_));
    for (int i = 0; i < n_; ++i) // periodic sqrt-Hann: analysis * synthesis = Hann (COLA at 50%)
        window_[i] = static_cast<float>(std::sqrt(0.5 - 0.5 * std::cos(2.0 * kPi * i / n_)));
    const std::size_t bins = static_cast<std::size_t>(n_ / 2 + 1);
    inL_.assign(n_, 0.0f);
    inR_.assign(n_, 0.0f);
    outL_.assign(n_, 0.0f);
    outR_.assign(n_, 0.0f);
    specL_.assign(n_, {});
    specR_.assign(n_, {});
    noise_.assign(bins, 0.0f);
    prevGain_.assign(bins, 1.0f);
    prevSnr_.assign(bins, 1.0f);
    gain_.assign(bins, 1.0f);
    smoothPow_.assign(bins, 0.0f);
    reset();
}

void NoiseSuppressor::reset()
{
    std::fill(inL_.begin(), inL_.end(), 0.0f);
    std::fill(inR_.begin(), inR_.end(), 0.0f);
    std::fill(outL_.begin(), outL_.end(), 0.0f);
    std::fill(outR_.begin(), outR_.end(), 0.0f);
    std::fill(noise_.begin(), noise_.end(), 0.0f);
    std::fill(prevGain_.begin(), prevGain_.end(), 1.0f);
    std::fill(prevSnr_.begin(), prevSnr_.end(), 1.0f);
    std::fill(smoothPow_.begin(), smoothPow_.end(), 0.0f);
    fill_ = 0;
    framesSeen_ = 0;
}

void NoiseSuppressor::processStereo(float* stereo, std::size_t frames, bool monoInput)
{
    for (std::size_t f = 0; f < frames; ++f) {
        const float yl = outL_[static_cast<std::size_t>(fill_)];
        const float yr = monoInput ? yl : outR_[static_cast<std::size_t>(fill_)];
        inL_[static_cast<std::size_t>(n_ - hop_ + fill_)] = stereo[2 * f];
        inR_[static_cast<std::size_t>(n_ - hop_ + fill_)] = stereo[2 * f + 1];
        stereo[2 * f] = yl;
        stereo[2 * f + 1] = yr;
        if (++fill_ == hop_) {
            processFrame(monoInput);
            fill_ = 0;
        }
    }
}

void NoiseSuppressor::processFrame(bool monoInput)
{
    const int n = n_;
    const int bins = n / 2 + 1;
    // Pack both channels into one complex FFT: z = l + i*r.
    for (int i = 0; i < n; ++i)
        specL_[i] = {inL_[i] * window_[i], monoInput ? 0.0f : inR_[i] * window_[i]};
    fft_.forward(specL_.data());

    const bool warmup = framesSeen_ < 12;
    const float hopSec = static_cast<float>(hop_ / sampleRate_);
    const float noiseUp = std::pow(10.0f, 4.0f * hopSec / 10.0f); // noise floor may rise 4 dB/s
    const float overSub = 1.0f + strength_;
    const float gMin = dbToLinear(-(6.0f + 24.0f * strength_));

    for (int k = 0; k < bins; ++k) {
        const std::complex<float> z = specL_[k];
        const std::complex<float> zc = std::conj(specL_[(n - k) % n]);
        const std::complex<float> L = 0.5f * (z + zc);
        const std::complex<float> R = std::complex<float>(0.0f, -0.5f) * (z - zc);
        const float p = monoInput ? std::norm(L) : 0.5f * (std::norm(L) + std::norm(R));
        float& sp = smoothPow_[k];
        sp = 0.7f * sp + 0.3f * p;
        float& nz = noise_[k];
        if (warmup) {
            nz = framesSeen_ == 0 ? sp : nz + (sp - nz) / static_cast<float>(framesSeen_ + 1);
        } else if (sp < nz) {
            nz = 0.85f * nz + 0.15f * sp;
        } else {
            nz = std::min(nz * noiseUp, sp);
        }
        const float post = p / (nz * overSub + 1e-12f);
        const float prio = 0.98f * prevGain_[k] * prevGain_[k] * prevSnr_[k] + 0.02f * std::max(post - 1.0f, 0.0f);
        float g = prio / (1.0f + prio);
        if (g < gMin)
            g = gMin;
        gain_[k] = g;
        prevGain_[k] = g;
        prevSnr_[k] = std::min(post, 1e6f);
    }
    ++framesSeen_;

    // Light smoothing across frequency reduces "musical noise" artefacts.
    float prev = gain_[0];
    for (int k = 1; k < bins - 1; ++k) {
        const float cur = gain_[k];
        gain_[k] = 0.25f * prev + 0.5f * cur + 0.25f * gain_[k + 1];
        prev = cur;
    }
    if (warmup) // never gate during noise-floor initialisation
        for (int k = 0; k < bins; ++k)
            gain_[k] = 1.0f;

    for (int k = 0; k < bins; ++k) {
        specL_[k] *= gain_[k];
        if (k > 0 && k < n / 2)
            specL_[n - k] *= gain_[k];
    }
    fft_.inverse(specL_.data());

    // Overlap-add: drop the hop already emitted, append new frame.
    std::memmove(outL_.data(), outL_.data() + hop_, sizeof(float) * (n - hop_));
    std::memmove(outR_.data(), outR_.data() + hop_, sizeof(float) * (n - hop_));
    std::fill(outL_.begin() + (n - hop_), outL_.end(), 0.0f);
    std::fill(outR_.begin() + (n - hop_), outR_.end(), 0.0f);
    for (int i = 0; i < n; ++i) {
        outL_[i] += specL_[i].real() * window_[i];
        outR_[i] += specL_[i].imag() * window_[i];
    }
    // Slide the analysis window.
    std::memmove(inL_.data(), inL_.data() + hop_, sizeof(float) * (n - hop_));
    std::memmove(inR_.data(), inR_.data() + hop_, sizeof(float) * (n - hop_));
}

// ---------------------------------------------------------------------------------
void NoiseGate::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    auto coef = [sampleRate](double ms) {
        return static_cast<float>(std::exp(-1.0 / std::max(1.0, sampleRate * ms / 1000.0)));
    };
    envAttack_ = coef(1.0);
    envRelease_ = coef(60.0);
    gainAttack_ = coef(2.0);
    gainRelease_ = coef(80.0);
    holdSamples_ = static_cast<int>(sampleRate * 0.12);
    reset();
}

void NoiseGate::reset()
{
    env_ = 0.0f;
    gain_ = 1.0f;
    open_ = true;
    holdLeft_ = holdSamples_;
}

void NoiseGate::processStereo(float* stereo, std::size_t frames)
{
    const float openThr = dbToLinear(thresholdDb_);
    const float closeThr = openThr * 0.7079f; // 3 dB hysteresis
    const float floorGain = 0.01f;            // -40 dB when closed
    for (std::size_t f = 0; f < frames; ++f) {
        const float a = std::max(std::fabs(stereo[2 * f]), std::fabs(stereo[2 * f + 1]));
        const float c = a > env_ ? envAttack_ : envRelease_;
        env_ = c * env_ + (1.0f - c) * a;
        if (env_ > openThr) {
            open_ = true;
            holdLeft_ = holdSamples_;
        } else if (env_ < closeThr) {
            if (holdLeft_ > 0)
                --holdLeft_;
            else
                open_ = false;
        }
        const float target = open_ ? 1.0f : floorGain;
        const float gc = target > gain_ ? gainAttack_ : gainRelease_;
        gain_ = gc * gain_ + (1.0f - gc) * target;
        stereo[2 * f] *= gain_;
        stereo[2 * f + 1] *= gain_;
    }
}

// ---------------------------------------------------------------------------------
void Limiter::prepare(double sampleRate)
{
    release_ = static_cast<float>(std::exp(-1.0 / (sampleRate * 0.08)));
    tripSamples_ = sampleRate * 1.5;
    reset();
}

void Limiter::reset()
{
    env_ = 0.0f;
    minGain_ = 1.0f;
    hotSamples_ = 0.0;
}

float Limiter::gainReductionDb() const
{
    return -linearToDb(minGain_);
}

bool Limiter::processStereo(float* stereo, std::size_t frames)
{
    bool tripped = false;
    float minGain = 1.0f;
    const float hot = ceiling_ * 2.0f; // 6 dB above the ceiling
    for (std::size_t f = 0; f < frames; ++f) {
        float& l = stereo[2 * f];
        float& r = stereo[2 * f + 1];
        if (!(l == l)) l = 0.0f; // NaN guard
        if (!(r == r)) r = 0.0f;
        const float peak = std::max(std::fabs(l), std::fabs(r));
        env_ = std::max(peak, env_ * release_);
        if (feedbackGuard_) {
            if (env_ > hot)
                hotSamples_ += 1.0;
            else if (hotSamples_ > 0.0)
                hotSamples_ = std::max(0.0, hotSamples_ - 4.0);
            if (hotSamples_ > tripSamples_) {
                tripped = true;
                hotSamples_ = 0.0;
            }
        }
        if (enabled_) {
            if (env_ > ceiling_) {
                const float g = ceiling_ / env_;
                l *= g;
                r *= g;
                if (g < minGain)
                    minGain = g;
            }
        } else {
            l = l > 1.0f ? 1.0f : (l < -1.0f ? -1.0f : l);
            r = r > 1.0f ? 1.0f : (r < -1.0f ? -1.0f : r);
        }
    }
    minGain_ = minGain;
    return tripped;
}

// ---------------------------------------------------------------------------------
void LevelMeter::update(const float* stereo, std::size_t frames, double sampleRate)
{
    if (frames == 0)
        return;
    float pk = 0.0f;
    double sum = 0.0;
    for (std::size_t i = 0; i < frames * 2; ++i) {
        const float a = std::fabs(stereo[i]);
        if (a > pk)
            pk = a;
        sum += static_cast<double>(stereo[i]) * stereo[i];
    }
    const float blockRms = static_cast<float>(std::sqrt(sum / static_cast<double>(frames * 2)));
    const float coef = static_cast<float>(std::exp(-static_cast<double>(frames) / (0.3 * sampleRate)));
    rmsState_ = coef * rmsState_ + (1.0f - coef) * blockRms;
    if (rmsState_ < 1e-9f)
        rmsState_ = 0.0f;
    rms.store(rmsState_, std::memory_order_relaxed);
    if (pk > peak.load(std::memory_order_relaxed))
        peak.store(pk, std::memory_order_relaxed);
    if (pk >= 0.999f)
        clipped.store(true, std::memory_order_relaxed);
}

} // namespace ab
