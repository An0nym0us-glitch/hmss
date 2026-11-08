#include "WasapiCommon.h"

#include <cstdio>

namespace ab {
namespace wasapi {

#define AB_GUID(name, a, b, c, d0, d1, d2, d3, d4, d5, d6, d7) \
    const GUID name = {a, b, c, {d0, d1, d2, d3, d4, d5, d6, d7}}

AB_GUID(kCLSID_MMDeviceEnumerator, 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
AB_GUID(kIID_IMMDeviceEnumerator, 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
AB_GUID(kIID_IMMNotificationClient, 0x7991EEC9, 0x7E89, 0x4D85, 0x83, 0x90, 0x6C, 0x70, 0x3C, 0xEC, 0x60, 0xC0);
AB_GUID(kIID_IMMEndpoint, 0x1BE09788, 0x6894, 0x4089, 0x85, 0x86, 0x9A, 0x2A, 0x6C, 0x26, 0x5A, 0xC5);
AB_GUID(kIID_IAudioClient, 0x1CB9AD4C, 0xDBFA, 0x4C32, 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);
AB_GUID(kIID_IAudioClient2, 0x726778CD, 0xF60A, 0x4EDA, 0x82, 0xDE, 0xE4, 0x76, 0x10, 0xCD, 0x78, 0xAA);
AB_GUID(kIID_IAudioClient3, 0x7ED4EE07, 0x8E67, 0x4CD4, 0x8C, 0x1A, 0x2B, 0x7A, 0x59, 0x87, 0xAD, 0x42);
AB_GUID(kIID_IAudioCaptureClient, 0xC8ADBD64, 0xE71E, 0x48A0, 0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17);
AB_GUID(kIID_IAudioRenderClient, 0xF294ACFC, 0x3146, 0x4483, 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2);
AB_GUID(kIID_IDeviceTopology, 0x2A07407E, 0x6497, 0x4A18, 0x97, 0x87, 0x32, 0xF7, 0x9B, 0xD0, 0xD9, 0x8F);
AB_GUID(kIID_IPart, 0xAE2DE0E4, 0x5BCA, 0x4F2D, 0xAA, 0x46, 0x5D, 0x13, 0xF8, 0xFD, 0xB3, 0xA9);
AB_GUID(kIID_IAcousticEchoCancellationControl, 0xF4AE25B5, 0xAAA3, 0x437D, 0xB6, 0xB3, 0xDB, 0xBE, 0x2D, 0x0E, 0x95, 0x49);
AB_GUID(kSubtypePCM, 0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);
AB_GUID(kSubtypeIeeeFloat, 0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);

#define AB_PKEY(name, a, b, c, d0, d1, d2, d3, d4, d5, d6, d7, pid) \
    const PROPERTYKEY name = {{a, b, c, {d0, d1, d2, d3, d4, d5, d6, d7}}, pid}

AB_PKEY(kPKEY_Device_FriendlyName, 0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 14);
AB_PKEY(kPKEY_Device_DeviceDesc, 0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 2);
AB_PKEY(kPKEY_DeviceInterface_FriendlyName, 0x026e516e, 0xb814, 0x414b, 0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22, 2);
AB_PKEY(kPKEY_Device_ContainerId, 0x8c7ed206, 0x3f8a, 0x4827, 0xb3, 0xab, 0xae, 0x9e, 0x1f, 0xae, 0xfc, 0x6c, 2);
AB_PKEY(kPKEY_AudioEndpoint_FormFactor, 0x1da5d803, 0xd492, 0x4edd, 0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e, 0);
AB_PKEY(kPKEY_AudioEngine_DeviceFormat, 0xf19f064d, 0x082c, 0x4e27, 0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e, 0x4c, 0);

std::string toUtf8(const wchar_t* w)
{
    if (!w || !*w)
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1)
        return {};
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring toWide(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 1)
        return {};
    std::wstring w(static_cast<std::size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

std::string hresultText(HRESULT hr)
{
    switch (hr) {
    case AUDCLNT_E_DEVICE_INVALIDATED: return "The device was removed or reconfigured";
    case AUDCLNT_E_DEVICE_IN_USE: return "The device is used exclusively by another application";
    case AUDCLNT_E_UNSUPPORTED_FORMAT: return "The device does not support the requested format";
    case AUDCLNT_E_SERVICE_NOT_RUNNING: return "The Windows Audio service is not running";
    case AUDCLNT_E_ENDPOINT_CREATE_FAILED: return "Windows could not create the audio endpoint";
    case AUDCLNT_E_CPUUSAGE_EXCEEDED: return "Audio engine CPU usage exceeded";
    case AUDCLNT_E_BUFFER_SIZE_ERROR: return "Unsupported buffer size";
    case E_OUTOFMEMORY: return "Out of memory";
    case E_ACCESSDENIED: return "Access denied (check Windows microphone privacy settings)";
    case HRESULT_FROM_WIN32(ERROR_NOT_FOUND): return "Device not found";
    default: break;
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), "Windows audio error 0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

std::string guidText(const GUID& g)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                  static_cast<unsigned long>(g.Data1), g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2],
                  g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return buf;
}

} // namespace wasapi
} // namespace ab
