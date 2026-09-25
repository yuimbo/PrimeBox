# PrimeBox

**Run the Pioneer DJ XDJ-RX3 *rekordbox* standalone player on a Denon DJ Prime GO.**

PrimeBox documents and reproduces how the ARM32 `rekordbox` player application
(`rbp`) extracted from **XDJ-RX3 firmware v1.20** is made to run, natively and
fully usable, on a **Denon DJ Prime GO** (Rockchip RK3288) that ships with
Denon's Engine OS.

Everything on the Prime GO works: the 7″ touchscreen, all buttons / knobs /
faders / jog wheels, the rear USB-A rekordbox library, the scrolling waveforms,
and real 4-channel audio out of the master and headphone outputs.

> PrimeBox is an **interoperability / preservation** project. It contains **no
> Pioneer/AlphaTheta firmware, no `rbp` binary, no Denon software and no
> rekordbox content.** You supply your own firmware; the tooling in this repo
> extracts and patches it. See [NOTICE.md](NOTICE.md).

---

## Status

| Subsystem | State | Doc |
|---|---|---|
| Firmware (`.UPD`) extraction | ✅ Working | [docs/01](docs/01-firmware-extraction.md) |
| Display / rotation (800×1280 portrait fb → 1280×800 UI) | ✅ Working | [docs/03](docs/03-display.md) |
| Touchscreen (ILI2117) | ✅ Working | [docs/04](docs/04-touchscreen.md) |
| Buttons / knobs / faders / jog | ✅ Working | [docs/05](docs/05-controls.md) |
| USB stick + rekordbox DB (`export.pdb`) | ✅ Working | [docs/06](docs/06-usb.md) |
| Audio (44.1 kHz, master + headphones, mixer, EQ, xfader) | ✅ Working | [docs/07](docs/07-audio.md) |
| Beat FX + Sound Color FX | ✅ Working | [docs/08](docs/08-effects.md) |
| Boot menu (touch / knob, Engine OS fallback) | ✅ Working | [docs/09](docs/09-runtime-launcher.md) |

The authoritative "how it works" is the documentation in [`docs/`](docs/).
It describes the one working design — not the dead ends along the way.

---

## The idea in one diagram

```
                    XDJ-RX3 firmware v1.20 (.UPD)
                              │
       ┌──────────────────────┴───────────────────────┐
       │ 1. decrypt (AES-256-CBC / cryptoloop)         │  tools/rx3dec
       │ 2. extract pdj/rbp  (ARM32, soft-float)       │
       │ 3. apply interoperability patches             │  tools/patch-rbp
       └──────────────────────┬───────────────────────┘
                              │  rbp-audio
                              ▼
   Denon Prime GO  ──  soft-float glibc-2.13 chroot  ──  rbp
        │                    ( /data/primebox/rootfs )
        │
        ├── display   : rebuilt DirectFB fbdev module (rotate + force real fb format)
        ├── touch     : fbshim.so  (ILI2117 evdev → RX3 tsc2007 protocol)
        ├── controls  : knobshim.so   (Prime GO MIDI → rbp keycodes)
        ├── audio     : audioshim.so   (JUCE/ALSA → hw:1,0 4-channel JP11 codec)
        ├── usb       : usb-watch.sh + native DeviceSQL import
        └── daemons   : edb_streamd, systemd launcher entry
```

---

## Install

Two commands on a workstation with Docker, after putting the firmware key at
`keys/aes256.key` (see [keys/README.md](keys/README.md)):

```bash
make                      # firmware -> build/primebox.tar.gz  (~2 min, first run)
make install HOST=root@PRIMEGO.local
```

`make` downloads and extracts the official XDJ-RX3 v1.20 firmware, patches `rbp`,
builds the shims, the DirectFB display module and the boot menu, and assembles the
runtime tree. `make install` copies it to `/data/primebox` on the deck, installs the
boot-menu unit, and reboots. Power on: the menu shows; pick **REKORDBOX** with the
touchscreen or the browse knob, or let it fall through to **ENGINE OS**.

```bash
make uninstall HOST=root@PRIMEGO.local   # remove everything, back to stock
```

The detailed walkthrough, prerequisites and troubleshooting are in
**[TUTORIAL.md](TUTORIAL.md)**. To understand how it works, read
[`docs/`](docs/).

## Repository layout

```
PrimeBox/
├── Makefile                  build + install (make, make install HOST=…)
├── docker/Dockerfile         arm-linux-gnueabi toolchain image
├── TUTORIAL.md               step-by-step setup & first run
├── docs/                     findings & subsystem documentation
├── keys/                     where you put the firmware key (not committed)
├── scripts/
│   ├── device/               shell scripts that run on the Prime GO
│   └── shims/                runtime LD_PRELOAD shims (C) -> build/
└── tools/
    ├── extract-firmware.sh    .UPD → extracted/ (one command)
    ├── rx3dec/               .UPD → ISO decryptor (Rust)
    ├── patch-rbp/            rbp binary patcher + patch reference
    ├── build-directfb/       patched DirectFB fbdev module + diff
    ├── assemble-chroot.sh    build the runtime tree from extracted/ + build/
    ├── install.sh            copy to the deck, install the boot unit
    ├── launcher/             the boot menu (C, static ARM)
    └── debug/                probes and test helpers (not shipped)
```

Nothing under `build/` or `extracted/` is committed: `build/` is generated,
`extracted/` holds your own copy of the (copyrighted) firmware.

## What is *not* in this repo

To stay clean, PrimeBox deliberately excludes:

* any `.UPD`, `.iso`, firmware image or `rbp` executable,
* Denon / Engine OS files,
* rekordbox music databases or media,
* the firmware **decryption key** (you supply `keys/aes256.key` yourself; it is
  gitignored — see [keys/README.md](keys/README.md)),
* built binaries of the shims (build them from source).

---

## Follow the project

Updates and new developments are posted on Instagram:

[![Instagram: @i.erhan.es](https://img.shields.io/badge/Instagram-%40i.erhan.es-E4405F?logo=instagram&logoColor=white)](https://instagram.com/i.erhan.es)

**[@i.erhan.es](https://instagram.com/i.erhan.es)** — questions, build help, and
progress updates.

---

## Credits

* **[nsaintot / cdj3k-emu](https://github.com/nsaintot/cdj3k-emu)** — huge
  thanks to this project: its `tools/upd-decrypt` helper showed us how Pioneer
  `.UPD` firmware images are decrypted (LUKS keyfile → losetup/cryptsetup),
  which is what put us on the path to the XDJ-RX3 cryptoloop scheme. Without
  their work this project would not exist.
* Pioneer DJ / AlphaTheta — XDJ-RX3 and the GPL source distribution that made
  this research possible.
* Denon DJ / inMusic — Prime GO hardware.
* The community reverse-engineering work on Pioneer firmware containers
  (LUKS / cryptoloop) that this project builds on.
* DirectFB, JUCE, ALSA and the many open-source components in the RX3 firmware.

See [NOTICE.md](NOTICE.md) for licensing details.
