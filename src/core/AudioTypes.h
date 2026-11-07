// AudioBridge - core audio type definitions (platform independent).
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ab {

// Hard limits of the real-time graph. Slots are pre-allocated so routing
// changes never have to free memory that an audio thread might be using.
constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxInputs = 16;
constexpr int kMaxOutputs = 16;
constexpr int kEngineChannels = 2;          // internal bus format: stereo float32
constexpr std::size_t kMaxBlockFrames = 16384; // largest block processed in one call

enum class DeviceFlow { Capture, Render };

enum class DeviceState {
    Active,      // present and usable
    Disabled,    // disabled in Windows Sound settings
    NotPresent,  // removed / Bluetooth device disconnected
    Unplugged,   // jack-detected endpoint with nothing plugged in
    Unknown
};

// Physical connection type, derived from the kernel-streaming filter path.
enum class DeviceBus {
    Bluetooth,
    USB,
    Internal,     // HD Audio / Intel SST / AMD ACP codecs (laptop speakers, mic, jacks)
    Display,      // HDMI / DisplayPort audio
    Virtual,      // software endpoints (virtual cables, remote audio, ...)
    Unknown
};

// Mirrors the Windows EndpointFormFactor enumeration.
enum class FormFactor {
    RemoteNetworkDevice = 0,
    Speakers = 1,
    LineLevel = 2,
    Headphones = 3,
    Microphone = 4,
    Headset = 5,
    Handset = 6,
    UnknownDigitalPassthrough = 7,
    SPDIF = 8,
    DigitalAudioDisplayDevice = 9,
    Unknown = 10
};

enum class SampleType { Float32, Int16, Int24Packed, Int24In32, Int32, Unsupported };

struct StreamFormat {
    int sampleRate = 48000;
    int channels = 2;
    std::uint32_t channelMask = 0;  // SPEAKER_* bit mask, 0 = unknown
    SampleType sampleType = SampleType::Float32;
    int bytesPerSample() const;
    int bytesPerFrame() const { return bytesPerSample() * channels; }
    std::string describe() const;
};

struct DeviceInfo {
    std::string id;             // unique endpoint ID (IMMDevice::GetId), UTF-8
    std::string friendlyName;   // PKEY_Device_FriendlyName, e.g. "Microphone (USB Audio Device)"
    std::string displayName;    // friendlyName, disambiguated if it collides with another endpoint
    std::string description;    // PKEY_Device_DeviceDesc, e.g. "Microphone"
    std::string adapterName;    // PKEY_DeviceInterface_FriendlyName, e.g. "USB Audio Device"
    std::string containerId;    // PKEY_Device_ContainerId (groups endpoints of one physical device)
    std::string interfacePath;  // kernel-streaming filter path (diagnostics, bus detection)
    DeviceFlow flow = DeviceFlow::Capture;
    DeviceState state = DeviceState::Unknown;
    DeviceBus bus = DeviceBus::Unknown;
    FormFactor formFactor = FormFactor::Unknown;
    bool isDefault = false;              // default console device for this flow
    bool isDefaultCommunications = false;
    bool isBluetoothHandsFree = false;   // Bluetooth HFP/HSP endpoint (narrow-band mic profile)
    StreamFormat mixFormat;              // shared-mode mix format (valid for active devices)
    bool mixFormatValid = false;
};

const char* toString(DeviceState s);
const char* toString(DeviceBus b);
const char* toString(FormFactor f);
const char* toString(SampleType t);

// Human readable category such as "Bluetooth headset microphone" or "HDMI / DisplayPort audio".
std::string deviceCategory(const DeviceInfo& d);

// Assigns DeviceInfo::displayName for every device so that no two devices of the
// same flow share the same visible name. Uses adapter name, bus type and finally a
// short stable suffix derived from the endpoint ID.
void disambiguateDisplayNames(std::vector<DeviceInfo>& devices);

// Stable short hash of an endpoint ID (used for disambiguation suffixes).
std::string shortIdHash(const std::string& id);

// Heuristic: endpoints that belong to the same physical hardware (same container,
// e.g. headset mic + headset speaker, or laptop mic + laptop speakers) are very
// likely to produce acoustic feedback when routed to each other.
bool sameHardwareContainer(const DeviceInfo& a, const DeviceInfo& b);

inline float dbToLinear(float db) { return db <= -144.0f ? 0.0f : static_cast<float>(std::pow(10.0, db / 20.0)); }
float linearToDb(float lin);

} // namespace ab
