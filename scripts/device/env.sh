# env.sh — shared paths for the PrimeBox device scripts (sourced, not run).
PB=/data/primebox
ROOT=$PB/rootfs
LOG_DIR=$PB/log
USB_MNT=/media/usb1/sda1
ENGINE_UNITS="engine.service edisksd.service"
RBP_MATCH="^/lib/ld-linux.so.3 /root/pdj/rbp"
EDB_MATCH="^/lib/ld-linux.so.3 /usr/bin/edb_streamd"
mkdir -p "$LOG_DIR"
