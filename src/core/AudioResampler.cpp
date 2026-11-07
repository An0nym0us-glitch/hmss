#include "AudioResampler.h"
#include "AudioTypes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ab {

namespace {

double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    const double q = x * x / 4.0;
    for (int k = 1; k < 64; ++k) {
        term *= q / (static_cast<double>(k) * k);
        sum += term;
        if (term < 1e-12 * sum)
            break;
    }
    return sum;
}

double sinc(double x)
{
    if (std::fabs(x) < 1e-12)
        return 1.0;
    const double px = kPi * x;
    return std::sin(px) / px;
}

} // namespace

void AudioResampler::configure(int channels, double inputRate, double outputRate, std::size_t maxBlockFrames,
                               int halfTaps, int phases)
{
    channels_ = std::max(1, channels);
    halfTaps_ = std::max(2, halfTaps);
    phases_ = std::max(16, phases);
    nominalRatio_ = inputRate / outputRate;
    ratio_ = nominalRatio_;
    identity_ = std::fabs(nominalRatio_ - 1.0) < 1e-12;

    // Low-pass cut-off relative to the input Nyquist frequency: when down-sampling
    // the kernel must band-limit to the output Nyquist to avoid aliasing.
    const double cutoff = std::min(1.0, 1.0 / nominalRatio_) * 0.91;
    const double beta = 7.0; // Kaiser window, ~70 dB stop-band attenuation
    const double i0beta = besselI0(beta);
    const int taps = 2 * halfTaps_;
    table_.assign(static_cast<std::size_t>(phases_ + 1) * taps, 0.0f);
    for (int ph = 0; ph <= phases_; ++ph) {
        const double frac = static_cast<double>(ph) / phases_;
        double sum = 0.0;
        double tmp[256];
        for (int j = 0; j < taps && j < 256; ++j) {
            const double t = static_cast<double>(j - halfTaps_ + 1) - frac; // distance from output position
            const double w = std::fabs(t) / halfTaps_;
            double win = 0.0;
            if (w < 1.0)
                win = besselI0(beta * std::sqrt(1.0 - w * w)) / i0beta;
            tmp[j] = cutoff * sinc(cutoff * t) * win;
            sum += tmp[j];
        }
        // Normalise every phase to unity DC gain (removes tiny periodic gain ripple).
        for (int j = 0; j < taps; ++j)
            table_[static_cast<std::size_t>(ph) * taps + j] = static_cast<float>(tmp[j] / sum);
    }

    const double maxRatio = std::max(1.0, nominalRatio_ * 1.05);
    bufCapacity_ = static_cast<std::size_t>(std::ceil(static_cast<double>(maxBlockFrames) * maxRatio)) +
                   maxBlockFrames + 4 * static_cast<std::size_t>(halfTaps_) + 64;
    buf_.assign(bufCapacity_ * channels_, 0.0f);
    reset();
}

void AudioResampler::setRatio(double inputPerOutput)
{
    ratio_ = inputPerOutput;
    identity_ = std::fabs(ratio_ - 1.0) < 1e-12 && std::fabs(nominalRatio_ - 1.0) < 1e-12;
}

void AudioResampler::reset()
{
    std::fill(buf_.begin(), buf_.end(), 0.0f);
    bufFrames_ = static_cast<std::size_t>(halfTaps_);
    pos_ = static_cast<double>(halfTaps_);
}

std::size_t AudioResampler::requiredInput(std::size_t outFrames) const
{
    if (outFrames == 0)
        return 0;
    const double last = pos_ + static_cast<double>(outFrames - 1) * ratio_;
    const long long needIndex = static_cast<long long>(std::floor(last)) + halfTaps_; // highest index read
    const long long need = needIndex + 1 - static_cast<long long>(bufFrames_);
    return need > 0 ? static_cast<std::size_t>(need) : 0;
}

std::size_t AudioResampler::maxOutputFor(std::size_t inFrames) const
{
    return static_cast<std::size_t>(std::ceil((static_cast<double>(inFrames) + 2.0 * halfTaps_ + 2.0) / ratio_)) + 2;
}

void AudioResampler::append(const float* in, std::size_t frames)
{
    if (bufFrames_ + frames > bufCapacity_)
        compact();
    if (bufFrames_ + frames > bufCapacity_) {
        // Contract violation (block larger than configured). Drop the oldest excess
        // instead of overflowing; this keeps the process real-time safe.
        const std::size_t excess = bufFrames_ + frames - bufCapacity_;
        if (excess >= frames)
            return;
        in += excess * channels_;
        frames -= excess;
    }
    std::memcpy(buf_.data() + bufFrames_ * channels_, in, frames * channels_ * sizeof(float));
    bufFrames_ += frames;
}

void AudioResampler::compact()
{
    const long long first = static_cast<long long>(std::floor(pos_)) - halfTaps_ + 1;
    if (first <= 0)
        return;
    const std::size_t drop = std::min<std::size_t>(static_cast<std::size_t>(first), bufFrames_);
    std::memmove(buf_.data(), buf_.data() + drop * channels_, (bufFrames_ - drop) * channels_ * sizeof(float));
    bufFrames_ -= drop;
    pos_ -= static_cast<double>(drop);
}

std::size_t AudioResampler::produce(float* out, std::size_t maxOut)
{
    const int taps = 2 * halfTaps_;
    const int ch = channels_;
    std::size_t produced = 0;
    for (; produced < maxOut; ++produced) {
        const double p = pos_ + static_cast<double>(produced) * ratio_;
        const double fl = std::floor(p);
        const long long i = static_cast<long long>(fl);
        if (i + halfTaps_ >= static_cast<long long>(bufFrames_))
            break;
        const double frac = p - fl;
        const double phF = frac * phases_;
        int ph = static_cast<int>(phF);
        if (ph >= phases_)
            ph = phases_ - 1;
        const float t = static_cast<float>(phF - ph);
        const float* k0 = table_.data() + static_cast<std::size_t>(ph) * taps;
        const float* k1 = k0 + taps;
        const float* x = buf_.data() + static_cast<std::size_t>(i - halfTaps_ + 1) * ch;
        float* o = out + produced * ch;
        if (ch == 2) {
            float accL = 0.0f, accR = 0.0f;
            for (int j = 0; j < taps; ++j) {
                const float w = k0[j] + t * (k1[j] - k0[j]);
                accL += w * x[2 * j];
                accR += w * x[2 * j + 1];
            }
            o[0] = accL;
            o[1] = accR;
        } else {
            for (int c = 0; c < ch; ++c) {
                float acc = 0.0f;
                for (int j = 0; j < taps; ++j)
                    acc += (k0[j] + t * (k1[j] - k0[j])) * x[j * ch + c];
                o[c] = acc;
            }
        }
    }
    pos_ += static_cast<double>(produced) * ratio_;
    compact();
    return produced;
}

void AudioResampler::processPull(const float* in, std::size_t inFrames, float* out, std::size_t outFrames)
{
    append(in, inFrames);
    const std::size_t got = produce(out, outFrames);
    if (got < outFrames) // only on contract violation: never output garbage
        std::memset(out + got * channels_, 0, (outFrames - got) * channels_ * sizeof(float));
}

std::size_t AudioResampler::processPush(const float* in, std::size_t inFrames, float* out, std::size_t maxOut)
{
    append(in, inFrames);
    return produce(out, maxOut);
}

} // namespace ab
