#!/bin/sh
# install.sh — install or remove PrimeBox on a Denon Prime GO over SSH.
#
#   tools/install.sh root@PRIMEGO            install / update (idempotent)
#   tools/install.sh root@PRIMEGO uninstall  remove everything, back to stock
#
# Needs `make` to have run (build/primebox.tar.gz). Everything goes into
# /data/primebox except the boot unit, which must live on the rootfs for systemd
# to see it at boot (docs/09 §3). Writing to the rootfs leaves it writable until
# the next boot, so the install reboots the deck and waits for it to return.
set -eu

HOST=${1:?usage: install.sh root@PRIMEGO [uninstall]}
ACTION=${2:-install}
HERE=$(cd "$(dirname "$0")/.." && pwd)
PAYLOAD=$HERE/build/primebox.tar.gz

# one multiplexed connection, so the root password is asked once
CM=$(mktemp -u "${TMPDIR:-/tmp}/primebox-ssh.XXXXXX")
SSH_OPTS="-o ConnectTimeout=10 -o ControlMaster=auto -o ControlPath=$CM -o ControlPersist=600"
ssh() { command ssh $SSH_OPTS "$@"; }
cleanup() { ssh -O exit "$HOST" 2>/dev/null || true; rm -f "$CM"; }
trap cleanup EXIT

wait_up() {
    echo "   waiting for the deck to come back (ssh)"
    i=0
    while [ $i -lt 60 ]; do
        ssh -o ConnectTimeout=5 "$HOST" true 2>/dev/null && { echo "   deck is up"; return 0; }
        sleep 3; i=$((i + 1))
    done
    echo "   deck did not answer; check it by hand" >&2
    return 1
}

reboot_deck() {
    echo "== rebooting (restores the read-only rootfs and shows the menu)"
    ssh "$HOST" 'sync; (sleep 1; reboot) >/dev/null 2>&1 &' || true
    ssh -O exit "$HOST" 2>/dev/null || true
    rm -f "$CM"                     # the old multiplex socket dies with the reboot
    sleep 25
    wait_up
}

# rootfs write: stop Engine OS (it treats the writable rootfs as a disk and holds
# it busy), remount read-write, and bind-mount / to reach the lower /etc under
# the overlay. A reboot afterwards restores read-only; do not start Engine OS in
# the meantime. (This firmware cannot remount the rootfs read-only again once it
# has been written, which is why the install always reboots.)
ROOTFS_EDIT='
set -e
systemctl stop engine.service edisksd.service soundswitch.service
i=0
while pgrep -f EMain >/dev/null 2>&1 && [ $i -lt 20 ]; do sleep 0.5; i=$((i + 1)); done
mount -o remount,rw /
rm -rf /.cache /root/.cache
mkdir -p /tmp/pb-lower
mount -o bind / /tmp/pb-lower
L=/tmp/pb-lower/etc/systemd/system
'

check_deck() {
    ssh "$HOST" '
        set -e
        code=$(tr -d "\0" < /sys/firmware/devicetree/base/inmusic,product-code 2>/dev/null || true)
        [ "$code" = JP11 ] || echo "warning: product code is \"$code\", not JP11 (Prime GO)"
        avail=$(df -k /data | awk "NR==2 {print \$4}")
        [ "$avail" -gt 400000 ] || { echo "need 400 MB free on /data, have ${avail} kB"; exit 1; }'
}

case "$ACTION" in
install)
    [ -f "$PAYLOAD" ] || { echo "install: $PAYLOAD missing; run make first" >&2; exit 1; }
    echo "== checking the deck"
    check_deck

    echo "== stopping a running PrimeBox session"
    ssh "$HOST" '[ -x /data/primebox/usb-watch.sh ] && sh /data/primebox/usb-watch.sh stop >/dev/null 2>&1 || true
        pkill -9 -f "^/lib/ld-linux.so.3 /(root/pdj/rbp|usr/bin/edb_streamd)" 2>/dev/null || true
        for m in media/usb1/sda1 dev proc sys tmp; do umount "/data/primebox/rootfs/$m" 2>/dev/null || true; done
        true'

    echo "== copying payload ($(du -h "$PAYLOAD" | cut -f1))"
    ssh "$HOST" 'cat > /data/primebox.tar.gz' < "$PAYLOAD"

    echo "== unpacking into /data/primebox"
    ssh "$HOST" '
        set -e
        [ -f /data/primebox/launcher.conf ] && cp /data/primebox/launcher.conf /tmp/pb-launcher.conf
        [ -d /data/primebox/rootfs/root/settings ] && cp -a /data/primebox/rootfs/root/settings /tmp/pb-settings
        rm -rf /data/primebox.new
        mkdir -p /data/primebox.new
        tar xzf /data/primebox.tar.gz -C /data/primebox.new
        rm -f /data/primebox.tar.gz
        if mount | grep -q " /data/primebox/rootfs/"; then echo "   chroot still mounted; aborting"; exit 1; fi
        rm -rf /data/primebox
        mv /data/primebox.new /data/primebox
        [ -f /tmp/pb-launcher.conf ] && mv /tmp/pb-launcher.conf /data/primebox/launcher.conf
        [ -d /tmp/pb-settings ] && { rm -rf /data/primebox/rootfs/root/settings; mv /tmp/pb-settings /data/primebox/rootfs/root/settings; }
        echo "   $(du -sh /data/primebox | cut -f1) in /data/primebox"'

    echo "== installing the boot unit on the rootfs"
    ssh "$HOST" "$ROOTFS_EDIT"'
        cp /data/primebox/primebox-launcher.service $L/primebox-launcher.service
        chmod 644 $L/primebox-launcher.service
        mkdir -p $L/multi-user.target.wants
        ln -sf /etc/systemd/system/primebox-launcher.service $L/multi-user.target.wants/primebox-launcher.service
        sync
        umount /tmp/pb-lower; rmdir /tmp/pb-lower'

    reboot_deck
    echo "== done: the boot menu shows at power-on (see docs/09 §3)"
    ;;
uninstall)
    echo "== removing PrimeBox"
    ssh "$HOST" '[ -x /data/primebox/usb-watch.sh ] && sh /data/primebox/usb-watch.sh stop >/dev/null 2>&1 || true
        pkill -9 -f "^/lib/ld-linux.so.3 /(root/pdj/rbp|usr/bin/edb_streamd)" 2>/dev/null || true
        for m in media/usb1/sda1 dev proc sys tmp; do umount "/data/primebox/rootfs/$m" 2>/dev/null || true; done
        true'
    ssh "$HOST" "$ROOTFS_EDIT"'
        rm -f $L/primebox-launcher.service $L/multi-user.target.wants/primebox-launcher.service
        sync
        umount /tmp/pb-lower; rmdir /tmp/pb-lower'
    ssh "$HOST" '
        if mount | grep -q " /data/primebox/rootfs/"; then echo "   chroot still mounted; not deleting"; exit 1; fi
        rm -rf /data/primebox'
    reboot_deck
    echo "== done: the deck boots stock Engine OS"
    ;;
*)
    echo "usage: install.sh root@PRIMEGO [install|uninstall]" >&2; exit 2 ;;
esac
