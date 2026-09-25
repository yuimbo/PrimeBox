# tools/build-directfb

Patched **DirectFB 1.4.0** `systems/fbdev` module for the Prime GO's
`rockchipdrmfb`.

`rbp` renders through DirectFB. The stock RX3 fbdev driver assumes a 16 bpp,
1280×800, pannable i.MX6 framebuffer. The Prime GO has a fixed 32 bpp,
triple-buffered DRM framebuffer with no rotation. Without changes the modeset is
rejected (`EINVAL`), DirectFB corrupts its layer bookkeeping, and `rbp` crashes.

## Files

| File | What |
|---|---|
| `build.sh` | fetches DirectFB 1.4.0, applies the diff, builds the module |
| `directfb-1.4.0-fbdev.diff` | the PrimeBox changes to `systems/fbdev` (+ `compat_shim.c`) |
| `compat.c` + `compat.map` | versioned `fcntl`/`__fdelt_chk` shim for the RX3 glibc 2.13 |

There are **no upstream DirectFB sources in this repository**. The diff is the only
third-party-derived artefact; DirectFB is LGPL-2.1, and the diff (and any build you
make from it) remains under the LGPL.

The top-level `make` runs `build.sh` for you (into `build/libdirectfb_fbdev.so`);
run it by hand only to debug:

```bash
docker run --rm -v "$PWD":/src:ro -v "$PWD/extracted/XDJRX3-rootfs":/rx3:ro \
    -v "$PWD/build":/out -v primebox-directfb:/tmp/directfb \
    primebox-armel:18.04 sh /src/tools/build-directfb/build.sh
```

It clones DirectFB **1.4.0** (`DIRECTFB_1_4_0`), applies the diff, configures
`--with-gfxdrivers=none` against the RX3 rootfs as sysroot, builds `lib/direct`,
`lib/fusion`, `src` and `systems/fbdev`, and writes a stripped module to
`/out/libdirectfb_fbdev.so`. It fails if the module needs a `GLIBC` > 2.7 or is not
system ABI 9.

**Build against 1.4.0, not 1.4.16.** The RX3 core is 1.4.0 and its system-module
ABI is **9**; a 1.4.16 module declares ABI **10** and is rejected
(`No system found`). The `CoreSystemFuncs` layout must match too (0x58 vs 0x64).

## What the patch changes

1. **Serialise all fb ioctls** with a mutex: the DRM fb is unsafe under
   concurrent `FBIOPUT_VSCREENINFO`/`FBIOPAN_DISPLAY`.
2. **Force the real fb format** in `dfb_fbdev_set_mode()` and
   `dfb_fbdev_test_mode()`: before `FBIOPUT_VSCREENINFO`, overwrite
   `bits_per_pixel` and the colour bitfields from a live
   `FBIOGET_VSCREENINFO`, so the kernel accepts the modeset.
3. **Use the read-back state** after a rejected/clamped modeset for
   `shared->current_var` — never the rejected request.
4. **Software rotation + RGB565→RGB32 conversion** in
   `fbdev_rotate_primary()`, tiled and dirty-tile-skipping, from a
   system-memory layer buffer into the physical fb.
5. **Pan from a dedicated thread** (`rot_pan_thread`): `FBIOPAN_DISPLAY` blocks
   ~one refresh on `rockchipdrmfb`, so panning on the render thread halves the
   frame rate. `rot_pan_claim()` keeps the buffer being rotated into off screen.
6. **Force `DLBM_TRIPLE`** at layer init so flips occur (DirectFB's default
   `BACKSYSTEM` never calls the driver's `FlipRegion`, leaving the screen blank).

The module is built with `compat_shim.c` in `systems/fbdev` and `compat.c` in
`lib/direct`, which forward `fcntl`/`__fdelt_chk` via `syscall()` so the RX3
glibc 2.13 loader accepts them.

## Gotchas

* DirectFB 1.4's dependency tracking is broken: after editing `fbdev.c`, delete the
  object before rebuilding (`rm -f systems/fbdev/fbdev.lo systems/fbdev/.libs/fbdev.o`).
  `build.sh` does this.
* `Makefile.in` is gitignored upstream, so `build.sh` runs `autoreconf` before
  `configure`. Do not `git clean -x` the tree: it deletes the generated Makefiles.
* The module must be soft-float, reference only `GLIBC_2.4`/`GLIBC_2.7`
  (`arm-linux-gnueabi-objdump -T`), and declare system ABI **9** (`mov r1, #9`
  in `directfb_fbdev`). `build.sh` checks all three.
* Do not ship the RX3 `gfxdrivers/` directory into the chroot: the `gal` driver
  cannot initialise on the Prime GO and makes `graphics_core` fail.
* Do not set `layer-size` in `directfbrc`; do not rely on `layer-rotate`.
* The rotation direction comes from `DFB_ROTATE` (`left`/`right`/`180`) in
  `system_initialize`. PrimeBox uses `DFB_ROTATE=left`.
