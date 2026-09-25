# 07 — Audio

Audio is what makes `rbp` a *player* rather than a static UI: the ALSA callback
clocks the whole engine. This document covers the codec mismatch, the binary
patch, and the `audioshim` multiplexer.

## 1. Hardware mismatch

| | XDJ-RX3 | Prime GO |
|---|---|---|
| Outputs | 3 stereo DACs: master, headphones, booth (CS4344) | single `JP11` codec |
| Card | `hw:cs4344audiorev8,0/1/2`, `hw:esaics4344audio,0` | card 1 `JP11` = `hw:1,0` (`pcmC1D0p`) |
| Channels | separate stereo devices | **one 4-channel** device: 0/1 master L/R, 2/3 headphone L/R |
| Rate | 44.1 / 48 kHz | **44100 Hz only** |
| Formats | S24_LE, S32_LE | S16_LE, **S24_LE** (no S32/FLOAT) |

## 2. Why playback was frozen

`PlayEngine::update()` is called **only** from
`DjEngineIF::audioDeviceIOCallback`, which fires when ALSA completes a period.
No audio device ⇒ no callback ⇒ the playhead never advances, the waveform never
scrolls, and jog/pitch do nothing. Four separate problems had to be fixed.

### 2a. CPU board revision check

`ALSAAudioIODeviceType::scanForDevices()` only builds the device list when
`SystemStats::board_is_rev(BOARD_REV_RX3)` matches an i.MX6 CPU revision
(`0x700`) parsed from `/proc/cpuinfo`. On Rockchip it returns 0, so the device
list is empty.

**Patch** (in `rbp_patch.py`):

| VA | stock | patched | meaning |
|---|---|---|---|
| `0x3c665c` | `1a000054` (`bne`) | `ea000054` (`b`) | always configure the RX3 device list |

### 2b. Engine OS holds the codec

If `engine.service` is running, `snd_pcm_open("hw:1,0")` returns `-EBUSY`.
The launcher stops it first.

### 2c. `sampleRate == 0` crash

JUCE calls `getDeviceProperties()`, which opens the ALSA **control** device
(`snd_ctl_open("hw:cs4344audiorev8", …)`). Since that card doesn't exist, ALSA
returns `-ENOENT`; JUCE bails out of property enumeration and leaves the
supported-rate list empty. `AudioDeviceManager` then defaults the rate to `0`
and `DjEngineIF::audioDeviceAboutToStart()` aborts:

```
ERROR: sampleRate:0 != OVER_SAMPLING_RATE:44100
```

`audioshim` therefore also intercepts the **control** API
(`snd_ctl_open`/`snd_ctl_close`/`snd_ctl_pcm_info`) and reports a valid 44.1 kHz
device.

### 2d. `/dev/paudiog0` gadget path

If a `/dev/paudiog0` stub exists, JUCE opens it and issues USB-gadget audio
ioctls that fail with `ENOTTY`, then `exit(0)`. `setup-chroot.sh` ensures the node
does **not** exist, so `open()` returns `ENOENT` and JUCE cleanly skips the
gadget path.

## 3. `audioshim.so`

`audioshim.c` is an `LD_PRELOAD` library that presents the RX3's multiple
devices as the Prime GO's single 4-channel codec and multiplexes between them.

### Intercepted API

| Category | Functions |
|---|---|
| PCM | `snd_pcm_open`, `snd_pcm_close`, `snd_pcm_hw_params_*`, `snd_pcm_sw_params_*`, `snd_pcm_prepare`, `snd_pcm_link`, `snd_pcm_writei`, `snd_pcm_readi` |
| Control | `snd_ctl_open`, `snd_ctl_close`, `snd_ctl_pcm_info` |
| Scheduling | `pthread_setaffinity_np`, `sched_setaffinity`, `sched_setscheduler` (stubbed to avoid RT cores starving userspace) |

### Negotiated parameters (verified live)

| Parameter | Value |
|---|---|
| Access | `SND_PCM_ACCESS_RW_INTERLEAVED` |
| Format | `SND_PCM_FORMAT_S24_LE` |
| Rate | 44100 |
| Channels | 4 |
| Period size | 64 frames |
| Periods | 2 |

At 44.1 kHz and 64 frames/period this is ~689 periods/s. The pump runs
continuously with no drift.

### Channel multiplexing

`rbp` opens independent master / headphone (and booth) streams. `audioshim`
interleaves them into the hardware's four channels and issues one blocking
`snd_pcm_writei` per period:

```
logical stream 0 (master)     → hw ch 0, 1
logical stream 1 (headphones) → hw ch 2, 3
```

Capture reads (`snd_pcm_readi`) are stubbed with silence so JUCE's input side
does not error.

To hear audio on the headphones without pressing Cue, master is also mirrored
to the headphone pair.

### glibc 2.13 linking

`dlopen`/`dlsym` must resolve to `libdl.so.2` at `GLIBC_2.4`, not to `libc`
`GLIBC_2.34`. `audioshim.c` uses assembly `.symver` bindings for the `libdl`
functions and links against the RX3 `libdl.so.2`:

```bash
arm-linux-gnueabi-gcc -march=armv5t -mfloat-abi=soft -fPIC -shared -O2 \
    -o audioshim.so audioshim.c extracted/XDJRX3-rootfs/lib/libdl.so.2
```

## 4. Mixer routing and values

`rbp` defaults every mixer channel to deck 1. On real RX3 hardware the panel
MCU sets the input→player route; on the Prime GO the shim does it at startup:

```c
*(volatile uint32_t *)0x01149f54 = 0x01149f10;  /* mixer ch 2 -> player 2 */
```

Analogue controls are sent as normalised floats (`send_rx_key_f`), which is what
`IKeyManager::sendKey` expects for mixer parameters. Faders, trims, EQs,
crossfader and the colour knob all act on the live DSP.

Startup defaults make the mixer immediately usable (faders 1.0, trims/EQs 0.5,
crossfader 0.5).

## 5. Verification

```sh
# inside the chroot logs
cat /tmp/audioshim.log        # negotiated params + write counters

# on the host: confirm the codec and stream
aplay -l                       # card 1: JP11
cat /proc/asound/card1/pcm0p/sub0/hw_params   # while rbp plays
```

Expected: `S24_LE`, 4 channels, 44100 Hz, period 64.

| Symptom | Cause |
|---|---|
| play button lights, no movement | audio callback not running (device not opened) |
| `sampleRate:0` abort | control-device interception missing |
| `-EBUSY` on open | `engine.service` still running |
| `ENOTTY` then exit | `/dev/paudiog0` stub present |
| silence on headphones | master not mirrored / cue not routed |
| distorted / wrong speed | wrong format (S32/FLOAT) or period size |
