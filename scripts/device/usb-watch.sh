#!/bin/sh
# =============================================================================
# usb-watch.sh — Prime GO rear USB-A stick hotplug -> rbp (rekordbox player)
#
# The Prime GO has exactly ONE USB host port: the rear USB-A (per the user
# guide: "1 USB port (for USB drives)"). In sysfs that is the EHCI/OHCI
# controller pair: usb3 (EHCI, high speed) + usb4 (OHCI full/low-speed
# companion). A stick in the rear port enumerates as 3-1 (high speed) or 4-1
# (low speed). The internal control surface (1-1 on DWC OTG usb1) and the
# USB-B computer port (usb2) are deliberately NOT watched — "only the
# backside of the device for rb".
#
# On attach (any mass-storage device appears on usb3/usb4):
#   mount  /dev/sdX1 -> /media/usb1/sda1            (RX3-style vfat options)
#   bind   /media/usb1/sda1 -> /data/primebox/rootfs/media/usb1/sda1  (chroot view)
#   write  "mount /media/usb1/sda1" -> /tmp/udev_usb1          (rbp FIFO)
# On detach (device disappears):
#   write  "umount /media/usb1/sda1" -> /tmp/udev_usb1
#   umount -l both mounts
#
# This mirrors the XDJ-RX3 udev rule 12-usb-memory-auto-mount.rules
# (echo -n mount /media/usb1/sda1 > /proc/udev_usb1) and cdj3k-emu's
# usb-external-attach.sh. The mount event alone is sufficient — we NEVER
# write to /tmp/udev_usbctn* ("connect" there triggers rbp's
# "USB Error. Remove the device." popup).
#
# Handles random attach/remove: debounced state machine, any sdX name
# (sda/sdb/...), partition vs whole-disk sticks, stick swaps (detach old,
# attach new), and rbp restarts (re-notifies the current state).
#
# Usage:  sh /data/primebox/usb-watch.sh start|stop|status|run
# Env:    USBWATCH_BUSES="3 4"   (sysfs usbN controllers to watch)
#         USBWATCH_POLL=1        (poll interval seconds)
# =============================================================================

. /data/primebox/env.sh
MNT=$USB_MNT
CH_MNT=$ROOT$USB_MNT
FIFO=/tmp/udev_usb1
LOG=$LOG_DIR/usbwatch.log
PIDFILE=/tmp/usbwatch.pid
BUSES="${USBWATCH_BUSES:-3 4}"
POLL="${USBWATCH_POLL:-1}"

log() { echo "$(date '+%F %T') $$ $*" >> "$LOG"; }

# --- locate the sd block device of a rear-port USB mass-storage device ------
# /sys/block/sdX -> ../../devices/platform/ff500000.usb/usb3/3-1/3-1:1.0/...
# The readlink path contains the "/usbN/" root-hub segment; we match only
# the rear-port controllers (default usb3/usb4).
find_rear_sd() {
  for blk in /sys/block/sd*; do
    [ -e "$blk" ] || continue
    tgt=$(readlink "$blk" 2>/dev/null) || continue
    for b in $BUSES; do
      case "$tgt" in
        *"/usb$b/"*) echo "${blk##*/}"; return 0 ;;
      esac
    done
  done
  return 1
}

# --- wait for the first partition; fall back to whole-disk filesystem ------
find_partition() {
  dev=$1
  i=0
  while [ $i -lt 40 ]; do                 # up to 4 s (0.1 s steps)
    [ -b "/dev/${dev}1" ] && { echo "${dev}1"; return 0; }
    i=$((i + 1)); sleep 0.1
  done
  if blkid "/dev/$dev" >/dev/null 2>&1; then echo "$dev"; return 0; fi
  return 1
}

# --- write to the FIFO, bounded to 3 s (the host busybox has no `timeout`) ---
bounded_write() {
  printf "%s" "$1" > "$FIFO" 2>/dev/null &
  w=$!
  ( sleep 3; kill -9 $w 2>/dev/null ) &
  k=$!
  wait $w 2>/dev/null
  rc=$?
  kill $k 2>/dev/null
  return $rc
}

# --- tell rbp about a USB event (FIFO; rbp keeps it open O_RDWR) -----------
notify() {
  msg=$1
  if [ ! -p "$FIFO" ]; then
    log "notify: $FIFO missing (rbp down?) — skipping"
    return 1
  fi
  if bounded_write "$msg"; then
    log "notify: $msg"
    return 0
  fi
  log "notify: FAILED to write '$msg' (rbp down?)"
  return 1
}

# --- mount + chroot bind + notify ------------------------------------------
attach() {
  dev=$1
  part=$(find_partition "$dev") || { log "attach: no usable partition on $dev"; return 1; }

  if mountpoint -q "$MNT"; then
    log "attach: $MNT already mounted (refreshing bind only)"
  else
    fstype=$(blkid -s TYPE -o value "/dev/$part" 2>/dev/null)
    [ -n "$fstype" ] || fstype=vfat
    mkdir -p "$MNT"
    case "$fstype" in
      vfat)    mount -t vfat -o flush,rw,noatime,shortname=mixed,dmask=000,fmask=000,codepage=437,iocharset=iso8859-1,usefree,utf8 "/dev/$part" "$MNT" ;;
      exfat)   mount -t exfat -o rw,noatime "/dev/$part" "$MNT" ;;
      hfsplus) mount -t hfsplus -o force,rw,noatime "/dev/$part" "$MNT" ;;
      *)       mount "/dev/$part" "$MNT" ;;
    esac
    rc=$?
    if [ $rc -ne 0 ]; then
      log "attach: mount /dev/$part -> $MNT failed rc=$rc"
      return 1
    fi
    log "attach: mounted /dev/$part ($fstype) -> $MNT"
  fi

  mkdir -p "$CH_MNT"
  if ! mountpoint -q "$CH_MNT"; then
    if ! mount --bind "$MNT" "$CH_MNT"; then
      log "attach: chroot bind $MNT -> $CH_MNT failed"
      return 1
    fi
    log "attach: chroot bind ok ($CH_MNT)"
  fi

  # bind must exist BEFORE rbp checks the stick's files (export.pdb etc.)
  # Reset PathDecider state with umount first, then send native mount notification
  notify "umount $MNT"
  sleep 0.3
  notify "mount $MNT"
  log "attach: notified native mount $MNT"
  return 0
}

# --- notify rbp + release mounts ---------------------------------------------
detach() {
  log "detach: notifying rbp"
  notify "umount $MNT"
  sleep 1
  if mountpoint -q "$CH_MNT"; then umount -l "$CH_MNT"; log "detach: umount -l $CH_MNT"; fi
  if mountpoint -q "$MNT";    then umount -l "$MNT";    log "detach: umount -l $MNT";    fi
  rmdir "$CH_MNT" 2>/dev/null
  rmdir "$MNT" 2>/dev/null
}

rbp_pid() { pgrep -f "$RBP_MATCH" | head -n 1; }

run() {
  log "=== usb-watch run: watching buses [$BUSES], poll ${POLL}s ==="
  cur=""
  last_rbp=$(rbp_pid)

  while :; do
    dev=$(find_rear_sd) || dev=""
    rbp=$(rbp_pid)

    if [ -n "$dev" ]; then
      if [ "$dev" != "$cur" ]; then
        [ -n "$cur" ] && detach
        log "attach: detected $dev on rear port"
        if attach "$dev"; then
          cur=$dev
        else
          cur=""
        fi
      elif [ -n "$rbp" ] && [ "$rbp" != "$last_rbp" ]; then
        log "attach: rbp restarted ($last_rbp -> $rbp), re-notifying mount"
        sleep 2
        notify "umount $MNT"
        sleep 0.3
        notify "mount $MNT"
      fi
    else
      if [ -n "$cur" ]; then
        detach
        cur=""
      fi
    fi
    last_rbp=$rbp
    sleep "$POLL"
  done
}

start() {
  if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
    echo "already running (pid $(cat "$PIDFILE"))"
    return 0
  fi
  log "=== usb-watch start ==="
  nohup sh "$0" run >/dev/null 2>&1 &
  echo $! > "$PIDFILE"
  sleep 1
  echo "started pid $(cat "$PIDFILE")"
}

stop() {
  if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
    kill "$(cat "$PIDFILE")" 2>/dev/null
    rm -f "$PIDFILE"
    echo "stopped"
  else
    echo "not running"
  fi
}

status() {
  echo "pid: $(cat "$PIDFILE" 2>/dev/null || echo none)"
  echo "rear-port sd: $(find_rear_sd || echo none)"
  echo "mounted: $(mountpoint -q "$MNT" && echo yes || echo no)"
  echo "chroot bind: $(mountpoint -q "$CH_MNT" && echo yes || echo no)"
  echo "--- log tail ---"
  tail -15 "$LOG" 2>/dev/null
}

case "$1" in
  start)  start ;;
  stop)   stop ;;
  status) status ;;
  run)    run ;;
  *) echo "usage: $0 start|stop|status"; exit 1 ;;
esac
