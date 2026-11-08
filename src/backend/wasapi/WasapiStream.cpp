#include "WasapiStream.h"

#include "core/AudioInputProcessor.h"
#include "core/AudioMixer.h"

#include <avrt.h>

#include <algorithm>
#include <sstream>

#ifndef AUDCLNT_E_RESOURCES_INVALIDATED
#define AUDCLNT_E_RESOURCES_INVALIDATED AUDCLNT_ERR(0x026)
#endif
#ifndef AUDCLNT_STREAMFLAGS_NOPERSIST
#define AUDCLNT_STREAMFLAGS_NOPERSIST 0x00080000
#endif

namespace ab {
namespace wasapi {

namespace {

constexpr REFERENCE_TIME kHnsPerMs = 10000;

struct CoTaskFree {
    void* p;
    ~CoTaskFree() { CoTaskMemFree(p); }
};

} // namespace

bool parseWaveFormat(const WAVEFORMATEX* wfx, StreamFormat* out)
{
    if (!wfx || wfx->nChannels == 0 || wfx->nSamplesPerSec == 0)
        return false;
    StreamFormat f;
    f.sampleRate = static_cast<int>(wfx->nSamplesPerSec);
    f.channels = wfx->nChannels;
    f.channelMask = 0;
    bool isFloat = wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    bool isPcm = wfx->wFormatTag == WAVE_FORMAT_PCM;
    int validBits = wfx->wBitsPerSample;
    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wfx->cbSize >= 22) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx);
        isFloat = IsEqualGUID(ext->SubFormat, kSubtypeIeeeFloat) != 0;
        isPcm = IsEqualGUID(ext->SubFormat, kSubtypePCM) != 0;
        if (ext->Samples.wValidBitsPerSample)
            validBits = ext->Samples.wValidBitsPerSample;
        f.channelMask = ext->dwChannelMask;
    }
    if (isFloat && wfx->wBitsPerSample == 32)
        f.sampleType = SampleType::Float32;
    else if (isPcm && wfx->wBitsPerSample == 16)
        f.sampleType = SampleType::Int16;
    else if (isPcm && wfx->wBitsPerSample == 24)
        f.sampleType = SampleType::Int24Packed;
    else if (isPcm && wfx->wBitsPerSample == 32)
        f.sampleType = validBits == 24 ? SampleType::Int24In32 : SampleType::Int32;
    else
        return false;
    if (f.bytesPerFrame() != wfx->nBlockAlign)
        return false;
    *out = f;
    return true;
}

std::unique_ptr<WasapiStream> WasapiStream::openCapture(IMMDeviceEnumerator* enumerator, const DeviceInfo& device,
                                                        const StreamOptions& options, AudioInputProcessor& processor,
                                                        std::string* error)
{
    std::unique_ptr<WasapiStream> s(new WasapiStream());
    s->processor_ = &processor;
    if (!s->init(enumerator, device, options, true, error)) {
        if (!options.rawMode)
            return nullptr;
        // Raw mode refused by the driver: fall back to a normal stream.
        StreamOptions normal = options;
        normal.rawMode = false;
        s.reset(new WasapiStream());
        s->processor_ = &processor;
        if (!s->init(enumerator, device, normal, true, error))
            return nullptr;
        s->notes_ = "This device does not support bypassing audio effects; using normal mode. " + s->notes_;
    }
    processor.prepare(s->format_);
    if (!s->start(error))
        return nullptr;
    return s;
}

std::unique_ptr<WasapiStream> WasapiStream::openRender(IMMDeviceEnumerator* enumerator, const DeviceInfo& device,
                                                       const StreamOptions& options, AudioOutputMixer& mixer,
                                                       std::string* error)
{
    std::unique_ptr<WasapiStream> s(new WasapiStream());
    s->mixer_ = &mixer;
    if (!s->init(enumerator, device, options, false, error)) {
        if (!options.rawMode)
            return nullptr;
        StreamOptions normal = options;
        normal.rawMode = false;
        s.reset(new WasapiStream());
        s->mixer_ = &mixer;
        if (!s->init(enumerator, device, normal, false, error))
            return nullptr;
        s->notes_ = "This device does not support bypassing audio effects; using normal mode. " + s->notes_;
    }
    mixer.prepare(s->format_);
    if (!s->start(error))
        return nullptr;
    return s;
}

bool WasapiStream::init(IMMDeviceEnumerator* enumerator, const DeviceInfo& info, const StreamOptions& options,
                        bool capture, std::string* error)
{
    capture_ = capture;
    auto failWith = [&](HRESULT hr, const char* step) {
        if (error)
            *error = hresultText(hr) + " (" + step + ")";
        return false;
    };

    const std::wstring wid = toWide(info.id);
    HRESULT hr = enumerator->GetDevice(wid.c_str(), device_.put());
    if (FAILED(hr))
        return failWith(hr, "GetDevice");
    DWORD state = 0;
    if (SUCCEEDED(device_->GetState(&state)) && state != DEVICE_STATE_ACTIVE) {
        if (error)
            *error = "Device is not active";
        return false;
    }
    hr = device_->Activate(kIID_IAudioClient, CLSCTX_ALL, nullptr, client_.putVoid());
    if (FAILED(hr))
        return failWith(hr, "Activate");

    std::ostringstream notes;

    // Optional stream properties (must be set before Initialize):
    //  * voice processing: Communications category -> the OS/driver applies its voice
    //    processing chain (AEC / NS / AGC) where available;
    //  * raw mode: skip the endpoint's audio effects (APOs) such as Nahimic or Dolby,
    //    if the driver supports it.
    const bool wantComms = capture && options.voiceProcessing;
    const bool wantRaw = options.rawMode && !wantComms;
    if (options.rawMode && wantComms)
        notes << "Bypassing audio effects is not combined with Windows voice processing. ";
    if (wantComms || wantRaw) {
        ComPtr<IAudioClient2> client2;
        if (SUCCEEDED(client_->QueryInterface(kIID_IAudioClient2, client2.putVoid()))) {
            AudioClientProperties props = {};
            props.cbSize = sizeof(props);
            props.bIsOffload = FALSE;
            props.eCategory = wantComms ? AudioCategory_Communications : AudioCategory_Other;
            props.Options = wantRaw ? AUDCLNT_STREAMOPTIONS_RAW : AUDCLNT_STREAMOPTIONS_NONE;
            const HRESULT phr = client2->SetClientProperties(&props);
            if (SUCCEEDED(phr)) {
                if (wantComms)
                    notes << "Windows voice processing (communications mode) requested. ";
                if (wantRaw)
                    notes << "Windows audio effects bypassed (raw mode). ";
            } else {
                if (wantRaw && error)
                    *error = "This device does not support bypassing audio effects (raw mode)";
                if (wantRaw)
                    return false; // caller retries without raw mode
                notes << "Voice processing not accepted by this device. ";
            }
        } else {
            notes << "Stream properties require Windows 8.1 or later. ";
        }
    }

    WAVEFORMATEX* wfx = nullptr;
    hr = client_->GetMixFormat(&wfx);
    if (FAILED(hr) || !wfx)
        return failWith(hr, "GetMixFormat");
    CoTaskFree freeFmt{wfx};
    if (!parseWaveFormat(wfx, &format_)) {
        if (error)
            *error = "Unsupported device mix format";
        return false;
    }

    REFERENCE_TIME defaultPeriod = 0, minPeriod = 0;
    client_->GetDevicePeriod(&defaultPeriod, &minPeriod);
    const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST;

    bool initialized = false;
    if (options.lowLatency) {
        ComPtr<IAudioClient3> client3;
        if (SUCCEEDED(client_->QueryInterface(kIID_IAudioClient3, client3.putVoid()))) {
            UINT32 def = 0, fund = 0, minF = 0, maxF = 0;
            if (SUCCEEDED(client3->GetSharedModeEnginePeriod(wfx, &def, &fund, &minF, &maxF)) && minF > 0 &&
                SUCCEEDED(client3->InitializeSharedAudioStream(flags, minF, wfx, nullptr))) {
                initialized = true;
                notes << "Low-latency engine period " << (1000.0 * minF / format_.sampleRate) << " ms. ";
            }
        }
        if (!initialized) {
            // A failed InitializeSharedAudioStream leaves the client unusable: start over.
            client_.reset();
            hr = device_->Activate(kIID_IAudioClient, CLSCTX_ALL, nullptr, client_.putVoid());
            if (FAILED(hr))
                return failWith(hr, "Activate");
            notes << "Low-latency mode not supported by this driver. ";
        }
    }
    if (!initialized) {
        // Capture: a generous buffer costs no latency (it is drained on every event)
        // and protects against scheduling hiccups. Render: the buffer *is* the latency.
        REFERENCE_TIME duration = capture
                                      ? std::max<REFERENCE_TIME>(100 * kHnsPerMs, 4 * defaultPeriod)
                                      : std::max<REFERENCE_TIME>(options.bufferMs * kHnsPerMs, 2 * defaultPeriod);
        hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, duration, 0, wfx, nullptr);
        if (FAILED(hr))
            return failWith(hr, "Initialize");
    }

    hr = client_->GetBufferSize(&bufferFrames_);
    if (FAILED(hr))
        return failWith(hr, "GetBufferSize");
    REFERENCE_TIME streamLatency = 0;
    client_->GetStreamLatency(&streamLatency);
    latencyMs_ = static_cast<double>(streamLatency) / kHnsPerMs +
                 (capture ? 0.0 : 1000.0 * bufferFrames_ / format_.sampleRate);

    audioEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!audioEvent_ || !stopEvent_) {
        if (error)
            *error = "CreateEvent failed";
        return false;
    }
    hr = client_->SetEventHandle(audioEvent_);
    if (FAILED(hr))
        return failWith(hr, "SetEventHandle");

    if (capture) {
        hr = client_->GetService(kIID_IAudioCaptureClient, captureClient_.putVoid());
        if (FAILED(hr))
            return failWith(hr, "GetService(capture)");
        if (options.voiceProcessing && !options.echoReferenceId.empty()) {
            // Windows 11: route the AEC reference to the speaker this mic is played on.
            ComPtr<IAcousticEchoCancellationControlAB> aec;
            if (SUCCEEDED(client_->GetService(kIID_IAcousticEchoCancellationControl, aec.putVoid()))) {
                const std::wstring ref = toWide(options.echoReferenceId);
                if (SUCCEEDED(aec->SetEchoCancellationRenderEndpoint(ref.c_str())))
                    notes << "Echo cancellation reference set to the routed output. ";
                else
                    notes << "Echo cancellation reference could not be set. ";
            } else {
                notes << "This device's driver does not expose echo-cancellation control "
                         "(needs Windows 11 22H2+ and an AEC-capable driver). ";
            }
        }
    } else {
        hr = client_->GetService(kIID_IAudioRenderClient, renderClient_.putVoid());
        if (FAILED(hr))
            return failWith(hr, "GetService(render)");
        // Pre-roll silence so the first period does not underrun.
        BYTE* data = nullptr;
        if (SUCCEEDED(renderClient_->GetBuffer(bufferFrames_, &data)))
            renderClient_->ReleaseBuffer(bufferFrames_, AUDCLNT_BUFFERFLAGS_SILENT);
    }
    notes_ = notes.str();
    if (!notes_.empty() && notes_.back() == ' ')
        notes_.pop_back();
    return true;
}

bool WasapiStream::start(std::string* error)
{
    HRESULT hr = client_->Start();
    if (FAILED(hr)) {
        if (error)
            *error = hresultText(hr) + " (Start)";
        return false;
    }
    started_ = true;
    thread_ = std::thread([this] { threadMain(); });
    return true;
}

WasapiStream::~WasapiStream()
{
    if (stopEvent_)
        SetEvent(stopEvent_);
    if (thread_.joinable())
        thread_.join();
    if (client_ && started_)
        client_->Stop();
    captureClient_.reset();
    renderClient_.reset();
    client_.reset();
    device_.reset();
    if (audioEvent_)
        CloseHandle(audioEvent_);
    if (stopEvent_)
        CloseHandle(stopEvent_);
}

std::string WasapiStream::lastError() const
{
    std::lock_guard<std::mutex> lk(errorMutex_);
    return error_;
}

void WasapiStream::fail(HRESULT hr)
{
    {
        std::lock_guard<std::mutex> lk(errorMutex_);
        error_ = hresultText(hr);
    }
    if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
        status_.store(Status::DeviceLost);
    else if (hr == AUDCLNT_E_RESOURCES_INVALIDATED)
        status_.store(Status::FormatChanged);
    else
        status_.store(Status::Failed);
}

void WasapiStream::threadMain()
{
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    if (!mmcss)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    HANDLE waits[2] = {stopEvent_, audioEvent_};
    for (;;) {
        const DWORD w = WaitForMultipleObjects(2, waits, FALSE, 200);
        if (w == WAIT_OBJECT_0)
            break; // stop requested
        if (w != WAIT_OBJECT_0 + 1 && w != WAIT_TIMEOUT) {
            fail(HRESULT_FROM_WIN32(GetLastError()));
            break;
        }
        // On timeout we still service the stream: some drivers signal irregularly.
        const HRESULT hr = capture_ ? captureCycle() : renderCycle();
        if (FAILED(hr)) {
            fail(hr);
            break;
        }
    }

    if (mmcss)
        AvRevertMmThreadCharacteristics(mmcss);
    if (SUCCEEDED(comHr))
        CoUninitialize();
}

HRESULT WasapiStream::captureCycle()
{
    UINT32 packet = 0;
    HRESULT hr = captureClient_->GetNextPacketSize(&packet);
    while (SUCCEEDED(hr) && packet > 0) {
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        hr = captureClient_->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
        if (FAILED(hr))
            return hr;
        if (hr == AUDCLNT_S_BUFFER_EMPTY)
            break;
        if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
            processor_->reportDiscontinuity();
        processor_->process(data, frames, (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0);
        hr = captureClient_->ReleaseBuffer(frames);
        if (FAILED(hr))
            return hr;
        hr = captureClient_->GetNextPacketSize(&packet);
    }
    return FAILED(hr) ? hr : S_OK;
}

HRESULT WasapiStream::renderCycle()
{
    UINT32 padding = 0;
    HRESULT hr = client_->GetCurrentPadding(&padding);
    if (FAILED(hr))
        return hr;
    const UINT32 available = bufferFrames_ > padding ? bufferFrames_ - padding : 0;
    if (available == 0)
        return S_OK;
    BYTE* data = nullptr;
    hr = renderClient_->GetBuffer(available, &data);
    if (FAILED(hr))
        return hr;
    mixer_->render(data, available);
    return renderClient_->ReleaseBuffer(available, 0);
}

} // namespace wasapi
} // namespace ab
