#!/bin/sh
# assemble-chroot.sh — build the PrimeBox runtime tree that runs on the Prime GO
# as /data/rbx3-run (a soft-float XDJ-RX3 chroot).
#
#   tools/assemble-chroot.sh EXTRACTED BUILD OUT
#
#   EXTRACTED  extracted firmware (XDJRX3-rootfs/, XDJRX3-gui/, XDJRX3/gui/)
#   BUILD      build outputs (rbp, *.so, libdirectfb_fbdev.so)
#   OUT        tree to create (replaced)
#
# Only stock RX3 files plus PrimeBox's own build outputs go in. The ISO's
# lib/ and usr/ are NOT used: their symlinks are flattened to 0-byte files.
set -eu

X=$1; B=$2; OUT=$3
for f in "$X/XDJRX3-rootfs/lib/ld-2.13.so" "$X/XDJRX3-gui/pset" "$X/XDJRX3/gui/fontdata" \
         "$B/rbp" "$B/libdirectfb_fbdev.so" "$B/knobshim.so" "$B/audioshim.so" "$B/fbshim.so"; do
    [ -e "$f" ] || { echo "assemble-chroot: missing $f (run make first)" >&2; exit 1; }
done

rm -rf "$OUT"
mkdir -p "$OUT"
cp -pR "$X/XDJRX3-rootfs/." "$OUT/"
rm -rf "$OUT/dev" "$OUT/proc" "$OUT/sys" "$OUT/tmp"
mkdir -p "$OUT/dev" "$OUT/proc" "$OUT/sys" "$OUT/tmp" \
         "$OUT/root/gui" "$OUT/root/pdj" "$OUT/media/usb1/sda1" "$OUT/usr/etc"

# gui partition (pset/, system/) + the ISO's gui/ (fontdata/, imagedata/)
cp -pR "$X/XDJRX3-gui/pset" "$X/XDJRX3-gui/system" "$OUT/root/gui/"
cp -pR "$X/XDJRX3/gui/." "$OUT/root/gui/"

# patched player
install -m 755 "$B/rbp" "$OUT/root/pdj/rbp"

# shims (LD_PRELOADed by start-rb.sh)
for s in knobshim audioshim fbshim; do
    install -m 755 "$B/$s.so" "$OUT/usr/lib/$s.so"
done

# DirectFB: patched fbdev module; drop the gal gfxdriver (graphics_core fails with it)
D="$OUT/usr/lib/directfb-1.4-0"
install -m 755 "$B/libdirectfb_fbdev.so" "$D/systems/libdirectfb_fbdev.so"
rm -rf "$D/gfxdrivers"
install -m 644 "$(dirname "$0")/../scripts/device/directfbrc" "$OUT/usr/etc/directfbrc"

ln -sf /proc/mounts "$OUT/etc/mtab"
find "$OUT" \( -name .DS_Store -o -name '._*' \) -exec rm -f {} + 2>/dev/null || true
echo "assemble-chroot: $OUT ready ($(du -sh "$OUT" | cut -f1))"
