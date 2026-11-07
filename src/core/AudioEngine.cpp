#include "AudioEngine.h"

#include <algorithm>

namespace ab {

const char* toString(RunState s)
{
    switch (s) {
    case RunState::Off: return "Off";
    case RunState::Running: return "Streaming";
    case RunState::WaitingForDevice: return "Waiting for device";
    case RunState::Error: return "Error";
    }
    return "Unknown";
}

AudioEngine::AudioEngine(std::unique_ptr<IAudioBackend> backend) : backend_(std::move(backend))
{
    applyGlobalParams();
}

AudioEngine::~AudioEngine()
{
    backend_->setListener(nullptr);
    closeAll();
}

bool AudioEngine::initialize(std::string* error)
{
    if (initialized_)
        return true;
    if (!backend_->initialize(error))
        return false;
    backend_->setListener(this);
    initialized_ = true;
    refreshDevices();
    return true;
}

void AudioEngine::onDeviceListChanged()
{
    devicesDirty_.store(true);
    if (hook_)
        hook_();
}

// ---------------------------------------------------------------------------------
// Device table

void AudioEngine::refreshDevices()
{
    devicesDirty_.store(false);
    std::vector<DeviceInfo> list = backend_->enumerateDevices();
    disambiguateDisplayNames(list);

    for (auto& kv : inputs_)
        kv.second.present = false;
    for (auto& kv : outputs_)
        kv.second.present = false;

    for (const auto& d : list) {
        const bool present = d.state != DeviceState::NotPresent;
        if (d.flow == DeviceFlow::Capture) {
            auto& rt = inputs_[d.id];
            if (rt.cfg.id.empty())
                rt.cfg.id = d.id;
            rt.info = d;
            rt.present = present;
            if (present || rt.cfg.lastKnownName.empty())
                rt.cfg.lastKnownName = d.displayName;
        } else {
            auto& rt = outputs_[d.id];
            if (rt.cfg.id.empty())
                rt.cfg.id = d.id;
            rt.info = d;
            rt.present = present;
            if (present || rt.cfg.lastKnownName.empty())
                rt.cfg.lastKnownName = d.displayName;
        }
    }
    // Remembered devices the OS no longer reports at all.
    for (auto& kv : inputs_)
        if (!kv.second.present) {
            kv.second.info.state = DeviceState::NotPresent;
            if (kv.second.info.id.empty()) {
                kv.second.info.id = kv.first;
                kv.second.info.flow = DeviceFlow::Capture;
            }
            if (kv.second.info.displayName.empty())
                kv.second.info.displayName = kv.second.info.friendlyName = kv.second.cfg.lastKnownName;
        }
    for (auto& kv : outputs_)
        if (!kv.second.present) {
            kv.second.info.state = DeviceState::NotPresent;
            if (kv.second.info.id.empty()) {
                kv.second.info.id = kv.first;
                kv.second.info.flow = DeviceFlow::Render;
            }
            if (kv.second.info.displayName.empty())
                kv.second.info.displayName = kv.second.info.friendlyName = kv.second.cfg.lastKnownName;
        }

    ensureStreams();
    bump();
}

void AudioEngine::housekeeping()
{
    if (devicesDirty_.load())
        refreshDevices();
    else
        ensureStreams();
}

// ---------------------------------------------------------------------------------
// Monitoring

void AudioEngine::startMonitoring()
{
    if (monitoring_)
        return;
    monitoring_ = true;
    graph_.paused.store(false);
    for (auto& kv : inputs_) { kv.second.failures = 0; kv.second.nextRetry = {}; }
    for (auto& kv : outputs_) { kv.second.failures = 0; kv.second.nextRetry = {}; }
    // Clear any latched feedback trips from a previous session.
    for (auto& o : graph_.outputs)
        o.feedbackTripped.store(false);
    ensureStreams();
    bump();
}

void AudioEngine::stopMonitoring()
{
    if (!monitoring_)
        return;
    monitoring_ = false;
    closeAll();
    ensureStreams(); // updates states
    bump();
}

void AudioEngine::setPaused(bool paused)
{
    if (graph_.paused.exchange(paused) != paused)
        bump();
}

void AudioEngine::closeAll()
{
    // Outputs first: they read input rings, but rings are never freed, so the order
    // is only cosmetic (avoids a final partial block from a disappearing input).
    for (auto& kv : outputs_)
        closeOutput(kv.second);
    for (auto& kv : inputs_)
        closeInput(kv.second);
}

// ---------------------------------------------------------------------------------
// Settings

void AudioEngine::applyGlobalParams()
{
    graph_.masterVolume.store(settings_.masterVolume);
    graph_.globalMute.store(settings_.globalMute);
    graph_.limiterEnabled.store(settings_.limiter);
    graph_.feedbackGuard.store(settings_.feedbackGuard);
    graph_.targetLatencyFrames.store(std::max(1, settings_.bufferMs * graph_.engineRate.load() / 1000));
}

void AudioEngine::setSettings(const EngineSettings& s)
{
    EngineSettings n = s;
    n.sampleRate = std::max(8000, std::min(n.sampleRate, 192000));
    n.bufferMs = std::max(3, std::min(n.bufferMs, 500));
    n.masterVolume = std::max(0.0f, std::min(n.masterVolume, 2.0f));
    const bool needRestart =
        n.sampleRate != settings_.sampleRate || n.bufferMs != settings_.bufferMs || n.lowLatency != settings_.lowLatency;
    settings_ = n;
    if (needRestart) {
        const bool wasMonitoring = monitoring_;
        closeAll(); // the engine rate may only change while no stream runs
        graph_.engineRate.store(settings_.sampleRate);
        applyGlobalParams();
        if (wasMonitoring) {
            for (auto& kv : inputs_) kv.second.nextRetry = {};
            for (auto& kv : outputs_) kv.second.nextRetry = {};
        }
        ensureStreams();
    } else {
        applyGlobalParams();
    }
    bump();
}

void AudioEngine::setMasterVolume(float v)
{
    settings_.masterVolume = std::max(0.0f, std::min(v, 2.0f));
    graph_.masterVolume.store(settings_.masterVolume);
}

void AudioEngine::setGlobalMute(bool m)
{
    settings_.globalMute = m;
    graph_.globalMute.store(m);
    bump();
}

StreamOptions AudioEngine::streamOptions() const
{
    StreamOptions o;
    o.bufferMs = settings_.bufferMs;
    o.lowLatency = settings_.lowLatency;
    return o;
}

// ---------------------------------------------------------------------------------
// Slots and parameters

bool AudioEngine::assignInputSlot(InputRuntime& rt, std::string* error)
{
    if (rt.slot >= 0)
        return true;
    bool used[kMaxInputs] = {};
    for (const auto& kv : inputs_)
        if (kv.second.slot >= 0)
            used[kv.second.slot] = true;
    for (int i = 0; i < kMaxInputs; ++i)
        if (!used[i]) {
            rt.slot = i;
            graph_.ensureInputRing(i);
            graph_.inputs[i].meter.clear();
            graph_.inputs[i].glitches.store(0);
            graph_.inputs[i].maxBurstFrames.store(0);
            applyInputParams(rt);
            return true;
        }
    if (error)
        *error = "At most " + std::to_string(kMaxInputs) + " microphones can be enabled at the same time.";
    return false;
}

bool AudioEngine::assignOutputSlot(OutputRuntime& rt, std::string* error)
{
    if (rt.slot >= 0)
        return true;
    bool used[kMaxOutputs] = {};
    for (const auto& kv : outputs_)
        if (kv.second.slot >= 0)
            used[kv.second.slot] = true;
    for (int i = 0; i < kMaxOutputs; ++i)
        if (!used[i]) {
            rt.slot = i;
            OutputSlot& s = graph_.outputs[i];
            s.meter.clear();
            s.feedbackTripped.store(false);
            s.underruns.store(0);
            s.resyncs.store(0);
            applyOutputParams(rt);
            return true;
        }
    if (error)
        *error = "At most " + std::to_string(kMaxOutputs) + " outputs can be enabled at the same time.";
    return false;
}

void AudioEngine::releaseInputSlot(InputRuntime& rt)
{
    closeInput(rt);
    if (rt.slot >= 0) {
        graph_.inputs[rt.slot].active.store(false);
        graph_.inputs[rt.slot].meter.clear();
        for (int o = 0; o < kMaxOutputs; ++o)
            graph_.routes[rt.slot][o].enabled.store(false);
    }
    rt.slot = -1;
}

void AudioEngine::releaseOutputSlot(OutputRuntime& rt)
{
    closeOutput(rt);
    if (rt.slot >= 0) {
        graph_.outputs[rt.slot].meter.clear();
        for (int i = 0; i < kMaxInputs; ++i)
            graph_.routes[i][rt.slot].enabled.store(false);
    }
    rt.slot = -1;
}

void AudioEngine::applyInputParams(const InputRuntime& rt)
{
    if (rt.slot < 0)
        return;
    InputParams& p = graph_.inputs[rt.slot].params;
    const InputConfig& c = rt.cfg;
    p.enabled.store(c.enabled);
    p.gainDb.store(std::max(-24.0f, std::min(c.gainDb, 36.0f)));
    p.volume.store(std::max(0.0f, std::min(c.volume, 2.0f)));
    p.mute.store(c.mute);
    p.solo.store(c.solo);
    p.monoMix.store(c.monoMix);
    p.highPass.store(c.highPass);
    p.noiseSuppression.store(c.noiseSuppression);
    p.noiseSuppressionStrength.store(std::max(0.0f, std::min(c.noiseSuppressionStrength, 1.0f)));
    p.noiseGate.store(c.noiseGate);
    p.gateThresholdDb.store(std::max(-90.0f, std::min(c.gateThresholdDb, 0.0f)));
}

void AudioEngine::applyOutputParams(const OutputRuntime& rt)
{
    if (rt.slot < 0)
        return;
    OutputParams& p = graph_.outputs[rt.slot].params;
    p.enabled.store(rt.cfg.enabled);
    p.volume.store(std::max(0.0f, std::min(rt.cfg.volume, 2.0f)));
    p.mute.store(rt.cfg.mute);
    p.extraDelayMs.store(std::max(0.0f, std::min(rt.cfg.delayMs, 1000.0f)));
}

void AudioEngine::applySolo()
{
    int n = 0;
    for (const auto& kv : inputs_)
        if (kv.second.cfg.enabled && kv.second.cfg.solo)
            ++n;
    graph_.soloCount.store(n);
}

void AudioEngine::applyRoutes()
{
    bool want[kMaxInputs][kMaxOutputs] = {};
    float gain[kMaxInputs][kMaxOutputs] = {};
    for (const auto& r : matrix_.routes()) {
        const int i = inputSlot(r.inputId);
        const int o = outputSlot(r.outputId);
        if (i >= 0 && o >= 0) {
            want[i][o] = true;
            gain[i][o] = r.gain;
        }
    }
    for (int i = 0; i < kMaxInputs; ++i)
        for (int o = 0; o < kMaxOutputs; ++o) {
            RouteCell& c = graph_.routes[i][o];
            c.gain.store(want[i][o] ? gain[i][o] : 1.0f);
            c.enabled.store(want[i][o], std::memory_order_release);
        }
}

int AudioEngine::inputSlot(const std::string& id) const
{
    auto it = inputs_.find(id);
    return it == inputs_.end() ? -1 : it->second.slot;
}

int AudioEngine::outputSlot(const std::string& id) const
{
    auto it = outputs_.find(id);
    return it == outputs_.end() ? -1 : it->second.slot;
}

bool AudioEngine::isRouted(const std::string& id) const
{
    for (const auto& r : matrix_.routes())
        if (r.inputId == id || r.outputId == id)
            return true;
    return false;
}

// ---------------------------------------------------------------------------------
// Configuration API

bool AudioEngine::setInputConfig(const InputConfig& cfgIn, std::string* error)
{
    auto& rt = inputs_[cfgIn.id];
    if (rt.info.id.empty()) { // configured before ever being enumerated (loaded config)
        rt.info.id = cfgIn.id;
        rt.info.flow = DeviceFlow::Capture;
        rt.info.state = DeviceState::NotPresent;
        rt.info.displayName = rt.info.friendlyName = cfgIn.lastKnownName;
    }
    InputConfig cfg = cfgIn;
    if (cfg.lastKnownName.empty())
        cfg.lastKnownName = rt.cfg.lastKnownName;
    const bool wasEnabled = rt.cfg.enabled;
    const bool reopen = rt.stream && cfg.voiceProcessing != rt.cfg.voiceProcessing;
    rt.cfg = cfg;
    bool ok = true;
    if (cfg.enabled && !wasEnabled) {
        if (!assignInputSlot(rt, error)) {
            rt.cfg.enabled = false;
            ok = false;
        } else {
            rt.failures = 0;
            rt.nextRetry = {};
        }
    } else if (!cfg.enabled && wasEnabled) {
        releaseInputSlot(rt);
    }
    if (reopen) {
        closeInput(rt);
        rt.nextRetry = {};
    }
    applyInputParams(rt);
    applySolo();
    applyRoutes();
    ensureStreams();
    bump();
    return ok;
}

bool AudioEngine::setOutputConfig(const OutputConfig& cfgIn, std::string* error)
{
    auto& rt = outputs_[cfgIn.id];
    if (rt.info.id.empty()) {
        rt.info.id = cfgIn.id;
        rt.info.flow = DeviceFlow::Render;
        rt.info.state = DeviceState::NotPresent;
        rt.info.displayName = rt.info.friendlyName = cfgIn.lastKnownName;
    }
    OutputConfig cfg = cfgIn;
    if (cfg.lastKnownName.empty())
        cfg.lastKnownName = rt.cfg.lastKnownName;
    const bool wasEnabled = rt.cfg.enabled;
    const bool unmuted = rt.cfg.mute && !cfg.mute;
    rt.cfg = cfg;
    bool ok = true;
    if (cfg.enabled && !wasEnabled) {
        if (!assignOutputSlot(rt, error)) {
            rt.cfg.enabled = false;
            ok = false;
        } else {
            rt.failures = 0;
            rt.nextRetry = {};
        }
    } else if (!cfg.enabled && wasEnabled) {
        releaseOutputSlot(rt);
    }
    // Unmuting an output also acknowledges a feedback-guard trip.
    if (unmuted && rt.slot >= 0)
        graph_.outputs[rt.slot].feedbackTripped.store(false);
    applyOutputParams(rt);
    applyRoutes();
    ensureStreams();
    bump();
    return ok;
}

bool AudioEngine::inputConfig(const std::string& id, InputConfig* out) const
{
    auto it = inputs_.find(id);
    if (it == inputs_.end())
        return false;
    *out = it->second.cfg;
    return true;
}

bool AudioEngine::outputConfig(const std::string& id, OutputConfig* out) const
{
    auto it = outputs_.find(id);
    if (it == outputs_.end())
        return false;
    *out = it->second.cfg;
    return true;
}

void AudioEngine::setRoute(const std::string& inputId, const std::string& outputId, bool enabled)
{
    if (matrix_.setRoute(inputId, outputId, enabled)) {
        applyRoutes();
        // The AEC reference of a voice-processing input follows its first route.
        auto it = inputs_.find(inputId);
        if (it != inputs_.end() && it->second.stream && it->second.cfg.voiceProcessing &&
            echoReferenceFor(it->second) != it->second.echoReference) {
            closeInput(it->second);
            it->second.nextRetry = {};
        }
        ensureStreams();
        bump();
    }
}

void AudioEngine::setRouteGain(const std::string& inputId, const std::string& outputId, float gain)
{
    if (matrix_.setRouteGain(inputId, outputId, gain)) {
        const int i = inputSlot(inputId), o = outputSlot(outputId);
        if (i >= 0 && o >= 0)
            graph_.routes[i][o].gain.store(matrix_.find(inputId, outputId)->gain);
        bump();
    }
}

AudioRoutingMatrix::FeedbackRisk AudioEngine::feedbackRisk(const std::string& inputId,
                                                           const std::string& outputId) const
{
    auto i = inputs_.find(inputId);
    auto o = outputs_.find(outputId);
    if (i == inputs_.end() || o == outputs_.end())
        return AudioRoutingMatrix::FeedbackRisk::None;
    return AudioRoutingMatrix::feedbackRisk(i->second.info, o->second.info);
}

void AudioEngine::clearFeedbackTrip(const std::string& outputId)
{
    const int o = outputSlot(outputId);
    if (o >= 0) {
        graph_.outputs[o].feedbackTripped.store(false);
        bump();
    }
}

EngineConfig AudioEngine::config() const
{
    EngineConfig c;
    c.settings = settings_;
    for (const auto& kv : inputs_) {
        const InputConfig& ic = kv.second.cfg;
        // Persist only devices the user touched (enabled, routed or non-default settings).
        InputConfig def;
        def.id = ic.id;
        def.lastKnownName = ic.lastKnownName;
        const bool touched = ic.enabled || isRouted(ic.id) || ic.gainDb != def.gainDb || ic.volume != def.volume ||
                             ic.mute || ic.solo || ic.monoMix != def.monoMix || ic.highPass || ic.noiseSuppression ||
                             ic.noiseGate || ic.voiceProcessing;
        if (touched)
            c.inputs.push_back(ic);
    }
    for (const auto& kv : outputs_) {
        const OutputConfig& oc = kv.second.cfg;
        const bool touched = oc.enabled || isRouted(oc.id) || oc.volume != 1.0f || oc.mute || oc.delayMs != 0.0f;
        if (touched)
            c.outputs.push_back(oc);
    }
    c.routes = matrix_.routes();
    return c;
}

void AudioEngine::loadConfig(const EngineConfig& cfg)
{
    const bool wasMonitoring = monitoring_;
    closeAll();
    for (auto& kv : inputs_) {
        releaseInputSlot(kv.second);
        kv.second.cfg.enabled = false;
    }
    for (auto& kv : outputs_) {
        releaseOutputSlot(kv.second);
        kv.second.cfg.enabled = false;
    }
    matrix_.setRoutes(cfg.routes);
    setSettings(cfg.settings);
    for (const auto& ic : cfg.inputs)
        setInputConfig(ic);
    for (const auto& oc : cfg.outputs)
        setOutputConfig(oc);
    applyRoutes();
    monitoring_ = wasMonitoring;
    ensureStreams();
    bump();
}

// ---------------------------------------------------------------------------------
// Stream lifecycle

std::string AudioEngine::echoReferenceFor(const InputRuntime& rt) const
{
    for (const auto& r : matrix_.routes())
        if (r.inputId == rt.cfg.id) {
            auto it = outputs_.find(r.outputId);
            if (it != outputs_.end() && it->second.cfg.enabled)
                return r.outputId;
        }
    return {};
}

void AudioEngine::closeInput(InputRuntime& rt)
{
    if (rt.slot >= 0)
        graph_.inputs[rt.slot].active.store(false, std::memory_order_release);
    rt.stream.reset(); // joins the capture thread
    rt.proc.reset();
}

void AudioEngine::closeOutput(OutputRuntime& rt)
{
    if (rt.slot >= 0)
        graph_.outputs[rt.slot].active.store(false, std::memory_order_release);
    rt.stream.reset(); // joins the playback thread
    rt.mixer.reset();
}

static int backoffSeconds(int failures)
{
    static const int steps[] = {1, 2, 3, 5, 8, 10};
    return steps[std::min(failures, 5)];
}

void AudioEngine::ensureInput(InputRuntime& rt)
{
    const bool usable = rt.present && rt.info.state == DeviceState::Active;
    const bool want = monitoring_ && rt.cfg.enabled && rt.slot >= 0 && usable;

    if (rt.stream) {
        const IAudioStream::Status st = rt.stream->status();
        if (st != IAudioStream::Status::Running) {
            const std::string err = rt.stream->lastError();
            closeInput(rt);
            if (st == IAudioStream::Status::DeviceLost) {
                rt.state = RunState::WaitingForDevice;
                rt.message = "Device disconnected - waiting for it to reconnect";
                rt.nextRetry = Clock::now() + std::chrono::seconds(1);
            } else if (st == IAudioStream::Status::FormatChanged) {
                rt.message = "Device format changed - reopening";
                rt.nextRetry = {};
            } else {
                rt.state = RunState::Error;
                rt.message = err.empty() ? "Stream stopped unexpectedly" : err;
                rt.nextRetry = Clock::now() + std::chrono::seconds(backoffSeconds(rt.failures++));
            }
            bump();
        } else if (!want) {
            closeInput(rt);
            bump();
        }
    }

    if (!rt.stream && want && Clock::now() >= rt.nextRetry) {
        rt.proc.reset(new AudioInputProcessor());
        rt.proc->bind(&graph_, rt.slot);
        applyInputParams(rt);
        StreamOptions opt = streamOptions();
        opt.voiceProcessing = rt.cfg.voiceProcessing;
        rt.echoReference = rt.cfg.voiceProcessing ? echoReferenceFor(rt) : std::string();
        opt.echoReferenceId = rt.echoReference;
        std::string err;
        rt.stream = backend_->openCapture(rt.info, opt, *rt.proc, &err);
        if (rt.stream) {
            InputSlot& s = graph_.inputs[rt.slot];
            s.maxBurstFrames.store(0);
            s.generation.fetch_add(1, std::memory_order_acq_rel);
            s.active.store(true, std::memory_order_release);
            rt.state = RunState::Running;
            rt.message.clear();
            rt.failures = 0;
        } else {
            rt.proc.reset();
            rt.state = RunState::Error;
            rt.message = err.empty() ? "Could not open the device" : err;
            rt.nextRetry = Clock::now() + std::chrono::seconds(backoffSeconds(rt.failures++));
        }
        bump();
    }

    if (!rt.stream) {
        RunState s = RunState::Off;
        std::string msg;
        if (rt.cfg.enabled) {
            if (!usable) {
                s = RunState::WaitingForDevice;
                msg = rt.present ? std::string("Device is ") + toString(rt.info.state)
                                 : "Device disconnected - waiting for it to reconnect";
            } else if (monitoring_ && (rt.state == RunState::Error || rt.state == RunState::WaitingForDevice)) {
                s = rt.state; // keep the reason until the scheduled retry succeeds
                msg = rt.message;
            }
        }
        if (s != rt.state || msg != rt.message) {
            rt.state = s;
            rt.message = msg;
            bump();
        }
    }
}

void AudioEngine::ensureOutput(OutputRuntime& rt)
{
    const bool usable = rt.present && rt.info.state == DeviceState::Active;
    const bool want = monitoring_ && rt.cfg.enabled && rt.slot >= 0 && usable;

    if (rt.stream) {
        const IAudioStream::Status st = rt.stream->status();
        if (st != IAudioStream::Status::Running) {
            const std::string err = rt.stream->lastError();
            closeOutput(rt);
            if (st == IAudioStream::Status::DeviceLost) {
                rt.state = RunState::WaitingForDevice;
                rt.message = "Device disconnected - waiting for it to reconnect";
                rt.nextRetry = Clock::now() + std::chrono::seconds(1);
            } else if (st == IAudioStream::Status::FormatChanged) {
                rt.message = "Device format changed - reopening";
                rt.nextRetry = {};
            } else {
                rt.state = RunState::Error;
                rt.message = err.empty() ? "Stream stopped unexpectedly" : err;
                rt.nextRetry = Clock::now() + std::chrono::seconds(backoffSeconds(rt.failures++));
            }
            bump();
        } else if (!want) {
            closeOutput(rt);
            bump();
        } else {
            graph_.outputs[rt.slot].deviceLatencyMs.store(static_cast<float>(rt.stream->deviceLatencyMs()));
        }
    }

    if (!rt.stream && want && Clock::now() >= rt.nextRetry) {
        rt.mixer.reset(new AudioOutputMixer());
        rt.mixer->bind(&graph_, rt.slot);
        applyOutputParams(rt);
        std::string err;
        rt.stream = backend_->openRender(rt.info, streamOptions(), *rt.mixer, &err);
        if (rt.stream) {
            graph_.outputs[rt.slot].active.store(true, std::memory_order_release);
            graph_.outputs[rt.slot].deviceLatencyMs.store(static_cast<float>(rt.stream->deviceLatencyMs()));
            rt.state = RunState::Running;
            rt.message.clear();
            rt.failures = 0;
        } else {
            rt.mixer.reset();
            rt.state = RunState::Error;
            rt.message = err.empty() ? "Could not open the device" : err;
            rt.nextRetry = Clock::now() + std::chrono::seconds(backoffSeconds(rt.failures++));
        }
        bump();
    }

    if (!rt.stream) {
        RunState s = RunState::Off;
        std::string msg;
        if (rt.cfg.enabled) {
            if (!usable) {
                s = RunState::WaitingForDevice;
                msg = rt.present ? std::string("Device is ") + toString(rt.info.state)
                                 : "Device disconnected - waiting for it to reconnect";
            } else if (monitoring_ && (rt.state == RunState::Error || rt.state == RunState::WaitingForDevice)) {
                s = rt.state; // keep the reason until the scheduled retry succeeds
                msg = rt.message;
            }
        }
        if (s != rt.state || msg != rt.message) {
            rt.state = s;
            rt.message = msg;
            bump();
        }
    }
}

void AudioEngine::ensureStreams()
{
    for (auto& kv : outputs_)
        ensureOutput(kv.second);
    for (auto& kv : inputs_)
        ensureInput(kv.second);
}

// ---------------------------------------------------------------------------------
// Snapshot

EngineSnapshot AudioEngine::snapshot() const
{
    EngineSnapshot s;
    s.revision = revision_;
    s.backendName = backend_->name();
    s.monitoring = monitoring_;
    s.paused = graph_.paused.load();
    s.settings = settings_;
    s.routes = matrix_.routes();
    s.voiceProcessingSupported = backend_->supportsVoiceProcessing();

    auto fill = [](DeviceView& v, const DeviceInfo& info, bool present, RunState st, const std::string& msg, int slot,
                   const IAudioStream* stream) {
        v.info = info;
        v.present = present;
        v.runState = st;
        v.message = msg;
        v.slot = slot;
        v.streamOpen = stream != nullptr;
        if (stream) {
            v.streamFormat = stream->format();
            v.deviceLatencyMs = stream->deviceLatencyMs();
            v.notes = stream->notes();
        }
    };
    for (const auto& kv : inputs_) {
        const auto& rt = kv.second;
        // Hide stale "not present" endpoints Windows still remembers, unless the user uses them.
        if (!rt.present && !rt.cfg.enabled && !isRouted(kv.first))
            continue;
        InputView v;
        fill(v, rt.info, rt.present, rt.state, rt.message, rt.slot, rt.stream.get());
        v.config = rt.cfg;
        s.inputs.push_back(std::move(v));
    }
    for (const auto& kv : outputs_) {
        const auto& rt = kv.second;
        if (!rt.present && !rt.cfg.enabled && !isRouted(kv.first))
            continue;
        OutputView v;
        fill(v, rt.info, rt.present, rt.state, rt.message, rt.slot, rt.stream.get());
        v.config = rt.cfg;
        s.outputs.push_back(std::move(v));
    }
    auto order = [](const DeviceView& a, const DeviceView& b) {
        if (a.present != b.present)
            return a.present; // connected devices first
        return a.info.displayName < b.info.displayName;
    };
    std::sort(s.inputs.begin(), s.inputs.end(), order);
    std::sort(s.outputs.begin(), s.outputs.end(), order);
    return s;
}

} // namespace ab
