# PrimeBox — build the Prime GO runtime and install it.
#
#   make firmware      decrypt + extract the XDJ-RX3 v1.20 update (needs the key)
#   make               build everything into build/ (in the primebox-armel container)
#   make install HOST=root@primego.local
#   make uninstall HOST=root@primego.local
#
# See README.md for the full walkthrough.

X     := extracted
B     := build
IMAGE := primebox-armel:18.04
HOST  ?= root@primego.local

# Everything that compiles for ARM runs inside the toolchain image.
# /rx3 is the RX3 rootfs used as sysroot.
DOCKER := docker run --rm -v "$(CURDIR)":/src -v "$(CURDIR)/$(X)/XDJRX3-rootfs":/rx3:ro -w /src $(IMAGE)

STOCK_RBP_MD5   := 4f2efcfc0c9e3f539289f863acfddcc6
PATCHED_RBP_MD5 := 3706c68f7242779d46afa09f35a39acf

.PHONY: all image firmware shims launcher directfb rbp payload install uninstall debug clean distclean

all: payload

image:
	docker image inspect $(IMAGE) >/dev/null 2>&1 || docker build -t $(IMAGE) docker/

firmware:
	@tools/extract-firmware.sh

shims: image firmware
	$(DOCKER) make -s -C scripts/shims RX3=/rx3 OUT=/src/$(B)

launcher: image
	$(DOCKER) make -s -C tools/launcher OUT=/src/$(B)

directfb: $(B)/libdirectfb_fbdev.so
$(B)/libdirectfb_fbdev.so: tools/build-directfb/directfb-1.4.0-fbdev.diff tools/build-directfb/build.sh | image firmware
	mkdir -p $(B)
	docker run --rm -v "$(CURDIR)":/src:ro -v "$(CURDIR)/$(X)/XDJRX3-rootfs":/rx3:ro \
	    -v "$(CURDIR)/$(B)":/out -v primebox-directfb:/tmp/directfb $(IMAGE) \
	    sh /src/tools/build-directfb/build.sh

rbp: $(B)/rbp
$(B)/rbp: tools/patch-rbp/rbp_patch.py | firmware
	mkdir -p $(B)
	@echo "$(STOCK_RBP_MD5)  $(X)/XDJRX3/pdj/rbp" | md5sum -c --quiet 2>/dev/null \
	    || [ "$$(md5 -q $(X)/XDJRX3/pdj/rbp 2>/dev/null)" = "$(STOCK_RBP_MD5)" ] \
	    || { echo "stock rbp is not XDJ-RX3 v1.20"; exit 1; }
	python3 tools/patch-rbp/rbp_patch.py $(X)/XDJRX3/pdj/rbp -o $@

# build/primebox.tar.gz = what lands in /data/primebox on the deck
payload: shims launcher directfb rbp
	tools/assemble-chroot.sh $(X) $(B) $(B)/primebox/rootfs
	cp scripts/device/env.sh scripts/device/setup-chroot.sh scripts/device/start-rb.sh \
	   scripts/device/usb-watch.sh scripts/device/launcher.sh scripts/device/launcher.conf \
	   scripts/device/primebox-launcher.service $(B)/primebox-launcher $(B)/primebox/
	chmod 755 $(B)/primebox/*.sh $(B)/primebox/primebox-launcher
	COPYFILE_DISABLE=1 tar czf $(B)/primebox.tar.gz --uid 0 --gid 0 -C $(B)/primebox . 2>/dev/null \
	    || COPYFILE_DISABLE=1 tar czf $(B)/primebox.tar.gz -C $(B)/primebox .
	@echo "payload: $(B)/primebox.tar.gz ($$(du -h $(B)/primebox.tar.gz | cut -f1))"

install: payload
	tools/install.sh $(HOST)

uninstall:
	tools/install.sh $(HOST) uninstall

debug: image
	$(DOCKER) make -s -C tools/debug RX3=/rx3 OUT=/src/$(B)/debug

clean:
	rm -rf $(B)

distclean: clean
	docker volume rm primebox-directfb 2>/dev/null || true
