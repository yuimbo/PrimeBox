# PrimeBox documentation

These documents describe the **working** design of PrimeBox: how the
XDJ-RX3 `rekordbox` player (`rbp`) is extracted and made to run on a Denon
Prime GO. They are the authoritative reference and describe only what shipped
and works — not exploratory dead ends.

## Contents

| # | Document | What it covers |
|---|---|---|
| 00 | [Overview](00-overview.md) | Big picture, architecture, data flow |
| 01 | [Firmware extraction](01-firmware-extraction.md) | `.UPD` formats, LUKS vs cryptoloop, key, extracting the ISO/rootfs |
| 02 | [Hardware & environment](02-hardware.md) | XDJ-RX3 vs Prime GO, the chroot, toolchain |
| 03 | [Display](03-display.md) | fb geometry, rotation, DirectFB rebuild, pixel formats |
| 04 | [Touchscreen](04-touchscreen.md) | ILI2117 → RX3 tsc2007 protocol, calibration, debounce |
| 05 | [Controls](05-controls.md) | MIDI surface mapping, jog/pitch/fader maths, keycodes |
| 06 | [USB & rekordbox DB](06-usb.md) | hotplug, DeviceSQL, phantom USB2, property block |
| 07 | [Audio](07-audio.md) | JP11 codec, JUCE/ALSA shim, 4-channel routing |
| 08 | [Effects](08-effects.md) | Beat FX and Sound Color FX keycodes |
| 09 | [Runtime & launcher](09-runtime-launcher.md) | boot flow, daemons, launcher menu |
| 10 | [Memory map](10-memory-map.md) | runtime addresses and patch reference |
| 11 | [Troubleshooting](11-troubleshooting.md) | symptoms → cause → fix |
| 12 | [Runtime audit](12-runtime-audit.md) | whole-chroot symbol/ABI audit, module ABI |

## Conventions

* **VA** = virtual address in `rbp`. `rbp` is a non-PIE ARM32 ELF with load
  bias `0x8000`, so `file offset = VA - 0x8000`.
* *stock* = unmodified `rbp` from XDJ-RX3 firmware v1.20
  (md5 `4f2efcfc0c9e3f539289f863acfddcc6`).
* *patched rbp* = the build produced by `tools/patch-rbp/rbp_patch.py`
  (md5 `3706c68f7242779d46afa09f35a39acf`).
* All ARM assembler is ARM (not Thumb) unless stated.
