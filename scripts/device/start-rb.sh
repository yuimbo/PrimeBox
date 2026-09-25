#!/bin/sh
# start-rb.sh — run the XDJ-RX3 rekordbox player on the Prime GO.
#
# Stops Engine OS, starts edb_streamd + rbp in the chroot and the USB
# watcher, stays in the foreground while rbp runs, and restarts Engine OS
# when rbp exits or fails to start. Launched from the boot menu.
#
#   RBP_ENV="KNOB_VERBOSE=1 PRIMEGO_TIMING=1" sh start-rb.sh   extra rbp env
. /data/primebox/env.sh
LOG=$LOG_DIR/rbp.log

stop_all() {
    sh "$PB/usb-watch.sh" stop >/dev/null
    pkill -9 -f "$RBP_MATCH" 2>/dev/null
    pkill -9 -f "$EDB_MATCH" 2>/dev/null
    chmod 640 /dev/mem 2>/dev/null
}
back_to_engine() {
    stop_all
    systemctl start --no-block $ENGINE_UNITS 2>/dev/null
}

# Engine OS holds audio, USB and the control surface; edisksd unmounts sticks
systemctl stop $ENGINE_UNITS 2>/dev/null
stop_all
sh "$PB/setup-chroot.sh"
rm -f /tmp/guard_LocalDBServer /tmp/req_LocalDBServer /tmp/knobshim.log /tmp/audioshim.log

chroot "$ROOT" env EDB_BIN=/usr/bin /lib/ld-linux.so.3 /usr/bin/edb_streamd \
    >"$LOG_DIR/edb.log" 2>&1 &
sleep 1

chroot "$ROOT" env DFB_ROTATE=left $RBP_ENV \
    LD_PRELOAD=/usr/lib/fbshim.so:/usr/lib/audioshim.so:/usr/lib/knobshim.so \
    /lib/ld-linux.so.3 /root/pdj/rbp -a </dev/null >"$LOG" 2>&1 &

# rbp is ready once it holds the USB notification FIFO (bounded: 15 s)
RBP=""
i=0
while [ $i -lt 30 ]; do
    RBP=$(pgrep -f "$RBP_MATCH" | head -n 1)
    [ -n "$RBP" ] && ls -l "/proc/$RBP/fd" 2>/dev/null | grep -q udev_usb1 && break
    RBP=""
    i=$((i + 1)); sleep 0.5
done
if [ -z "$RBP" ]; then
    echo "start-rb: rbp did not start (see $LOG); back to Engine OS"
    back_to_engine
    exit 1
fi
echo "start-rb: rbp $RBP ready"

sh "$PB/usb-watch.sh" start
while kill -0 "$RBP" 2>/dev/null; do sleep 2; done
echo "start-rb: rbp exited; back to Engine OS"
back_to_engine
