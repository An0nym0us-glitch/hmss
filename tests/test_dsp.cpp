// Unit tests of the platform-independent DSP / buffering building blocks.
#include "SignalAnalysis.h"
#include "TestFramework.h"

#include "core/AudioProcessing.h"
#include "core/AudioResampler.h"
#include "core/AudioRoutingMatrix.h"
#include "core/BroadcastRing.h"
#include "core/SampleConvert.h"

#include <random>

using namespace ab;
using namespace abtest;

namespace {

std::vector<float> sineStereo(double hz, double rate, std::size_t frames, float amp)
{
    std::vector<float> v(frames * 2);
    for (std::size_t i = 0; i < frames; ++i)
        v[2 * i] = v[2 * i + 1] = amp * static_cast<float>(std::sin(2.0 * kPi * hz * i / rate));
    return v;
}

std::vector<float> leftChannel(const std::vector<float>& st)
{
    std::vector<float> v(st.size() / 2);
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = st[2 * i];
    return v;
}

// Push-mode conversion of a whole signal in device-sized blocks.
std::vector<float> resamplePush(const std::vector<float>& in, double from, double to, std::size_t block)
{
    AudioResampler r;
    r.configure(2, from, to, block);
    std::vector<float> out;
    std::vector<float> tmp(r.maxOutputFor(block) * 2 + 64);
    for (std::size_t pos = 0; pos < in.size() / 2; pos += block) {
        const std::size_t n = std::min(block, in.size() / 2 - pos);
        const std::size_t got = r.processPush(in.data() + pos * 2, n, tmp.data(), tmp.size() / 2);
        out.insert(out.end(), tmp.begin(), tmp.begin() + static_cast<std::ptrdiff_t>(got * 2));
    }
    return out;
}

void checkConversion(double from, double to, double hz)
{
    const std::size_t frames = static_cast<std::size_t>(from * 2);
    const auto in = sineStereo(hz, from, frames, 0.5f);
    const auto out = leftChannel(resamplePush(in, from, to, 441));
    // The kernel look-ahead (24 input frames) is held back at the end of the stream.
    CHECK_NEAR(static_cast<double>(out.size()), frames * to / from, 24.0 * to / from + 4.0);
    const std::size_t skip = static_cast<std::size_t>(to * 0.1);
    const std::size_t n = out.size() - 2 * skip;
    const double a = toneAmplitude(out, hz, to, skip, n);
    CHECK_MSG(std::fabs(toDb(a / 0.5)) < 0.2, from << "->" << to << " Hz amplitude " << toDb(a / 0.5) << " dB");
    // Residual after removing the ideal tone: noise + distortion must be far below the tone.
    double bestErr = 1e9;
    // Find the phase by least squares on sin/cos basis.
    double ss = 0, sc = 0, cc = 0, xs = 0, xc = 0;
    for (std::size_t i = skip; i < skip + n; ++i) {
        const double s = std::sin(2 * kPi * hz * i / to), c = std::cos(2 * kPi * hz * i / to);
        ss += s * s; sc += s * c; cc += c * c; xs += out[i] * s; xc += out[i] * c;
    }
    const double det = ss * cc - sc * sc;
    const double A = (xs * cc - xc * sc) / det, B = (xc * ss - xs * sc) / det;
    double err = 0;
    for (std::size_t i = skip; i < skip + n; ++i) {
        const double e = out[i] - (A * std::sin(2 * kPi * hz * i / to) + B * std::cos(2 * kPi * hz * i / to));
        err += e * e;
    }
    bestErr = std::sqrt(err / n);
    const double snr = toDb(0.5 / std::sqrt(2.0) / bestErr);
    CHECK_MSG(snr > 60.0, from << "->" << to << " SNR " << snr << " dB");
}

} // namespace

TEST(resampler_rate_conversions)
{
    checkConversion(44100, 48000, 1000);
    checkConversion(48000, 44100, 1000);
    checkConversion(16000, 48000, 440);   // Bluetooth HFP wide-band mic -> engine
    checkConversion(8000, 48000, 440);    // Bluetooth HFP narrow-band mic -> engine
    checkConversion(48000, 96000, 3000);
    checkConversion(96000, 48000, 3000);
    checkConversion(48000, 48000, 1000);  // pure delay path
}

TEST(resampler_pull_mode_variable_ratio)
{
    // Pull exact block sizes while the ratio wanders by +-500 ppm (drift correction).
    AudioResampler r;
    r.configure(2, 48000, 48000, 512);
    const auto in = sineStereo(1000, 48000, 48000 * 3, 0.5f);
    std::size_t pos = 0;
    std::vector<float> out, tmp(512 * 2);
    for (int k = 0; pos < in.size() / 2 - 2000; ++k) {
        r.setRatio(1.0 + 0.0005 * std::sin(k * 0.01));
        const std::size_t need = r.requiredInput(480);
        r.processPull(in.data() + pos * 2, need, tmp.data(), 480);
        pos += need;
        out.insert(out.end(), tmp.begin(), tmp.begin() + 960);
    }
    const auto l = leftChannel(out);
    // The ratio sweep wanders the time base on purpose, so measure the amplitude in
    // short windows (phase-insensitive RMS) rather than with one long coherent DFT.
    for (std::size_t w = 4800; w + 4800 < l.size(); w += 4800) {
        const double a = rms(l, w, 4800) * std::sqrt(2.0);
        CHECK_MSG(std::fabs(toDb(a / 0.5)) < 0.1, "window " << w << " amplitude " << toDb(a / 0.5));
    }
    CHECK(longestSilentRun(std::vector<float>(l.begin() + 100, l.end()), 1e-5f) < 3);
}

TEST(sample_format_roundtrip)
{
    const SampleType types[] = {SampleType::Float32, SampleType::Int16, SampleType::Int24Packed,
                                SampleType::Int24In32, SampleType::Int32};
    std::vector<float> src = {0.0f, 0.5f, -0.5f, 0.999f, -1.0f, 0.123456f, 1.5f, -2.0f};
    for (SampleType t : types) {
        StreamFormat f;
        f.sampleType = t;
        f.channels = 2;
        std::vector<std::uint8_t> raw(src.size() * f.bytesPerSample());
        floatToDevice(src.data(), t, 2, src.size() / 2, raw.data());
        std::vector<float> back(src.size());
        deviceToFloat(raw.data(), t, 2, src.size() / 2, back.data());
        const double tol = t == SampleType::Int16 ? 1.0 / 16000 : 1e-5;
        for (std::size_t i = 0; i < src.size(); ++i) {
            const float expect = std::max(-1.0f, std::min(1.0f, src[i]));
            CHECK_MSG(std::fabs(back[i] - expect) <= tol, toString(t) << " idx " << i << " " << back[i]);
        }
    }
}

TEST(channel_mapping)
{
    // Mono device -> stereo bus duplicates.
    float mono[3] = {0.1f, 0.2f, 0.3f};
    float st[6];
    channelsToStereo(mono, 1, 0, 3, false, st);
    CHECK(st[0] == 0.1f && st[1] == 0.1f && st[4] == 0.3f && st[5] == 0.3f);
    // 4-capsule mic array summed to mono.
    float arr[4] = {0.4f, 0.0f, 0.2f, 0.2f};
    channelsToStereo(arr, 4, 0, 1, true, st);
    CHECK_NEAR(st[0], 0.2, 1e-6);
    CHECK_NEAR(st[1], 0.2, 1e-6);
    // Stereo bus -> 5.1 device: L/R on the front pair, silence elsewhere.
    const std::uint32_t mask51 = 0x3F;
    float bus[2] = {0.5f, -0.25f};
    float six[6];
    stereoToChannels(bus, 1, 6, mask51, six);
    CHECK(six[0] == 0.5f && six[1] == -0.25f && six[2] == 0.0f && six[3] == 0.0f);
    // Stereo bus -> mono device averages.
    float one[1];
    stereoToChannels(bus, 1, 1, 0, one);
    CHECK_NEAR(one[0], 0.125, 1e-6);
}

TEST(broadcast_ring_multiple_readers)
{
    BroadcastRing ring;
    ring.allocate(1 << 15, 2);
    std::vector<float> block(256 * 2);
    for (std::size_t i = 0; i < block.size(); ++i)
        block[i] = static_cast<float>(i);
    ring.write(block.data(), 256);
    std::vector<float> a(256 * 2), b(100 * 2);
    CHECK(ring.read(0, a.data(), 256));
    CHECK(a == block);
    CHECK(ring.read(10, b.data(), 100)); // second, independent reader
    CHECK(b[0] == block[20]);
    CHECK(!ring.read(200, b.data(), 100)); // not yet written
    // Fill far beyond capacity: an old cursor must be reported as overwritten.
    for (int k = 0; k < 200; ++k)
        ring.write(block.data(), 256);
    CHECK(!ring.read(0, a.data(), 256));
    const std::uint64_t w = ring.writeIndex();
    CHECK(ring.read(w - 256, a.data(), 256));
}

TEST(noise_suppressor_reduces_noise_keeps_tone)
{
    const double rate = 48000;
    NoiseSuppressor ns;
    ns.prepare(rate);
    ns.setStrength(0.8f);
    std::mt19937 rng(1);
    std::normal_distribution<float> g(0.0f, 0.02f);
    const std::size_t total = static_cast<std::size_t>(rate * 6);
    std::vector<float> st(total * 2);
    // 0-3 s: noise only (fan / hiss). 3-6 s: noise + 1 kHz "voice" tone.
    for (std::size_t i = 0; i < total; ++i) {
        float v = g(rng);
        if (i >= total / 2)
            v += 0.2f * static_cast<float>(std::sin(2 * kPi * 1000 * i / rate));
        st[2 * i] = st[2 * i + 1] = v;
    }
    const auto inL = leftChannel(st);
    for (std::size_t pos = 0; pos < total; pos += 480)
        ns.processStereo(st.data() + pos * 2, std::min<std::size_t>(480, total - pos), true);
    const auto outL = leftChannel(st);
    const std::size_t s0 = static_cast<std::size_t>(rate * 1.5), n0 = static_cast<std::size_t>(rate * 1.4);
    const double noiseIn = rms(inL, s0, n0), noiseOut = rms(outL, s0, n0);
    const double reduction = toDb(noiseIn / noiseOut);
    CHECK_MSG(reduction > 10.0, "noise reduction " << reduction << " dB");
    const std::size_t s1 = static_cast<std::size_t>(rate * 4), n1 = static_cast<std::size_t>(rate * 1.5);
    const double toneIn = toneAmplitude(inL, 1000, rate, s1, n1), toneOut = toneAmplitude(outL, 1000, rate, s1, n1);
    CHECK_MSG(std::fabs(toDb(toneOut / toneIn)) < 3.0, "tone change " << toDb(toneOut / toneIn) << " dB");
}

TEST(high_pass_filter)
{
    HighPassFilter h;
    h.prepare(48000, 80);
    auto low = sineStereo(20, 48000, 48000, 0.5f);
    auto mid = sineStereo(1000, 48000, 48000, 0.5f);
    h.processStereo(low.data(), 48000);
    h.reset();
    h.processStereo(mid.data(), 48000);
    const double aLow = toneAmplitude(leftChannel(low), 20, 48000, 24000);
    const double aMid = toneAmplitude(leftChannel(mid), 1000, 48000, 24000);
    CHECK_MSG(toDb(aLow / 0.5) < -20.0, "20 Hz attenuation " << toDb(aLow / 0.5));
    CHECK_MSG(std::fabs(toDb(aMid / 0.5)) < 0.1, "1 kHz change " << toDb(aMid / 0.5));
}

TEST(noise_gate)
{
    NoiseGate gate;
    gate.prepare(48000);
    gate.setThresholdDb(-40.0f);
    auto quiet = sineStereo(500, 48000, 48000, 0.003f); // -50 dBFS: below threshold
    gate.processStereo(quiet.data(), 48000);
    CHECK(!gate.isOpen());
    CHECK_MSG(toneAmplitude(leftChannel(quiet), 500, 48000, 24000) < 0.003 * 0.05, "gate did not close");
    auto loud = sineStereo(500, 48000, 48000, 0.1f);
    gate.processStereo(loud.data(), 48000);
    CHECK(gate.isOpen());
    CHECK_NEAR(toneAmplitude(leftChannel(loud), 500, 48000, 24000), 0.1, 0.005);
}

TEST(limiter_never_clips_and_feedback_guard)
{
    Limiter lim;
    lim.prepare(48000);
    auto hot = sineStereo(800, 48000, 48000 / 2, 3.0f); // +9.5 dBFS for 0.5 s
    bool tripped = lim.processStereo(hot.data(), hot.size() / 2);
    CHECK(peak(hot) <= 0.8913 + 1e-4);
    CHECK(!tripped); // short burst is not feedback
    // Sustained howl: 3 s far above full scale -> guard trips.
    lim.reset();
    auto howl = sineStereo(2500, 48000, 48000 * 3, 4.0f);
    for (std::size_t pos = 0; pos < howl.size() / 2; pos += 480)
        tripped = lim.processStereo(howl.data() + pos * 2, 480) || tripped;
    CHECK(tripped);
    // Guard disabled -> never trips.
    lim.reset();
    lim.setFeedbackGuard(false);
    auto howl2 = sineStereo(2500, 48000, 48000 * 3, 4.0f);
    CHECK(!lim.processStereo(howl2.data(), howl2.size() / 2));
}

TEST(device_name_disambiguation)
{
    std::vector<DeviceInfo> d(4);
    d[0].id = "a"; d[0].friendlyName = "Microphone (USB Audio Device)"; d[0].adapterName = "USB Audio Device";
    d[1].id = "b"; d[1].friendlyName = "Microphone (USB Audio Device)"; d[1].adapterName = "USB Audio Device";
    d[2].id = "c"; d[2].friendlyName = "Speakers (Realtek(R) Audio)"; d[2].flow = DeviceFlow::Render;
    d[3].id = "d"; d[3].friendlyName = "Microphone (USB Audio Device)"; d[3].flow = DeviceFlow::Render; // other flow
    for (auto& x : d) x.state = DeviceState::Active;
    disambiguateDisplayNames(d);
    CHECK(d[0].displayName != d[1].displayName);
    CHECK(d[0].displayName.find("Microphone (USB Audio Device)") == 0); // real name kept as prefix
    CHECK(d[2].displayName == "Speakers (Realtek(R) Audio)");
    CHECK(d[3].displayName == "Microphone (USB Audio Device)"); // unique within its flow
}

TEST(routing_matrix_and_feedback_risk)
{
    AudioRoutingMatrix m;
    CHECK(m.setRoute("mic1", "spk1", true));
    CHECK(!m.setRoute("mic1", "spk1", true));
    CHECK(m.setRoute("mic1", "spk2", true));
    CHECK(m.setRoute("mic2", "spk1", true));
    CHECK(m.routesFrom("mic1").size() == 2);
    CHECK(m.routesTo("spk1").size() == 2);
    CHECK(m.setRouteGain("mic1", "spk2", 0.5f));
    CHECK_NEAR(m.find("mic1", "spk2")->gain, 0.5, 1e-6);
    m.removeDevice("spk1");
    CHECK(m.size() == 1);

    DeviceInfo headsetMic, headsetSpk, laptopMic, laptopSpk, usbSpk;
    headsetMic.containerId = headsetSpk.containerId = "{BT-HEADSET}";
    headsetMic.bus = headsetSpk.bus = DeviceBus::Bluetooth;
    laptopMic.containerId = laptopSpk.containerId = "{00000000-0000-0000-FFFF-FFFFFFFFFFFF}";
    laptopMic.bus = laptopSpk.bus = DeviceBus::Internal;
    usbSpk.containerId = "{USB-SPK}";
    usbSpk.bus = DeviceBus::USB;
    CHECK(AudioRoutingMatrix::feedbackRisk(headsetMic, headsetSpk) == AudioRoutingMatrix::FeedbackRisk::SameDevice);
    CHECK(AudioRoutingMatrix::feedbackRisk(laptopMic, laptopSpk) == AudioRoutingMatrix::FeedbackRisk::SameComputer);
    CHECK(AudioRoutingMatrix::feedbackRisk(headsetMic, usbSpk) == AudioRoutingMatrix::FeedbackRisk::None);
}

TEST(device_bus_classification)
{
    bool hf = false;
    CHECK(classifyDeviceBus("{2}.\\\\?\\bthhfenum#bthhfpaudio#8&2c5d&0&97#{6994ad04-93ef-11d0-a3cc-00a0c9223196}",
                            "Headset Microphone (Earbuds)", "Earbuds", FormFactor::Headset, &hf) == DeviceBus::Bluetooth);
    CHECK(hf);
    CHECK(classifyDeviceBus("{2}.\\\\?\\bthenum#{0000110b-0000-1000-8000-00805f9b34fb}_vid&0001",
                            "Headphones (Earbuds Stereo)", "Earbuds Stereo", FormFactor::Headphones, &hf) == DeviceBus::Bluetooth);
    CHECK(!hf);
    CHECK(classifyDeviceBus("{2}.\\\\?\\usb#vid_046d&pid_0825&mi_02#6&1b2c#0000#{6994ad04}",
                            "Microphone (HD Webcam C270)", "HD Webcam C270", FormFactor::Microphone, &hf) == DeviceBus::USB);
    CHECK(classifyDeviceBus("{2}.\\\\?\\hdaudio#func_01&ven_10de&dev_009f#5&2f&0&0001#{6994ad04}",
                            "NVIDIA High Definition Audio", "", FormFactor::DigitalAudioDisplayDevice, &hf) == DeviceBus::Display);
    CHECK(classifyDeviceBus("{2}.\\\\?\\intelaudio#func_01&ven_10ec&dev_0257#5&1a#{6994ad04}",
                            "Speakers (Realtek(R) Audio)", "Realtek(R) Audio", FormFactor::Speakers, &hf) == DeviceBus::Internal);
    CHECK(classifyDeviceBus("{2}.\\\\?\\root#media#0000#{6994ad04}", "CABLE Output (VB-Audio Virtual Cable)", "",
                            FormFactor::Microphone, &hf) == DeviceBus::Virtual);
    // No topology information (endpoint not active): name fallbacks.
    CHECK(classifyDeviceBus("", "Headset (Earbuds Hands-Free AG Audio)", "", FormFactor::Headset, &hf) == DeviceBus::Bluetooth);
    CHECK(hf);
    CHECK(classifyDeviceBus("", "Microphone Array (Intel Smart Sound Technology)", "", FormFactor::Microphone, &hf) ==
          DeviceBus::Internal);
    CHECK(classifyDeviceBus("", "Something", "", FormFactor::Speakers, &hf) == DeviceBus::Unknown);
}
