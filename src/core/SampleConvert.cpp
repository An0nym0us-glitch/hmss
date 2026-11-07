#include "SampleConvert.h"

#include <algorithm>
#include <cstring>

namespace ab {

namespace {

inline float clamp1(float v) { return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v); }

// Returns the interleaved index of the speaker with bit `bit`, or -1.
int channelIndexForMask(std::uint32_t mask, std::uint32_t bit)
{
    if (!(mask & bit))
        return -1;
    int idx = 0;
    for (std::uint32_t b = 1; b < bit; b <<= 1)
        if (mask & b)
            ++idx;
    return idx;
}

void frontPair(int channels, std::uint32_t mask, int& left, int& right)
{
    left = 0;
    right = channels > 1 ? 1 : 0;
    if (mask != 0 && channels > 2) {
        const int l = channelIndexForMask(mask, kSpeakerFrontLeft);
        const int r = channelIndexForMask(mask, kSpeakerFrontRight);
        if (l >= 0 && l < channels)
            left = l;
        if (r >= 0 && r < channels)
            right = r;
    }
}

} // namespace

void deviceToFloat(const void* src, SampleType type, int channels, std::size_t frames, float* dst)
{
    const std::size_t n = frames * static_cast<std::size_t>(channels);
    switch (type) {
    case SampleType::Float32:
        std::memcpy(dst, src, n * sizeof(float));
        // Shared-mode float streams may legitimately exceed +-1 slightly; keep them finite.
        for (std::size_t i = 0; i < n; ++i)
            if (!(dst[i] == dst[i]))
                dst[i] = 0.0f; // NaN guard
        break;
    case SampleType::Int16: {
        const auto* s = static_cast<const std::int16_t*>(src);
        for (std::size_t i = 0; i < n; ++i)
            dst[i] = static_cast<float>(s[i]) * (1.0f / 32768.0f);
        break;
    }
    case SampleType::Int24Packed: {
        const auto* s = static_cast<const std::uint8_t*>(src);
        for (std::size_t i = 0; i < n; ++i) {
            std::int32_t v = static_cast<std::int32_t>(static_cast<std::uint32_t>(s[3 * i]) << 8 |
                                                       static_cast<std::uint32_t>(s[3 * i + 1]) << 16 |
                                                       static_cast<std::uint32_t>(s[3 * i + 2]) << 24);
            dst[i] = static_cast<float>(v >> 8) * (1.0f / 8388608.0f);
        }
        break;
    }
    case SampleType::Int24In32:
    case SampleType::Int32: {
        // 24-in-32 is left aligned (valid bits in the MSBs), so both convert identically.
        const auto* s = static_cast<const std::int32_t*>(src);
        for (std::size_t i = 0; i < n; ++i)
            dst[i] = static_cast<float>(static_cast<double>(s[i]) * (1.0 / 2147483648.0));
        break;
    }
    default:
        std::fill(dst, dst + n, 0.0f);
        break;
    }
}

void floatToDevice(const float* src, SampleType type, int channels, std::size_t frames, void* dst)
{
    const std::size_t n = frames * static_cast<std::size_t>(channels);
    switch (type) {
    case SampleType::Float32: {
        auto* d = static_cast<float*>(dst);
        for (std::size_t i = 0; i < n; ++i)
            d[i] = clamp1(src[i]);
        break;
    }
    case SampleType::Int16: {
        auto* d = static_cast<std::int16_t*>(dst);
        for (std::size_t i = 0; i < n; ++i)
            d[i] = static_cast<std::int16_t>(std::lrintf(clamp1(src[i]) * 32767.0f));
        break;
    }
    case SampleType::Int24Packed: {
        auto* d = static_cast<std::uint8_t*>(dst);
        for (std::size_t i = 0; i < n; ++i) {
            const std::int32_t v = static_cast<std::int32_t>(std::lrintf(clamp1(src[i]) * 8388607.0f));
            d[3 * i] = static_cast<std::uint8_t>(v & 0xFF);
            d[3 * i + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
            d[3 * i + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
        }
        break;
    }
    case SampleType::Int24In32: {
        auto* d = static_cast<std::int32_t*>(dst);
        for (std::size_t i = 0; i < n; ++i)
            d[i] = static_cast<std::int32_t>(std::lrintf(clamp1(src[i]) * 8388607.0f)) * 256;
        break;
    }
    case SampleType::Int32: {
        auto* d = static_cast<std::int32_t*>(dst);
        for (std::size_t i = 0; i < n; ++i)
            d[i] = static_cast<std::int32_t>(std::llrint(static_cast<double>(clamp1(src[i])) * 2147483647.0));
        break;
    }
    default:
        std::memset(dst, 0, n * 4);
        break;
    }
}

void channelsToStereo(const float* src, int channels, std::uint32_t channelMask, std::size_t frames, bool monoMix,
                      float* stereoDst)
{
    if (channels <= 0) {
        std::fill(stereoDst, stereoDst + frames * 2, 0.0f);
        return;
    }
    if (channels == 1) {
        for (std::size_t f = 0; f < frames; ++f)
            stereoDst[2 * f] = stereoDst[2 * f + 1] = src[f];
        return;
    }
    if (monoMix) {
        // Average of all capture channels (e.g. the capsules of a microphone array).
        // LFE is excluded when the layout is known.
        const int lfe = channelMask ? channelIndexForMask(channelMask, kSpeakerLowFrequency) : -1;
        const int used = channels - (lfe >= 0 ? 1 : 0);
        const float scale = 1.0f / static_cast<float>(used > 0 ? used : 1);
        for (std::size_t f = 0; f < frames; ++f) {
            float sum = 0.0f;
            const float* fr = src + f * channels;
            for (int c = 0; c < channels; ++c)
                if (c != lfe)
                    sum += fr[c];
            stereoDst[2 * f] = stereoDst[2 * f + 1] = sum * scale;
        }
        return;
    }
    int l, r;
    frontPair(channels, channelMask, l, r);
    if (channels == 2 || channelMask == 0) {
        for (std::size_t f = 0; f < frames; ++f) {
            stereoDst[2 * f] = src[f * channels + l];
            stereoDst[2 * f + 1] = src[f * channels + r];
        }
        return;
    }
    // Multi-channel with known layout: ITU style downmix of centre and surrounds at -3 dB.
    const int c = channelIndexForMask(channelMask, kSpeakerFrontCenter);
    const int bl = channelIndexForMask(channelMask, kSpeakerBackLeft);
    const int br = channelIndexForMask(channelMask, kSpeakerBackRight);
    const int sl = channelIndexForMask(channelMask, kSpeakerSideLeft);
    const int sr = channelIndexForMask(channelMask, kSpeakerSideRight);
    const float k = 0.7071f;
    for (std::size_t f = 0; f < frames; ++f) {
        const float* fr = src + f * channels;
        float L = fr[l], R = fr[r];
        if (c >= 0) { L += k * fr[c]; R += k * fr[c]; }
        if (bl >= 0) L += k * fr[bl];
        if (br >= 0) R += k * fr[br];
        if (sl >= 0) L += k * fr[sl];
        if (sr >= 0) R += k * fr[sr];
        stereoDst[2 * f] = L * 0.5f;
        stereoDst[2 * f + 1] = R * 0.5f;
    }
}

void stereoToChannels(const float* stereoSrc, std::size_t frames, int channels, std::uint32_t channelMask, float* dst)
{
    if (channels == 2) {
        std::memcpy(dst, stereoSrc, frames * 2 * sizeof(float));
        return;
    }
    if (channels == 1) {
        for (std::size_t f = 0; f < frames; ++f)
            dst[f] = 0.5f * (stereoSrc[2 * f] + stereoSrc[2 * f + 1]);
        return;
    }
    int l, r;
    frontPair(channels, channelMask, l, r);
    std::fill(dst, dst + frames * static_cast<std::size_t>(channels), 0.0f);
    for (std::size_t f = 0; f < frames; ++f) {
        dst[f * channels + l] = stereoSrc[2 * f];
        dst[f * channels + r] = stereoSrc[2 * f + 1];
    }
}

} // namespace ab
