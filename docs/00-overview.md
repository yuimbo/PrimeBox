# 00 — Overview

PrimeBox runs the **Pioneer DJ XDJ-RX3 standalone rekordbox player** on a
**Denon DJ Prime GO**. The two machines are very different, but both are ARMv7
Linux devices, and the XDJ-RX3 firmware happens to build its player as
soft-float ARM32 — which the Prime GO's kernel executes natively.

There is no emulation involved. The real `rbp` binary from the XDJ-RX3 firmware
runs directly, with a set of thin shims translating the Prime GO's hardware
into what `rbp` expects.

## The pieces

```
┌──────────────────────────────────────────────────────────────────────┐
│                          Denon Prime GO                              │
│  Rockchip RK3288 · 800×1280 portrait panel · ILI2117 touch           │
│  JP11 4-ch audio codec · 1 rear USB-A · MIDI control surface         │
│                                                                      │
│  ┌──────────────────────── /data/primebox/rootfs (chroot) ────────────────┐ │
│  │  soft-float glibc 2.13 + RX3 libs + DirectFB 1.4               │ │
│  │                                                                │ │
│  │   build/rbp  ──  the XDJ-RX3 rekordbox player                  │ │
│  │      ▲  ▲  ▲                                                   │ │
│  │      │  │  └── knobshim.so   Prime GO MIDI → RX3 keycodes     │ │
│  │      │  └───── audioshim.so   JUCE/ALSA → hw:1,0 (4ch)         │ │
│  │      └──────── fbshim.so  fb ioctl + touch translation     │ │
│  │                                                                │ │
│  │   libdirectfb_fbdev.so (rebuilt) ── rotation + RGB565→RGB32    │ │
│  └────────────────────────────────────────────────────────────────┘ │
│        ▲              ▲                ▲               ▲            │
│     /dev/fb0     /dev/input/event0   MIDI 16:0     /tmp/udev_usb1   │
│   (800x1280x32)   (ILI2117 evdev)   control surface   (hotplug)     │
└──────────────────────────────────────────────────────────────────────┘
```

## Why each piece is needed

| Mismatch | XDJ-RX3 has | Prime GO has | Solution |
|---|---|---|---|
| CPU float ABI | soft-float ARM32 | hard-float ARMv7 kernel | soft-float chroot; kernel runs soft-float ELF fine |
| Display | 1280×800 landscape, RGB565 | 800×1280 portrait, RGB32, triple-buffered DRM fb | rebuilt DirectFB fbdev driver rotates + converts |
| Touchscreen | tsc2007 resistive via `/dev/tsc2007_2-0048` | ILI2117 capacitive evdev | `fbshim.so` synthesises the tsc2007 protocol |
| Controls | Pioneer front-panel MCUs (EUP/SUB) | ALSA MIDI "PRIME GO Control Surface" | `knobshim.so` maps MIDI → `sendKey()` |
| Audio | 3× discrete CS4344 DACs | single JP11 4-channel codec | `audioshim.so` multiplexes 4 channels onto `hw:1,0` |
| USB | 2 host ports + sub-MCU | 1 host port | `usb-watch.sh` + native DeviceSQL import |
| Music DB | internal EDB daemon | — | RX3 `edb_streamd` runs in the chroot |

## Data flow for a typical action

**Browsing a USB stick**

```
stick → kernel usb3 → usb-watch.sh mounts /media/usb1/sda1
      → bind-mount into chroot
      → write "mount /media/usb1/sda1" to /tmp/udev_usb1
      → rbp UsbMountManager → DbProxy → DbIF::mount('C')
      → DeviceSQL scans export.pdb → detect flag = 2
      → source list shows the drive, categories populate natively
```

**Loading + playing a track**

```
LOAD button → Prime GO MIDI note → knobshim → sendKey(0x4311)
      → rbp loads track + ANLZ analysis → waveform
PLAY button → knobshim → sendKey(0x4101)
      → DjEngineIF::play → PlayEngine clocked by the ALSA callback
      → audioshim feeds 4-channel S24_LE periods to hw:1,0 @ 44.1 kHz
      → master (ch 0/1) + headphones (ch 2/3)
```

## Repository map

See the top-level [README](../README.md). Each subsystem has a focused
document; the memory map and patch table live in
[10 — Memory map](10-memory-map.md) and
[`tools/patch-rbp/PATCHES.md`](../tools/patch-rbp/PATCHES.md).

## Prerequisites

* A Denon Prime GO with **root SSH** enabled (see [TUTORIAL](../TUTORIAL.md)).
* A Linux workstation with `arm-linux-gnueabi-gcc` (soft-float) and Docker.
* XDJ-RX3 firmware v1.20 and the key from [`keys/`](../keys/).
* ~2 GB free on the Prime GO's `/data` partition.

## Risks

* Running `rbp` without ptrace on the wrong display stack has historically
  **panicked the kernel** (the fb path). The shipped launcher uses the tuned,
  stable stack. Read [11 — Troubleshooting](11-troubleshooting.md) first.
* `/data` on the Prime GO is small (~50 MB free by default). The chroot +
  assets are trimmed to fit; do not add large files.
