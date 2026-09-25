#!/bin/sh
# build.sh — build the PrimeBox DirectFB fbdev system module.
#
# Runs inside the primebox-armel image (see docker/Dockerfile):
#   /src  = this repo (read-only is fine)
#   /rx3  = extracted/XDJRX3-rootfs (sysroot, read-only)
#   /out  = output dir; receives libdirectfb_fbdev.so
#
# The module is built from DirectFB 1.4.0, the RX3 core's version: its system
# ABI (9) and CoreSystemFuncs layout must match libdirectfb-1.4.so.0 in the
# RX3 rootfs. Only systems/fbdev is patched; the rest of the tree is built
# solely to link against.
set -eu

REV=DIRECTFB_1_4_0
WORK=${WORK:-/tmp/directfb}
CFLAGS_T="-O2 -march=armv7-a -mtune=cortex-a17 -mfloat-abi=soft --sysroot=/rx3 -D_GNU_SOURCE"

if [ ! -d "$WORK/.git" ]; then
    git clone -q --branch "$REV" --depth 1 https://github.com/Distrotech/DirectFB.git "$WORK"
fi
cd "$WORK"
git checkout -q -- . && git clean -qfd systems/fbdev
git apply /src/tools/build-directfb/directfb-1.4.0-fbdev.diff

if [ ! -f systems/fbdev/Makefile ]; then
    # Makefile.in is gitignored in the upstream tree: regenerate it from the
    # patched Makefile.am files before configure.
    autoreconf -fi >/dev/null 2>&1
    ./configure -q --host=arm-linux-gnueabi --prefix=/usr \
        --disable-x11 --disable-sdl --disable-vnc --disable-osx --disable-devmem \
        --with-gfxdrivers=none --disable-tests --disable-tools --disable-static \
        --sysconfdir=/usr/etc --localstatedir=/var --disable-dependency-tracking \
        CC=arm-linux-gnueabi-gcc CFLAGS="$CFLAGS_T" \
        LDFLAGS="--sysroot=/rx3 -Wl,-rpath-link,/rx3/lib:/rx3/usr/lib"
fi

# the module links against libdirect/libfusion/libdirectfb from this tree
make -s -C include
for d in lib/direct lib/fusion src; do make -s -C $d >/dev/null; done
make -s -C systems/fbdev clean >/dev/null
make -s -C systems/fbdev CFLAGS="$CFLAGS_T -Werror-implicit-function-declaration" libdirectfb_fbdev.la

M=systems/fbdev/.libs/libdirectfb_fbdev.so
arm-linux-gnueabi-strip --strip-unneeded -o /out/libdirectfb_fbdev.so "$M"

# guards: only old glibc symbols, system ABI 9
bad=$(arm-linux-gnueabi-objdump -T /out/libdirectfb_fbdev.so | grep -oE 'GLIBC_[0-9.]+' | sort -u | grep -vE '^GLIBC_2\.(4|7)$' || true)
[ -z "$bad" ] || { echo "build.sh: module needs $bad (RX3 has glibc 2.13)" >&2; exit 1; }
arm-linux-gnueabi-objdump -d "$M" | grep -A8 '<directfb_fbdev>:' | grep -q 'mov	r1, #9' \
    || { echo "build.sh: module is not system ABI 9" >&2; exit 1; }
echo "build.sh: /out/libdirectfb_fbdev.so OK (ABI 9, GLIBC 2.4/2.7)"
