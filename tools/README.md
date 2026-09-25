# tools/

Workstation-side tooling. Nothing here runs on the Prime GO.

| Tool | Language | Purpose |
|---|---|---|
| [`extract-firmware.sh`](extract-firmware.sh) | shell | download + decrypt + extract the XDJ-RX3 v1.20 firmware into `extracted/` |
| [`get-firmware.sh`](get-firmware.sh) | shell | download the official firmware only |
| [`rx3dec/`](rx3dec/) | Rust | `.UPD` → ISO 9660 decryptor |
| [`patch-rbp/`](patch-rbp/) | Python | apply the PrimeBox patches to `rbp` |
| [`build-directfb/`](build-directfb/) | C / patch | patched DirectFB 1.4.0 fbdev module for the Rockchip fb |
| [`assemble-chroot.sh`](assemble-chroot.sh) | shell | build the runtime tree from `extracted/` + `build/` |
| [`install.sh`](install.sh) | shell | copy the payload to the deck, install the boot unit |
| [`launcher/`](launcher/) | C | the boot menu (static ARM) |
| [`debug/`](debug/) | C | probes and test helpers (not shipped) |
| [`audit-runtime.py`](audit-runtime.py) | Python | whole-chroot symbol/ABI audit ([docs/12](../docs/12-runtime-audit.md)) |

The top-level `make` drives all of these; you rarely call them directly:

```sh
make                     # -> build/primebox.tar.gz
make install HOST=root@PRIMEGO.local
make debug               # -> build/debug/ (seqinject2, peek, probes)
```

`extract-firmware.sh` needs the firmware key at `keys/aes256.key`; it skips each
step whose output already exists, so it is safe to re-run.

## `patch-rbp`

`rbp_patch.py` contains the complete, verified instruction table that turns the
stock v1.20 `rbp` (md5 `4f2efcfc0c9e3f539289f863acfddcc6`) into the patched
player (md5 `3706c68f7242779d46afa09f35a39acf`). It is idempotent and validates
the stock words before writing. [`PATCHES.md`](patch-rbp/PATCHES.md) explains each
patch.

## `build-directfb`

Contains `directfb-1.4.0-fbdev.diff` (the patched `systems/fbdev`, applied to
DirectFB 1.4.0) and `compat.c`/`compat.map` (the glibc 2.13 shim for
`lib/direct`). No upstream DirectFB sources are shipped; `build.sh` fetches them
and builds the module inside the `primebox-armel` image.

## Credit

The `.UPD` decryption approach (and the keyfile format used by the Pioneer
updaters) was learned from
[`nsaintot/cdj3k-emu`](https://github.com/nsaintot/cdj3k-emu/tree/main), whose
`tools/upd-decrypt` helper showed how the images are unwrapped. Thanks!
