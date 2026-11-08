# AudioBridge architecture

## Threads

```
 GUI thread (Qt)            Engine thread (EngineController)        Audio threads (MMCSS "Pro Audio")
 ───────────────            ─────────────────────────────────        ─────────────────────────────────
 pages, widgets  ──post──►  AudioEngine (control plane)              capture thread per input device
 30 fps meter timer         device table, slots, recovery,           playback thread per output device
   reads AudioGraph         opens/closes WASAPI streams
   atomics (MeterHub)  ◄─── snapshots (coalesced, queued)
                            IMMNotificationClient ──► debounced refresh
```

* The GUI never touches audio APIs. Opening a Bluetooth device can take seconds; it happens
  on the engine thread.
* Audio threads never lock, never allocate, never wait on the control plane. They read
  parameters from atomics and exchange audio through lock-free ring buffers.

## Real-time graph (`src/core/AudioGraph.h`)

```
capture i ─► AudioInputProcessor ─► InputSlot[i].ring (BroadcastRing, engine rate, stereo float)
                                           │  one writer, any number of readers
             ┌─────────────────────────────┼─────────────────────────────┐
             ▼                             ▼                             ▼
   route reader (i,o)            route reader (i,p)                  ...
   cursor + DriftController      cursor + DriftController
   + AudioResampler + gain       + AudioResampler + gain
             ▼                             ▼
   AudioOutputMixer o            AudioOutputMixer p   ─► volume/master/mute ─► limiter + feedback guard ─► device format
```

* **Fixed slots** (16 inputs × 16 outputs) are pre-allocated. Enabling a device assigns it a
  slot; routes are an atomic flag + gain per (input slot, output slot) cell. Routing changes
  therefore never free memory an audio thread might be using - no RCU or hazard pointers needed.
* **BroadcastRing**: single-producer / multi-consumer ring. The writer never waits; each
  reader keeps its own cursor and detects (seqlock-style) if it fell too far behind.
* **Per-route clock domain bridging**: each route reader has a latency target, a PI drift
  controller acting on the smoothed buffer fill, and a variable-ratio windowed-sinc resampler
  (engine rate → output device rate, ratio corrected by the controller). Underruns re-prime
  the route with a larger adaptive margin (jitter buffer); the margin is slowly released
  after long clean periods.
* **Input processing** (capture thread): device format → float → stereo (mono-sum for mics)
  → resample to engine rate → gain → high-pass → noise suppression → gate → meter → fader /
  mute / solo → ring.
* **Output processing** (playback thread): mix routes → output volume × master (smoothed) →
  peak limiter (zero attack, -1 dBFS) with feedback guard → meter → channel mapping →
  device sample format.

## Modules

| Module (suggested name) | Implementation |
|---|---|
| AudioDeviceManager | `IAudioBackend` (`core/AudioBackend.h`), `WasapiBackend` (enumeration, properties, topology, notifications) |
| AudioCaptureEngine / AudioPlaybackEngine | `WasapiStream` (event-driven shared-mode capture/render, MMCSS) |
| AudioGraph | `core/AudioGraph.h` |
| AudioMixer | `AudioOutputMixer` (`core/AudioMixer.*`) |
| AudioRouter / AudioRoutingMatrix | `AudioRoutingMatrix` (routes by endpoint ID, feedback-risk analysis) + route cells in the graph |
| AudioProcessingEngine | `core/AudioProcessing.*` (HPF, noise suppressor, gate, limiter, meters, smoothing) and `AudioInputProcessor` |
| AudioResampler | `core/AudioResampler.*` + `DriftController` |
| AudioSettings | `EngineConfig` / `EngineSettings` (core) + `AppSettings` (QSettings persistence) |
| MainWindow | `app/MainWindow.*` + pages in `app/pages` |

## Device identity and recovery

Devices are identified by their WASAPI endpoint ID (stable across reboots and reconnects of
the same device). Configuration and routes are keyed by that ID. When a stream reports
`AUDCLNT_E_DEVICE_INVALIDATED`, the engine closes it, keeps the slot and routes, shows
"Disconnected - waiting", and re-opens it (with back-off) once the same endpoint is active
again. Format changes (the user changing the device format in Windows) re-open the stream
with the new format.

## Virtual microphone (future)

A virtual microphone requires a Windows audio driver (e.g. a SysVAD-derived WaveRT driver or a
user-mode APO-based solution that exposes a capture endpoint). In AudioBridge's architecture it
would be added as an additional `IAudioStream` implementation behind a normal output slot:
the mixer already produces a fully mixed, processed stereo bus per output, so feeding a
driver's shared buffer requires no engine changes. Version 1 deliberately does not ship a driver.
