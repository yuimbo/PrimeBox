# primebox-launcher: static ARM boot menu (runs on the Prime GO host).
CROSS ?= arm-linux-gnueabi-
OUT   ?= ../../build

$(OUT)/primebox-launcher: launcher.c font8x8_basic.h
	mkdir -p $(OUT)
	$(CROSS)gcc -O2 -Wall -Wextra -static -o $@ launcher.c

clean:
	rm -f $(OUT)/primebox-launcher
