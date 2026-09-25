# 03 — Display

Getting `rbp` to draw correctly was the hardest part of the port. This document
describes the working stack.

## 1. The mismatch

| | XDJ-RX3 | Prime GO |
|---|---|---|
| Resolution | 1280×800 **landscape** | 800×1280 **portrait** |
| Framebuffer | `mxcfb`, RGB565 (16 bpp) | `rockchipdrmfb`, RGB32 (32 bpp) |
| Buffering | single / pannable | triple-buffered (`yres_virtual=3840`, `line_length=3200`) |
| Rotation | panel is already landscape | DRM planes expose **no rotation property** |

`rbp` asks DirectFB for a 1280×800 **RGB565** window. On the Prime GO:

1. DirectFB's stock fbdev driver issues `FBIOPUT_VSCREENINFO` for 16 bpp /
   `yres_virtual=1280`. The DRM framebuffer is fixed at 32 bpp / 3840 rows, so
   the kernel returns `EINVAL`.
2. DirectFB does not handle that gracefully: its layer bookkeeping is corrupted
   and `rbp` segfaults in `DS_HW_Core_Surface_Create`, often taking the kernel
   down with it (the device boots with `panic_on_oops=1`).
3. Even if the modeset succeeded, the image would be the wrong orientation and
   the wrong pixel format.

Hardware/kernel rotation is not an option: the Rockchip DRM planes have no
rotation property and the kernel fb `rotate` field is a no-op. DirectFB's
`layer-rotate` config option is unimplemented.

## 2. The working stack

Three coordinated pieces:

```
 rbp  ──renders RGB565 1280×800──►  DirectFB (RX3 libs)
                                        │
                          rebuilt libdirectfb_fbdev.so
                          • serialises fb ioctls
                          • forces real fb format on set/test mode
                          • rotates 1280×800 RGB565 → 800×1280 RGB32
                                        │
                                        ▼
                              /dev/fb0  800×1280×32 (rockchipdrmfb)

 LD_PRELOAD fbshim.so (PART 1)
   FBIOGET_VSCREENINFO → 1280×800, 16 bpp, RGB565 bitfields, yv=800
   FBIOGET_FSCREENINFO → line_length = 2560
   FBIOPUT_VSCREENINFO → accept silently
   FBIOPAN_DISPLAY     → 60 fps pacing + serialisation
```

* **`fbshim.so` PART 1** makes DirectFB create **RGB16** surfaces, matching
  what `rbp` actually writes. (If DirectFB is instead forced to RGB32, `rbp`'s
  RGB565 pixels are misread as packed 32-bit words: the UI appears half-width,
  duplicated and colour-shifted.)
* **Patched `libdirectfb_fbdev.so`** does the real work:
  * *serialise* every fb ioctl with a mutex (the DRM fb is not safe under
    concurrent modesets),
  * in `dfb_fbdev_set_mode()` / `dfb_fbdev_test_mode()`, **override the
    requested format with the live `FBIOGET_VSCREENINFO`** (32 bpp, real
    bitfields) so the kernel accepts the modeset,
  * after any rejected modeset, use the **read-back** `current_var` rather than
    the rejected request,
  * fall back to `FRONTONLY` when the fb cannot pan; keep the real
    `yres_virtual`,
  * software-rotate and convert RGB565→RGB32 in `fbdev_rotate_primary()`,
    rendering into a system-memory source buffer (no tearing),
  * force `DLBM_TRIPLE` at layer init so flips actually occur.
* Rotation direction is selected at runtime with `DFB_ROTATE=left` (90° CCW),
  `right`, or `180`. PrimeBox uses `DFB_ROTATE=left`.

## 3. Building the patched DirectFB fbdev module

> **Match the RX3 core's version and ABI.** The RX3 ships DirectFB **1.4.0**,
> whose system-module ABI is **9**. A module built from 1.4.16 declares ABI
> **10** and is silently rejected by the RX3 core
> (`Direct/Modules: ABI version ... (10) does not match 9!` →
> `No system found` → `DirectFBCreate()` fails). Build the module from the
> **DirectFB 1.4.0** source so the ABI and the `CoreSystemFuncs` layout match:

```bash
# 1. get DirectFB 1.4.0 source (Distrotech mirror)
git clone --branch DIRECTFB_1_4_0 https://github.com/Distrotech/DirectFB.git directfb

# 2. apply the PrimeBox fbdev changes
patch -p1 --fuzz=5 < /path/to/PrimeBox/tools/build-directfb/directfb-1.4.0-fbdev.diff

# 3. build only the fbdev module against the RX3 sysroot.
#    --with-gfxdrivers=none is required: the RX3 gal driver cannot
#    initialise on the Prime GO and makes graphics_core fail.
./autogen.sh --host=arm-linux-gnueabi \
    --prefix=/usr --with-gfxdrivers=none --disable-x11 ...
make -C systems/fbdev libdirectfb_fbdev.la

# 4. fix the NEEDED sonames to match the RX3 libs (.so.6 -> .so.0)
for s in libdirect-1.4.so.6 libfusion-1.4.so.6 libdirectfb-1.4.so.6; do
  patchelf --replace-needed $s ${s%.6}.0 \
      systems/fbdev/.libs/libdirectfb_fbdev.so
done
```

> **Modern-toolchain libc leak.** A modern cross toolchain resolves `fcntl` to
> `fcntl@GLIBC_2.28` and emits `__fdelt_chk@GLIBC_2.15`. The RX3 glibc 2.13
> has neither, so `libdirect` fails to load. Add
> [`tools/build-directfb/compat.c`](../tools/build-directfb/compat.c) +
> `compat.map` (which define both, versioned) to `lib/direct` and rebuild:
>
> ```bash
> cp tools/build-directfb/compat.c lib/direct/compat_shim.c
> # add compat_shim.lo to am_libdirect_la_OBJECTS in lib/direct/Makefile
> make -C lib/direct
> ```

See [`tools/build-directfb/README.md`](../tools/build-directfb/README.md) for
the full recipe and the exact configure flags used.

> **Build gotcha:** DirectFB 1.4's dependency tracking is broken. After editing
> `fbdev.c`, always `rm -f systems/fbdev/fbdev.lo systems/fbdev/.libs/fbdev.o`
> before `make`, or your changes are silently ignored.

Deploy to the directory the **RX3 core** actually loads from:

```
/data/primebox/rootfs/usr/lib/directfb-1.4-0/systems/libdirectfb_fbdev.so
```

The module directory name comes from the core's build (`directfb-1.4-0` for
1.4.0, `directfb-1.4-6` for 1.4.16). To be safe, keep
`directfb-1.4-6` as a symlink to `directfb-1.4-0` so either name resolves.

Also required, from the RX3 rootfs (already patched in the reference image):

* `inputdrivers/libdirectfb_linux_input.so` — VT gate removed (DirectFB must
  read evdev without owning a VT; there is no VT on the Prime GO).
* `wm/libdirectfbwm_default.so` — cursor gates removed.

## 4. `/usr/etc/directfbrc`

Minimal, working config inside the chroot:

See [`scripts/device/directfbrc`](../scripts/device/directfbrc); `start-rb.sh`
copies it into the chroot on every start. `layer-buffer-mode=triple` is required:
in the default BACKSYSTEM mode DirectFB never calls the driver's `FlipRegion`,
so nothing is rotated onto the panel.

Do **not** set `layer-size` or `layer-rotate` (both caused corruption or were
no-ops). `DFB_ROTATE=left` in the environment drives the patched driver.

## 5. Frame pacing and CPU

On the Rockchip DRM fb, `FBIOPAN_DISPLAY` blocks until the page flip lands
(~16.8 ms), and `FBIO_WAITFORVSYNC` returns immediately. The patched module
pans from a dedicated thread so rendering and rotation overlap the flip
(~47 fps, limited by rbp's own software rendering). `fbshim.c` also
enforces a minimum pan interval, `FBSHIM_PAN_NS` (default 16666666 = 60 Hz,
`0` = off).

Two other CPU hogs were eliminated on the way to a smooth UI (both in
`fbshim.c`):

* `GpioManager` polls `/dev/gpiodrv` which never signals → the shim's `poll()`
  parks the thread (`sleep`) instead of busy-spinning at RT priority.
* `read()` on `/dev/gpiodrv` returns one zero byte immediately, so the main
  thread never blocks during startup.

Result on hardware: `gui_task` gets ~54 jiffies/s, system ~81 % idle, steady
60 FPS.

## 6. Sanity checks

```sh
# inside the chroot / on device
cat /sys/class/graphics/fb0/virtual_size      # 800,1280
cat /sys/class/graphics/fb0/bits_per_pixel    # 32
```

While `rbp` runs:

```sh
# rotation + real fb geometry logged by the patched driver
cat /tmp/dfbdig9.log        # "ROTINIT: real_fb=800x1280 pitch=3200"
```

If the UI is sideways, `DFB_ROTATE` is wrong; if it is half-width/duplicated
with wrong colours, DirectFB is creating RGB32 surfaces (the 16 bpp fb shim is
not active — check `LD_PRELOAD`).
