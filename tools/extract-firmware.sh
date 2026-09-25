#!/bin/sh
# extract-firmware.sh — produce extracted/ from the official XDJ-RX3 v1.20 update.
#
#   tools/extract-firmware.sh [DOWNLOAD_DIR]
#
# Needs keys/aes256.key (see keys/README.md), cargo, 7z and docker. Each step
# is skipped when its output already exists, so it is safe to re-run.
set -eu

HERE=$(cd "$(dirname "$0")/.." && pwd)
DL=${1:-$HERE/extracted/download}
X=$HERE/extracted
KEY=$HERE/keys/aes256.key

[ -f "$KEY" ] || { echo "missing $KEY (see keys/README.md)"; exit 1; }
for t in cargo 7z docker; do
    command -v $t >/dev/null || { echo "missing tool: $t"; exit 1; }
done

step() { echo "== $*"; }

find_upd() { find "$DL" -iname 'XDJ*RX3*.UPD' 2>/dev/null | head -n 1; }
UPD=$(find_upd)
if [ -z "$UPD" ]; then
    step "download XDJ-RX3 v1.20"
    sh "$HERE/tools/get-firmware.sh" "$DL" >/dev/null
    UPD=$(find_upd)
fi
[ -n "$UPD" ] || { echo "no XDJ-RX3 .UPD found in $DL"; exit 1; }

if [ ! -f "$X/XDJRX3.iso" ]; then
    step "decrypt $UPD"
    (cd "$HERE/tools/rx3dec" && cargo build -q --release)
    "$HERE/tools/rx3dec/target/release/rx3dec" "$UPD" "$KEY" "$X/XDJRX3.iso"
fi

if [ ! -f "$X/XDJRX3/images/release.txt" ]; then
    step "extract ISO"
    mkdir -p "$X/XDJRX3"
    7z x -y "$X/XDJRX3.iso" -o"$X/XDJRX3" >/dev/null
fi
grep -q '1.20' "$X/XDJRX3/images/release.txt" || { echo "not firmware v1.20"; exit 1; }

if [ ! -f "$X/XDJRX3/pdj/rbp" ]; then
    step "extract pdj (the rekordbox player)"
    tar xzf "$X/XDJRX3/images/pdj.tar.gz" -C "$X/XDJRX3"
fi

if [ ! -d "$X/XDJRX3-gui/pset" ]; then
    step "extract gui partition"
    mkdir -p "$X/XDJRX3-gui"
    tar xzf "$X/XDJRX3/images/gui.tar.gz" -C "$X/XDJRX3-gui"
fi

if [ ! -f "$X/XDJRX3-rootfs/lib/ld-2.13.so" ]; then
    step "extract rootfs (cramfs, in docker)"
    mkdir -p "$X/XDJRX3-rootfs"
    docker run --rm -v "$X/XDJRX3/images/rootfs.cramfs":/in.cramfs:ro -v "$X/XDJRX3-rootfs":/out \
        ubuntu:18.04 sh -c 'apt-get update -qq >/dev/null && apt-get install -y -qq util-linux >/dev/null &&
            fsck.cramfs --extract=/tmp/r /in.cramfs >/dev/null && rm -rf /tmp/r/dev/* && chmod -R u+rwX /tmp/r && cp -a /tmp/r/. /out/'
fi

find "$X" -name .DS_Store -exec rm -f {} + 2>/dev/null || true
echo "extracted/ ready. Next: make"
