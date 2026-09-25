# PrimeBox runtime shims — LD_PRELOADed into rbp inside the XDJ-RX3 chroot.
#
# All must be soft-float EABI5 and reference only GLIBC_2.4/2.7 (the RX3 runs
# glibc 2.13). The RX3 rootfs is the sysroot, so symbol versions match.
#   make RX3=/path/to/extracted/XDJRX3-rootfs OUT=/path/to/build
# Normally driven by the top-level Makefile.

CROSS ?= arm-linux-gnueabi-
CC    := $(CROSS)gcc
RX3   ?= ../../extracted/XDJRX3-rootfs
OUT   ?= ../../build

CFLAGS  := -O2 -march=armv5t -mfloat-abi=soft -fno-stack-protector -fPIC \
           -Wall -Wextra -Wno-unused-parameter
LDFLAGS := -L$(RX3)/lib -L$(RX3)/usr/lib -Wl,-rpath-link,$(RX3)/lib:$(RX3)/usr/lib

SHIMS := $(OUT)/knobshim.so $(OUT)/audioshim.so $(OUT)/fbshim.so

.PHONY: all check clean
all: $(SHIMS) check

$(OUT):
	mkdir -p $@

# control surface -> RX3 keys (ALSA sequencer client 16:0)
$(OUT)/knobshim.so: knobshim.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -lpthread -lc $(LDFLAGS)

# ALSA device/format adaptation; dlopen/dlsym must bind to libdl GLIBC_2.4
$(OUT)/audioshim.so: audioshim.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< $(RX3)/lib/libdl.so.2 -lc $(LDFLAGS)

# logical 1280x800 fb geometry, tsc2007 touch emulation, GPIO stubs
$(OUT)/fbshim.so: fbshim.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -lpthread -lc $(LDFLAGS)

# fail if a newer GLIBC version or a hard-float tag sneaks in
check: $(SHIMS)
	@for f in $(SHIMS); do \
	  bad=$$($(CROSS)objdump -T $$f | grep -oE 'GLIBC_[0-9.]+' | sort -u | grep -vE '^GLIBC_2\.(4|7)$$'); \
	  [ -z "$$bad" ] || { echo "$$f needs $$bad (RX3 has glibc 2.13)"; exit 1; }; \
	  ! $(CROSS)readelf -A $$f | grep -q Tag_ABI_VFP_args || { echo "$$f is hard-float"; exit 1; }; \
	done
	@echo "shims OK (soft-float, GLIBC 2.4/2.7)"

clean:
	rm -f $(SHIMS)
