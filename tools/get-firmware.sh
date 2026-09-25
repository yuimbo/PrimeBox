#!/bin/sh
# get-firmware.sh - download the official XDJ-RX3 v1.20 firmware update.
#
# The archive is published by AlphaTheta. This script only downloads it; it
# contains no Pioneer code. Unzip to obtain XDJRX3.UPD, then see
# docs/01-firmware-extraction.md.
#
# usage: ./get-firmware.sh [output-dir]
set -eu

URL="https://downloads.support.alphatheta.com/firmwares/all-in-one-dj-systems/XDJ-RX3/XDJ-RX3_v120.zip"
DEST="${1:-.}"
OUT="$DEST/XDJ-RX3_v120.zip"

mkdir -p "$DEST"

if [ -f "$OUT" ]; then
    echo "[=] $OUT already exists, skipping download"
else
    echo "[+] downloading $URL"
    if command -v curl >/dev/null 2>&1; then
        curl -L --fail --progress-bar -o "$OUT" "$URL"
    elif command -v wget >/dev/null 2>&1; then
        wget -O "$OUT" "$URL"
    else
        echo "!! need curl or wget" >&2
        exit 1
    fi
fi

echo "[+] sha256: $(sha256sum "$OUT" 2>/dev/null | awk '{print $1}')"
echo "[+] unzipping"
if command -v unzip >/dev/null 2>&1; then
    unzip -o "$OUT" -d "$DEST"
else
    echo "!! unzip not found; extract $OUT manually" >&2
    exit 1
fi

echo
echo "Next:"
echo "  1. obtain the firmware key from AlphaTheta's GPL archive and save it"
echo "     as keys/aes256.key  (see keys/README.md - the key is NOT shipped)"
echo "  2. cd tools/rx3dec && cargo build --release"
echo "  3. ./target/release/rx3dec $DEST/*/XDJ-RX3.UPD ../../keys/aes256.key XDJRX3.iso"
