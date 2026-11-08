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

std::string jackSummary(const DeviceInfo& d)
{
    std::string out;
    for (const JackInfo& j : d.jacks) {
        if (j.integrated)
            continue;
        if (!out.empty())
            out += ", ";
        out += (j.connector.empty() ? std::string("Jack") : j.connector + " jack");
        if (!j.location.empty())
            out += " (" + j.location + ")";
        out += j.connected ? ": plugged in" : ": empty";
    }
    return out;
}

bool hasPluggedJack(const DeviceInfo& d)
{
    for (const JackInfo& j : d.jacks)
        if (!j.integrated && j.connected)
            return true;
    return false;
}

bool isEnhancementVirtualDevice(const DeviceInfo& d)
{
    const std::string n = d.friendlyName + " " + d.adapterName + " " + d.description;
    return containsNoCase(n, "nahimic") || containsNoCase(n, "a-volute") || containsNoCase(n, "mirroring") ||
           containsNoCase(n, "sonic studio virtual") || containsNoCase(n, "sonic radar");
}

std::string deviceCategory(const DeviceInfo& d)
{
    if (isEnhancementVirtualDevice(d))
        return containsNoCase(d.friendlyName + d.adapterName, "nahimic") ? "Nahimic virtual device (no speaker)"
                                                                         : "Virtual effects device (no speaker)";
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
    if (!d.jacks.empty() && d.bus != DeviceBus::Display) {
        // The driver told us about real sockets: say what is actually connected.
        if (hasPluggedJack(d))
            return "AUX / headphone jack (plugged in)";
        bool integrated = false;
        for (const JackInfo& j : d.jacks)
            integrated = integrated || j.integrated;
        if (integrated)
            return "Laptop speakers (jack empty)";
        return "AUX / headphone jack (nothing plugged in)";
    }
    if (d.formFactor == FormFactor::Headphones)
        return "Wired headphones / AUX";
    if (d.formFactor == FormFactor::LineLevel)
        return "Line out / AUX";
    if (d.formFactor == FormFactor::Speakers)
        return d.bus == DeviceBus::Internal ? "Speakers / AUX jack (built-in)" : "Speakers";
    return "Playback device";
}

DeviceBus classifyDeviceBus(const std::string& interfacePath, const std::string& friendlyName,
                            const std::string& adapterName, FormFactor formFactor, bool* handsFree)
{
    const std::string names = friendlyName + " " + adapterName;
    if (containsNoCase(names, "nahimic") || containsNoCase(names, "a-volute") || containsNoCase(names, "mirroring") ||
        containsNoCase(names, "sonic studio virtual")) {
        if (handsFree)
            *handsFree = false;
        return DeviceBus::Virtual; // enhancement software endpoint, not hardware
    }
    bool hf = containsNoCase(interfacePath, "bthhfenum") || containsNoCase(names, "hands-free") ||
              containsNoCase(names, "handsfree");
    DeviceBus bus = DeviceBus::Unknown;
    if (containsNoCase(interfacePath, "bthhfenum") || containsNoCase(interfacePath, "bthenum") ||
        containsNoCase(interfacePath, "bthleenum") || containsNoCase(interfacePath, "bthle") ||
        containsNoCase(interfacePath, "{0000110b-") || containsNoCase(interfacePath, "{0000111e-"))
        bus = DeviceBus::Bluetooth;
    else if (containsNoCase(interfacePath, "usb#") || containsNoCase(interfacePath, "\\usb") ||
             containsNoCase(interfacePath, "usbaudio"))
        bus = DeviceBus::USB;
    else if (containsNoCase(interfacePath, "hdaudio#") || containsNoCase(interfacePath, "intelaudio#") ||
             containsNoCase(interfacePath, "intcaudiobus") || containsNoCase(interfacePath, "acp#") ||
             containsNoCase(interfacePath, "amdaudio") || containsNoCase(interfacePath, "sst") ||
             containsNoCase(interfacePath, "pci#"))
        bus = formFactor == FormFactor::DigitalAudioDisplayDevice ? DeviceBus::Display : DeviceBus::Internal;
    else if (containsNoCase(interfacePath, "root#"))
        bus = DeviceBus::Virtual;

    if (bus == DeviceBus::Unknown) {
        // Name based fallbacks (endpoint not active, or unusual driver stack).
        if (hf || containsNoCase(names, "bluetooth") || containsNoCase(names, "a2dp") ||
            containsNoCase(names, "le audio"))
            bus = DeviceBus::Bluetooth;
        else if (formFactor == FormFactor::DigitalAudioDisplayDevice || containsNoCase(names, "hdmi") ||
                 containsNoCase(names, "displayport") || containsNoCase(names, "display audio") ||
                 containsNoCase(names, "nvidia high definition"))
            bus = DeviceBus::Display;
        else if (containsNoCase(names, "usb"))
            bus = DeviceBus::USB;
        else if (containsNoCase(names, "virtual") || containsNoCase(names, "cable") ||
                 containsNoCase(names, "voicemeeter") || containsNoCase(names, "steam streaming"))
            bus = DeviceBus::Virtual;
        else if (containsNoCase(names, "realtek") || containsNoCase(names, "smart sound") ||
                 containsNoCase(names, "high definition audio") || containsNoCase(names, "conexant") ||
                 containsNoCase(names, "microphone array") || containsNoCase(names, "internal"))
            bus = DeviceBus::Internal;
    }
    if (bus != DeviceBus::Bluetooth)
        hf = false;
    if (handsFree)
        *handsFree = hf;
    return bus;
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
