#!/bin/sh
# launcher.sh — boot menu entry point (run by primebox-launcher.service).
#
# Shows the menu, then runs the chosen launcher.conf command. An empty
# command means Engine OS. Engine OS is started on every exit path, so a
# failed or finished app always returns to a working deck with sshd up.
#
#   sh /data/primebox/launcher.sh     show the menu now (stops Engine OS first)
#   touch /data/primebox/launcher.skip   skip the menu on the next boot
. /data/primebox/env.sh

start_engine() { systemctl start --no-block $ENGINE_UNITS 2>/dev/null; }
trap start_engine EXIT

exec >>"$LOG_DIR/launcher.log" 2>&1
echo "=== $(date '+%F %T') launcher start"

if [ -f "$PB/launcher.skip" ]; then
    rm -f "$PB/launcher.skip"
    echo "skip file present -> Engine OS"
    exit 0
fi

systemctl stop $ENGINE_UNITS 2>/dev/null

# At boot Engine OS has not run its setup yet: it loads snd_seq_midi (the
# control surface, needed by the knob and rbp), pins audio/GPU IRQs and sets
# the GPU governor. Run it so every menu entry gets the same hardware state.
/usr/Engine/Scripts/setup-prerequisites.sh \
    "$(cat /sys/firmware/devicetree/base/inmusic,product-code 2>/dev/null)" >/dev/null 2>&1

cmd=$("$PB/primebox-launcher" "$PB/launcher.conf") || { echo "menu failed -> Engine OS"; exit 0; }
echo "chosen: '$cmd'"
[ -z "$cmd" ] && exit 0

case "$cmd" in
    bg/*) sh -c "${cmd#bg/}" & ;;
    *)    sh -c "$cmd" ;;
esac
echo "command exited rc=$?"
