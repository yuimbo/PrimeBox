# 11 — Troubleshooting

Consolidated symptom → cause → fix. Each subsystem doc has more detail.

## Startup / launch

| Symptom | Cause | Fix |
|---|---|---|
| `rbp` exits immediately, no window | chroot `/dev` not populated (`/dev/fb0` missing) | run `/data/primebox/setup-chroot.sh` after every reboot |
| Black screen, no response | `engine.service` still owns `/dev/fb0` | `systemctl stop engine.service edisksd.service` |
| `rbp` hangs before the UI appears | stale `guard_LocalDBServer` lock, or a frozen stale `rbp` holding it | `kill -9` stale `rbp`/`edb_streamd`, `rm -f /tmp/guard_LocalDBServer /tmp/req_LocalDBServer` |
| Process runs but no UI threads | `/dev/gpiodrv` is a FIFO and the main thread blocks on `read` | ensure `setup-chroot.sh` makes it a regular file; `fbshim` must intercept `read`/`poll` |
| Device reboots / panics | old display stack modesetting the DRM fb | use the patched DirectFB fd dev module + 60 fps pacing; never run the raw stack |

## Display

| Symptom | Cause | Fix |
|---|---|---|
| UI rotated 90° | wrong/absent `DFB_ROTATE` | use `DFB_ROTATE=left` |
| Half-width, duplicated, wrong colours | DirectFB creating RGB32 while `rbp` writes RGB565 | ensure `fbshim.so` (16 bpp) is first in `LD_PRELOAD` |
| Flicker | rotating directly into the visible fb buffer | use the driver's system-memory source surface |
| Very low FPS / one core pegged | `FBIOPAN_DISPLAY` not throttled | use the patched shim's 60 fps `FBIOPAN_DISPLAY` |
| `EINVAL` on `FBIOPUT_VSCREENINFO` | modeset for 16 bpp/`yv=1280` on fixed 32 bpp DRM fb | patched DirectFB fbdev overrides the format from `FBIOGET_VSCREENINFO` |

## Touch

| Symptom | Cause | Fix |
|---|---|---|
| No touch response at all | browse caution id non-zero blocks dispatch | apply `0x2dc228`/`0x2dc46c` patches; clear `0x05a191fc` |
| Only slow drags register | hysteresis eats the first frame | debounce burst (2 down frames) in `fbshim` |
| Horizontal mirror | firmware `invertX` not cancelled | correct transform (`*lx = py`) |
| Taps work, drag-scroll doesn't | list-scroll deadlock | apply `0x363774`/`0x363794` patches |
| Touch works but UI lags | `GpioManager` busy-spins at RT priority | `poll()`/`read()` shims for `/dev/gpiodrv` |

## Controls

| Symptom | Cause | Fix |
|---|---|---|
| No controls at all | no ALSA sequencer subscription | `knobshim` must create/subscribe to seq `16:0` |
| Presses do nothing, LED/UI ignores | wrong keycode or `op` | check the keycode table; analog must use `OP_VALUE`, buttons `OP_PRESS`/`OP_RELEASE` |
| Knob scrolls erratically | encoder delta not decoded as two's complement | `v >= 64 ? v-64 : v` |
| Pitch fader dead | sent as `0x4107` (TempoRange, `op` must be 0) | send `0x4109` with `OP_VALUE` |
| Pitch always negative | `0x2000f` integer-literal bug | use `8192.0f` |
| Deck 2 fader controls deck 1 | mixer route defaults to player 0 | set `*(0x01149f54) = 0x01149f10` |
| Keys crash after loading a track | `FixedAddressArray` throw / corrupt target | apply the `fixthrow` patches |

## USB

| Symptom | Cause | Fix |
|---|---|---|
| Stick ejects after ~30 s | `edisksd.service` running | stop it in the launcher |
| Only "USB2" with 0 tracks | Kind 3 not suppressed | force Kind 3 = 0, `uiConnectedMedia = 0x2` |
| "USB1" 0 GB / 0 songs | property block empty / no analysis | run `edb_streamd`; symlink `/etc/mtab`; ensure PM stubs patched |
| `881466368.0 GB` | capacity u64 word order | write high word at +132, low at +136 |
| Mount not detected | FIFO missing, or `mount` without preceding `umount` | create `/tmp/udev_usb1`; send `umount` then `mount` |
| "Please select a source" | browse mode 3 (search) | select USB1 → `DISPMODE_LIST` (5) |
| "USB Error. Remove the device." caution | something wrote to `/tmp/udev_usbctn*` | never write connect events there; GPIO stub fix |

## Audio

| Symptom | Cause | Fix |
|---|---|---|
| PLAY lights but nothing moves | audio callback not running | fix device list patch + `audioshim` |
| `sampleRate:0` abort | control device not intercepted | intercept `snd_ctl_*` in `audioshim` |
| `-EBUSY` opening `hw:1,0` | `engine.service` holds the codec | stop it first |
| exits after `ENOTTY` | `/dev/paudiog0` stub exists | remove it |
| Headphones silent | cue/master mirroring | mirror master → headphone channels |
| Crackle / wrong speed | unsupported format or period | S24_LE, 4 ch, 44100, 64-frame period |

## Shims / toolchain

| Symptom | Cause | Fix |
|---|---|---|
| `version 'GLIBC_2.17' not found` | linked host glibc | link RX3 libs; version-script `GLIBC_2.4` |
| `symbol dlopen, version GLIBC_2.4 not defined` | `dlopen` bound to libc 2.34 | link `libdl.so.2`, use `.symver` |
| `cannot be preloaded … ignored` then works | harmless loader notice | verify the shim's log fills; ignore the stderr line |
| DirectFB changes silently ignored | broken dependency tracking | `rm -f systems/fbdev/fbdev.lo systems/fbdev/.libs/fbdev.o` before `make` |
| `libdirectfb_fbdev.so` won't load | NEEDED soname mismatch | `patchelf --replace-needed libdirectfb-1.4.so.6 …so.0` |

## Recovering the stock device

```sh
systemctl start engine.service
```

If you ever disabled it: `systemctl enable engine.service`. Engine OS should
boot normally again; PrimeBox does not modify the host rootfs.
