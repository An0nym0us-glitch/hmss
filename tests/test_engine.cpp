// End-to-end engine scenarios on SIMULATED endpoints.
//
// These run the real AudioEngine, AudioInputProcessor and AudioOutputMixer code
// against simulated devices that have independent, deliberately inaccurate clocks,
// different sample rates / channel counts / sample formats and (for "Bluetooth"
// devices) irregular, bursty packet delivery. They are NOT hardware tests: they prove
// the routing, mixing, conversion, drift compensation and recovery logic. Hardware
// validation is described in docs/TESTING.md.
#include "SignalAnalysis.h"
#include "TestFramework.h"

#include "backend/sim/SimulatedBackend.h"
#include "core/AudioEngine.h"

#include <chrono>
#include <thread>

using namespace ab;
using namespace abtest;

namespace {

const char* kBtMic = "{SIM}.BT-MIC";
const char* kUsbMic = "{SIM}.USB-MIC";
const char* kLaptopMic = "{SIM}.LAPTOP-MIC";
const char* kAux = "{SIM}.AUX-OUT";
const char* kBtSpk = "{SIM}.BT-SPK";
const char* kUsbSpk = "{SIM}.USB-SPK";

constexpr double kBtHz = 300, kUsbHz = 700, kLaptopHz = 1100;
constexpr float kAmp = 0.2f;

SimDeviceSpec btMic()
{
    SimDeviceSpec s;
    s.id = kBtMic;
    s.name = "Headset Microphone (Simulated Bluetooth Earbuds)";
    s.flow = DeviceFlow::Capture;
    s.bus = DeviceBus::Bluetooth;
    s.formFactor = FormFactor::Headset;
    s.containerId = "{BT-EARBUDS}";
    s.sampleRate = 16000; // HFP wide-band speech
    s.channels = 1;
    s.sampleType = SampleType::Int16;
    s.clockPpm = 150.0;
    s.periodMs = 7.5;
    s.burstyDelivery = true;
    s.toneHz = kBtHz;
    s.toneAmplitude = kAmp;
    return s;
}

SimDeviceSpec usbMic()
{
    SimDeviceSpec s;
    s.id = kUsbMic;
    s.name = "Microphone (Simulated USB Audio Device)";
    s.flow = DeviceFlow::Capture;
    s.bus = DeviceBus::USB;
    s.containerId = "{USB-MIC}";
    s.sampleRate = 44100;
    s.channels = 2;
    s.sampleType = SampleType::Int16;
    s.clockPpm = -60.0;
    s.toneHz = kUsbHz;
    s.toneAmplitude = kAmp;
    return s;
}

SimDeviceSpec laptopMic()
{
    SimDeviceSpec s;
    s.id = kLaptopMic;
    s.name = "Microphone Array (Simulated Smart Sound)";
    s.flow = DeviceFlow::Capture;
    s.bus = DeviceBus::Internal;
    s.containerId = "{00000000-0000-0000-FFFF-FFFFFFFFFFFF}";
    s.sampleRate = 48000;
    s.channels = 2;
    s.sampleType = SampleType::Float32;
    s.clockPpm = 20.0;
    s.toneHz = kLaptopHz;
    s.toneAmplitude = kAmp;
    return s;
}

SimDeviceSpec auxOut()
{
    SimDeviceSpec s;
    s.id = kAux;
    s.name = "Speakers (Simulated Realtek Audio)";
    s.flow = DeviceFlow::Render;
    s.bus = DeviceBus::Internal;
    s.formFactor = FormFactor::Headphones;
    s.containerId = "{00000000-0000-0000-FFFF-FFFFFFFFFFFF}";
    s.sampleRate = 48000;
    s.channels = 2;
    s.sampleType = SampleType::Float32;
    s.clockPpm = -35.0;
    return s;
}

SimDeviceSpec btSpeaker()
{
    SimDeviceSpec s;
    s.id = kBtSpk;
    s.name = "Speaker (Simulated Bluetooth Speaker)";
    s.flow = DeviceFlow::Render;
    s.bus = DeviceBus::Bluetooth;
    s.formFactor = FormFactor::Speakers;
    s.containerId = "{BT-SPEAKER}";
    s.sampleRate = 44100;
    s.channels = 2;
    s.sampleType = SampleType::Int16;
    s.clockPpm = 90.0;
    s.periodMs = 20.0;
    return s;
}

SimDeviceSpec usbSpeaker()
{
    SimDeviceSpec s;
    s.id = kUsbSpk;
    s.name = "Speakers (Simulated USB Audio Device)";
    s.flow = DeviceFlow::Render;
    s.bus = DeviceBus::USB;
    s.formFactor = FormFactor::Speakers;
    s.containerId = "{USB-SPK}";
    s.sampleRate = 96000;
    s.channels = 2;
    s.sampleType = SampleType::Int32;
    s.clockPpm = -10.0;
    return s;
}

struct Rig {
    SimulatedBackend* sim = nullptr;
    std::unique_ptr<AudioEngine> eng;

    explicit Rig(int engineRate = 48000, int bufferMs = 20)
    {
        auto b = std::make_unique<SimulatedBackend>(false);
        sim = b.get();
        sim->addDevice(btMic());
        sim->addDevice(usbMic());
        sim->addDevice(laptopMic());
        sim->addDevice(auxOut());
        sim->addDevice(btSpeaker());
        sim->addDevice(usbSpeaker());
        eng.reset(new AudioEngine(std::move(b)));
        eng->setTimeSource([this] { return sim->now(); });
        std::string err;
        CHECK(eng->initialize(&err));
        EngineSettings s = eng->settings();
        s.sampleRate = engineRate;
        s.bufferMs = bufferMs;
        s.masterVolume = 1.0f;
        eng->setSettings(s);
    }

    void enableIn(const char* id, bool on = true)
    {
        InputConfig c;
        CHECK(eng->inputConfig(id, &c));
        c.enabled = on;
        CHECK(eng->setInputConfig(c));
    }
    void enableOut(const char* id, bool on = true)
    {
        OutputConfig c;
        CHECK(eng->outputConfig(id, &c));
        c.enabled = on;
        CHECK(eng->setOutputConfig(c));
    }
    template <class F> void editIn(const char* id, F f)
    {
        InputConfig c;
        eng->inputConfig(id, &c);
        f(c);
        eng->setInputConfig(c);
    }
    template <class F> void editOut(const char* id, F f)
    {
        OutputConfig c;
        eng->outputConfig(id, &c);
        f(c);
        eng->setOutputConfig(c);
    }
    void route(const char* in, const char* out, bool on = true) { eng->setRoute(in, out, on); }

    void run(double seconds)
    {
        const int steps = static_cast<int>(seconds / 0.05 + 0.5);
        for (int i = 0; i < steps; ++i) {
            sim->advance(0.05);
            eng->housekeeping();
        }
    }
    std::vector<float> record(const char* out, double seconds)
    {
        sim->setRecording(out, true);
        run(seconds);
        auto r = sim->takeRecording(out);
        sim->setRecording(out, false);
        return r;
    }
    int rate(const char* out) const
    {
        for (const auto& o : eng->snapshot().outputs)
            if (o.info.id == out)
                return o.streamFormat.sampleRate;
        return 48000;
    }
    const OutputSlot& outSlot(const char* id) const { return eng->graph().outputs[eng->outputSlot(id)]; }
    InputView inView(const char* id) const
    {
        for (const auto& v : eng->snapshot().inputs)
            if (v.info.id == id)
                return v;
        return {};
    }
    OutputView outView(const char* id) const
    {
        for (const auto& v : eng->snapshot().outputs)
            if (v.info.id == id)
                return v;
        return {};
    }
};

// Level of a tone: mean of 0.25 s window measurements. Short windows make the level
// insensitive to the tiny (< 0.1 %) pitch offsets the drift controller applies while
// it locks onto two device clocks.
double level(const std::vector<float>& rec, double hz, int rate)
{
    const std::size_t win = static_cast<std::size_t>(rate / 4);
    double sum = 0.0;
    int n = 0;
    for (std::size_t s = rec.size() / 10; s + win <= rec.size() * 9 / 10; s += win, ++n)
        sum += toneAmplitude(rec, hz, rate, s, win);
    return n ? sum / n : 0.0;
}

// Leakage of a tone that must be absent: long window for maximum sensitivity.
double leakage(const std::vector<float>& rec, double hz, int rate)
{
    return toneAmplitude(rec, hz, rate, rec.size() / 10, rec.size() * 8 / 10);
}

void expectTone(const std::vector<float>& rec, double hz, int rate, double expectAmp, const char* what)
{
    const double a = level(rec, hz, rate);
    CHECK_MSG(std::fabs(toDb(a / expectAmp)) < 1.0,
              what << ": " << hz << " Hz at " << toDb(a / expectAmp) << " dB from expected");
}

void expectAbsent(const std::vector<float>& rec, double hz, int rate, const char* what)
{
    const double a = leakage(rec, hz, rate);
    CHECK_MSG(toDb(a / kAmp) < -60.0, what << ": " << hz << " Hz leaks at " << toDb(a / kAmp) << " dB");
}

void expectContinuous(const std::vector<float>& rec, const char* what)
{
    const std::size_t gap = longestSilentRun(rec, 1e-4f);
    CHECK_MSG(gap < 8, what << ": dropout of " << gap << " samples");
}

} // namespace

// Scenario 1: one Bluetooth microphone -> one AUX (wired) speaker.
TEST(scenario01_bt_mic_to_aux)
{
    Rig r;
    r.enableIn(kBtMic);
    r.enableOut(kAux);
    r.route(kBtMic, kAux);
    r.eng->startMonitoring();
    CHECK(r.inView(kBtMic).runState == RunState::Running);
    CHECK(r.outView(kAux).runState == RunState::Running);
    r.run(3.0);
    const auto u0 = r.outSlot(kAux).underruns.load();
    auto rec = r.record(kAux, 4.0);
    expectTone(rec, kBtHz, 48000, kAmp, "BT mic on AUX");
    expectContinuous(rec, "BT mic on AUX");
    CHECK_MSG(r.outSlot(kAux).underruns.load() == u0, "underruns during steady state");
}

// Scenario 2: one USB microphone -> one Bluetooth speaker (44.1 kHz, 16 bit).
TEST(scenario02_usb_mic_to_bt_speaker)
{
    Rig r;
    r.enableIn(kUsbMic);
    r.enableOut(kBtSpk);
    r.route(kUsbMic, kBtSpk);
    r.eng->startMonitoring();
    r.run(2.0);
    auto rec = r.record(kBtSpk, 3.0);
    expectTone(rec, kUsbHz, 44100, kAmp, "USB mic on BT speaker");
    expectContinuous(rec, "USB mic on BT speaker");
}

// Scenario 3: two microphones mixed into one speaker.
TEST(scenario03_two_mics_one_speaker)
{
    Rig r;
    r.enableIn(kBtMic);
    r.enableIn(kUsbMic);
    r.enableOut(kAux);
    r.route(kBtMic, kAux);
    r.route(kUsbMic, kAux);
    r.eng->startMonitoring();
    r.run(2.0);
    auto rec = r.record(kAux, 3.0);
    expectTone(rec, kBtHz, 48000, kAmp, "mix");
    expectTone(rec, kUsbHz, 48000, kAmp, "mix");
}

// Scenario 4: one microphone distributed to two speakers on different clocks.
TEST(scenario04_one_mic_two_speakers)
{
    Rig r;
    r.enableIn(kLaptopMic);
    r.enableOut(kAux);
    r.enableOut(kUsbSpk);
    r.route(kLaptopMic, kAux);
    r.route(kLaptopMic, kUsbSpk);
    r.eng->startMonitoring();
    r.run(2.0);
    r.sim->setRecording(kAux, true);
    r.sim->setRecording(kUsbSpk, true);
    r.run(3.0);
    auto a = r.sim->takeRecording(kAux);
    auto u = r.sim->takeRecording(kUsbSpk);
    expectTone(a, kLaptopHz, 48000, kAmp, "AUX");
    expectTone(u, kLaptopHz, 96000, kAmp, "USB speaker 96k");
    expectContinuous(a, "AUX");
    expectContinuous(u, "USB speaker");
}

// Scenario 5: two microphones -> two speakers with crossed, independent routes.
TEST(scenario05_two_mics_two_speakers_isolation)
{
    Rig r;
    r.enableIn(kBtMic);
    r.enableIn(kUsbMic);
    r.enableOut(kAux);
    r.enableOut(kBtSpk);
    r.route(kBtMic, kAux);
    r.route(kUsbMic, kBtSpk);
    r.eng->startMonitoring();
    r.run(2.0);
    r.sim->setRecording(kAux, true);
    r.sim->setRecording(kBtSpk, true);
    r.run(3.0);
    auto a = r.sim->takeRecording(kAux);
    auto b = r.sim->takeRecording(kBtSpk);
    expectTone(a, kBtHz, 48000, kAmp, "AUX");
    expectAbsent(a, kUsbHz, 48000, "AUX");
    expectTone(b, kUsbHz, 44100, kAmp, "BT speaker");
    expectAbsent(b, kBtHz, 44100, "BT speaker");
}

// Scenario 6: three microphones -> three speakers, arbitrary matrix.
TEST(scenario06_three_mics_multiple_speakers)
{
    Rig r;
    for (auto id : {kBtMic, kUsbMic, kLaptopMic})
        r.enableIn(id);
    for (auto id : {kAux, kBtSpk, kUsbSpk})
        r.enableOut(id);
    // Matrix:          AUX  BT-SPK  USB-SPK
    //   BT mic         ON    OFF     ON
    //   USB mic        OFF   ON      OFF
    //   Laptop mic     ON    ON      OFF
    r.route(kBtMic, kAux);
    r.route(kBtMic, kUsbSpk);
    r.route(kUsbMic, kBtSpk);
    r.route(kLaptopMic, kAux);
    r.route(kLaptopMic, kBtSpk);
    r.eng->startMonitoring();
    r.run(2.0);
    for (auto id : {kAux, kBtSpk, kUsbSpk})
        r.sim->setRecording(id, true);
    r.run(3.0);
    auto a = r.sim->takeRecording(kAux), b = r.sim->takeRecording(kBtSpk), u = r.sim->takeRecording(kUsbSpk);
    expectTone(a, kBtHz, 48000, kAmp, "AUX");
    expectAbsent(a, kUsbHz, 48000, "AUX");
    expectTone(a, kLaptopHz, 48000, kAmp, "AUX");
    expectAbsent(b, kBtHz, 44100, "BT spk");
    expectTone(b, kUsbHz, 44100, kAmp, "BT spk");
    expectTone(b, kLaptopHz, 44100, kAmp, "BT spk");
    expectTone(u, kBtHz, 96000, kAmp, "USB spk");
    expectAbsent(u, kUsbHz, 96000, "USB spk");
    expectAbsent(u, kLaptopHz, 96000, "USB spk");
    CHECK(r.eng->routing().size() == 5);
}

// Scenario 7: Bluetooth microphone disconnects and reconnects (same endpoint ID).
TEST(scenario07_bluetooth_disconnect_reconnect)
{
    Rig r;
    r.enableIn(kBtMic);
    r.enableOut(kAux);
    r.route(kBtMic, kAux);
    r.eng->startMonitoring();
    r.run(2.0);
    r.sim->disconnect(kBtMic);
    r.run(1.0);
    const InputView v = r.inView(kBtMic);
    CHECK(v.runState == RunState::WaitingForDevice);
    CHECK(!v.present);
    CHECK(v.config.enabled);                           // user choice kept
    CHECK(r.eng->routing().isRouted(kBtMic, kAux));    // route kept
    CHECK(r.outView(kAux).runState == RunState::Running); // speaker unaffected, no substitution
    auto silent = r.record(kAux, 1.0);
    expectAbsent(silent, kBtHz, 48000, "while disconnected");

    r.sim->reconnect(kBtMic);
    r.run(3.0);
    CHECK(r.inView(kBtMic).runState == RunState::Running);
    auto rec = r.record(kAux, 3.0);
    expectTone(rec, kBtHz, 48000, kAmp, "after reconnect");
    expectContinuous(rec, "after reconnect");
}

// Scenario 8: USB speaker removed while another output keeps playing.
TEST(scenario08_usb_output_removed)
{
    Rig r;
    r.enableIn(kLaptopMic);
    r.enableOut(kAux);
    r.enableOut(kUsbSpk);
    r.route(kLaptopMic, kAux);
    r.route(kLaptopMic, kUsbSpk);
    r.eng->startMonitoring();
    r.run(2.0);
    r.sim->setRecording(kAux, true);
    r.run(1.0);
    r.sim->disconnect(kUsbSpk);
    r.run(2.0);
    auto a = r.sim->takeRecording(kAux);
    r.sim->setRecording(kAux, false);
    expectContinuous(a, "AUX while USB speaker is removed");
    CHECK(r.outView(kUsbSpk).runState == RunState::WaitingForDevice);
    // A USB microphone removal behaves the same way for inputs.
    r.enableIn(kUsbMic);
    r.route(kUsbMic, kAux);
    r.run(1.0);
    r.sim->disconnect(kUsbMic);
    r.run(1.0);
    CHECK(r.inView(kUsbMic).runState == RunState::WaitingForDevice);
    r.sim->reconnect(kUsbSpk);
    r.run(3.0);
    CHECK(r.outView(kUsbSpk).runState == RunState::Running);
    auto u = r.record(kUsbSpk, 2.0);
    expectTone(u, kLaptopHz, 96000, kAmp, "USB speaker after reconnect");
}

// Scenario 9: different engine sample rates with mixed device rates.
TEST(scenario09_engine_sample_rates)
{
    for (int engineRate : {44100, 48000, 96000}) {
        Rig r(engineRate, 30);
        r.enableIn(kBtMic);    // 16 kHz
        r.enableIn(kUsbMic);   // 44.1 kHz
        r.enableOut(kUsbSpk);  // 96 kHz
        r.enableOut(kBtSpk);   // 44.1 kHz
        r.route(kBtMic, kUsbSpk);
        r.route(kUsbMic, kUsbSpk);
        r.route(kUsbMic, kBtSpk);
        r.eng->startMonitoring();
        r.run(2.0);
        r.sim->setRecording(kUsbSpk, true);
        r.sim->setRecording(kBtSpk, true);
        r.run(2.0);
        auto u = r.sim->takeRecording(kUsbSpk), b = r.sim->takeRecording(kBtSpk);
        expectTone(u, kBtHz, 96000, kAmp, "engine rate run");
        expectTone(u, kUsbHz, 96000, kAmp, "engine rate run");
        expectTone(b, kUsbHz, 44100, kAmp, "engine rate run");
        expectAbsent(b, kBtHz, 44100, "engine rate run");
    }
    // Changing the rate while streaming restarts the streams transparently.
    Rig r(48000);
    r.enableIn(kUsbMic);
    r.enableOut(kAux);
    r.route(kUsbMic, kAux);
    r.eng->startMonitoring();
    r.run(1.0);
    EngineSettings s = r.eng->settings();
    s.sampleRate = 96000;
    r.eng->setSettings(s);
    r.run(2.0);
    CHECK(r.eng->graph().engineRate.load() == 96000);
    auto rec = r.record(kAux, 2.0);
    expectTone(rec, kUsbHz, 48000, kAmp, "after rate change");
}

// Scenario 10: mute / unmute at every level (input, solo, output, global).
TEST(scenario10_mute_unmute_solo)
{
    Rig r;
    r.enableIn(kBtMic);
    r.enableIn(kUsbMic);
    r.enableOut(kAux);
    r.route(kBtMic, kAux);
    r.route(kUsbMic, kAux);
    r.eng->startMonitoring();
    r.run(2.0);

    r.editIn(kBtMic, [](InputConfig& c) { c.mute = true; });
    r.run(0.3);
    auto m = r.record(kAux, 1.5);
    expectAbsent(m, kBtHz, 48000, "input muted");
    expectTone(m, kUsbHz, 48000, kAmp, "other input unaffected");

    r.editIn(kBtMic, [](InputConfig& c) { c.mute = false; });
    r.run(0.3);
    expectTone(r.record(kAux, 1.5), kBtHz, 48000, kAmp, "input unmuted");

    r.editIn(kUsbMic, [](InputConfig& c) { c.solo = true; });
    r.run(0.3);
    auto s = r.record(kAux, 1.5);
    expectAbsent(s, kBtHz, 48000, "solo other");
    expectTone(s, kUsbHz, 48000, kAmp, "solo");
    r.editIn(kUsbMic, [](InputConfig& c) { c.solo = false; });

    r.editOut(kAux, [](OutputConfig& c) { c.mute = true; });
    r.run(0.3);
    CHECK(peak(r.record(kAux, 1.0)) < 1e-6);
    r.editOut(kAux, [](OutputConfig& c) { c.mute = false; });

    r.eng->setGlobalMute(true);
    r.run(0.3);
    CHECK(peak(r.record(kAux, 1.0)) < 1e-6);
    r.eng->setGlobalMute(false);
    r.run(0.3);
    auto back = r.record(kAux, 1.5);
    expectTone(back, kBtHz, 48000, kAmp, "global unmute");
    expectTone(back, kUsbHz, 48000, kAmp, "global unmute");

    // Pause keeps devices open but silent; resume restores audio.
    r.eng->setPaused(true);
    r.run(0.5);
    CHECK(peak(r.record(kAux, 1.0)) < 1e-6);
    CHECK(r.outView(kAux).streamOpen);
    r.eng->setPaused(false);
    r.run(1.0);
    expectTone(r.record(kAux, 1.5), kUsbHz, 48000, kAmp, "resume");
}

// Scenario 11: individual channel volume, gain, route gain, output and master volume.
TEST(scenario11_individual_volumes)
{
    Rig r;
    r.enableIn(kBtMic);
    r.enableIn(kUsbMic);
    r.enableOut(kAux);
    r.route(kBtMic, kAux);
    r.route(kUsbMic, kAux);
    r.eng->startMonitoring();
    r.run(2.0);
    r.editIn(kBtMic, [](InputConfig& c) { c.volume = 0.5f; });     // -6 dB
    r.editIn(kUsbMic, [](InputConfig& c) { c.gainDb = 3.0f; });    // +3 dB
    r.run(0.5);
    auto a = r.record(kAux, 2.0);
    CHECK_NEAR(toDb(level(a, kBtHz, 48000) / kAmp), -6.02, 0.3);
    CHECK_NEAR(toDb(level(a, kUsbHz, 48000) / kAmp), 3.0, 0.3);

    r.eng->setRouteGain(kUsbMic, kAux, 0.5f);
    r.editOut(kAux, [](OutputConfig& c) { c.volume = 0.5f; });
    r.eng->setMasterVolume(0.5f);
    r.run(0.5);
    auto b = r.record(kAux, 2.0);
    CHECK_NEAR(toDb(level(b, kBtHz, 48000) / kAmp), -6.02 - 12.04, 0.3);
    CHECK_NEAR(toDb(level(b, kUsbHz, 48000) / kAmp), 3.0 - 6.02 - 12.04, 0.3);
}

// Scenario 12: routing matrix changes while streaming.
TEST(scenario12_routing_changes_while_streaming)
{
    Rig r;
    for (auto id : {kBtMic, kUsbMic, kLaptopMic})
        r.enableIn(id);
    r.enableOut(kAux);
    r.enableOut(kBtSpk);
    r.eng->startMonitoring();
    const char* ins[] = {kBtMic, kUsbMic, kLaptopMic};
    const char* outs[] = {kAux, kBtSpk};
    // 40 random toggles while audio flows.
    unsigned seed = 12345;
    bool state[3][2] = {};
    for (int k = 0; k < 40; ++k) {
        seed = seed * 1103515245u + 12345u;
        const int i = (seed >> 8) % 3, o = (seed >> 16) % 2;
        state[i][o] = !state[i][o];
        r.route(ins[i], outs[o], state[i][o]);
        r.run(0.15);
    }
    // Final state must be exactly reflected in the audio.
    r.run(1.5);
    const double hz[] = {kBtHz, kUsbHz, kLaptopHz};
    for (int o = 0; o < 2; ++o) {
        const int rate = o == 0 ? 48000 : 44100;
        auto rec = r.record(outs[o], 2.0);
        for (int i = 0; i < 3; ++i) {
            CHECK(r.eng->routing().isRouted(ins[i], outs[o]) == state[i][o]);
            if (state[i][o])
                expectTone(rec, hz[i], rate, kAmp, "final matrix");
            else
                expectAbsent(rec, hz[i], rate, "final matrix");
        }
    }
}

// Scenario 13: 30 minutes of continuous operation (virtual time) with drifting,
// bursty Bluetooth clocks. Checks: no dropouts after the initial adaptation, latency
// does not creep, the drift controller locks.
TEST(scenario13_thirty_minutes_continuous)
{
    Rig r(48000, 20);
    r.enableIn(kBtMic);
    r.enableIn(kLaptopMic);
    r.enableOut(kAux);
    r.enableOut(kBtSpk);
    r.route(kBtMic, kAux);
    r.route(kBtMic, kBtSpk);
    r.route(kLaptopMic, kBtSpk);
    r.eng->startMonitoring();
    r.run(60.0); // first minute: adaptation
    const auto uA = r.outSlot(kAux).underruns.load(), uB = r.outSlot(kBtSpk).underruns.load();
    const auto rsA = r.outSlot(kAux).resyncs.load(), rsB = r.outSlot(kBtSpk).resyncs.load();
    const float latA0 = r.outSlot(kAux).routeLatencyMs.load();
    std::printf("    after 1 min: AUX latency %.1f ms, drift %.0f ppm; BT latency %.1f ms, drift %.0f ppm\n", latA0,
                r.outSlot(kAux).driftPpm.load(), r.outSlot(kBtSpk).routeLatencyMs.load(),
                r.outSlot(kBtSpk).driftPpm.load());
    for (int minute = 1; minute < 30; ++minute)
        r.run(60.0);
    std::printf("    after 30 min: AUX latency %.1f ms, drift %.0f ppm, underruns %llu; BT latency %.1f ms, drift %.0f "
                "ppm, underruns %llu\n",
                r.outSlot(kAux).routeLatencyMs.load(), r.outSlot(kAux).driftPpm.load(),
                static_cast<unsigned long long>(r.outSlot(kAux).underruns.load()),
                r.outSlot(kBtSpk).routeLatencyMs.load(), r.outSlot(kBtSpk).driftPpm.load(),
                static_cast<unsigned long long>(r.outSlot(kBtSpk).underruns.load()));
    CHECK_MSG(r.outSlot(kAux).underruns.load() == uA, "AUX underruns after adaptation");
    CHECK_MSG(r.outSlot(kBtSpk).underruns.load() == uB, "BT speaker underruns after adaptation");
    CHECK(r.outSlot(kAux).resyncs.load() == rsA);
    CHECK(r.outSlot(kBtSpk).resyncs.load() == rsB);
    CHECK(r.outSlot(kAux).routeLatencyMs.load() < 120.0f);
    // BT mic +150 ppm vs AUX -35 ppm: the controller must have learned ~185 ppm.
    CHECK_NEAR(r.outSlot(kAux).driftPpm.load(), 185.0, 40.0);
    auto rec = r.record(kAux, 5.0);
    expectTone(rec, kBtHz, 48000, kAmp, "after 30 minutes");
    expectContinuous(rec, "after 30 minutes");
}

// Device that cannot be opened (e.g. exclusive use by another app) is retried.
TEST(engine_open_failure_retry)
{
    Rig r;
    r.sim->setFailOpen(kAux, true);
    r.enableIn(kUsbMic);
    r.enableOut(kAux);
    r.route(kUsbMic, kAux);
    r.eng->startMonitoring();
    CHECK(r.outView(kAux).runState == RunState::Error);
    CHECK(!r.outView(kAux).message.empty());
    r.sim->setFailOpen(kAux, false);
    r.run(3.0);
    CHECK(r.outView(kAux).runState == RunState::Running);
    expectTone(r.record(kAux, 2.0), kUsbHz, 48000, kAmp, "after retry");
}

// Stop releases every device; config survives; start restores.
TEST(engine_stop_start_and_config_roundtrip)
{
    Rig r;
    r.enableIn(kUsbMic);
    r.enableOut(kAux);
    r.route(kUsbMic, kAux);
    r.eng->startMonitoring();
    r.run(1.0);
    CHECK(r.sim->openStreamCount() == 2);
    r.eng->stopMonitoring();
    CHECK(r.sim->openStreamCount() == 0);
    const EngineConfig cfg = r.eng->config();
    CHECK(cfg.routes.size() == 1);
    CHECK(cfg.inputs.size() == 1 && cfg.outputs.size() == 1);

    Rig r2;
    r2.eng->loadConfig(cfg);
    r2.eng->startMonitoring();
    r2.run(2.0);
    expectTone(r2.record(kAux, 2.0), kUsbHz, 48000, kAmp, "restored config");
}

// The feedback guard mutes an output that is driven far into the limiter for a
// sustained time, and the user can re-arm it.
TEST(engine_feedback_guard)
{
    Rig r;
    r.enableIn(kLaptopMic);
    r.enableOut(kAux);
    CHECK(r.eng->feedbackRisk(kLaptopMic, kAux) != AudioRoutingMatrix::FeedbackRisk::None);
    CHECK(r.eng->feedbackRisk(kBtMic, kAux) == AudioRoutingMatrix::FeedbackRisk::None);
    r.route(kLaptopMic, kAux);
    r.editIn(kLaptopMic, [](InputConfig& c) { c.gainDb = 30.0f; }); // 0.2 * 31.6 = +16 dBFS
    r.eng->startMonitoring();
    r.run(4.0);
    CHECK(r.outSlot(kAux).feedbackTripped.load());
    CHECK(peak(r.record(kAux, 0.5)) < 1e-4);
    r.editIn(kLaptopMic, [](InputConfig& c) { c.gainDb = 0.0f; });
    r.eng->clearFeedbackTrip(kAux);
    r.run(1.0);
    expectTone(r.record(kAux, 1.5), kLaptopHz, 48000, kAmp, "after re-arm");
}

// Real threads: every simulated device on its own thread while the control thread
// hammers the engine with parameter and routing changes (run under TSan in CI).
TEST(threaded_stress_routing_changes)
{
    auto b = std::make_unique<SimulatedBackend>(true);
    SimulatedBackend* sim = b.get();
    sim->addDevice(btMic());
    sim->addDevice(usbMic());
    sim->addDevice(laptopMic());
    sim->addDevice(auxOut());
    sim->addDevice(btSpeaker());
    AudioEngine eng(std::move(b));
    std::string err;
    CHECK(eng.initialize(&err));
    for (auto id : {kBtMic, kUsbMic, kLaptopMic}) {
        InputConfig c;
        eng.inputConfig(id, &c);
        c.enabled = true;
        eng.setInputConfig(c);
    }
    for (auto id : {kAux, kBtSpk}) {
        OutputConfig c;
        eng.outputConfig(id, &c);
        c.enabled = true;
        eng.setOutputConfig(c);
    }
    eng.startMonitoring();
    const auto t0 = std::chrono::steady_clock::now();
    unsigned seed = 7;
    int ops = 0;
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(2500)) {
        seed = seed * 1103515245u + 12345u;
        const char* ins[] = {kBtMic, kUsbMic, kLaptopMic};
        const char* outs[] = {kAux, kBtSpk};
        const char* in = ins[(seed >> 8) % 3];
        const char* out = outs[(seed >> 12) % 2];
        switch ((seed >> 16) % 5) {
        case 0: eng.setRoute(in, out, (seed >> 20) & 1); break;
        case 1: eng.setRouteGain(in, out, ((seed >> 20) % 100) / 50.0f); break;
        case 2: { InputConfig c; eng.inputConfig(in, &c); c.mute = !c.mute; c.noiseSuppression = (seed >> 21) & 1; eng.setInputConfig(c); } break;
        case 3: { OutputConfig c; eng.outputConfig(out, &c); c.volume = ((seed >> 20) % 100) / 60.0f; eng.setOutputConfig(c); } break;
        case 4: eng.housekeeping(); break;
        }
        ++ops;
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    CHECK(sim->renderedFrames(kAux) > 48000);
    CHECK(sim->renderedFrames(kBtSpk) > 44100);
    std::printf("    %d control operations while streaming\n", ops);
    eng.stopMonitoring();
    CHECK(sim->openStreamCount() == 0);
}

// "Test" button: an identification chime on exactly one output, even with no routes
// and with the output fader down.
TEST(engine_test_tone_identifies_output)
{
    Rig r;
    r.enableOut(kAux);
    r.enableOut(kUsbSpk);
    CHECK(!r.eng->playTestTone(kAux)); // not streaming yet
    r.eng->startMonitoring();
    r.run(0.5);
    r.editOut(kAux, [](OutputConfig& c) { c.volume = 0.0f; });
    r.sim->setRecording(kAux, true);
    r.sim->setRecording(kUsbSpk, true);
    CHECK(r.eng->playTestTone(kAux));
    r.run(1.0);
    auto a = r.sim->takeRecording(kAux), u = r.sim->takeRecording(kUsbSpk);
    CHECK_MSG(peak(a) > 0.05, "test chime missing on the selected output, peak " << peak(a));
    CHECK_MSG(peak(u) < 1e-6, "test chime leaked to another output");
    CHECK_MSG(toneAmplitude(a, 880.0, 48000, 0, 48000 / 4) > 0.02, "first note (880 Hz) not found");
}

// "Bypass Windows audio effects" reaches the stream and re-opens a running device.
TEST(engine_bypass_effects_reopens_stream)
{
    Rig r;
    r.enableIn(kUsbMic);
    r.enableOut(kAux);
    r.route(kUsbMic, kAux);
    r.eng->startMonitoring();
    r.run(1.0);
    CHECK(!r.sim->lastOptions(kAux).rawMode);
    r.editOut(kAux, [](OutputConfig& c) { c.bypassEffects = true; });
    r.editIn(kUsbMic, [](InputConfig& c) { c.bypassEffects = true; });
    r.run(1.0);
    CHECK(r.sim->lastOptions(kAux).rawMode);
    CHECK(r.sim->lastOptions(kUsbMic).rawMode);
    CHECK(r.outView(kAux).runState == RunState::Running);
    r.run(1.0);
    expectTone(r.record(kAux, 2.0), kUsbHz, 48000, kAmp, "after re-open in raw mode");
}
