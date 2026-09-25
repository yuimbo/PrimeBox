# 12 — Runtime symbol & ABI audit

A canary run only exercises the code paths it happens to reach. To be sure
nothing *else* is missing, the whole runtime is audited statically (this is what
`make` runs):

```bash
WORKSTATION$ python3 tools/audit-runtime.py build/primebox/rootfs \
    build/primebox/rootfs/root/pdj/rbp \
    build/primebox/rootfs/usr/lib/knobshim.so \
    build/primebox/rootfs/usr/lib/audioshim.so \
    build/primebox/rootfs/usr/lib/fbshim.so \
    build/primebox/rootfs/usr/lib/directfb-1.4-0/systems/libdirectfb_fbdev.so \
    build/primebox/rootfs/usr/lib/libdirectfb-1.4.so.0 \
    build/primebox/rootfs/usr/lib/libdirect-1.4.so.0 \
    build/primebox/rootfs/usr/lib/libfusion-1.4.so.0
```

The tool walks the whole chroot (not a fixed list) and checks, for every
binary and for `rbp` + all shims + the DirectFB core and module:

| Check | Meaning |
|---|---|
| every `NEEDED` library exists in the chroot | no missing `.so` at load |
| every undefined symbol is defined somewhere in the chroot | no `symbol lookup error` |
| every `GLIBC_*` version reference is provided by the chroot glibc | no `version ... not found` |

Weak/optional symbols (`__gmon_start__`, `_Jv_RegisterClasses`,
`_ITM_*`) are ignored: the loader does not require them.

A clean run prints:

```
OK - no missing symbols/versions
```

## DirectFB module ABI

The DirectFB **module ABI is a separate contract** the symbol audit does not
cover: the RX3 core compares the ABI version a module passes to
`direct_modules_register` against its own `DFB_CORE_SYSTEM_ABI_VERSION`.
The core is DirectFB **1.4.0**, so the expected value is **9**:

```bash
# the module must register ABI 9 (mov r1, #9 in directfb_fbdev)
WORKSTATION$ objdump -d extracted/libdirectfb_fbdev-140.so | \
    grep -A6 '<directfb_fbdev>:' | grep 'mov.*r1'
```

and its `CoreSystemFuncs` table must have the same size as the core's
(`0x58` = 22 pointers for 1.4.0). A 1.4.16 build emits `mov r1, #10` and a
`0x64` (25-pointer) table; the core rejects it with
`ABI version ... (10) does not match 9!` and `No system found`.

## What the audit cannot see

* **Runtime ABI of `CoreDFB` / `CoreSystemInfo` internals.** The ABI version
  number is the only compatibility contract; the audit checks that number (via the
  module build), not the struct layout. Always build the module from the RX3
  core's DirectFB version (1.4.0) so both match.
* **Kernel/driver contracts** (fb ioctls, DRM, ALSA). Those are checked by
  running `dfbinfo` and `rbp` (see [03 — Display](03-display.md),
  [11 — Troubleshooting](11-troubleshooting.md)).
