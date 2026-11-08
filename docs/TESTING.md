# Testing AudioBridge

AudioBridge was developed without access to physical Windows audio hardware. This document
states precisely **what has been verified, and how**, and gives a checklist for verification on
real hardware. Nothing in the simulated tests is presented as a hardware result.

## 1. Automated tests (simulated endpoints)

`ctest` runs `audiobridge_tests`. The end-to-end scenarios drive the **real** engine code
(`AudioEngine`, `AudioInputProcessor`, `AudioOutputMixer`, resampler, drift controller, DSP)
through `SimulatedBackend`, whose devices behave like real ones:

| Simulated device | Rate | Ch | Format | Clock error | Delivery |
|---|---|---|---|---|---|
| "Bluetooth" headset mic | 16 kHz | 1 | 16-bit | +150 ppm | bursty, 7.5 ms packets |
| USB mic | 44.1 kHz | 2 | 16-bit | -60 ppm | 10 ms |
| Laptop mic array | 48 kHz | 2 | float | +20 ppm | 10 ms |
| AUX (laptop) output | 48 kHz | 2 | float | -35 ppm | 10 ms |
| "Bluetooth" speaker | 44.1 kHz | 2 | 16-bit | +90 ppm | 20 ms |
| USB speaker | 96 kHz | 2 | 32-bit | -10 ppm | 10 ms |

Each microphone plays its own test tone (300 / 700 / 1100 Hz). Outputs are recorded and
analysed: tone levels (±1 dB), isolation of unrouted tones (< -60 dB), dropouts (no silent
gap), underrun/resync counters.

| # | Scenario (from the specification) | Test | Result |
|---|---|---|---|
| 1 | One Bluetooth mic → one AUX speaker | `scenario01_bt_mic_to_aux` | pass (simulated) |
| 2 | One USB mic → one Bluetooth speaker | `scenario02_usb_mic_to_bt_speaker` | pass (simulated) |
| 3 | Two mics → one speaker | `scenario03_two_mics_one_speaker` | pass (simulated) |
| 4 | One mic → two speakers | `scenario04_one_mic_two_speakers` | pass (simulated) |
| 5 | Two mics → two speakers (isolation) | `scenario05_two_mics_two_speakers_isolation` | pass (simulated) |
| 6 | Three mics → multiple speakers | `scenario06_three_mics_multiple_speakers` | pass (simulated) |
| 7 | Bluetooth disconnection and reconnection | `scenario07_bluetooth_disconnect_reconnect` | pass (simulated) |
| 8 | USB device disconnection | `scenario08_usb_output_removed` | pass (simulated) |
| 9 | Different sample rates (engine 44.1/48/96 kHz, devices 16-96 kHz, live rate change) | `scenario09_engine_sample_rates` | pass (simulated) |
| 10 | Mute and unmute (input, solo, output, global, pause/resume) | `scenario10_mute_unmute_solo` | pass (simulated) |
| 11 | Individual channel volume (gain, fader, route, output, master; measured in dB) | `scenario11_individual_volumes` | pass (simulated) |
| 12 | Routing matrix changes while streaming (40 random toggles) | `scenario12_routing_changes_while_streaming` | pass (simulated) |
| 13 | 30 minutes continuous operation (virtual time, drifting + bursty clocks) | `scenario13_thirty_minutes_continuous` | pass (simulated): 0 underruns, 0 resyncs, constant latency, drift controller locked to the real clock difference |
| - | Device busy / open failure → automatic retry | `engine_open_failure_retry` | pass (simulated) |
| - | Stop/start, configuration round trip | `engine_stop_start_and_config_roundtrip` | pass (simulated) |
| - | Feedback guard trips on sustained howling, can be re-armed | `engine_feedback_guard` | pass (simulated) |
| - | Real threads: 4000+ routing/parameter changes while streaming | `threaded_stress_routing_changes` | pass; clean under ThreadSanitizer and AddressSanitizer/UBSan |

DSP unit tests: resampler (8/16/44.1/48/96 kHz conversions, SNR > 60 dB, ±500 ppm variable
ratio without dropouts), all sample formats, channel mapping (mono, mic arrays, 5.1), broadcast
ring overrun detection, noise suppressor (> 10 dB noise reduction, tone preserved within 3 dB),
high-pass, gate, limiter (never exceeds -1 dBFS), device-name disambiguation, connection-type
classification from real Windows device paths, routing matrix and feedback-risk analysis.

The suite runs on Linux (GCC, Clang) and as a Windows executable (MinGW build, run under
Wine). The GitHub Actions workflow additionally builds and runs it with MSVC on a Windows runner.

## 2. Windows build verified under Wine

The cross-compiled Windows executables were run under Wine 9 with a virtual (null) sound device:

* `AudioBridgeCLI.exe --list` enumerated the endpoints through `IMMDeviceEnumerator` with
  their friendly names, endpoint IDs, default flags and mix formats from the property store.
* `AudioBridgeCLI.exe --route 1 2` opened event-driven shared-mode capture and render streams,
  streamed for several seconds at 48 and 44.1 kHz engine rates with 0 underruns, and stopped
  cleanly.
* `AudioBridge.exe` (GUI) started, listed the devices, created a route through the routing
  matrix, started monitoring with both streams running, and shut down cleanly.

Wine's audio stack is not Windows: this proves the API usage and code paths, **not** audio
quality or behaviour with real drivers.

## 3. Real-hardware checklist (to be done on the target PC)

Hardware: Lenovo LOQ 15IRX9, OnePlus Nord Buds 2r, Bluetooth speaker on the AUX jack.

1. `AudioBridgeCLI.exe --list`: every device shows its real name (earbuds appear as
   "Headset Microphone (…)" / "Headphones (…)"), Bluetooth/USB/built-in correctly detected.
2. Earbuds mic → laptop AUX (speaker connected by cable): enable both, route, start.
   Speak; level meters move; sound on the speaker; latency figure on the Outputs page.
3. Leave it running 30+ minutes: underruns stay at 0 (or stop increasing after the first
   minute), no audible clicks, latency stable.
4. Switch the earbuds off: input shows "Disconnected - waiting", AUX keeps running. Switch
   them on again: the microphone reconnects by itself and audio returns.
5. Unplug/replug the AUX cable. Behaviour depends on the audio driver: many Realtek
   drivers keep one "Speakers" endpoint and silently switch between the laptop speakers and
   the jack; drivers with a separate headphone endpoint report it as "Unplugged" and
   AudioBridge resumes it when the cable is plugged back in.
6. Two microphones (earbuds + laptop mic) → AUX: both audible; mute/solo/volume each.
7. One microphone → AUX + another output; use *Sync delay* to align them.
8. Toggle routes in the matrix while speaking: changes are immediate, no clicks.
9. Change sample rate / buffer size in Settings while streaming: brief restart, then audio.
10. Laptop mic → laptop speakers: feedback warning appears; at high volume the feedback
    guard mutes the output.
11. Noise suppression on the earbuds mic in a noisy room: steady noise clearly reduced.
12. Install / uninstall with `AudioBridge-Setup-x64.exe`.

Please report any step that fails together with the output of `AudioBridgeCLI.exe --list`.
