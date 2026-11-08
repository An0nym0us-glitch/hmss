// SIMULATED audio backend - for automated tests and non-Windows development builds
// ONLY. It is never compiled into the Windows release of AudioBridge.
//
// Each simulated device has its own sample rate, channel layout, sample format and
// clock error (ppm), so the tests exercise sample-rate conversion, channel mapping
// and drift compensation exactly as with real hardware. Capture devices generate a
// test tone; render devices record what they receive for analysis.
//
// Two driving modes:
//   * deterministic (default): the test calls advance(seconds) and all streams are
//     stepped in virtual time - fast, reproducible, allows hours of simulated audio;
//   * threaded: every stream runs on its own real thread (stress / race testing and
//     the development GUI build).
#pragma once

#include "core/AudioBackend.h"
#include "core/AudioInputProcessor.h"
#include "core/AudioMixer.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ab {

struct SimDeviceSpec {
    std::string id;
    std::string name;
    DeviceFlow flow = DeviceFlow::Capture;
    DeviceBus bus = DeviceBus::USB;
    FormFactor formFactor = FormFactor::Microphone;
    std::string containerId;
    int sampleRate = 48000;
    int channels = 1;
    SampleType sampleType = SampleType::Float32;
    double clockPpm = 0.0;     // device clock error
    double periodMs = 10.0;    // device period (packet size)
    // Capture signal: sine of `toneHz` at `toneAmplitude` (+ optional white noise).
    double toneHz = 440.0;
    float toneAmplitude = 0.25f;
    float noiseAmplitude = 0.0f;
    bool burstyDelivery = false; // deliver 2-3 periods at once at irregular intervals (Bluetooth-like)
};

class SimulatedBackend : public IAudioBackend {
public:
    explicit SimulatedBackend(bool threaded = false);
    ~SimulatedBackend() override;

    // --- IAudioBackend
    std::string name() const override { return threaded_ ? "Simulated (threaded)" : "Simulated"; }
    bool initialize(std::string*) override { return true; }
    std::vector<DeviceInfo> enumerateDevices() override;
    void setListener(IDeviceListener* l) override;
    std::unique_ptr<IAudioStream> openCapture(const DeviceInfo& device, const StreamOptions& options,
                                              AudioInputProcessor& processor, std::string* error) override;
    std::unique_ptr<IAudioStream> openRender(const DeviceInfo& device, const StreamOptions& options,
                                             AudioOutputMixer& mixer, std::string* error) override;

    // --- test control
    void addDevice(const SimDeviceSpec& spec);
    void disconnect(const std::string& id);   // stream reports DeviceLost, device becomes NotPresent
    void reconnect(const std::string& id);    // device becomes Active again (same endpoint ID)
    void setTone(const std::string& id, double hz, float amplitude);
    void setFailOpen(const std::string& id, bool fail); // simulate "device in use"

    // Deterministic mode: advance virtual time.
    void advance(double seconds);
    double now() const { return now_; }

    // Recorded output of a render device: mono (average of channels) at device rate.
    std::vector<float> takeRecording(const std::string& id);
    void setRecording(const std::string& id, bool on);
    std::uint64_t renderedFrames(const std::string& id) const;
    int openStreamCount() const;

    struct Device;
    class Stream;

private:
    Device* find(const std::string& id) const;
    void notify();

    bool threaded_;
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<Device>> devices_;
    IDeviceListener* listener_ = nullptr;
    double now_ = 0.0;
    std::vector<Stream*> streams_; // open streams (owned by the engine)
    friend class Stream;
};

} // namespace ab
