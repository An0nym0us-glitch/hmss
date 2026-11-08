#include "WasapiBackend.h"

#include "WasapiCommon.h"
#include "WasapiStream.h"

#include <algorithm>
#include <atomic>
#include <mutex>

namespace ab {

using namespace wasapi;

namespace {

std::string readString(IPropertyStore* store, const PROPERTYKEY& key)
{
    PROPVARIANT pv;
    PropVariantInit(&pv);
    std::string s;
    if (SUCCEEDED(store->GetValue(key, &pv)) && pv.vt == VT_LPWSTR)
        s = toUtf8(pv.pwszVal);
    PropVariantClear(&pv);
    return s;
}

std::string readGuid(IPropertyStore* store, const PROPERTYKEY& key)
{
    PROPVARIANT pv;
    PropVariantInit(&pv);
    std::string s;
    if (SUCCEEDED(store->GetValue(key, &pv)) && pv.vt == VT_CLSID && pv.puuid)
        s = guidText(*pv.puuid);
    PropVariantClear(&pv);
    return s;
}

int readUInt(IPropertyStore* store, const PROPERTYKEY& key, int def)
{
    PROPVARIANT pv;
    PropVariantInit(&pv);
    int v = def;
    if (SUCCEEDED(store->GetValue(key, &pv)) && (pv.vt == VT_UI4 || pv.vt == VT_I4))
        v = static_cast<int>(pv.ulVal);
    PropVariantClear(&pv);
    return v;
}

bool readFormat(IPropertyStore* store, const PROPERTYKEY& key, StreamFormat* out)
{
    PROPVARIANT pv;
    PropVariantInit(&pv);
    bool ok = false;
    if (SUCCEEDED(store->GetValue(key, &pv)) && pv.vt == VT_BLOB && pv.blob.pBlobData &&
        pv.blob.cbSize >= sizeof(WAVEFORMATEX))
        ok = parseWaveFormat(reinterpret_cast<const WAVEFORMATEX*>(pv.blob.pBlobData), out);
    PropVariantClear(&pv);
    return ok;
}

// Kernel-streaming filter path of the adapter the endpoint is connected to, e.g.
// "{2}.\\?\usb#vid_..." or "{2}.\\?\bthhfenum#...". Only works for active endpoints.
std::string topologyPath(IMMDevice* device)
{
    ComPtr<IDeviceTopology> topo;
    if (FAILED(device->Activate(kIID_IDeviceTopology, CLSCTX_ALL, nullptr, topo.putVoid())))
        return {};
    ComPtr<IConnector> conn;
    if (FAILED(topo->GetConnector(0, conn.put())))
        return {};
    ComPtr<IConnector> to;
    if (FAILED(conn->GetConnectedTo(to.put())))
        return {};
    ComPtr<IPart> part;
    if (FAILED(to->QueryInterface(kIID_IPart, part.putVoid())))
        return {};
    ComPtr<IDeviceTopology> adapter;
#if defined(__MINGW32__)
    // MinGW-w64's devicetopology.h misspells this method (same vtable slot).
    if (FAILED(part->GetTopologyObjects(adapter.put())))
#else
    if (FAILED(part->GetTopologyObject(adapter.put())))
#endif
        return {};
    LPWSTR id = nullptr;
    std::string s;
    if (SUCCEEDED(adapter->GetDeviceId(&id)) && id) {
        s = toUtf8(id);
        CoTaskMemFree(id);
    }
    return s;
}

std::string defaultId(IMMDeviceEnumerator* e, EDataFlow flow, ERole role)
{
    ComPtr<IMMDevice> d;
    if (FAILED(e->GetDefaultAudioEndpoint(flow, role, d.put())))
        return {};
    LPWSTR id = nullptr;
    std::string s;
    if (SUCCEEDED(d->GetId(&id)) && id) {
        s = toUtf8(id);
        CoTaskMemFree(id);
    }
    return s;
}

DeviceState mapState(DWORD s)
{
    switch (s) {
    case DEVICE_STATE_ACTIVE: return DeviceState::Active;
    case DEVICE_STATE_DISABLED: return DeviceState::Disabled;
    case DEVICE_STATE_NOTPRESENT: return DeviceState::NotPresent;
    case DEVICE_STATE_UNPLUGGED: return DeviceState::Unplugged;
    default: return DeviceState::Unknown;
    }
}

// Receives hot-plug notifications on a system thread and forwards them to the
// engine's listener, which only schedules a refresh (no work on this thread).
class NotificationClient : public IMMNotificationClient {
public:
    virtual ~NotificationClient() = default;
    void setListener(IDeviceListener* l)
    {
        std::lock_guard<std::mutex> lk(m_);
        listener_ = l;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG r = --refs_;
        if (r == 0)
            delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, kIID_IMMNotificationClient)) {
            *ppv = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { notify(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { notify(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { notify(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override { notify(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY key) override
    {
        // Property changes are frequent; only names and formats matter to us.
        auto is = [&](const PROPERTYKEY& k) { return IsEqualGUID(key.fmtid, k.fmtid) && key.pid == k.pid; };
        if (is(kPKEY_Device_FriendlyName) || is(kPKEY_AudioEngine_DeviceFormat) ||
            is(kPKEY_DeviceInterface_FriendlyName))
            notify();
        return S_OK;
    }

private:
    void notify()
    {
        std::lock_guard<std::mutex> lk(m_);
        if (listener_)
            listener_->onDeviceListChanged();
    }
    std::atomic<ULONG> refs_{1};
    std::mutex m_;
    IDeviceListener* listener_ = nullptr;
};

} // namespace

struct WasapiBackend::Impl {
    bool comInitialized = false;
    ComPtr<IMMDeviceEnumerator> enumerator;
    NotificationClient* notifier = nullptr;
    bool registered = false;
};

WasapiBackend::WasapiBackend() : d_(new Impl) {}

WasapiBackend::~WasapiBackend()
{
    if (d_->registered && d_->enumerator)
        d_->enumerator->UnregisterEndpointNotificationCallback(d_->notifier);
    if (d_->notifier) {
        d_->notifier->setListener(nullptr);
        d_->notifier->Release();
    }
    d_->enumerator.reset();
    if (d_->comInitialized)
        CoUninitialize();
}

bool WasapiBackend::initialize(std::string* error)
{
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    d_->comInitialized = SUCCEEDED(co); // S_FALSE also needs a matching CoUninitialize
    HRESULT hr = CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, kIID_IMMDeviceEnumerator,
                                  d_->enumerator.putVoid());
    if (FAILED(hr)) {
        if (error)
            *error = "Cannot access Windows audio devices: " + hresultText(hr);
        return false;
    }
    d_->notifier = new NotificationClient();
    d_->registered = SUCCEEDED(d_->enumerator->RegisterEndpointNotificationCallback(d_->notifier));
    return true;
}

void WasapiBackend::setListener(IDeviceListener* listener)
{
    if (d_->notifier)
        d_->notifier->setListener(listener);
}

std::vector<DeviceInfo> WasapiBackend::enumerateDevices()
{
    std::vector<DeviceInfo> list;
    IMMDeviceEnumerator* e = d_->enumerator.get();
    if (!e)
        return list;
    const std::string defRender = defaultId(e, eRender, eConsole);
    const std::string defCapture = defaultId(e, eCapture, eConsole);
    const std::string comRender = defaultId(e, eRender, eCommunications);
    const std::string comCapture = defaultId(e, eCapture, eCommunications);

    ComPtr<IMMDeviceCollection> col;
    if (FAILED(e->EnumAudioEndpoints(eAll, DEVICE_STATEMASK_ALL, col.put())))
        return list;
    UINT count = 0;
    col->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> dev;
        if (FAILED(col->Item(i, dev.put())))
            continue;
        DeviceInfo info;
        LPWSTR id = nullptr;
        if (FAILED(dev->GetId(&id)) || !id)
            continue;
        info.id = toUtf8(id);
        CoTaskMemFree(id);

        DWORD state = 0;
        dev->GetState(&state);
        info.state = mapState(state);

        ComPtr<IMMEndpoint> ep;
        EDataFlow flow = eRender;
        if (SUCCEEDED(dev->QueryInterface(kIID_IMMEndpoint, ep.putVoid())))
            ep->GetDataFlow(&flow);
        info.flow = flow == eCapture ? DeviceFlow::Capture : DeviceFlow::Render;

        ComPtr<IPropertyStore> store;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, store.put()))) {
            info.friendlyName = readString(store.get(), kPKEY_Device_FriendlyName);
            info.description = readString(store.get(), kPKEY_Device_DeviceDesc);
            info.adapterName = readString(store.get(), kPKEY_DeviceInterface_FriendlyName);
            info.containerId = readGuid(store.get(), kPKEY_Device_ContainerId);
            info.formFactor = static_cast<FormFactor>(
                std::min(readUInt(store.get(), kPKEY_AudioEndpoint_FormFactor, 10), 10));
            info.mixFormatValid = readFormat(store.get(), kPKEY_AudioEngine_DeviceFormat, &info.mixFormat);
        }
        if (info.friendlyName.empty()) {
            // Extremely rare (broken driver). Still never invent "Microphone 1":
            // compose from what Windows does provide.
            info.friendlyName = !info.description.empty() ? info.description : info.adapterName;
        }
        if (info.state == DeviceState::Active)
            info.interfacePath = topologyPath(dev.get());
        bool handsFree = false;
        info.bus = classifyDeviceBus(info.interfacePath, info.friendlyName, info.adapterName, info.formFactor,
                                     &handsFree);
        info.isBluetoothHandsFree = handsFree;
        if (info.flow == DeviceFlow::Capture) {
            info.isDefault = info.id == defCapture;
            info.isDefaultCommunications = info.id == comCapture;
        } else {
            info.isDefault = info.id == defRender;
            info.isDefaultCommunications = info.id == comRender;
        }
        list.push_back(std::move(info));
    }
    return list;
}

std::unique_ptr<IAudioStream> WasapiBackend::openCapture(const DeviceInfo& device, const StreamOptions& options,
                                                         AudioInputProcessor& processor, std::string* error)
{
    if (!d_->enumerator) {
        if (error) *error = "Audio backend not initialised";
        return nullptr;
    }
    return WasapiStream::openCapture(d_->enumerator.get(), device, options, processor, error);
}

std::unique_ptr<IAudioStream> WasapiBackend::openRender(const DeviceInfo& device, const StreamOptions& options,
                                                        AudioOutputMixer& mixer, std::string* error)
{
    if (!d_->enumerator) {
        if (error) *error = "Audio backend not initialised";
        return nullptr;
    }
    return WasapiStream::openRender(d_->enumerator.get(), device, options, mixer, error);
}

} // namespace ab
