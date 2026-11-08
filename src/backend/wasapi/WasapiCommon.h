// Shared helpers for the WASAPI / Core Audio backend (Windows only).
#pragma once

#ifndef _WIN32
#error "The WASAPI backend is Windows-only"
#endif

#include <windows.h>

#include <audioclient.h>
#include <devicetopology.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <propidl.h>
#include <propsys.h>

#include <string>

namespace ab {
namespace wasapi {

// Minimal intrusive COM smart pointer (avoids a WRL/ATL dependency).
template <class T> class ComPtr {
public:
    ComPtr() = default;
    ComPtr(const ComPtr& o) : p_(o.p_) { if (p_) p_->AddRef(); }
    ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ~ComPtr() { reset(); }
    ComPtr& operator=(ComPtr o) { std::swap(p_, o.p_); return *this; }
    void reset() { if (p_) { p_->Release(); p_ = nullptr; } }
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    T** put() { reset(); return &p_; }
    void** putVoid() { reset(); return reinterpret_cast<void**>(&p_); }
private:
    T* p_ = nullptr;
};

// GUIDs are defined explicitly (values from the Windows SDK) so the code builds
// identically with MSVC and MinGW-w64, independent of uuid library coverage.
extern const CLSID kCLSID_MMDeviceEnumerator;
extern const IID kIID_IMMDeviceEnumerator;
extern const IID kIID_IMMNotificationClient;
extern const IID kIID_IMMEndpoint;
extern const IID kIID_IAudioClient;
extern const IID kIID_IAudioClient2;
extern const IID kIID_IAudioClient3;
extern const IID kIID_IAudioCaptureClient;
extern const IID kIID_IAudioRenderClient;
extern const IID kIID_IDeviceTopology;
extern const IID kIID_IPart;
extern const IID kIID_IAcousticEchoCancellationControl;
extern const GUID kSubtypePCM;
extern const GUID kSubtypeIeeeFloat;

extern const PROPERTYKEY kPKEY_Device_FriendlyName;
extern const PROPERTYKEY kPKEY_Device_DeviceDesc;
extern const PROPERTYKEY kPKEY_DeviceInterface_FriendlyName;
extern const PROPERTYKEY kPKEY_Device_ContainerId;
extern const PROPERTYKEY kPKEY_AudioEndpoint_FormFactor;
extern const PROPERTYKEY kPKEY_AudioEngine_DeviceFormat;

// Windows 11 (22H2+): lets a communications-mode capture stream choose which render
// endpoint is used as the echo-cancellation reference. Declared here because older
// SDKs / MinGW do not ship it. IID f4ae25b5-aaa3-437d-b6b3-dbbe2d0e9549.
struct IAcousticEchoCancellationControlAB : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetEchoCancellationRenderEndpoint(LPCWSTR endpointId) = 0;
};

std::string toUtf8(const wchar_t* w);
std::wstring toWide(const std::string& s);
std::string hresultText(HRESULT hr);
std::string guidText(const GUID& g);

} // namespace wasapi
} // namespace ab
