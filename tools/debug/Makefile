# PrimeBox debug tools — not part of the runtime. See tools/debug/README.md.
#   make RX3=/path/to/extracted/XDJRX3-rootfs OUT=/path/to/build/debug
CROSS ?= arm-linux-gnueabi-
CC    := $(CROSS)gcc
RX3   ?= ../../extracted/XDJRX3-rootfs
OUT   ?= ../../build/debug

CFLAGS  := -O2 -march=armv5t -mfloat-abi=soft -fno-stack-protector -fPIC \
           -Wall -Wextra -Wno-unused-parameter
LDFLAGS := -L$(RX3)/lib -L$(RX3)/usr/lib -Wl,-rpath-link,$(RX3)/lib:$(RX3)/usr/lib
STATIC  := -O2 -march=armv5t -mfloat-abi=soft -static

# LD_PRELOAD probes (run inside the chroot)
PRELOAD := crashcatch.so dfbprobe.so fusprobe.so pcprof.so
# static helpers (run on the host)
HOSTBIN := seqinject2 udplog peek vsyncprobe

.PHONY: all clean
all: $(addprefix $(OUT)/,$(PRELOAD) $(HOSTBIN))

$(OUT):
	mkdir -p $@

$(OUT)/crashcatch.so: crashcatch.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -lc $(LDFLAGS)
$(OUT)/pcprof.so: pcprof.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -lpthread -lc $(LDFLAGS)
$(OUT)/dfbprobe.so $(OUT)/fusprobe.so: $(OUT)/%.so: %.c | $(OUT)
	$(CC) $(CFLAGS) -shared -Wl,--allow-shlib-undefined -o $@ $< $(RX3)/lib/libdl.so.2 -lc $(LDFLAGS)
$(OUT)/%: %.c | $(OUT)
	$(CC) $(STATIC) -o $@ $<

clean:
	rm -f $(addprefix $(OUT)/,$(PRELOAD) $(HOSTBIN))
