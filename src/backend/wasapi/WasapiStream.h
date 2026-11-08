// Event-driven shared-mode WASAPI stream (capture or render), one MMCSS thread each.
#pragma once

#include "WasapiCommon.h"

#include "core/AudioBackend.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace ab {

class AudioInputProcessor;
class AudioOutputMixer;

namespace wasapi {

// Parses a WAVEFORMATEX / WAVEFORMATEXTENSIBLE into the engine's StreamFormat.
bool parseWaveFormat(const WAVEFORMATEX* wfx, StreamFormat* out);

class WasapiStream : public IAudioStream {
public:
    static std::unique_ptr<WasapiStream> openCapture(IMMDeviceEnumerator* enumerator, const DeviceInfo& device,
                                                     const StreamOptions& options, AudioInputProcessor& processor,
                                                     std::string* error);
    static std::unique_ptr<WasapiStream> openRender(IMMDeviceEnumerator* enumerator, const DeviceInfo& device,
                                                    const StreamOptions& options, AudioOutputMixer& mixer,
                                                    std::string* error);
    ~WasapiStream() override;

    Status status() const override { return status_.load(); }
    std::string lastError() const override;
    StreamFormat format() const override { return format_; }
    double deviceLatencyMs() const override { return latencyMs_; }
    std::string notes() const override { return notes_; }

private:
    WasapiStream() = default;
    bool init(IMMDeviceEnumerator* enumerator, const DeviceInfo& device, const StreamOptions& options, bool capture,
              std::string* error);
    bool start(std::string* error);
    void threadMain();
    HRESULT captureCycle();
    HRESULT renderCycle();
    void fail(HRESULT hr);

    bool capture_ = true;
    AudioInputProcessor* processor_ = nullptr;
    AudioOutputMixer* mixer_ = nullptr;
    ComPtr<IMMDevice> device_;
    ComPtr<IAudioClient> client_;
    ComPtr<IAudioCaptureClient> captureClient_;
    ComPtr<IAudioRenderClient> renderClient_;
    HANDLE audioEvent_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    UINT32 bufferFrames_ = 0;
    StreamFormat format_;
    double latencyMs_ = 0.0;
    std::string notes_;
    std::atomic<Status> status_{Status::Running};
    mutable std::mutex errorMutex_;
    std::string error_;
    std::thread thread_;
    bool started_ = false;
};

} // namespace wasapi
} // namespace ab
