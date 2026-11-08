// Signal analysis helpers for tests.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace abtest {

// Amplitude of a sinusoid at `hz` in `x` (Hann-windowed single-bin DFT).
inline double toneAmplitude(const std::vector<float>& x, double hz, double rate, std::size_t start = 0,
                            std::size_t count = 0)
{
    if (count == 0 || start + count > x.size())
        count = x.size() > start ? x.size() - start : 0;
    if (count < 16)
        return 0.0;
    const double pi = 3.14159265358979323846;
    double re = 0.0, im = 0.0, wsum = 0.0;
    const double w0 = 2.0 * pi * hz / rate;
    for (std::size_t i = 0; i < count; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i) / static_cast<double>(count - 1));
        const double v = x[start + i] * w;
        re += v * std::cos(w0 * static_cast<double>(i));
        im -= v * std::sin(w0 * static_cast<double>(i));
        wsum += w;
    }
    return 2.0 * std::sqrt(re * re + im * im) / wsum;
}

inline double rms(const std::vector<float>& x, std::size_t start = 0, std::size_t count = 0)
{
    if (count == 0 || start + count > x.size())
        count = x.size() > start ? x.size() - start : 0;
    if (count == 0)
        return 0.0;
    double s = 0.0;
    for (std::size_t i = 0; i < count; ++i)
        s += static_cast<double>(x[start + i]) * x[start + i];
    return std::sqrt(s / static_cast<double>(count));
}

inline double peak(const std::vector<float>& x)
{
    double p = 0.0;
    for (float v : x)
        p = std::max(p, static_cast<double>(std::fabs(v)));
    return p;
}

inline double toDb(double v) { return v <= 1e-12 ? -240.0 : 20.0 * std::log10(v); }

// Longest run (in samples) of near-silence inside x - detects dropouts in a
// continuous tone.
inline std::size_t longestSilentRun(const std::vector<float>& x, float threshold)
{
    std::size_t best = 0, cur = 0;
    for (float v : x) {
        if (std::fabs(v) < threshold)
            ++cur;
        else
            cur = 0;
        best = std::max(best, cur);
    }
    return best;
}

} // namespace abtest
