#include "SimulatedBackend.h"

#include "core/SampleConvert.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

namespace ab {

struct SimulatedBackend::Device {
    SimDeviceSpec spec;
    DeviceState state = DeviceState::Active;
    bool failOpen = false;
    std::atomic<double> toneHz{440.0};
    std::atomic<float> toneAmp{0.25f};
    std::mutex recMutex;
    bool recording = false;
    std::vector<float> rec;
    std::atomic<std::uint64_t> rendered{0};
    Stream* stream = nullptr;
    StreamOptions lastOptions;
};

class SimulatedBackend::Stream : public IAudioStream {
public:
    Stream(SimulatedBackend* be, Device* dev, AudioInputProcessor* proc, AudioOutputMixer* mixer, double startTime)
        : be_(be), dev_(dev), proc_(proc), mixer_(mixer), rng_(std::hash<std::string>{}(dev->spec.id))
    {
        fmt_.sampleRate = dev->spec.sampleRate;
        fmt_.channels = dev->spec.channels;
        fmt_.sampleType = dev->spec.sampleType;
        fmt_.channelMask = dev->spec.channels == 2 ? (kSpeakerFrontLeft | kSpeakerFrontRight) : 0;
        actualRate_ = dev->spec.sampleRate * (1.0 + dev->spec.clockPpm * 1e-6);
        period_ = dev->spec.periodMs / 1000.0;
        std::uniform_real_distribution<double> u(0.0, period_);
        next_ = startTime + u(rng_); // random phase between devices
        const std::size_t maxFrames = static_cast<std::size_t>(actualRate_ * period_ * 4) + 16;
        raw_.resize(maxFrames * static_cast<std::size_t>(fmt_.bytesPerFrame()));
        fbuf_.resize(maxFrames * static_cast<std::size_t>(fmt_.channels));
    }

    ~Stream() override
    {
        stop_.store(true);
        if (thread_.joinable())
            thread_.join();
        std::lock_guard<std::mutex> lk(be_->mutex_);
        be_->streams_.erase(std::remove(be_->streams_.begin(), be_->streams_.end(), this), be_->streams_.end());
        if (dev_->stream == this)
            dev_->stream = nullptr;
    }

    void startThread()
    {
        thread_ = std::thread([this] {
            auto t0 = std::chrono::steady_clock::now();
            std::uint64_t k = 0;
            while (!stop_.load()) {
                ++k;
                std::this_thread::sleep_until(t0 + std::chrono::microseconds(static_cast<long long>(k * period_ * 1e6)));
                step();
            }
        });
    }

    // One device period.
    void step()
    {
        if (status_.load() != Status::Running)
            return;
        const double exact = actualRate_ * period_ + frac_;
        std::size_t n = static_cast<std::size_t>(exact);
        frac_ = exact - static_cast<double>(n);
        if (proc_) {
            pending_ += n;
            if (dev_->spec.burstyDelivery) {
                // Deliver irregularly: hold back 0..2 periods.
                std::uniform_int_distribution<int> d(0, 2);
                if (held_ < 2 && d(rng_) != 0) {
                    ++held_;
                    return;
                }
                held_ = 0;
            }
            n = pending_;
            pending_ = 0;
            generate(n);
            floatToDevice(fbuf_.data(), fmt_.sampleType, fmt_.channels, n, raw_.data());
            proc_->process(raw_.data(), n, false);
        } else {
            mixer_->render(raw_.data(), n);
            dev_->rendered.fetch_add(n);
            std::lock_guard<std::mutex> lk(dev_->recMutex);
            if (dev_->recording) {
                deviceToFloat(raw_.data(), fmt_.sampleType, fmt_.channels, n, fbuf_.data());
                for (std::size_t f = 0; f < n; ++f) {
                    float s = 0.0f;
                    for (int c = 0; c < fmt_.channels; ++c)
                        s += fbuf_[f * fmt_.channels + c];
                    dev_->rec.push_back(s / static_cast<float>(fmt_.channels));
                }
            }
        }
    }

    void generate(std::size_t n)
    {
        if (n * fmt_.channels > fbuf_.size())
            fbuf_.resize(n * fmt_.channels), raw_.resize(n * fmt_.bytesPerFrame());
        const double hz = dev_->toneHz.load();
        const float amp = dev_->toneAmp.load();
        const float noise = dev_->spec.noiseAmplitude;
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        // The tone is generated against the device's *nominal* rate: a fast clock
        // therefore really delivers more samples per second, as real hardware does.
        const double inc = 2.0 * kPi * hz / fmt_.sampleRate;
        for (std::size_t f = 0; f < n; ++f) {
            float v = amp * static_cast<float>(std::sin(phase_));
            if (noise > 0.0f)
                v += noise * u(rng_);
            phase_ += inc;
            if (phase_ > 2.0 * kPi)
                phase_ -= 2.0 * kPi;
            for (int c = 0; c < fmt_.channels; ++c)
                fbuf_[f * fmt_.channels + c] = v;
        }
    }

    Status status() const override { return status_.load(); }
    std::string lastError() const override { return status_.load() == Status::DeviceLost ? "Device removed" : ""; }
    StreamFormat format() const override { return fmt_; }
    double deviceLatencyMs() const override { return dev_->spec.periodMs; }
    std::string notes() const override { return "Simulated device"; }

    SimulatedBackend* be_;
    Device* dev_;
    AudioInputProcessor* proc_;
    AudioOutputMixer* mixer_;
    StreamFormat fmt_;
    double actualRate_ = 48000.0;
    double period_ = 0.01;
    double next_ = 0.0;
    double frac_ = 0.0;
    double phase_ = 0.0;
    std::size_t pending_ = 0;
    int held_ = 0;
    std::mt19937 rng_;
    std::vector<std::uint8_t> raw_;
    std::vector<float> fbuf_;
    std::atomic<Status> status_{Status::Running};
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

SimulatedBackend::SimulatedBackend(bool threaded) : threaded_(threaded) {}

SimulatedBackend::~SimulatedBackend() = default;

void SimulatedBackend::setListener(IDeviceListener* l)
{
    std::lock_guard<std::mutex> lk(mutex_);
    listener_ = l;
}

void SimulatedBackend::notify()
{
    IDeviceListener* l;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        l = listener_;
    }
    if (l)
        l->onDeviceListChanged();
}

SimulatedBackend::Device* SimulatedBackend::find(const std::string& id) const
{
    for (const auto& d : devices_)
        if (d->spec.id == id)
            return d.get();
    return nullptr;
}

void SimulatedBackend::addDevice(const SimDeviceSpec& spec)
{
    {
        std::lock_guard<std::mutex> lk(mutex_);
        auto d = std::make_unique<Device>();
        d->spec = spec;
        d->toneHz.store(spec.toneHz);
        d->toneAmp.store(spec.toneAmplitude);
        devices_.push_back(std::move(d));
    }
    notify();
}

std::vector<DeviceInfo> SimulatedBackend::enumerateDevices()
{
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<DeviceInfo> v;
    for (const auto& d : devices_) {
        DeviceInfo i;
        i.id = d->spec.id;
        i.friendlyName = d->spec.name;
        i.flow = d->spec.flow;
        i.bus = d->spec.bus;
        i.formFactor = d->spec.formFactor;
        i.containerId = d->spec.containerId;
        i.state = d->state;
        i.isBluetoothHandsFree = d->spec.bus == DeviceBus::Bluetooth && d->spec.flow == DeviceFlow::Capture;
        i.mixFormat.sampleRate = d->spec.sampleRate;
        i.mixFormat.channels = d->spec.channels;
        i.mixFormat.sampleType = d->spec.sampleType;
        i.mixFormatValid = true;
        i.jacks = d->spec.jacks;
        i.windowsVolume = d->spec.windowsVolume;
        i.windowsMuted = d->spec.windowsMuted;
        v.push_back(i);
    }
    return v;
}

std::unique_ptr<IAudioStream> SimulatedBackend::openCapture(const DeviceInfo& device, const StreamOptions& options,
                                                            AudioInputProcessor& processor, std::string* error)
{
    std::unique_ptr<Stream> s;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        Device* d = find(device.id);
        if (!d || d->spec.flow != DeviceFlow::Capture || d->state != DeviceState::Active) {
            if (error) *error = "Device not available";
            return nullptr;
        }
        if (d->failOpen) {
            if (error) *error = "Device is in use by another application";
            return nullptr;
        }
        d->lastOptions = options;
        s.reset(new Stream(this, d, &processor, nullptr, now_));
        processor.prepare(s->fmt_);
        d->stream = s.get();
        streams_.push_back(s.get());
    }
    if (threaded_)
        s->startThread();
    return s;
}

std::unique_ptr<IAudioStream> SimulatedBackend::openRender(const DeviceInfo& device, const StreamOptions& options,
                                                           AudioOutputMixer& mixer, std::string* error)
{
    std::unique_ptr<Stream> s;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        Device* d = find(device.id);
        if (!d || d->spec.flow != DeviceFlow::Render || d->state != DeviceState::Active) {
            if (error) *error = "Device not available";
            return nullptr;
        }
        if (d->failOpen) {
            if (error) *error = "Device is in use by another application";
            return nullptr;
        }
        d->lastOptions = options;
        s.reset(new Stream(this, d, nullptr, &mixer, now_));
        mixer.prepare(s->fmt_);
        d->stream = s.get();
        streams_.push_back(s.get());
    }
    if (threaded_)
        s->startThread();
    return s;
}

void SimulatedBackend::disconnect(const std::string& id)
{
    {
        std::lock_guard<std::mutex> lk(mutex_);
        Device* d = find(id);
        if (!d)
            return;
        d->state = DeviceState::NotPresent;
        if (d->stream)
            d->stream->status_.store(IAudioStream::Status::DeviceLost);
    }
    notify();
}

void SimulatedBackend::reconnect(const std::string& id)
{
    {
        std::lock_guard<std::mutex> lk(mutex_);
        Device* d = find(id);
        if (!d)
            return;
        d->state = DeviceState::Active;
    }
    notify();
}

void SimulatedBackend::setTone(const std::string& id, double hz, float amplitude)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (Device* d = find(id)) {
        d->toneHz.store(hz);
        d->toneAmp.store(amplitude);
    }
}

void SimulatedBackend::setFailOpen(const std::string& id, bool fail)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (Device* d = find(id))
        d->failOpen = fail;
}

void SimulatedBackend::advance(double seconds)
{
    const double end = now_ + seconds;
    for (;;) {
        Stream* next = nullptr;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            for (Stream* s : streams_)
                if (s->next_ <= end && (!next || s->next_ < next->next_))
                    next = s;
        }
        if (!next)
            break;
        now_ = next->next_;
        next->step();
        next->next_ += next->period_;
    }
    now_ = end;
}

std::vector<float> SimulatedBackend::takeRecording(const std::string& id)
{
    Device* d;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        d = find(id);
    }
    if (!d)
        return {};
    std::lock_guard<std::mutex> lk(d->recMutex);
    std::vector<float> r;
    r.swap(d->rec);
    return r;
}

void SimulatedBackend::setRecording(const std::string& id, bool on)
{
    Device* d;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        d = find(id);
    }
    if (!d)
        return;
    std::lock_guard<std::mutex> lk(d->recMutex);
    d->recording = on;
    if (!on)
        d->rec.clear();
}

std::uint64_t SimulatedBackend::renderedFrames(const std::string& id) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    Device* d = find(id);
    return d ? d->rendered.load() : 0;
}

StreamOptions SimulatedBackend::lastOptions(const std::string& id) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    Device* d = find(id);
    return d ? d->lastOptions : StreamOptions();
}

int SimulatedBackend::openStreamCount() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return static_cast<int>(streams_.size());
}

} // namespace ab
