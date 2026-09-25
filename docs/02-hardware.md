# 02 — Hardware & environment

## 1. The two machines

| | Pioneer XDJ-RX3 (source) | Denon DJ Prime GO (target) |
|---|---|---|
| SoC | NXP i.MX6 (Quad), ARMv7 **soft-float** | Rockchip RK3288, ARMv7 **hard-float** |
| Kernel | Linux 3.0.101 | Linux 6.1.111-inmusic PREEMPT_RT |
| OS | BusyBox / in-house init | Buildroot 2023.02.11, systemd |
| Display | 1280×800 landscape, RGB565 | 800×1280 portrait, RGB32, triple-buffered `rockchipdrmfb` |
| Touch | tsc2007 resistive (`/dev/tsc2007_2-0048`) | ILI2117 capacitive (evdev `/dev/input/event0`) |
| Audio | 3× CS4344 DACs + ESAI ADC | single `JP11` 4-channel codec (`hw:1,0`) |
| Controls | EUP / SUB microcontrollers over SPI | ALSA MIDI "PRIME GO Control Surface" (seq 16:0) |
| USB | 2 host ports + sub-MCU | 1 rear USB-A host port |
| RAM / storage | — | 2 GB / root 466 MB, `/data` persistent |

The CPU difference does **not** matter for running `rbp`: the Linux kernel
executes soft-float EABI5 userspace natively. What matters is the *userland
libraries*, which is why the RX3 rootfs is used in a chroot.

## 2. Prime GO access

```sh
ssh root@<your-primego-address>
```

Use your own device's hostname/IP and password. The examples in this repo use
`YOUR_PRIMEGO` as a placeholder. The device has **no internet access**, so
fetch anything you need on the workstation and `scp` it over.

For non-interactive SSH (used by scripts), provide the password via
`SSH_ASKPASS`:

```sh
cat > /tmp/askpass.sh <<'EOF'
#!/bin/sh
echo 'YOUR_PASSWORD'
EOF
chmod +x /tmp/askpass.sh
SSH_ASKPASS=/tmp/askpass.sh DISPLAY=:0 \
  ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  root@YOUR_PRIMEGO "uname -a"
```

## 3. The soft-float chroot

`rbp` and its libraries are soft-float glibc 2.13. To run them we assemble an
RX3 userland at `/data/primebox/rootfs` and `chroot` into it. The Prime GO kernel
provides `/lib32`/hard-float tooling for the host side; the chroot is pure RX3
soft-float.

### Contents of `/data/primebox/rootfs`

```
/data/primebox/rootfs/
├── lib/ld-linux.so.3 -> ld-2.13.so     soft-float loader
├── lib/libc.so.6, libpthread.so.0, ... RX3 glibc 2.13
├── usr/lib/                            libstdc++, DirectFB 1.4, freetype, ...
├── usr/lib/directfb-1.4-0/             ← RX3 core's module dir (ABI 9)
│   ├── systems/libdirectfb_fbdev.so    ← our rebuilt, patched module
│   ├── inputdrivers/…                  linux_input (VT gate removed)
│   └── wm/libdirectfbwm_default.so
├── root/pdj/rbp                        ← rbp-audio
├── root/gui/                           fonts + pset + imagedata
├── usr/bin/edb_streamd, kill_daemon    DeviceSQL
├── bin/sh -> busybox
├── usr/share/alsa/                     ALSA config
├── media/usb1/sda1                     bind-mount point for the stick
├── dev/ proc/ sys/ tmp/                bind-mounted from host
└── etc/mtab -> /proc/mounts
```

### Bind mounts (run after every reboot)

`scripts/device/setup-chroot.sh` recreates everything:

```sh
umount /data/primebox/rootfs/dev 2>/dev/null; rm -rf /data/primebox/rootfs/dev
mkdir -p /data/primebox/rootfs/dev
mount --bind /dev  /data/primebox/rootfs/dev
mount --bind /proc /data/primebox/rootfs/proc
mount --bind /sys  /data/primebox/rootfs/sys
mount --bind /tmp  /data/primebox/rootfs/tmp
```

`/tmp` is shared, so FIFOs created on the host (e.g. `/tmp/udev_usb1`) are the
same objects `rbp` sees inside the chroot.

### Device stubs

`rbp` talks to i.MX6 devices that do not exist on the Rockchip. They are
emulated so that `open()` succeeds and the corresponding thread does not spin
or crash:

| Device | Type | Why |
|---|---|---|
| `/dev/gpiodrv` | regular file + `read()` shim | `GpioManager` blocks/polls on it; `fbshim` returns a byte and parks the poll so it does not burn CPU |
| `/dev/subucom_spi{1,2}.0`, `/dev/subucom_spi_rdy{3,4}.0` | FIFOs | polled SPI to the (absent) sub-MCU; FIFOs block instead of busy-spin |
| `/dev/hidg0` | FIFO | USB HID gadget for rekordbox HID mode |
| `/dev/printkdrv0`, `/dev/tsc2007_2-0048` | regular files | ioctl-only; the touch one is replaced by the shim |
| `/dev/paudiog0` | **absent** | if present, JUCE tries gadget-audio ioctls and exits; absence makes it skip cleanly |
| `/dev/mem` | chmod 000 | `rbp` maps i.MX6 physical registers; on Rockchip that is real hardware and must be blocked |

### `/etc/mtab`

Pioneer's VFS resolver (`vfs_getfsys`) reads `/etc/mtab`. Inside the chroot it
is symlinked to `/proc/mounts`:

```sh
ln -sf /proc/mounts /data/primebox/rootfs/etc/mtab
```

Without it, the USB mount is never classified as `vfat` and browsing fails.

## 4. Cross toolchain

The shims must be **soft-float EABI5, GLIBC_2.4-only** to load under the RX3
glibc 2.13. The working toolchain is Ubuntu's `arm-linux-gnueabi-gcc`.

```bash
sudo apt-get install gcc-arm-linux-gnueabi libc6-dev-armel-cross
```

Crucially, link against the **RX3 rootfs libraries**, not the host's, so that
symbol versioning is correct:

```bash
RX3=extracted/XDJRX3-rootfs
arm-linux-gnueabi-gcc -O2 -march=armv5t -mfloat-abi=soft \
    -fno-stack-protector -fPIC -shared \
    -o knobshim.so knobshim.c \
    -I"$RX3/usr/include" \
    -L"$RX3/lib" -L"$RX3/usr/lib" \
    -lpthread -lc -Wl,-rpath-link,"$RX3/lib:$RX3/usr/lib"
```

Verify with:

```bash
arm-linux-gnueabi-objdump -T knobshim.so | grep GLIBC | sort -u
# must only reference GLIBC_2.4 / GLIBC_2.7 (no 2.17!)
```

Common pitfall: `clock_gettime` and `dlopen` on modern hosts resolve to
`GLIBC_2.17`/`GLIBC_2.34`. On the target they must bind to `librt.so.1`
(`GLIBC_2.4`) and `libdl.so.2` respectively. Linking against the RX3 libs (or
the assembly `.symver` trick used in `audioshim.c`) fixes it.

`scripts/shims/Makefile` encodes all of this; `make RX3=<rootfs>` builds every
shim.

## 5. DirectFB

`rbp` renders through DirectFB 1.4. The stock RX3 `libdirectfb_fbdev.so`
assumes an i.MX6 fbdev (16 bpp, 1280×800) and crashes on the Rockchip DRM fb.
PrimeBox ships a **patched fbdev module source/diff** in
`tools/build-directfb/`; see [03 — Display](03-display.md) for the build.

## 6. Disk budget

`/data` is tight. A trimmed chroot is roughly:

| Item | Size |
|---|---|
| glibc + libstdc++ + DirectFB + freetype | ~30 MB |
| `rbp` | 7.6 MB |
| `gui/` fonts + imagedata | ~15 MB |
| EDB daemons | <1 MB |
| shims + scripts | <1 MB |

Keep `/data` above ~10 MB free. The USB watcher log and strace logs grow;
rotate/truncate them (`: > /data/trc-p.log`).
