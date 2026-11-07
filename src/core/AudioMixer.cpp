#include "AudioMixer.h"

#include "SampleConvert.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ab {

void AudioOutputMixer::prepare(const StreamFormat& deviceFormat)
{
    format_ = deviceFormat;
    engineRate_ = graph_->engineRate.load();
    nominalRatio_ = engineRate_ / static_cast<double>(format_.sampleRate);

    for (auto& r : readers_) {
        r.resampler.configure(kEngineChannels, engineRate_, format_.sampleRate, kChunk);
        r.drift.configure();
        r.drift.reset();
        r.gain.prepare(format_.sampleRate, 20.0);
        r.gain.setImmediate(0.0f);
        r.state = RouteState::Idle;
        r.adaptiveMarginFrames = 0.0;
        r.cleanCallbacks = 0;
    }
    outGain_.prepare(format_.sampleRate, 20.0);
    outGain_.setImmediate(0.0f);
    limiter_.prepare(format_.sampleRate);
    maxBlockSeen_ = 0;

    const std::size_t maxIn = static_cast<std::size_t>(std::ceil(kChunk * nominalRatio_ * 1.02)) + 128;
    mix_.assign(kChunk * kEngineChannels, 0.0f);
    routeIn_.assign(maxIn * kEngineChannels, 0.0f);
    routeOut_.assign(kChunk * kEngineChannels, 0.0f);
    devBuf_.assign(kChunk * static_cast<std::size_t>(std::max(1, format_.channels)), 0.0f);
}

void AudioOutputMixer::render(void* dst, std::size_t frames)
{
    auto* bytes = static_cast<std::uint8_t*>(dst);
    const std::size_t bpf = static_cast<std::size_t>(format_.bytesPerFrame());
    while (frames > 0) {
        const std::size_t n = std::min(frames, kChunk);
        renderChunk(bytes, n);
        bytes += n * bpf;
        frames -= n;
    }
}

void AudioOutputMixer::renderSilence(void* dst, std::size_t frames)
{
    std::memset(dst, 0, frames * static_cast<std::size_t>(format_.bytesPerFrame()));
}

double AudioOutputMixer::targetFillFrames(const RouteReader& r, const InputSlot& in) const
{
    const double user = static_cast<double>(graph_->targetLatencyFrames.load(std::memory_order_relaxed));
    // Never target less than what the two device clocks can physically deliver:
    // one capture burst + one render block + a small safety margin.
    const double floor = static_cast<double>(in.maxBurstFrames.load(std::memory_order_relaxed)) +
                         static_cast<double>(maxBlockSeen_) * nominalRatio_ + 0.002 * engineRate_;
    const double delay = static_cast<double>(graph_->outputs[slot_].params.extraDelayMs.load(std::memory_order_relaxed)) *
                         engineRate_ / 1000.0;
    double t = std::max(user, floor) + r.adaptiveMarginFrames + std::max(0.0, delay);
    const double cap = static_cast<double>(in.ring.maxReadableBacklog()) * 0.75;
    return std::min(t, cap);
}

void AudioOutputMixer::mixRoute(int i, std::size_t frames)
{
    RouteCell& cell = graph_->routes[i][slot_];
    InputSlot& in = graph_->inputs[i];
    OutputSlot& out = graph_->outputs[slot_];
    RouteReader& r = readers_[i];

    if (!cell.enabled.load(std::memory_order_acquire) || !in.active.load(std::memory_order_acquire) ||
        !in.ring.isAllocated()) {
        if (r.state != RouteState::Idle) {
            r.state = RouteState::Idle;
            cell.meter.clear();
        }
        return;
    }

    const std::uint32_t gen = in.generation.load(std::memory_order_acquire);
    const std::uint64_t w = in.ring.writeIndex();
    if (r.state == RouteState::Idle || r.generation != gen) {
        if (r.generation != gen)
            r.adaptiveMarginFrames = 0.0;
        r.state = RouteState::Priming;
        r.generation = gen;
        r.readIndex = w;
        r.drift.reset();
        r.gain.setImmediate(0.0f);
    }
    if (r.readIndex > w) // cannot happen unless the ring was re-created; resync
        r.readIndex = w;

    const double fill = static_cast<double>(w - r.readIndex);
    const double target = targetFillFrames(r, in);
    const double dt = static_cast<double>(frames) / format_.sampleRate;

    if (r.state == RouteState::Priming) {
        if (fill < target)
            return; // still buffering: route contributes silence
        r.readIndex = w - static_cast<std::uint64_t>(target);
        r.resampler.reset();
        r.drift.softReset();
        r.state = RouteState::Running;
    } else {
        const double maxExcess = std::max(target, 0.12 * engineRate_);
        if (fill > target + maxExcess || fill > static_cast<double>(in.ring.maxReadableBacklog())) {
            // Far too much audio buffered (device stalled and then delivered a backlog, or
            // the user lowered the latency): jump back to the target instead of letting
            // the latency grow.
            r.readIndex = w - static_cast<std::uint64_t>(target);
            r.drift.softReset();
            out.resyncs.fetch_add(1, std::memory_order_relaxed);
        }
    }

    const double fillNow = static_cast<double>(w - r.readIndex);
    const double factor = r.drift.update((fillNow - target) / engineRate_, dt);
    r.resampler.setRatio(nominalRatio_ * factor);

    const std::size_t need = r.resampler.requiredInput(frames);
    if (static_cast<double>(need) > fillNow || need * kEngineChannels > routeIn_.size()) {
        // Starved: the input delivered less than the output consumed (device hiccup,
        // Bluetooth packet loss, profile switch ...). Re-buffer with a larger margin.
        r.state = RouteState::Priming;
        r.adaptiveMarginFrames = std::min(r.adaptiveMarginFrames + 0.005 * engineRate_, 0.25 * engineRate_);
        r.cleanCallbacks = 0;
        out.underruns.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!in.ring.read(r.readIndex, routeIn_.data(), need)) {
        r.state = RouteState::Priming;
        return;
    }
    r.readIndex += need;
    r.resampler.processPull(routeIn_.data(), need, routeOut_.data(), frames);

    r.gain.setTarget(cell.gain.load(std::memory_order_relaxed));
    r.gain.applyStereo(routeOut_.data(), frames);
    cell.meter.update(routeOut_.data(), frames, format_.sampleRate);

    float* mix = mix_.data();
    const float* src = routeOut_.data();
    for (std::size_t k = 0; k < frames * kEngineChannels; ++k)
        mix[k] += src[k];

    // Slowly give back adaptive margin after long clean periods (1 ms per 20 s).
    if (++r.cleanCallbacks * frames > static_cast<std::uint64_t>(20.0 * format_.sampleRate) &&
        r.adaptiveMarginFrames > 0.0) {
        r.adaptiveMarginFrames = std::max(0.0, r.adaptiveMarginFrames - 0.001 * engineRate_);
        r.cleanCallbacks = 0;
    }
}

void AudioOutputMixer::renderChunk(std::uint8_t* dst, std::size_t frames)
{
    OutputSlot& out = graph_->outputs[slot_];
    const OutputParams& p = out.params;
    if (frames > maxBlockSeen_)
        maxBlockSeen_ = frames;

    std::fill(mix_.begin(), mix_.begin() + static_cast<std::ptrdiff_t>(frames * kEngineChannels), 0.0f);
    float latency = 0.0f;
    bool driftReported = false;
    for (int i = 0; i < kMaxInputs; ++i) {
        mixRoute(i, frames);
        const RouteReader& r = readers_[i];
        if (r.state == RouteState::Running) {
            const float ms = static_cast<float>(targetFillFrames(r, graph_->inputs[i]) * 1000.0 / engineRate_);
            latency = std::max(latency, ms);
            if (!driftReported) {
                out.driftPpm.store(static_cast<float>(r.drift.lastCorrectionPpm()), std::memory_order_relaxed);
                driftReported = true;
            }
        }
    }
    out.routeLatencyMs.store(latency, std::memory_order_relaxed);

    const bool tripped = out.feedbackTripped.load(std::memory_order_relaxed);
    const bool audible = p.enabled.load(std::memory_order_relaxed) && !p.mute.load(std::memory_order_relaxed) &&
                         !graph_->globalMute.load(std::memory_order_relaxed) &&
                         !graph_->paused.load(std::memory_order_relaxed) && !tripped;
    outGain_.setTarget(audible ? p.volume.load(std::memory_order_relaxed) *
                                     graph_->masterVolume.load(std::memory_order_relaxed)
                               : 0.0f);
    outGain_.applyStereo(mix_.data(), frames);

    limiter_.setEnabled(graph_->limiterEnabled.load(std::memory_order_relaxed));
    limiter_.setFeedbackGuard(graph_->feedbackGuard.load(std::memory_order_relaxed));
    if (limiter_.processStereo(mix_.data(), frames))
        out.feedbackTripped.store(true, std::memory_order_relaxed);
    out.limiterReductionDb.store(limiter_.gainReductionDb(), std::memory_order_relaxed);
    out.meter.update(mix_.data(), frames, format_.sampleRate);

    stereoToChannels(mix_.data(), frames, format_.channels, format_.channelMask, devBuf_.data());
    floatToDevice(devBuf_.data(), format_.sampleType, format_.channels, frames, dst);
}

} // namespace ab
