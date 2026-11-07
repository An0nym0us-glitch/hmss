#include "AudioInputProcessor.h"

#include "SampleConvert.h"

#include <algorithm>
#include <cstring>

namespace ab {

void AudioInputProcessor::prepare(const StreamFormat& deviceFormat)
{
    AudioGraph* graph = graph_;
    const int slot = slot_;
    format_ = deviceFormat;
    engineRate_ = graph->engineRate.load();

    resampler_.configure(kEngineChannels, format_.sampleRate, engineRate_, kChunk);
    hpf_.prepare(engineRate_, 80.0);
    ns_.prepare(engineRate_);
    gate_.prepare(engineRate_);
    gain_.prepare(engineRate_, 20.0);
    fader_.prepare(engineRate_, 20.0);

    const InputParams& p = graph->inputs[slot].params;
    gain_.setImmediate(dbToLinear(p.gainDb.load()));
    fader_.setImmediate(0.0f); // fade in on start: no click
    hpfOn_ = nsOn_ = gateOn_ = false;

    devBuf_.assign(kChunk * static_cast<std::size_t>(std::max(1, format_.channels)), 0.0f);
    stereoBuf_.assign(kChunk * kEngineChannels, 0.0f);
    resampled_.assign(resampler_.maxOutputFor(kChunk) * kEngineChannels + 64, 0.0f);
    graph->ensureInputRing(slot);
}

void AudioInputProcessor::reportDiscontinuity()
{
    graph_->inputs[slot_].glitches.fetch_add(1, std::memory_order_relaxed);
}

void AudioInputProcessor::process(const void* data, std::size_t frames, bool silent)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    const std::size_t bpf = static_cast<std::size_t>(format_.bytesPerFrame());
    while (frames > 0) {
        const std::size_t n = std::min(frames, kChunk);
        processChunk(bytes, n, silent || bytes == nullptr);
        if (bytes)
            bytes += n * bpf;
        frames -= n;
    }
}

void AudioInputProcessor::processChunk(const std::uint8_t* data, std::size_t frames, bool silent)
{
    InputSlot& slot = graph_->inputs[slot_];
    const InputParams& p = slot.params;
    const bool mono = p.monoMix.load(std::memory_order_relaxed);

    // 1) Device format -> stereo float at device rate.
    if (silent) {
        std::fill(stereoBuf_.begin(), stereoBuf_.begin() + static_cast<std::ptrdiff_t>(frames * 2), 0.0f);
    } else {
        deviceToFloat(data, format_.sampleType, format_.channels, frames, devBuf_.data());
        channelsToStereo(devBuf_.data(), format_.channels, format_.channelMask, frames, mono, stereoBuf_.data());
    }

    // 2) Sample-rate conversion to the engine rate (bypassed when rates match).
    float* work = stereoBuf_.data();
    std::size_t n = frames;
    if (!resampler_.isIdentity()) {
        n = resampler_.processPush(stereoBuf_.data(), frames, resampled_.data(),
                                   resampled_.size() / kEngineChannels);
        work = resampled_.data();
    }
    if (n == 0)
        return;

    // 3) Channel strip.
    gain_.setTarget(dbToLinear(p.gainDb.load(std::memory_order_relaxed)));
    gain_.applyStereo(work, n);

    const bool hpf = p.highPass.load(std::memory_order_relaxed);
    if (hpf != hpfOn_) { hpf_.reset(); hpfOn_ = hpf; }
    if (hpf)
        hpf_.processStereo(work, n);

    const bool ns = p.noiseSuppression.load(std::memory_order_relaxed);
    if (ns != nsOn_) { ns_.reset(); nsOn_ = ns; }
    if (ns) {
        ns_.setStrength(p.noiseSuppressionStrength.load(std::memory_order_relaxed));
        ns_.processStereo(work, n, mono || format_.channels == 1);
    }

    const bool gate = p.noiseGate.load(std::memory_order_relaxed);
    if (gate != gateOn_) { gate_.reset(); gateOn_ = gate; }
    if (gate) {
        gate_.setThresholdDb(p.gateThresholdDb.load(std::memory_order_relaxed));
        gate_.processStereo(work, n);
    }

    const bool paused = graph_->paused.load(std::memory_order_relaxed);
    slot.meter.update(work, paused ? 0 : n, engineRate_);

    const bool soloActive = graph_->soloCount.load(std::memory_order_relaxed) > 0;
    const bool audible = p.enabled.load(std::memory_order_relaxed) && !p.mute.load(std::memory_order_relaxed) &&
                         !paused && (!soloActive || p.solo.load(std::memory_order_relaxed));
    fader_.setTarget(audible ? p.volume.load(std::memory_order_relaxed) : 0.0f);
    fader_.applyStereo(work, n);

    // 4) Publish to all routes.
    slot.ring.write(work, n);
    slot.framesCaptured.fetch_add(n, std::memory_order_relaxed);
    if (n > slot.maxBurstFrames.load(std::memory_order_relaxed))
        slot.maxBurstFrames.store(static_cast<std::uint32_t>(n), std::memory_order_relaxed);
}

} // namespace ab
