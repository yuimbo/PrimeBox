#!/bin/sh
# setup-chroot.sh — prepare /data/primebox/rootfs for one rbp session:
# bind the host's /dev /proc /sys /tmp and create the XDJ-RX3 device stubs.
# Idempotent; run by start-rb.sh.
. /data/primebox/env.sh

mountpoint -q "$ROOT/dev"  || mount --bind /dev  "$ROOT/dev"
mountpoint -q "$ROOT/proc" || mount --bind /proc "$ROOT/proc"
mountpoint -q "$ROOT/sys"  || mount --bind /sys  "$ROOT/sys"
mountpoint -q "$ROOT/tmp"  || mount --bind /tmp  "$ROOT/tmp"

# RX3 devices that do not exist on the Prime GO. They live in the bind-mounted
# /dev (a devtmpfs), so they are gone after reboot and re-created here.
#   FIFOs: threads that read/poll them block instead of busy-spinning
for d in subucom_spi1.0 subucom_spi2.0 subucom_spi_rdy3.0 subucom_spi_rdy4.0 hidg0; do
    [ -p "/dev/$d" ] || { rm -f "/dev/$d"; mkfifo -m 666 "/dev/$d"; }
done
#   regular files: ioctl-only devices and polled GPIOs (fbshim answers them)
for d in printkdrv0 tsc2007_2-0048 gpiodrv; do
    [ -f "/dev/$d" ] || { rm -f "/dev/$d"; : > "/dev/$d"; chmod 666 "/dev/$d"; }
done
#   no USB audio gadget: JUCE then skips the gadget ioctls
rm -f /dev/paudiog0
#   rbp probes i.MX6 registers through /dev/mem; deny it (restored by stop)
chmod 000 /dev/mem

# rbp's USB mount notifications (read by rbp, written by usb-watch.sh)
for f in udev_usb1 udev_usb2 udev_usbctn1 udev_usbctn2; do
    [ -p "/tmp/$f" ] || { rm -f "/tmp/$f"; mkfifo -m 666 "/tmp/$f"; }
done
