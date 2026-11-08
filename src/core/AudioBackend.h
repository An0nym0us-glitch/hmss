// Abstraction of the platform audio API. The Windows implementation (WASAPI +
// Core Audio device enumeration) lives in src/backend/wasapi. The test suite uses a
// simulated backend that drives the exact same processors with generated signals.
#pragma once

#include "AudioTypes.h"

#include <memory>
#include <string>
#include <vector>

namespace ab {

class AudioInputProcessor;
class AudioOutputMixer;

struct StreamOptions {
    int bufferMs = 20;               // requested device buffer duration
    bool lowLatency = false;         // use the smallest shared-mode period (IAudioClient3)
    bool voiceProcessing = false;    // capture: open in Communications mode so Windows applies
                                     // its voice processing (AEC / NS / AGC) if the endpoint has it
    std::string echoReferenceId;     // capture + voiceProcessing: render endpoint used as AEC reference
    bool rawMode = false;            // bypass Windows audio effects/APOs (Nahimic, Dolby, ...) if the
                                     // driver supports RAW mode; ignored together with voiceProcessing
};

class IAudioStream {
public:
    enum class Status { Running, DeviceLost, FormatChanged, Failed, Stopped };
    virtual ~IAudioStream() = default; // stops the audio thread and releases the device
    virtual Status status() const = 0;
    virtual std::string lastError() const = 0;
    virtual StreamFormat format() const = 0;
    virtual double deviceLatencyMs() const = 0;
    // Free text describing optional features actually in effect (e.g. "Windows echo cancellation active").
    virtual std::string notes() const = 0;
};

// Device change notifications. May be invoked on any thread (on Windows: the
// IMMNotificationClient thread); implementations must only schedule work.
class IDeviceListener {
public:
    virtual ~IDeviceListener() = default;
    virtual void onDeviceListChanged() = 0;
};

class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;
    virtual std::string name() const = 0;
    virtual bool initialize(std::string* error) = 0;
    // All endpoints of both flows, in every state (active, disabled, unplugged, not present).
    virtual std::vector<DeviceInfo> enumerateDevices() = 0;
    virtual void setListener(IDeviceListener* listener) = 0;

    // Opens and starts a capture stream. The backend calls processor.prepare(format)
    // with the device format before the audio thread starts.
    virtual std::unique_ptr<IAudioStream> openCapture(const DeviceInfo& device, const StreamOptions& options,
                                                      AudioInputProcessor& processor, std::string* error) = 0;
    virtual std::unique_ptr<IAudioStream> openRender(const DeviceInfo& device, const StreamOptions& options,
                                                     AudioOutputMixer& mixer, std::string* error) = 0;

    // Whether the platform can honour StreamOptions::voiceProcessing at all.
    virtual bool supportsVoiceProcessing() const { return false; }
};

} // namespace ab
