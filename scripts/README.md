# scripts/

* **[`device/`](device/)** — shell scripts that run **on the Prime GO** (installed
  into `/data/primebox` by `tools/install.sh`).
* **[`shims/`](shims/)** — the runtime C `LD_PRELOAD` libraries, cross-compiled
  by the top-level `make` into `build/`.

## `device/`

| Script | Purpose |
|---|---|
| `env.sh` | shared paths, sourced by the others |
| `setup-chroot.sh` | bind `/dev /proc /sys /tmp`; create the RX3 device stubs and USB FIFOs |
| `start-rb.sh` | stop Engine OS, start `edb_streamd` + `rbp` + the USB watcher, stay foreground, restart Engine OS on exit |
| `usb-watch.sh` | USB hotplug daemon (`start`/`stop`/`status`) |
| `launcher.sh` | boot-menu wrapper: Engine OS setup, the menu, the chosen command |
| `launcher.conf` | menu entries, default, countdown |
| `primebox-launcher.service` | the boot unit (installed on the rootfs) |

Build the payload and install with the top-level `make` / `make install` (see
[TUTORIAL](../TUTORIAL.md)); you do not copy these by hand.

## `shims/`

| Source | Output | Role |
|---|---|---|
| `knobshim.c` | `knobshim.so` | Prime GO MIDI → `rbp` keycodes, USB/library glue, FX defaults |
| `audioshim.c` | `audioshim.so` | JUCE/ALSA → `hw:1,0` 4-channel JP11 codec |
| `fbshim.c` | `fbshim.so` | fb ioctl shim (16 bpp) + ILI2117 → tsc2007 touch + poll/read/pacing |

Build and check (the top-level `make` does both):

```sh
make -C scripts/shims RX3=/path/to/extracted/XDJRX3-rootfs OUT=$PWD/build
```

`check` verifies only `GLIBC_2.4`/`GLIBC_2.7` and no hard-float tag. See
[`shims/README.md`](shims/README.md) for details and gotchas. Debug probes
(`crashcatch`, `dfbprobe`, `pcprof`, `seqinject2`, `peek`, …) live in
[`../tools/debug`](../tools/debug/).
