# PrimeBox handoff — state, build, install, run, debug, pitfalls, todo

**Read this first.** PrimeBox runs the XDJ-RX3 `rbp` rekordbox player on a
rooted Denon Prime GO (RK3288). This is the current, honest state.

Device in this session: `primego.local` (root SSH). All work is under `/data`; the
host rootfs is only touched by `make install` (one boot unit).

---

## 1. Current state

### Working

* Firmware extraction (`.UPD` → ISO → trees) — `tools/extract-firmware.sh`.
* `rbp` patching → `build/rbp` (md5 `3706c68f7242779d46afa09f35a39acf`).
* Soft-float chroot boots: RX3 glibc 2.13, `bin/sh`, ALSA, `edb_streamd`,
  `rbp` all run.
* DirectFB 1.4.0 core + patched fbdev module: init succeeds end to end
  (`clipboard → … → wm_core` all 0), `DirectFBCreate -> 0`.
* rbp renders its UI rotated onto the panel at **~47 fps**, touch/knob/audio
  work, the rear USB stick is detected, and rekordbox plays from it.
* Boot menu: at power-on, tap or browse-knob-pick REKORDBOX, or fall through to
  Engine OS. Verified across reboots; killing rbp returns to Engine OS.
* Whole-chroot symbol/ABI audit is clean (`tools/audit-runtime.py`).
* All shims build soft-float EABI5, `GLIBC_2.4/2.7` only.

### How it fits together

`make` builds `build/primebox.tar.gz`; `make install HOST=…` copies it to
`/data/primebox`, installs the boot unit on the rootfs and reboots. See
[docs/09](docs/09-runtime-launcher.md) for the boot menu, layout and safety.

The graphics-core blocker was three chroot/config problems, not a DirectFB bug:
`gfxdrivers/libdirectfb_gal.so` present, 1.4.16 leftovers in
`directfb-1.4-0/{wm,interfaces}` needing `libdirect-1.4.so.6`, and DirectFB's
default BACKSYSTEM buffer mode (which never calls the driver's `FlipRegion`). All
three are handled by the build/install now.

### Next

* Hands-on: touch mapping and jog wheels under a loaded track (music and the
  rekordbox library already work).
* Further fps: the ceiling is rbp's software blit. Options: accelerate
  `Bop_16_Kto_Aop` (rebuild `libdirectfb` generic with NEON / -O2 armv7), or
  make the DirectFB surface 32 bpp so rbp's blits hit the faster `Bop_32` paths.
* `gui_task` is ~36 % of one core: ~48 % of samples in the rotate loop,
  ~25 % in DirectFB's software blit, ~20 % libc memcpy.

---

## 2. How to build

```bash
WORKSTATION$ make                      # -> build/primebox.tar.gz (~1 min warm)
```

Prerequisites: Docker, plus the firmware key at `keys/aes256.key`
([keys/README.md](keys/README.md)). `make` builds the `primebox-armel` image on
first use (native on arm64 and amd64, no qemu), then runs
`tools/extract-firmware.sh` (download + decrypt + extract, skips existing steps),
patches rbp, builds the shims, the DirectFB module and the boot menu, assembles
`build/primebox/rootfs` and packs the payload.

Individual steps, when you need them:

```bash
WORKSTATION$ make image                # build the toolchain image
WORKSTATION$ make firmware             # extracted/ only
WORKSTATION$ make shims launcher directfb rbp   # individual build outputs
WORKSTATION$ make debug                # build/debug/ probes + helpers
```

`build/` and `extracted/` are generated and gitignored. The DirectFB module is
built from DirectFB 1.4.0 (the RX3 core's version, system ABI 9) by
`tools/build-directfb/build.sh`; see that directory's README for the details and
the two build routes.

---

## 3. How to install

```bash
WORKSTATION$ make install HOST=root@YOUR_PRIMEGO.local     # install / update
WORKSTATION$ make uninstall HOST=root@YOUR_PRIMEGO.local     # remove
```

`tools/install.sh` copies the payload to `/data/primebox`, writes the boot unit to
`/usr/lib/systemd/system` on the rootfs, and reboots. The rootfs write leaves it
writable until the reboot (this firmware cannot remount it read-only again once
written, and Engine OS fills the ~7 MB free space with caches while it is writable),
which is why the install always reboots. It keeps `/data/primebox/launcher.conf`
and the player's `root/settings` across updates.

The chroot is assembled by `tools/assemble-chroot.sh` from the stock RX3 rootfs +
gui partition + the ISO `gui/` dir + our build outputs. **Do not overlay the ISO's
`lib`/`usr`** (their symlinks are flattened to 0-byte files).

---

## 4. How to run

Power on → boot menu → REKORDBOX (tap it, or turn + push the browse knob).
Without input the menu starts Engine OS after 5 s. Details and safety:
[docs/09](docs/09-runtime-launcher.md).

```bash
PRIMEGO# sh /data/primebox/launcher.sh        # show the menu now
PRIMEGO# sh /data/primebox/start-rb.sh         # rekordbox directly
PRIMEGO# RBP_ENV="KNOB_VERBOSE=1 PRIMEGO_TIMING=1" sh /data/primebox/start-rb.sh
```

Every exit path restarts Engine OS. By hand: `systemctl start engine.service`.

---

## 5. Boot integration (verified)

* `primebox-launcher.service` (on the rootfs, `Before=engine.service`) starts at
  ~5 s. With no input, Engine OS starts at ~10 s. Picking REKORDBOX starts rbp
  and the USB watcher. Killing rbp brings Engine OS back.
* The unit must be on the rootfs (`/usr/lib/systemd/system`): `/etc` is an overlay
  and systemd reads the rootfs units at boot. Do **not** `rm` it by hand: that
  creates an overlay *whiteout* which hides the unit; use `make uninstall`.
* `launcher.sh` runs `/usr/Engine/Scripts/setup-prerequisites.sh` first. At boot
  Engine OS has not done it yet, and it loads `snd_seq_midi` (the control surface:
  browse knob and rbp controls).

---

## 6. Debugging & probing — best practices

All probes live in `tools/debug/` (`make debug` → `build/debug/`), are soft-float
`.so`s, and are `LD_PRELOAD`ed (preload order = first `ioctl`/`open`/`read` wins).
Copy them to `/data/primebox-debug` and run rbp with the probe first:

```bash
PRIMEGO# pkill -9 -f "root/pdj/rbp"; sleep 2
PRIMEGO# chroot /data/primebox/rootfs env DFB_ROTATE=left \
    LD_PRELOAD=/usr/lib/<probe>.so:/usr/lib/fbshim.so:/usr/lib/audioshim.so:/usr/lib/knobshim.so \
    /lib/ld-linux.so.3 /root/pdj/rbp -a
```

### Tools

* **`dfbprobe.so`** — clears DirectFB `quiet`, logs `DirectFBSetOption`,
  every `dfb_core_part_initialize` result, and flip rate (`fps N`) to
  `/tmp/dfbprobe.log`. **Start here for any DirectFB failure.**
* **`vsyncprobe`** (`tools/debug/vsyncprobe.c`, static) — times
  `FBIOPAN_DISPLAY` / `FBIO_WAITFORVSYNC` on `/dev/fb0` (stop Engine OS first).
* **`pcprof.so`** — SIGPROF sampler for one thread (`PCPROF_THREAD=gui_task`,
  `PCPROF_DELAY`, `PCPROF_SAMPLES`); writes `/tmp/pcprof.txt` + `.maps`.
  Resolve offsets with `arm-linux-gnueabi-nm -n` in the `primebox-armel` image.
* **`crashcatch.so`** — installs a `SIGSEGV` handler and logs
  `pc, addr, lr, r0..r12` to `/tmp/crash.log`, the stack to `/tmp/crash.stack`,
  and the address space to `/tmp/crash.maps`. **This is the single most useful
  probe** — put it first in `LD_PRELOAD`.
* **`fusprobe.so`** — interposes `fusion_reactor_attach`, `fusion_reactor_new`,
  `fusion_shm_pool_create`, `fusion_object_create` and logs the pointer + caller to
  `/tmp/fus.log`. Use it to see whether a fusion object/pool is created at all.
* **`peek`** (`tools/debug/peek.c`) — read 32-bit words from another process
  (`peek PID ADDR [COUNT]`, `/proc/PID/mem`); how the USB/browse state was found.
* **`seqinject2`** (`tools/debug/seqinject2.c`) — inject control-surface events
  into the shims (`seqinject2 --dest C:P cc 15 5 1`, `note 15 6 on`).
* **`fbshim.c`** debug logs: `/tmp/dfbdig9.log` (`ROTINIT:`, `flip:`),
  `/tmp/dfbdig8.log` (window stack / WM).
* **built-core debug logs** (only in the built core): `/tmp/dfbdig2.log`
  (`dfb_core_create`), `dfbdig3.log` (`dfb_system_lookup`), `dfbdig4.log`
  (module dirs + ABI mismatch), `dfbdig5.log` (module `dlopen`), `dfbdig6.log`
  (`fusion_enter`), `dfbdig7.log` (**core parts initialized — tells you which
  part fails**), `dfbdig8.log` (`dfbdig9/10/11`).
* **`dfbinfo`** (in the chroot: `arm-none-linux-gnueabi-dfbinfo`) — initializes
  DirectFB without `rbp`; `no-hardware` matters (`DFBARGS` uses **commas**, not
  spaces: `DFBARGS="no-hardware,no-cursor,system=fbdev,fbdev=/dev/fb0"`).
* **`strace`** is in the chroot (`/usr/bin/strace`), but it segfaults on this
  kernel — prefer `crashcatch`.
* **`objdump`/`nm` on the workstation** resolve ARM addresses: `objdump -T` (symbols +
  GLIBC versions), `objdump -d` (disasm), `objdump -R` (relocations). To map a
  crash address to a symbol, get the library's `r-xp` base from `/tmp/crash.maps`
  and subtract.

### Recipes that worked

* ABI mismatch: `dfbinfo` prints
  `Direct/Modules: ABI version of 'X' (N) does not match M!` — the module's ABI
  immediate is the `mov r1, #imm` in `<directfb_fbdev>` (`objdump -d | grep -A6`).
* GLIBC leak: `objdump -T <lib> | grep -oE 'GLIBC_[0-9.]+' | sort -u` must be a
  subset of the chroot glibc's versions.
* Module-dir mismatch: the core loads from its own `MODULEDIR` (grep
  `directfb-1.4-N` in the core's `libdirect-*.so`), not from what the script
  assumes.

---

## 7. Traps & pitfalls

1. **The built 1.4.16 module declares ABI 10; the RX3 core wants 9.** This was
   the original "no system found". Build against 1.4.0 — the `CoreSystemFuncs`
   size must match too (0x58 vs 0x64).
2. **The `gal` gfxdriver makes `graphics_core` fail** on the Prime GO (even with
   `no-hardware`). `--with-gfxdrivers=none`, and `assemble-chroot.sh` removes the
   `gfxdrivers` dir.
3. **1.4.16 leftovers in `directfb-1.4-0/`** (`wm/` and some `interfaces/`
   modules need `libdirect-1.4.so.6`) make `wm_core` fail. Ship the stock RX3 ones.
4. **`lib/direct` built with a modern toolchain leaks GLIBC 2.15/2.28.** Link
   `compat.c`/`compat.map` into it.
5. **Without `layer-buffer-mode=triple` the screen stays blank** although rbp runs
   normally (BACKSYSTEM never calls the driver's `FlipRegion`).
6. **rbp sets DirectFB `quiet`.** A failed `DirectFBCreate` shows only
   `DS_HW_Glib3_DFB.c <293>` and then a NULL-deref segfault at
   `init_Resource+0xdc` (`pc=0x1a357c`). Use `dfbprobe.so` to un-silence it.
7. **The fbdev module must not call `clock_gettime`** (or anything resolving to
   a GLIBC > 2.7 symbol): the toolchain binds it to `GLIBC_2.17` and rbp crashes
   (rc=139). Use `syscall(SYS_clock_gettime, …)`, and check with
   `objdump -T <module> | grep -oE 'GLIBC_[0-9.]+' | sort -u`.
8. **`FBIOPAN_DISPLAY` blocks ~one refresh on rockchipdrmfb**, while
   `FBIO_WAITFORVSYNC` is a no-op. Never pan on the render thread (the module
   pans from its own thread).
9. **Do not overlay the ISO's `lib`/`usr` onto the chroot.** The ISO symlinks are
   flattened to 0-byte files; you will clobber `ld-linux.so.3`, `libc.so.6`, etc.
   `assemble-chroot.sh` overlays only the rootfs + gui + our outputs.
10. **Nothing under `/data` can start at boot on its own** (see §5). The boot unit
    must be on the rootfs.
11. **Never `rm /etc/systemd/system/primebox-launcher.service` by hand.** `/etc` is
    an overlay; the `rm` creates a *whiteout* that hides the unit from the rootfs
    even after you write it again. Use `make uninstall`.
12. **The rootfs cannot be remounted read-only again once written** on this
    firmware, and Engine OS fills the ~7 MB free space with caches while it is
    writable. Stop Engine OS, write, then reboot.
13. **Host `ps` is procps, not busybox.** `ps w` shows only TTY processes. Use
    `pgrep -f` / `pkill -f`.
14. **The browse knob sends CC5 as a relative delta** (1..63 = +n, 65..127 = −n,
    0 = none), like `knobshim` decodes it — not an absolute position.
15. **`gcc`/`qemu` ICE under colima (amd64 on arm64).** Use the native arm64
    `primebox-armel:18.04` image (`docker/Dockerfile`) — no qemu, `-O2` works.
16. **busybox `head` needs `-n N`**, not `-N`; busybox `find`/`xargs` lack
    `-delete`/`-0` — use `-exec ... {} \;`.
17. **macOS `tar` writes AppleDouble `._*` files.** `assemble-chroot.sh` removes
    them; use `COPYFILE_DISABLE=1 tar`.
18. **`chroot`'s `env` is the chroot's `/usr/bin/env`.** `chroot … env VAR=… /lib/ld-linux.so.3 <prog>`.
19. **`setup-chroot.sh` chmods `/dev/mem` to 000** (on the bind-mounted
    `/dev`, so it affects the host). Intentional; restore with `chmod 640`.
20. **`fbshim` lies about the fb geometry to *everyone*** (including the
    module). The module reads the real geometry with a raw `syscall(SYS_ioctl)`.

## 8. TODO

See §1 "Next". The graphics-core investigation is closed (§1 "Fixed in session 2").

### Known-good checkpoints (so you do not redo them)

* `objdump -T <module> | grep GLIBC` → only `GLIBC_2.4/2.7`.
* `objdump -d <module> | grep -A6 '<directfb_fbdev>:'` → `mov r1, #9`.
* `objdump -t <module> | grep system_funcs` → `00000058`.
* `python3 tools/audit-runtime.py build/primebox/rootfs <binaries...>` →
  `OK - no missing symbols/versions`.
* `dfbprobe.so` in `LD_PRELOAD` → `/tmp/dfbprobe.log` shows every
  `part_initialize ... -> 0`, `DirectFBCreate -> 0`, and `fps 47`.
* `dfbinfo` with the RX3 stack + no `gfxdrivers` →
  `Generic Software Rasterizer` + `WM: Default`.
