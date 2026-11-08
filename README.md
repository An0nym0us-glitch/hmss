# AudioBridge

**Real-time multi-device audio routing and mixing for Windows 10/11 (x64).**

Connect *any* microphone Windows knows about to *any* speaker - several at once - with a
live routing matrix, a mixer, and real-time processing. Typical use: Bluetooth earbuds
microphone → laptop AUX jack → Bluetooth speaker, but every combination works: USB mic →
HDMI monitor, laptop mic + USB mic → two speakers, etc.

AudioBridge is a native C++17 / Qt 5.15 application using Windows WASAPI (shared mode,
event driven). Audio flows continuously in real time; nothing is recorded to a file. It
works fully offline.

## Features

| Area | What it does |
|---|---|
| Device detection | Every Windows playback and recording endpoint, in every state, with the **real Windows friendly name** (`PKEY_Device_FriendlyName`), category (Bluetooth headset mic, USB microphone, webcam mic, built-in mic, AUX/headphones, HDMI/DisplayPort, …), connection type (Bluetooth / USB / built-in / HDMI / virtual, detected from the kernel-streaming device path), state, Windows defaults and format. Identical names are disambiguated. Hot-plug via `IMMNotificationClient`. |
| Multiple inputs | Up to 16 microphones at once, each with gain (-24…+36 dB), volume, mute, solo, live meter with peak hold, clip LED, status. |
| Multiple outputs | Up to 16 outputs at once, each with volume, mute, live meter, sync delay (to line up a wired speaker with a Bluetooth one), live latency / clock-drift / underrun statistics. |
| Routing matrix | Mic × speaker grid built from the real devices. Click to connect/disconnect, right-click for a per-route level. Changes apply instantly while streaming. Routes are stored by endpoint ID and survive disconnects and restarts. |
| Feedback safety | Routes where mic and speaker belong to the same hardware (headset, laptop) are flagged and need confirmation. A feedback guard automatically mutes an output on sustained howling; a peak limiter prevents clipping. |
| Mixer | Console-style channel strips for every enabled input and output, plus master volume and global mute. |
| Processing | Sample-rate conversion (band-limited windowed-sinc), channel conversion (mono/stereo/multichannel, mic arrays), 80 Hz rumble filter, spectral noise suppression, noise gate, clipping protection; optional Windows voice processing (communications mode, with Windows 11 echo-cancellation reference). |
| Clock handling | Every device runs on its own crystal. Each route has its own buffer and a drift controller that adjusts its resampler by a few hundred ppm, so latency stays constant for hours instead of drifting or dropping out. Bluetooth burst delivery is absorbed by an adaptive margin. |
| Recovery | When a device disconnects, its settings and routes are kept and it is re-opened automatically when **the same device** returns (same endpoint ID). AudioBridge never switches your audio to a different device on its own. |
| Desktop app | Dark Fluent-style UI: Dashboard (start/stop/pause, stats, live signal-flow diagram of the actual routes), Inputs, Outputs, Routing Matrix, Mixer, Settings. Tray icon, launch at sign-in, start minimized, single instance. |
| CLI | `AudioBridgeCLI.exe --list` prints every endpoint with its real name and ID; `--route` streams headlessly. |

## Install

Run **`AudioBridge-Setup-x64.exe`**. It installs to `Program Files\AudioBridge`, creates a
Start-menu entry (and optionally a desktop icon / sign-in startup entry) and registers an
uninstaller in *Settings → Apps*. A portable ZIP is also produced by the build.

On first start: open **Inputs**, enable your microphone(s); open **Outputs**, enable your
speaker(s); open **Routing Matrix** and click the cells to connect them; press
**Start monitoring**.

### Windows microphone privacy

Windows can block desktop apps from microphones (*Settings → Privacy & security →
Microphone → Let desktop apps access your microphone*). If a microphone shows
"Access denied", enable that switch.

### Bluetooth earbuds as a microphone

Windows exposes the earbuds' microphone through the Bluetooth **hands-free profile
(HFP)**. While that microphone is open, the earbuds switch to HFP and their *own*
playback drops to telephone quality - a Bluetooth limitation, not an AudioBridge one.
Playback on other devices (AUX jack, USB speakers, another Bluetooth speaker) is not
affected. Bluetooth audio also has inherently higher and burstier latency; AudioBridge
adapts its buffering automatically. Use **Sync delay** on a faster output if you play
the same microphone on a wired and a Bluetooth speaker simultaneously.

## Latency

Defaults: 48 kHz processing, 20 ms buffer. Typical end-to-end latency on wired devices
is the route buffer (≈ 20-30 ms) plus the Windows device buffer (≈ 10-20 ms). Bluetooth
links add their own codec latency (often 100-200 ms) that no application can remove.
Settings offers 10…200 ms buffers and a low-latency mode (`IAudioClient3`) for drivers
that support small periods.

## Not in version 1: virtual microphone

AudioBridge does **not** create a virtual microphone for Discord, OBS, games or meeting
apps. Exposing audio as a new recording device requires a kernel-mode Windows audio
driver; normal WASAPI playback cannot do it. If you install a virtual audio cable, it
appears in AudioBridge as an ordinary output, and you can route microphones to it today.
The engine is designed so a future driver-backed output can be added as just another
output slot (see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)).

## Building

See [docs/BUILDING.md](docs/BUILDING.md). Short version (Windows, Qt 5.15.2 MSVC, Visual Studio 2019/2022, CMake, Ninja):

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\5.15.2\msvc2019_64
cmake --build build
ctest --test-dir build --output-on-failure
```

## Testing

See [docs/TESTING.md](docs/TESTING.md) for what is verified automatically (on simulated
endpoints and through Wine's WASAPI implementation) and the checklist for real-hardware
testing.

## Project layout

```
src/core/              Platform-independent engine (C++17, no dependencies)
src/backend/wasapi/    Windows Core Audio / WASAPI backend
src/backend/sim/       Simulated devices (tests and non-Windows development only)
src/app/               Qt 5.15 desktop application (widgets, pages, .ui, resources)
src/cli/               Command line tool
tests/                 Automated tests (DSP units + end-to-end scenarios)
installer/             Inno Setup script
tools/                 Icon generator, MinGW deployment script
cmake/                 MinGW-w64 cross-compilation toolchain
docs/                  Architecture, building, testing, third-party notices
```
