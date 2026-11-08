// AudioBridge command line tool.
//
//   AudioBridgeCLI --list
//       Lists every Windows audio endpoint with its real name, category, state,
//       connection type, mix format and endpoint ID.
//   AudioBridgeCLI --route <input> <output> [--route <input> <output> ...]
//                  [--rate 48000] [--buffer 20] [--seconds N]
//       Streams audio headlessly. <input>/<output> are list numbers or a part of the
//       device name (case-insensitive). Prints levels once per second.
#include "core/AudioEngine.h"

#ifdef _WIN32
#include "backend/wasapi/WasapiBackend.h"
#include <windows.h>
#else
#include "backend/sim/SimulatedBackend.h"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <csignal>
#include <string>
#include <thread>
#include <vector>

using namespace ab;

namespace {

std::atomic<bool> g_quit{false};

void onSignal(int) { g_quit.store(true); }

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::unique_ptr<IAudioBackend> makeBackend()
{
#ifdef _WIN32
    return std::unique_ptr<IAudioBackend>(new WasapiBackend());
#else
    // Non-Windows development build: simulated devices.
    auto sim = std::make_unique<SimulatedBackend>(true);
    SimDeviceSpec m;
    m.id = "sim-mic"; m.name = "Microphone (SIMULATED)"; m.flow = DeviceFlow::Capture;
    sim->addDevice(m);
    SimDeviceSpec o;
    o.id = "sim-out"; o.name = "Speakers (SIMULATED)"; o.flow = DeviceFlow::Render; o.channels = 2;
    o.formFactor = FormFactor::Speakers;
    sim->addDevice(o);
    return sim;
#endif
}

void printDevices(const EngineSnapshot& s)
{
    int n = 1;
    std::printf("INPUT DEVICES (microphones / recording endpoints)\n");
    for (const auto& v : s.inputs) {
        const DeviceInfo& d = v.info;
        std::printf("  [%d] %s%s\n", n++, d.displayName.c_str(), d.isDefault ? "  (Windows default)" : "");
        std::printf("       %s | %s | %s", deviceCategory(d).c_str(), toString(d.bus), toString(d.state));
        if (d.mixFormatValid)
            std::printf(" | %s", d.mixFormat.describe().c_str());
        std::printf("\n       id: %s\n", d.id.c_str());
    }
    std::printf("\nOUTPUT DEVICES (speakers / playback endpoints)\n");
    for (const auto& v : s.outputs) {
        const DeviceInfo& d = v.info;
        std::printf("  [%d] %s%s\n", n++, d.displayName.c_str(), d.isDefault ? "  (Windows default)" : "");
        std::printf("       %s | %s | %s", deviceCategory(d).c_str(), toString(d.bus), toString(d.state));
        if (d.mixFormatValid)
            std::printf(" | %s", d.mixFormat.describe().c_str());
        std::printf("\n       id: %s\n", d.id.c_str());
    }
}

// Resolves "3" (list number) or a name fragment to an endpoint ID of the given flow.
std::string resolve(const EngineSnapshot& s, const std::string& sel, bool input)
{
    char* end = nullptr;
    const long num = std::strtol(sel.c_str(), &end, 10);
    std::vector<const DeviceView*> all;
    for (const auto& v : s.inputs) all.push_back(&v);
    for (const auto& v : s.outputs) all.push_back(&v);
    if (end && *end == '\0' && num >= 1 && num <= static_cast<long>(all.size())) {
        const DeviceView* v = all[static_cast<std::size_t>(num - 1)];
        if ((v->info.flow == DeviceFlow::Capture) == input)
            return v->info.id;
        return {};
    }
    std::string found;
    for (const DeviceView* v : all) {
        if ((v->info.flow == DeviceFlow::Capture) != input || v->info.state != DeviceState::Active)
            continue;
        if (lower(v->info.displayName).find(lower(sel)) != std::string::npos) {
            if (!found.empty()) {
                std::fprintf(stderr, "'%s' matches more than one device - use the list number\n", sel.c_str());
                return {};
            }
            found = v->info.id;
        }
    }
    return found;
}

std::string nameOf(const EngineSnapshot& s, const std::string& id)
{
    for (const auto& v : s.inputs) if (v.info.id == id) return v.info.displayName;
    for (const auto& v : s.outputs) if (v.info.id == id) return v.info.displayName;
    return id;
}

void usage()
{
    std::printf("AudioBridge command line tool\n\n"
                "  AudioBridgeCLI --list\n"
                "  AudioBridgeCLI --route <input> <output> [--route <input> <output> ...]\n"
                "                 [--rate 48000] [--buffer 20] [--seconds N]\n\n"
                "<input>/<output>: number from --list, or part of the device name.\n");
}

} // namespace

int main(int argc, char** argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8); // real device names may contain any Unicode character
#endif
    std::signal(SIGINT, onSignal);
    if (argc < 2) {
        usage();
        return 1;
    }
    bool list = false;
    std::vector<std::pair<std::string, std::string>> routes;
    int rate = 48000, buffer = 20;
    double seconds = 0.0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--list") list = true;
        else if (a == "--route" && i + 2 < argc) { routes.emplace_back(argv[i + 1], argv[i + 2]); i += 2; }
        else if (a == "--rate" && i + 1 < argc) rate = std::atoi(argv[++i]);
        else if (a == "--buffer" && i + 1 < argc) buffer = std::atoi(argv[++i]);
        else if (a == "--seconds" && i + 1 < argc) seconds = std::atof(argv[++i]);
        else { usage(); return 1; }
    }

    AudioEngine engine(makeBackend());
    std::string err;
    if (!engine.initialize(&err)) {
        std::fprintf(stderr, "Error: %s\n", err.c_str());
        return 2;
    }
    EngineSnapshot snap = engine.snapshot();
    if (list || routes.empty()) {
        printDevices(snap);
        return 0;
    }

    EngineSettings st = engine.settings();
    st.sampleRate = rate;
    st.bufferMs = buffer;
    st.masterVolume = 1.0f;
    engine.setSettings(st);
    for (const auto& r : routes) {
        const std::string in = resolve(snap, r.first, true);
        const std::string out = resolve(snap, r.second, false);
        if (in.empty() || out.empty()) {
            std::fprintf(stderr, "Cannot resolve route %s -> %s (run --list)\n", r.first.c_str(), r.second.c_str());
            return 3;
        }
        InputConfig ic;
        engine.inputConfig(in, &ic);
        ic.enabled = true;
        if (!engine.setInputConfig(ic, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 3; }
        OutputConfig oc;
        engine.outputConfig(out, &oc);
        oc.enabled = true;
        if (!engine.setOutputConfig(oc, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 3; }
        engine.setRoute(in, out, true);
        std::printf("Route: %s  ->  %s\n", nameOf(snap, in).c_str(), nameOf(snap, out).c_str());
    }
    engine.startMonitoring();
    std::printf("Monitoring (Ctrl+C to stop)...\n");

    const auto t0 = std::chrono::steady_clock::now();
    int tick = 0;
    while (!g_quit.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        engine.housekeeping();
        if (++tick % 4 == 0) {
            const EngineSnapshot s = engine.snapshot();
            for (const auto& v : s.inputs)
                if (v.config.enabled)
                    std::printf("  IN  %-48.48s %-18s peak %6.1f dBFS%s\n", v.info.displayName.c_str(),
                                toString(v.runState),
                                v.slot >= 0 ? linearToDb(engine.graph().inputs[v.slot].meter.takePeak()) : -180.0f,
                                v.message.empty() ? "" : ("  " + v.message).c_str());
            for (const auto& v : s.outputs)
                if (v.config.enabled) {
                    OutputSlot* o = v.slot >= 0 ? &engine.graph().outputs[v.slot] : nullptr;
                    std::printf("  OUT %-48.48s %-18s peak %6.1f dBFS  latency %.0f+%.0f ms  underruns %llu%s\n",
                                v.info.displayName.c_str(), toString(v.runState),
                                o ? linearToDb(o->meter.takePeak()) : -180.0f, o ? o->routeLatencyMs.load() : 0.0f,
                                v.deviceLatencyMs, o ? static_cast<unsigned long long>(o->underruns.load()) : 0ull,
                                v.message.empty() ? "" : ("  " + v.message).c_str());
                }
            std::fflush(stdout);
        }
        if (seconds > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() >= seconds)
            break;
    }
    engine.stopMonitoring();
    std::printf("Stopped.\n");
    return 0;
}
