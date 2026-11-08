// Windows Core Audio / WASAPI implementation of IAudioBackend.
//
//  * Enumeration: IMMDeviceEnumerator::EnumAudioEndpoints (all states), real friendly
//    names from the endpoint property store (PKEY_Device_FriendlyName), adapter name,
//    container ID, form factor; connection type (Bluetooth / USB / HD Audio / HDMI)
//    from the kernel-streaming filter reached through IDeviceTopology.
//  * Hot-plug: IMMNotificationClient.
//  * Streaming: event-driven shared-mode IAudioClient capture and render streams,
//    one MMCSS ("Pro Audio") thread per device (see WasapiStream.h).
#pragma once

#include "core/AudioBackend.h"

#include <memory>
#include <mutex>

namespace ab {

class WasapiBackend : public IAudioBackend {
public:
    WasapiBackend();
    ~WasapiBackend() override;

    std::string name() const override { return "Windows WASAPI (shared mode)"; }
    bool initialize(std::string* error) override;
    std::vector<DeviceInfo> enumerateDevices() override;
    void setListener(IDeviceListener* listener) override;
    std::unique_ptr<IAudioStream> openCapture(const DeviceInfo& device, const StreamOptions& options,
                                              AudioInputProcessor& processor, std::string* error) override;
    std::unique_ptr<IAudioStream> openRender(const DeviceInfo& device, const StreamOptions& options,
                                             AudioOutputMixer& mixer, std::string* error) override;
    bool supportsVoiceProcessing() const override { return true; }

    struct Impl;

private:
    std::unique_ptr<Impl> d_;
};

} // namespace ab
