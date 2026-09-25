# PrimeBox tutorial — from zero to rekordbox on a Prime GO

This is the complete, working procedure. Read it once before starting.

**What you need**

* A Denon DJ Prime GO with root SSH access (see [docs/09 §1](docs/09-runtime-launcher.md)).
* A workstation with **Docker** and ~4 GB free.
* The XDJ-RX3 v1.20 firmware key at `keys/aes256.key`
  ([keys/README.md](keys/README.md)).
* ~5 minutes for the first build, ~2 minutes for each install.

**Conventions**

* `WORKSTATION$` — commands on your PC.
* `PRIMEGO#` — commands on the device (over SSH).
* `$REPO` — the path to this repository.

> ⚠️ `rbp` on the wrong display stack has historically locked the device up hard
> enough to panic the kernel. The build and install here use the stable stack, and
> every failure path returns to Engine OS. If anything goes wrong, a reboot always
> gives you stock Engine OS again.

---

## Part A — Build and install

```bash
WORKSTATION$ cd "$REPO"
WORKSTATION$ make
```

`make` does everything, in the `primebox-armel` Docker image (built on first use):

1. download the official XDJ-RX3 v1.20 firmware and extract it to `extracted/`
   (`tools/extract-firmware.sh`; needs `keys/aes256.key`),
2. patch `rbp` → `build/rbp` (`tools/patch-rbp`),
3. build the runtime shims → `build/{knobshim,audioshim,fbshim}.so`,
4. build the patched DirectFB fbdev module → `build/libdirectfb_fbdev.so`
   (from DirectFB 1.4.0, `tools/build-directfb`),
5. build the boot menu → `build/primebox-launcher`,
6. assemble the runtime tree → `build/primebox/rootfs` (`tools/assemble-chroot.sh`),
7. pack `build/primebox.tar.gz`.

Expect `shims OK (soft-float, GLIBC 2.4/2.7)` and
`payload: build/primebox.tar.gz`. The first run downloads the firmware and builds
the Docker image; later runs are ~1 minute.

Then install onto the deck:

```bash
WORKSTATION$ make install HOST=root@YOUR_PRIMEGO.local
```

This copies the payload to `/data/primebox`, installs the boot-menu unit on the
rootfs, and reboots. The deck needs ~400 MB free on `/data` and root SSH.

---

## Part B — Use it

Power on. The boot menu appears in landscape:

```
PRIMEBOX                                        boot menu
  REKORDBOX (XDJ-RX3)
  ENGINE OS
```

* Pick an entry by **tapping it**, or by turning the **browse knob** and pushing it.
* Do nothing: after 5 s it starts **ENGINE OS**.
* **REKORDBOX** starts `rbp`; when it exits (or fails) the deck returns to
  Engine OS by itself, with sshd reachable. A normal reboot is always Engine OS.

Change the menu in `/data/primebox/launcher.conf`:

```
default = ENGINE OS        # entry the countdown picks
timeout = 5                # seconds; 0 = wait for input

# DJ Apps
REKORDBOX (XDJ-RX3) | sh /data/primebox/start-rb.sh
ENGINE OS |
```

`touch /data/primebox/launcher.skip` skips the menu on the next boot. Run the
menu by hand with `PRIMEGO# sh /data/primebox/launcher.sh`.

**rekordbox:** plug a rekordbox-exported USB stick into the rear port. Press
**VIEW** once to open the library (the Prime GO has no USB1 source button; the
shim maps VIEW to it until a source is chosen). Load a track, press PLAY.

---

## Part C — Update, remove, troubleshoot

```bash
WORKSTATION$ make && make install HOST=root@YOUR_PRIMEGO.local   # update
WORKSTATION$ make uninstall HOST=root@YOUR_PRIMEGO.local          # remove, stock Engine OS
```

`make install` keeps `/data/primebox/launcher.conf` and the player's
`root/settings` across updates.

Debug tools (built with `make debug`, copied to `/data/primebox-debug`):

* `seqinject2` — inject Prime GO control-surface events, e.g.
  `seqinject2 --dest CLIENT:0 note 15 1 on` (LOAD deck 1).
* `peek PID ADDR` — read words from rbp's memory (`/proc/PID/mem`).
* `crashcatch.so`, `dfbprobe.so`, `pcprof.so` — LD_PRELOAD probes
  (`tools/debug/README.md`).

Logs live in `/data/primebox/log/` (`launcher.log`, `rbp.log`, `edb.log`,
`usbwatch.log`) and `/tmp/{knobshim,audioshim}.log`.

| Symptom | Cause |
|---|---|
| No menu at boot | boot unit missing from the rootfs (e.g. after an Engine OS update); run `make install` again |
| Menu appears, Engine OS only | `default`/`timeout` in `launcher.conf`, or the countdown expired |
| rekordbox exits immediately | see `/data/primebox/log/rbp.log`; the deck returns to Engine OS |
| USB stick not seen | watcher before rbp opened the FIFO; `sh /data/primebox/usb-watch.sh status` |
| Empty library / "select a source" | press VIEW once (USB1 source), see [docs/06](docs/06-usb.md) |
| Device looks bricked | it is not: a normal reboot gives stock Engine OS; `make uninstall` removes PrimeBox |

Deeper subsystem troubleshooting: [docs/11](docs/11-troubleshooting.md).
