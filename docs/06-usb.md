# 06 — USB drive & rekordbox database

On the XDJ-RX3 a rekordbox USB stick is mounted by udev and DeviceSQL parses
`export.pdb` natively. PrimeBox reproduces that natively on the Prime GO's
single rear USB-A port — no memory injection, no hardcoded labels.

## 1. Physical port

The Prime GO has exactly one USB-A **host** port, on the rear. In sysfs it is
the EHCI/OHCI pair `usb3` (high speed) / `usb4` (full/low speed); a stick
enumerates as `3-1` or `4-1` and shows up as `/dev/sda`/`sdb`. The internal
control surface (OTG) and the USB-B computer port are deliberately ignored.

The reference RX3 mount convention is kept:

| Host path | Chroot path |
|---|---|
| `/media/usb1/sda1` | `/data/primebox/rootfs/media/usb1/sda1` |

## 2. `usb-watch.sh`

`scripts/device/usb-watch.sh` is a small hotplug daemon that replaces the RX3
udev rules:

1. Poll sysfs for an `sd*` device under `usb3`/`usb4`.
2. Wait for the first partition (or fall back to a whole-disk filesystem).
3. Mount it with RX3-compatible options, e.g.:

   ```sh
   mount -t vfat -o flush,rw,noatime,shortname=mixed,dmask=000,fmask=000,\
   codepage=437,iocharset=iso8859-1,usefree,utf8 /dev/sda1 /media/usb1/sda1
   ```

   (`exfat`, `hfsplus` and generic fallbacks are also handled.)
4. `mount --bind` it into the chroot at `/media/usb1/sda1`.
5. Tell `rbp` through the FIFO: write `umount <path>` then `mount <path>` to
   `/tmp/udev_usb1`.

The `umount`-then-`mount` order matters: `rbp`'s `PathDecider` ignores a
`mount` that is not preceded by an `umount` (it logs
`UNMOUNT was not executed before`). The watcher also re-notifies whenever `rbp`
restarts.

> Never write to `/tmp/udev_usbctn*`. A "connect" event there triggers the
> cosmetic **"USB Error. Remove the device."** caution. The mount event alone
> is sufficient.

## 3. FIFOs

`rbp` was patched to use `/tmp/udev_*` instead of `/proc/udev_*` (see the udev
string patches in the patch table), because `/tmp` is a tmpfs it can create.
`setup-chroot.sh` creates them:

```sh
for f in udev_usb1 udev_usb2 udev_usbctn1 udev_usbctn2; do
  [ -p /tmp/$f ] || { rm -f /tmp/$f; mkfifo /tmp/$f; chmod 666 /tmp/$f; }
done
```

`/tmp` is bind-mounted into the chroot, so host and chroot see the same FIFO.

## 4. Denon `edisksd` will eject your stick

Denon Engine OS runs `edisksd.service`. When it sees a storage mount it does
not manage, it bus-resets and unmounts it — `rbp` then dies with `EIO`. The
launcher stops both daemons:

```sh
systemctl stop engine.service edisksd.service
```

Do **not** disable `engine.service`; it should come back on a normal boot.

## 5. The native detection chain inside `rbp`

```
[stick mounts]  →  /tmp/udev_usb1: "mount /media/usb1/sda1"
        │
        ▼  ui::UsbMountManager::run()  (thread "UsbMountManager")
           read_sf_rbp() reads the FIFO, blkid reads LABEL/TYPE
        ▼  ui::UsbStorageManager::notify_usb_mount()
        ▼  ui::DbProxy::reqAttach()
        ▼  db::DbIF::mount(path, type=3)
           • vfs_setfsys('C', fs, "/media/usb1/sda1", 0)
           • posts mailbox message 11 to Total_MainTASK
        ▼  Total_MainUsbMessageProc (case 11)
           • SetMountInfo_DriveLetter(0, 3, 'C')
           • SetMountInfo_DeviceDetectFlg(0, 3, 1)
        ▼  DeviceSQL (edb_streamd) analyses export.pdb / exportExt.pdb
           • rewrites the DB on the stick
           • DBSMain_OnResult → DBC_ReAnalysisEnd
        ▼  detect flag (drive 0, kind 2) = 2
        ▼  compConnectedMedia() → uiConnectedMedia bit 1  (USB 1)
        ▼  Source / Browse UI
```

### Media "kinds"

Pioneer's engine inherited the CDJ-3000 media model:

| Kind | Engine meaning | RX3 UI | Detect flag | Property block |
|---|---|---|---|---|
| 2 | SD slot | **USB 1** | `0x03256888` | `0x0325688c` (168 B) |
| 3 | USB host | USB 2 | `0x03256944` | `0x03256948` |
| 4 | PRO DJ LINK | LINK/PC | `0x03256a00` | `0x03256a04` |

The Prime GO has only one port, so the shim **suppresses Kind 3** (writes 0)
and forces `uiConnectedMedia = 0x2` (USB 1 only). Without this, `rbp` shows a
phantom, empty "USB2".

### `DevicePropertyInfo` layout (`0x0325688c`)

| Offset | Type | Meaning |
|---|---|---|
| +0 | UTF-16LE | volume label |
| +64 | — | date string |
| +120 | u32 | song count |
| +124 | u8 | background colour |
| +126 | u8 | database ready flag (must be 1) |
| +128 | u32 | playlist count |
| +132 | u32 | total capacity **high** word |
| +136 | u32 | total capacity **low** word |
| +140 | u32 | free space high word |
| +144 | u32 | free space low word |

All of these are now filled **natively** by DeviceSQL reading the stick. The
capacity words are stored high-word-first because `setRightInfoSource` loads
`[+136]` into `r0` and `[+132]` into `r1` before `__aeabi_ul2d` (r0=low,
r1=high). Writing a normal little-endian u64 here produces the famous
`881466368.0 GB` display bug.

### Required environment

* **`edb_streamd`** (DeviceSQL) must be running inside the chroot before `rbp`:

  ```sh
  EDB_BIN=/usr/bin chroot /data/primebox/rootfs /lib/ld-linux.so.3 /usr/bin/edb_streamd
  ```

  It communicates over the FIFOs `/tmp/req_LocalDBServer` and
  `/tmp/guard_LocalDBServer`. Remove stale copies and any stale `rbp` holding
  the guard lock before starting.

* **`/etc/mtab`** must resolve to `/proc/mounts` inside the chroot, so
  `vfs_getfsys` classifies the mount as `vfat`:

  ```sh
  ln -sf /proc/mounts /data/primebox/rootfs/etc/mtab
  ```

* The `IPowerManager` notification stubs must be patched (there is no Pioneer
  power-manager MCU; the calls would dereference NULL and crash the
  `UsbMountManager` thread). See the `pm:` patches in the patch table.

## 6. Browsing UX

With the drive mounted and analysed, the shim makes browsing usable:

* `VIEW` opens the stick directly while no source is selected yet (the
  RX3 would show "Please select a source" and wait for its USB1 button, which
  the Prime GO lacks), so one VIEW press after boot shows the library;
* `FWD`/`SOURCE` opens the Source menu;
* pushing the browse knob on the Source menu selects **USB 1**
  (`BrowseUiIf::InputKey(UKEY_USB1)`), because the Prime GO has no physical
  USB1 button;
* the library opens in `DISPMODE_LIST` (mode 5), showing the native categories
  (TRACK, PLAYLIST, ARTIST, ALBUM, KEY, HISTORY, MATCHING, FOLDER);
* `VIEW` toggles between the library and the deck view.

## 7. Verifying

```sh
# from the device
sh /data/usb-watch.sh status
cat /data/usbwatch.log

# the drive's label / db
blkid /dev/sda1
ls /media/usb1/sda1/PIONEER/rekordbox/     # export.pdb, exportExt.pdb

# live flags while rbp runs (root can read /proc/pid/mem)
# detect(kind2) should be 2, uiConnectedMedia should be 2
```

| Symptom | Cause |
|---|---|
| stick vanishes after ~30 s | `edisksd.service` still running |
| "USB2" with 0 tracks | phantom USB2 suppression missing |
| generic "USB1", 0 GB | `DevicePropertyInfo` not populated / analysis not run |
| `881466368.0 GB` | capacity u64 word order wrong |
| mount never seen | FIFO missing, or mount sent without preceding umount |
| "Please select a source" | no source selected (browseDevice 0) and an old knobshim without the VIEW → USB1 remap; press FWD then push the knob |
