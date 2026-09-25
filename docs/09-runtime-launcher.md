# 09 — Runtime, install & boot menu

What runs on the Prime GO, in what order, how it is installed, and how the boot
menu works.

## 1. Install

Everything lives in `/data/primebox`; the only file outside it is the boot unit.

```bash
WORKSTATION$ make                      # build/primebox.tar.gz
WORKSTATION$ make install HOST=root@PRIMEGO.local
WORKSTATION$ make uninstall HOST=root@PRIMEGO.local
```

`make install` copies the payload to `/data/primebox`, writes the boot unit into
`/etc/systemd/system` on the rootfs, and reboots. Writing to the rootfs leaves it
writable until the next boot (this firmware cannot remount it read-only again once
written), and Engine OS fills the ~7 MB free space with caches while it is writable,
so the install reboots immediately and waits for the deck to come back. It keeps
`/data/primebox/launcher.conf` and the player's `root/settings` across updates.

An official Engine OS update replaces the rootfs and removes the boot unit: run
`make install` again afterwards. The rootfs unit is the same mechanism Djinn uses for
root/SSH access (`/opt/djinn/reapply.sh`).

### Root SSH

`make install` needs root SSH. On a Djinn-modified deck this is root + password
(see the Djinn recovery notes for your deck). The stock firmware has no root SSH;
install the Djinn mod first.

## 2. Layout on the device

```
/data/primebox/
├── env.sh                    shared paths for the scripts below
├── start-rb.sh                run rbp + edb_streamd + the USB watcher
├── usb-watch.sh               USB hotplug daemon
├── setup-chroot.sh            bind /dev /proc /sys /tmp; device stubs; FIFOs
├── launcher.sh                boot menu wrapper (runs the chosen command)
├── launcher.conf              menu entries, default, countdown
├── primebox-launcher          the menu binary
├── primebox-launcher.service  the boot unit (also on the rootfs)
├── rootfs/                    the soft-float RX3 chroot (104 MB)
│   ├── root/pdj/rbp            the patched player
│   ├── usr/lib/{knobshim,audioshim,fbshim}.so
│   ├── usr/lib/directfb-1.4-0/systems/libdirectfb_fbdev.so
│   └── usr/etc/directfbrc
└── log/                       launcher.log, rbp.log, edb.log, usbwatch.log
```

`/usr/lib/systemd/system/primebox-launcher.service` + its
`multi-user.target.wants` link are the only files on the rootfs.

## 3. Boot menu

At power-on the deck shows the PrimeBox menu (landscape, like rbp) before Engine
OS. Pick an entry by tapping it, or turn the browse knob and push it. The first
real input stops the countdown.

| Piece | Role |
|---|---|
| `primebox-launcher` | draws the menu, prints the chosen command |
| `launcher.sh` | runs Engine OS setup, the menu, the chosen command; always restarts Engine OS afterwards |
| `launcher.conf` | menu entries, default, countdown |
| `primebox-launcher.service` | runs `launcher.sh` `Before=engine.service` |

`launcher.conf`:

```
default = ENGINE OS        # entry the countdown picks
timeout = 5                # seconds; 0 = wait for input

# DJ Apps                  # "# Heading" lines become section headings
REKORDBOX (XDJ-RX3) | sh /data/primebox/start-rb.sh
ENGINE OS |                # empty command = Engine OS
```

A `bg/` prefix runs the command in the background (e.g.
`STREAM+ | bg/systemctl start enginestream.service`). `launcher.sh` logs to
`/data/primebox/log/launcher.log`. `touch /data/primebox/launcher.skip` skips the
menu once.

`launcher.sh` runs `/usr/Engine/Scripts/setup-prerequisites.sh` first: at boot
Engine OS has not done it yet, and it loads `snd_seq_midi` (the control surface
needed by the knob and rbp), pins audio/GPU IRQs and sets the GPU governor.

**The unit is on the rootfs because** `/etc` is an overlay whose upper layer is
`/data/system/etc/overlay`, and systemd reads the rootfs units at boot. Never
`rm /etc/systemd/system/primebox-launcher.service` by hand: that creates an overlay
*whiteout* which hides the unit even from the rootfs copy. Use `make uninstall`,
which removes it from the rootfs.

## 4. The launch sequence

```
systemctl stop engine.service edisksd.service soundswitch.service
        │
        ▼
sh /data/primebox/setup-chroot.sh    # bind /dev /proc /sys /tmp; stubs; FIFOs
        │
        ▼
start edb_streamd                     # DeviceSQL daemon (EDB_BIN=/usr/bin)
        │
        ▼
start rbp inside the chroot
   LD_PRELOAD = fbshim.so : audioshim.so : knobshim.so
   DFB_ROTATE = left
   /lib/ld-linux.so.3 /root/pdj/rbp -a
        │
        ▼
wait until rbp has /tmp/udev_usb1 open, then start usb-watch.sh
        │
        ▼
keep running while rbp lives; on exit stop the watcher and daemons,
and restart Engine OS
```

The `LD_PRELOAD` order matters only in that the **first** library's `ioctl`
wins; `fbshim.so` deliberately contains both the fb ioctl shim **and** the touch
emulation so one library owns `ioctl`. `audioshim` and `knobshim` follow.

## 5. Daemons

### `edb_streamd` (DeviceSQL)

Pioneer's embedded database server. Needed for USB library import/analysis and
playlist access.

```sh
EDB_BIN=/usr/bin chroot /data/primebox/rootfs /lib/ld-linux.so.3 /usr/bin/edb_streamd
```

### `usb-watch.sh`

Watches the rear USB-A port, mounts the stick, binds it into the chroot at
`/media/usb1/sda1`, and writes `mount`/`umount` lines to the FIFO rbp reads
(`/tmp/udev_usb1`). See [docs/06](06-usb.md).

## 6. Files

| Path | What |
|---|---|
| `/data/primebox/rootfs/` | soft-float chroot |
| `/data/primebox/rootfs/root/pdj/rbp` | player binary |
| `/data/primebox/rootfs/usr/lib/{knobshim,audioshim,fbshim}.so` | shims |
| `/data/primebox/rootfs/usr/lib/directfb-1.4-0/systems/libdirectfb_fbdev.so` | patched display driver |
| `/data/primebox/log/` | all logs |
| `/data/primebox/launcher.conf` | boot menu |
| `/usr/lib/systemd/system/primebox-launcher.service` | boot unit |

## 7. Troubleshooting

| Symptom | Cause |
|---|---|
| no boot menu, straight to Engine OS | boot unit missing from the rootfs (e.g. after an official update); `make install` again |
| menu shows, Engine OS only | `default`/`timeout` in `launcher.conf`, or the countdown expired |
| rbp hangs before UI | stale `guard_LocalDBServer` lock or stale frozen `rbp`; kill and clean |
| USB not seen | watcher started before `rbp` opened the FIFO; restart `usb-watch.sh` |
| device reboots under load | an old display stack panicking the fb path; use the shipped patched DirectFB module |
| `Direct/Modules: ABI version ... does not match` then `No system found` | module was built from 1.4.16 (ABI 10) but the RX3 core is 1.4.0 (ABI 9); `make` rebuilds it |
| `GLIBC_2.17 not found (required by libdirect)` | modern-toolchain libc leak; link `compat.c` into `lib/direct` and rebuild |
