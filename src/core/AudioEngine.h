// AudioEngine: the control-plane of AudioBridge.
//
// * keeps the table of every known endpoint (present or remembered),
// * maps enabled devices to slots of the real-time AudioGraph,
// * opens / closes capture and playback streams through the platform backend,
// * applies channel / route / master parameters to the graph (atomics),
// * recovers streams when devices disconnect and come back (same endpoint ID only -
//   it never silently substitutes another device).
//
// Threading: every public method must be called from ONE control thread (in the GUI
// application this is a dedicated engine thread, never the GUI thread). Audio runs on
// per-device threads owned by the backend streams. graph() may be read (meters) from
// any thread.
#pragma once

#include "AudioBackend.h"
#include "AudioGraph.h"
#include "AudioInputProcessor.h"
#include "AudioMixer.h"
#include "AudioRoutingMatrix.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ab {

struct InputConfig {
    std::string id;
    std::string lastKnownName;
    bool enabled = false;
    float gainDb = 0.0f;
    float volume = 1.0f;
    bool mute = false;
    bool solo = false;
    bool monoMix = true;
    bool highPass = false;
    bool noiseSuppression = false;
    float noiseSuppressionStrength = 0.6f;
    bool noiseGate = false;
    float gateThresholdDb = -50.0f;
    bool voiceProcessing = false; // Windows communications-mode processing (incl. AEC when available)
};

struct OutputConfig {
    std::string id;
    std::string lastKnownName;
    bool enabled = false;
    float volume = 1.0f;
    bool mute = false;
    float delayMs = 0.0f;
};

struct EngineSettings {
    int sampleRate = 48000;     // internal processing rate
    int bufferMs = 20;          // route latency target and device buffer request
    bool lowLatency = false;    // IAudioClient3 minimum period where supported
    bool limiter = true;        // clipping protection
    bool feedbackGuard = true;  // auto-mute output on sustained howling
    float masterVolume = 0.8f;
    bool globalMute = false;
};

struct EngineConfig {
    EngineSettings settings;
    std::vector<InputConfig> inputs;
    std::vector<OutputConfig> outputs;
    std::vector<Route> routes;
};

enum class RunState {
    Off,              // not enabled, or monitoring stopped
    Running,          // stream open and streaming
    WaitingForDevice, // enabled but the device is disconnected / unplugged / disabled
    Error             // the device refused to open; retried automatically
};
const char* toString(RunState s);

struct DeviceView {
    DeviceInfo info;
    bool present = false;
    RunState runState = RunState::Off;
    std::string message;
    int slot = -1;
    bool streamOpen = false;
    StreamFormat streamFormat;
    double deviceLatencyMs = 0.0;
    std::string notes;
};
struct InputView : DeviceView { InputConfig config; };
struct OutputView : DeviceView { OutputConfig config; };

struct EngineSnapshot {
    std::uint64_t revision = 0;
    std::string backendName;
    bool monitoring = false;
    bool paused = false;
    EngineSettings settings;
    std::vector<InputView> inputs;
    std::vector<OutputView> outputs;
    std::vector<Route> routes;
    bool voiceProcessingSupported = false;
};

class AudioEngine : private IDeviceListener {
public:
    explicit AudioEngine(std::unique_ptr<IAudioBackend> backend);
    ~AudioEngine() override;
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool initialize(std::string* error);

    // Invoked (from any thread) when the OS reports a device change. The host should
    // schedule refreshDevices() / housekeeping() on the control thread in response.
    void setDeviceChangeHook(std::function<void()> hook) { hook_ = std::move(hook); }

    // Monotonic time in seconds used for retry back-off. Defaults to steady_clock;
    // tests inject the simulator's virtual clock.
    void setTimeSource(std::function<double()> now) { now_ = std::move(now); }

    void refreshDevices();
    // Periodic maintenance (~4 Hz): stream health, reconnection, retries.
    void housekeeping();

    void startMonitoring();
    void stopMonitoring();
    bool isMonitoring() const { return monitoring_; }
    void setPaused(bool paused);
    bool isPaused() const { return graph_.paused.load(); }

    void setSettings(const EngineSettings& s);
    const EngineSettings& settings() const { return settings_; }
    void setMasterVolume(float v);
    void setGlobalMute(bool m);

    // Returns false (with a reason) if the configuration could not be applied, e.g. no free slot.
    bool setInputConfig(const InputConfig& cfg, std::string* error = nullptr);
    bool setOutputConfig(const OutputConfig& cfg, std::string* error = nullptr);
    bool inputConfig(const std::string& id, InputConfig* out) const;
    bool outputConfig(const std::string& id, OutputConfig* out) const;

    void setRoute(const std::string& inputId, const std::string& outputId, bool enabled);
    void setRouteGain(const std::string& inputId, const std::string& outputId, float gain);
    const AudioRoutingMatrix& routing() const { return matrix_; }
    AudioRoutingMatrix::FeedbackRisk feedbackRisk(const std::string& inputId, const std::string& outputId) const;

    void clearFeedbackTrip(const std::string& outputId);

    EngineConfig config() const;
    void loadConfig(const EngineConfig& cfg);

    EngineSnapshot snapshot() const;
    std::uint64_t revision() const { return revision_; }

    AudioGraph& graph() { return graph_; }
    const AudioGraph& graph() const { return graph_; }
    IAudioBackend& backend() { return *backend_; }

    int inputSlot(const std::string& id) const;
    int outputSlot(const std::string& id) const;

private:
    struct InputRuntime {
        InputConfig cfg;
        DeviceInfo info;
        bool present = false;
        int slot = -1;
        std::unique_ptr<AudioInputProcessor> proc;
        std::unique_ptr<IAudioStream> stream;
        RunState state = RunState::Off;
        std::string message;
        int failures = 0;
        double nextRetry = 0.0;
        std::string echoReference; // render endpoint used as AEC reference when opened
    };
    struct OutputRuntime {
        OutputConfig cfg;
        DeviceInfo info;
        bool present = false;
        int slot = -1;
        std::unique_ptr<AudioOutputMixer> mixer;
        std::unique_ptr<IAudioStream> stream;
        RunState state = RunState::Off;
        std::string message;
        int failures = 0;
        double nextRetry = 0.0;
    };

    void onDeviceListChanged() override; // IDeviceListener (any thread)

    void ensureStreams();
    void ensureInput(InputRuntime& rt);
    void ensureOutput(OutputRuntime& rt);
    void closeInput(InputRuntime& rt);
    void closeOutput(OutputRuntime& rt);
    void closeAll();
    bool assignInputSlot(InputRuntime& rt, std::string* error);
    bool assignOutputSlot(OutputRuntime& rt, std::string* error);
    void releaseInputSlot(InputRuntime& rt);
    void releaseOutputSlot(OutputRuntime& rt);
    void applyInputParams(const InputRuntime& rt);
    void applyOutputParams(const OutputRuntime& rt);
    void applyRoutes();
    void applySolo();
    void applyGlobalParams();
    std::string echoReferenceFor(const InputRuntime& rt) const;
    StreamOptions streamOptions() const;
    bool isRouted(const std::string& id) const;
    void bump() { ++revision_; }

    std::unique_ptr<IAudioBackend> backend_;
    AudioGraph graph_;
    AudioRoutingMatrix matrix_;
    EngineSettings settings_;
    std::map<std::string, InputRuntime> inputs_;
    std::map<std::string, OutputRuntime> outputs_;
    bool monitoring_ = false;
    bool initialized_ = false;
    std::atomic<bool> devicesDirty_{false};
    std::function<void()> hook_;
    std::function<double()> now_;
    std::uint64_t revision_ = 1;
};

} // namespace ab
