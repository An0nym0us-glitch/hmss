// Sample-format and channel-layout conversion between device formats and the
// internal stereo float32 bus. All functions are allocation free (real-time safe).
#pragma once

#include "AudioTypes.h"

namespace ab {

// Windows SPEAKER_* channel mask bits (ksmedia.h) - duplicated so the core stays portable.
constexpr std::uint32_t kSpeakerFrontLeft = 0x1;
constexpr std::uint32_t kSpeakerFrontRight = 0x2;
constexpr std::uint32_t kSpeakerFrontCenter = 0x4;
constexpr std::uint32_t kSpeakerLowFrequency = 0x8;
constexpr std::uint32_t kSpeakerBackLeft = 0x10;
constexpr std::uint32_t kSpeakerBackRight = 0x20;
constexpr std::uint32_t kSpeakerSideLeft = 0x200;
constexpr std::uint32_t kSpeakerSideRight = 0x400;

// Converts `frames` frames of raw device samples into interleaved float32 [-1,1].
void deviceToFloat(const void* src, SampleType type, int channels, std::size_t frames, float* dst);

// Converts interleaved float32 into raw device samples (with clamping to full scale).
void floatToDevice(const float* src, SampleType type, int channels, std::size_t frames, void* dst);

// Maps an interleaved device buffer (any channel count) to interleaved stereo.
// monoMix = true sums all channels into a centred mono signal (typical for microphones).
void channelsToStereo(const float* src, int channels, std::uint32_t channelMask, std::size_t frames, bool monoMix,
                      float* stereoDst);

// Maps interleaved stereo onto a device channel layout. Mono devices receive (L+R)/2,
// multi-channel devices receive L/R on the front pair and silence elsewhere.
void stereoToChannels(const float* stereoSrc, std::size_t frames, int channels, std::uint32_t channelMask, float* dst);

} // namespace ab
