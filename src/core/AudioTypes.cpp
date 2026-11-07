#include "AudioTypes.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace ab {

int StreamFormat::bytesPerSample() const
{
    switch (sampleType) {
    case SampleType::Float32: return 4;
    case SampleType::Int16: return 2;
    case SampleType::Int24Packed: return 3;
    case SampleType::Int24In32: return 4;
    case SampleType::Int32: return 4;
    default: return 0;
    }
}

std::string StreamFormat::describe() const
{
    std::ostringstream os;
    if (sampleRate % 1000 == 0)
        os << sampleRate / 1000 << " kHz";
    else
        os << sampleRate / 1000.0 << " kHz";
    os << " · " << channels << (channels == 1 ? " ch" : " ch") << " · " << toString(sampleType);
    return os.str();
}

const char* toString(DeviceState s)
{
    switch (s) {
    case DeviceState::Active: return "Connected";
    case DeviceState::Disabled: return "Disabled in Windows";
    case DeviceState::NotPresent: return "Disconnected";
    case DeviceState::Unplugged: return "Unplugged";
    default: return "Unknown";
    }
}

const char* toString(DeviceBus b)
{
    switch (b) {
    case DeviceBus::Bluetooth: return "Bluetooth";
    case DeviceBus::USB: return "USB";
    case DeviceBus::Internal: return "Built-in / analog";
    case DeviceBus::Display: return "HDMI / DisplayPort";
    case DeviceBus::Virtual: return "Virtual";
    default: return "Other";
    }
}

const char* toString(FormFactor f)
{
    switch (f) {
    case FormFactor::RemoteNetworkDevice: return "Network";
    case FormFactor::Speakers: return "Speakers";
    case FormFactor::LineLevel: return "Line level";
    case FormFactor::Headphones: return "Headphones";
    case FormFactor::Microphone: return "Microphone";
    case FormFactor::Headset: return "Headset";
    case FormFactor::Handset: return "Handset";
    case FormFactor::UnknownDigitalPassthrough: return "Digital passthrough";
    case FormFactor::SPDIF: return "S/PDIF";
    case FormFactor::DigitalAudioDisplayDevice: return "Display audio";
    default: return "Unknown";
    }
}

const char* toString(SampleType t)
{
    switch (t) {
    case SampleType::Float32: return "32-bit float";
    case SampleType::Int16: return "16-bit";
    case SampleType::Int24Packed: return "24-bit";
    case SampleType::Int24In32: return "24-bit (32)";
    case SampleType::Int32: return "32-bit";
    default: return "unsupported";
    }
}

static bool containsNoCase(const std::string& hay, const char* needle)
{
    std::string h = hay;
    std::string n = needle;
    std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return h.find(n) != std::string::npos;
}

std::string deviceCategory(const DeviceInfo& d)
{
    const bool capture = d.flow == DeviceFlow::Capture;
    const std::string allNames = d.friendlyName + " " + d.adapterName;
    switch (d.bus) {
    case DeviceBus::Bluetooth:
        if (capture)
            return d.isBluetoothHandsFree || d.formFactor == FormFactor::Headset ? "Bluetooth headset microphone"
                                                                              : "Bluetooth microphone";
        if (d.isBluetoothHandsFree)
            return "Bluetooth headset (hands-free)";
        if (d.formFactor == FormFactor::Headphones || d.formFactor == FormFactor::Headset)
            return "Bluetooth headphones";
        return "Bluetooth speaker";
    case DeviceBus::USB:
        if (capture) {
            if (containsNoCase(allNames, "cam") || containsNoCase(allNames, "video"))
                return "Webcam microphone";
            return d.formFactor == FormFactor::Headset ? "USB headset microphone" : "USB microphone";
        }
        if (d.formFactor == FormFactor::Headphones || d.formFactor == FormFactor::Headset)
            return "USB headphones";
        return "USB speakers";
    case DeviceBus::Display:
        return capture ? "Display audio input" : "HDMI / DisplayPort audio";
    case DeviceBus::Virtual:
        return capture ? "Virtual recording device" : "Virtual playback device";
    case DeviceBus::Internal:
    case DeviceBus::Unknown:
    default:
        break;
    }
    if (d.formFactor == FormFactor::DigitalAudioDisplayDevice)
        return "HDMI / DisplayPort audio";
    if (d.formFactor == FormFactor::SPDIF)
        return capture ? "S/PDIF input" : "S/PDIF output";
    if (capture) {
        if (containsNoCase(allNames, "array") || containsNoCase(d.friendlyName, "internal"))
            return "Built-in microphone";
        if (d.formFactor == FormFactor::LineLevel)
            return "Line input";
        if (d.formFactor == FormFactor::Headset)
            return "Wired headset microphone";
        return d.bus == DeviceBus::Internal ? "Microphone (built-in / jack)" : "Recording device";
    }
    if (d.formFactor == FormFactor::Headphones)
        return "Wired headphones / AUX";
    if (d.formFactor == FormFactor::LineLevel)
        return "Line out / AUX";
    if (d.formFactor == FormFactor::Speakers)
        return d.bus == DeviceBus::Internal ? "Speakers / AUX jack (built-in)" : "Speakers";
    return "Playback device";
}

std::string shortIdHash(const std::string& id)
{
    // FNV-1a 32 bit, rendered as 4 hex digits: stable across runs.
    std::uint32_t h = 2166136261u;
    for (unsigned char c : id) {
        h ^= c;
        h *= 16777619u;
    }
    static const char* hex = "0123456789ABCDEF";
    std::string s;
    for (int i = 3; i >= 0; --i)
        s.push_back(hex[(h >> (i * 4 + 16)) & 0xF]);
    return s;
}

void disambiguateDisplayNames(std::vector<DeviceInfo>& devices)
{
    for (auto& d : devices)
        d.displayName = d.friendlyName.empty() ? std::string("Unnamed audio endpoint") : d.friendlyName;

    for (int pass = 0; pass < 3; ++pass) {
        std::map<std::pair<int, std::string>, int> counts;
        for (const auto& d : devices)
            counts[{static_cast<int>(d.flow), d.displayName}]++;
        bool anyDup = false;
        for (auto& d : devices) {
            if (counts[{static_cast<int>(d.flow), d.displayName}] < 2)
                continue;
            anyDup = true;
            if (pass == 0) {
                // Add the adapter name if it is not already part of the friendly name.
                if (!d.adapterName.empty() && d.displayName.find(d.adapterName) == std::string::npos)
                    d.displayName += " — " + d.adapterName;
                else
                    d.displayName += std::string(" — ") + toString(d.bus);
            } else if (pass == 1) {
                d.displayName += std::string(" [") + toString(d.state) + "]";
            } else {
                d.displayName += " #" + shortIdHash(d.id);
            }
        }
        if (!anyDup)
            break;
    }
    // Final guarantee: always unique, no matter what.
    std::map<std::pair<int, std::string>, int> counts;
    for (const auto& d : devices)
        counts[{static_cast<int>(d.flow), d.displayName}]++;
    for (auto& d : devices)
        if (counts[{static_cast<int>(d.flow), d.displayName}] > 1)
            d.displayName += " #" + shortIdHash(d.id);
}

bool sameHardwareContainer(const DeviceInfo& a, const DeviceInfo& b)
{
    if (a.containerId.empty() || b.containerId.empty())
        return false;
    return a.containerId == b.containerId;
}

float linearToDb(float lin)
{
    if (lin <= 1e-9f)
        return -180.0f;
    return 20.0f * std::log10(lin);
}

} // namespace ab
