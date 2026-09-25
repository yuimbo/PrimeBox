# PrimeBox remediation & fix plan — tracker

Working document for the current round of fixes. It complements
[`HANDOFF.md`](HANDOFF.md) (bring-up state, build, traps) and supersedes the
tick-list in [`TODO.md`](TODO.md) for these items.

**How to use this document (resumability):**

1. Update the **Status board** row as work moves (`TODO` → `IN PROGRESS` →
   `BLOCKED` → `DONE`) and refresh the `Updated` date.
2. Append every material finding to **Findings log** (dated, newest first) —
   include the exact command run and the observed output, not a summary.
3. Keep **Anchors** current if line numbers move after an edit.
4. Do not delete closed items; move them to **Closed** with the reason.

---

## Status board

| # | Item | Scope | State | Effort | Updated |
|---|---|---|---|---|---|
| 1.1 | Tempo slider remap (**left deck only**) | `knobshim.c` | TODO | 1–2 h | 2026-09-25 |
| 1.2 | Menu access (MEDIA/EJECT, + SHIFT+VIEW chord) | `knobshim.c` | TODO | ≤1 h | 2026-09-25 |
| 1.3 | Touchscreen transform (taps land in wrong spots) | `fbshim.c` | TODO | 1 session | 2026-09-25 |
| 2.1 | Headphone per-deck cue — find RX3 cue keycode | RE `rbp` | TODO | ½–1 day | 2026-09-25 |
| 2.2 | Wire PFL → cue keycode, verify CUE MIX/GAIN | `knobshim.c` | TODO | (with 2.1) | 2026-09-25 |
| 3.1 | LED protocol discovery (MIDI-in? note/velocity map) | djinn / sniff | TODO | ~30 min | 2026-09-25 |
| 3.2 | Idle LED glow | `knobshim.c` | TODO | hours | 2026-09-25 |
| 3.3 | LED app-state feedback (PLAY/CUE first) | `knobshim.c` | TODO | days, incremental | 2026-09-25 |
| 4.1 | RE usb2 mount path + `UiKey_Usb2` | RE `rbp` | TODO | ½ day | 2026-09-25 |
| 4.2 | Multi-disk `usb-watch.sh` + un-suppress kind 3 | scripts | TODO | ½ day | 2026-09-25 |
| 4.3 | Source-menu selection for USB2 | `knobshim.c` | TODO | hours | 2026-09-25 |
| 5.1 | NEON blit + rotate | DirectFB / fbdev | TODO | 1–2 days | 2026-09-25 |
| 5.2 | RGA2 rotate/convert offload | fbdev module | TODO | 2–4 days | 2026-09-25 |
| — | rekordbox LINK | — | CLOSED (won't-do) | — | 2026-09-25 |

**Document state: AWAITING REVIEW — no fix work has started.**

---

## Decisions locked (2026-09-25, from Q&A)

1. **Tempo**: full **±100%** remap across the working physical travel, detent =
   exactly 0%, saturate-clamp past the endstop. **Applies to the LEFT deck only**
   (deck 1); the right deck keeps the stock formula (its fader is healthy).
2. **Headphones**: go for real **per-deck cue** (reverse-engineer the Pioneer
   cue keycode). No stopgap-first.
3. **LEDs**: **both**, staged — idle glow first, then app-state feedback
   incrementally.
4. **Touchscreen**: taps **do land but in wrong positions** → transform /
   calibration, not a dead pipeline.

---

## Context & anchors

### Device / run
- Device: `primego.local` root SSH; all work under `/data`; host rootfs untouched.
- Launch: `sh /data/start-rb.sh` (clean) or `sh /data/restart-knob2.sh`
  (deploy + verbose). Boot menu: [`docs/09`](docs/09-runtime-launcher.md).
- Logs: `/tmp/knobshim.log`, `/tmp/audioshim.log`, `/tmp/fbshim-tsc.log`(new),
  `/data/rbp-p.log`, `/data/usbwatch.log`. Chroot `/tmp` == host `/tmp`.

### Build / deploy loop
```bash
make -C scripts/shims RX3="$PWD/extracted/XDJRX3-rootfs"
arm-linux-gnueabi-objdump -T scripts/shims/<x>.so | grep -oE 'GLIBC_[0-9.]+' | sort -u   # must be 2.4/2.7 only
scp scripts/shims/<x>.so deploy/* root@YOUR_PRIMEGO:/data/
# on device: sh /data/restart-knob2.sh
```
DirectFB module (`tools/build-directfb/`) is only needed for Phase 5.

### Reference material
- Keycode list / control map: [`docs/05-controls.md`](docs/05-controls.md).
- Memory map & function reference: [`docs/10-memory-map.md`](docs/10-memory-map.md).
- Touch protocol / symptom table: [`docs/04-touchscreen.md`](docs/04-touchscreen.md).
- USB detection chain / media kinds: [`docs/06-usb.md`](docs/06-usb.md).
- Display stack & perf history: [`docs/03-display.md`](docs/03-display.md), `HANDOFF.md` §1.
- Rooted-Denon repo (Engine OS internals, QML assignments): `/Users/jimmy/src/djinn`.

---

## Phase 1 — quick wins

### 1.1 Tempo slider remap — LEFT DECK ONLY

**Problem.** `handle_pitch()` (`scripts/shims/knobshim.c:660`) maps
`norm = (0x2000 - pos) / 8192` over the full 14-bit span. The left fader is
electrically damaged: it only sweeps the **top half** of the range.

**Measurements decode** (in "100%" mode, `norm = (8192 - pos)/8192`):

| Fader | Reported | Raw `pos` |
|---|---|---|
| fully up | −100% (correct) | 16383 (`0x3FFF`) |
| middle | −47.45% (should be 0) | ~12077 |
| fully down | −0.01% (should be +100%) | ~8191 (`0x1FFF`) |
| fully down + pressure | jitters +0…+6% | dips to ~7700 |

So the reliable electrical range is `[8191 … 16383]`; the jitter is entirely
beyond the physical endstop (raw < 8191).

**Fix (deck 1 only — receive ch 2, send ch 1):**
1. Add env-calibrated 3-point remap; defaults from the table above:
   - `TEMPO_MIN` = 8191 (bottom → +1.0)
   - `TEMPO_DETENT` = 12077 (centre → 0.0) — **re-measure at the physical
     detent with `TEMPO_VERBOSE=1`** and use that value.
   - `TEMPO_MAX` = 16383 (top → −1.0)
2. Piecewise-linear:
   - `pos >= TEMPO_MAX` → `norm = -1`
   - `pos <= TEMPO_MIN` → `norm = +1`   (saturates the pressure overshoot)
   - `TEMPO_DETENT <= pos <= TEMPO_MAX` →
     `norm = -(pos - TEMPO_DETENT) / (TEMPO_MAX - TEMPO_DETENT)`
   - `TEMPO_MIN <= pos <= TEMPO_DETENT` →
     `norm = +(TEMPO_DETENT - pos) / (TEMPO_DETENT - TEMPO_MIN)`
3. Optional detent snap: `|norm| < TEMPO_SNAP` (default 0.004) → `0.0`.
4. Gate: `TEMPO_CAL_DECK` (default `1`). Deck 2 keeps the stock formula.
   `TEMPO_REV` still applied after.
5. Keep the existing `v10` display computation and the `CC 0x4B` dispatch rule
   (line 683–685).

**Anchors:** `handle_pitch` `knobshim.c:660`; formula `:698`; `pitch_state`
`{rch 2, rch 3}` `:421`; send channel `sch = (ch == 2) ? 1 : 2` `:704`.

**Verify:** `TEMPO_VERBOSE=1` → top `-1.000`, detent `0.000`, bottom `+1.000`,
and no jitter while pressing the fader at the bottom. Cross-check optional:
Engine OS pitch behaviour on the same unit (informational only).

**State:** TODO

### 1.2 Menu access

MEDIA/EJECT is already mapped to `K_MENU 0x0206`
(`add_note(15, 20, K_MENU, CH_GLOBAL)`, `knobshim.c:766`).

1. On device, press MEDIA/EJECT with `KNOB_VERBOSE=1`; check whether note 20
   arrives and `sendKey` fires (`/tmp/knobshim.log`).
2. If the menu does not open, add a **SHIFT+VIEW → K_MENU** chord in
   `handle_note()` (shift state already tracked at `knobshim.c:472`).
3. Record what actually appears.

**Anchors:** `K_MENU` `:140`; note map `:766`; `handle_note` `:468`.

**State:** TODO

### 1.3 Touchscreen transform (taps land in wrong spots)

The pipeline works (`evdev → transform → tsc2007 pipe → rbp`,
`fbshim.c:104-251`); only the mapping is wrong. `transform()` is hardcoded and
there is **no touch logging** today.

1. Add `TSC_VERBOSE` logging in `reader_thread` (`fbshim.c:170`): raw `rx,ry`
   and transformed `lx,ly` per contact → `/tmp/fbshim-tsc.log`.
2. Add env knobs applied in `transform()` (`fbshim.c:136`):
   `TSC_FLIP_X`, `TSC_FLIP_Y`, `TSC_SWAP` (and, if needed, `TSC_X_MIN/MAX`,
   `TSC_Y_MIN/MAX`) so iteration needs no rebuild.
3. Corner-tap diagnostic: tap the four physical corners + centre, read raw
   values, match against the symptom table in
   [`docs/04` §6](docs/04-touchscreen.md). Known classes: mirrored X (firmware
   `invertX` not cancelled), mirrored Y, swapped axes, offset, wrong scale.
4. Bake the winning transform into the defaults.
5. Confirm the identity `TouchCalib_User.dat` is present in the deployed
   chroot (`settings/TouchCalib_User.dat`; format/line order in docs/04 §3).

**Anchors:** `transform` `fbshim.c:136`; debounce double-push `:209`;
`RAW_MAX 2048`, `TSC_MAX_X 3`, `TSC_MAX_Y 3900` `:107-109`.

**State:** TODO

---

## Phase 2 — headphone per-deck cue

**Why the phones only play master today.** `audioshim` already routes rbp's
real headphone stream to hw ch 2/3 (`audioshim.c:433-447`) and the master→hp
mirror (`:463-467`) is only active while rbp's cue stream is silent
(`s_has_hp_audio`, `:431/:445`). No channel is cue-assigned because the
Prime GO PFL buttons are unmapped ("no RX3 code", `knobshim.c:809`).

**Steps:**
1. Reverse-engineer the RX3 per-channel **cue keycode**:
   - Disassemble `ui::Mixer::asEventCode` @ `0x2cfd5c` and the mixer key
     dispatch (workstation `objdump`; recipes in `HANDOFF.md` §6).
   - Prime suspect: the gap at **`0x501d`** in the found mixer sequence
     `0x5019 TRIM, 0x501a EQH, 0x501b EQM, 0x501c EQL, [0x501d?], 0x501e FADER`.
   - Cross-check the `allinone_debug` button/knob tables from the
     primego-mapping notes referenced in `knobshim.c` header.
2. Wire mixer note 13 (PFL) → cue keycode in `build_maps()` (replace the
   log-only entry at `knobshim.c:809`), press/release, send channel = mixer ch.
3. Verify via `/tmp/audioshim.log`: `peak_hp` becomes non-zero → mirror
   auto-disables; then confirm CUE MIX (`K_HPMIX 0x4405`) and CUE GAIN
   (`K_HPLEVEL 0x4406`) act audibly, per-deck cue toggles correctly.
4. **Fallback** if rbp has no cue keycode (handled by RX3 panel MCU on real
   hardware): memory-poke the cue-assign state in the mixer-route
   neighbourhood (pattern of the `0x01149f50` / `0x01149f54` routing pokes,
   see `docs/10` §2), or a small `rbp` binary patch.

**Anchors:** `audioshim.c:431,433-447,463-467`; `knobshim.c:772-773,809`;
`docs/10` §2/§3; `docs/05` §3.3.

**State:** TODO

---

## Phase 3 — LEDs (staged)

**Why dark:** Engine OS userspace drives the control-surface MCU; the launcher
stops `engine.service`, so nothing drives LEDs. knobshim already holds
`/dev/snd/seq` `O_RDWR` (`knobshim.c:273`), so it can send if the surface
accepts host→device MIDI.

### 3.1 Protocol discovery (~30 min)
- Check `/Users/jimmy/src/djinn` for the Engine OS
  `JP11_Controller_Assignments.qml` (Denon assignment files usually define LED
  feedback: note + velocity/colour).
- Or live-sniff: subscribe a second ALSA-seq reader while Engine OS runs and
  press buttons / start playback, logging MIDI-out from the surface.
- Confirm the surface accepts **host→device** MIDI (write note-ons with Engine
  OS stopped, watch the LEDs).

### 3.2 Idle glow (hours)
- On startup, send dim note-ons for all button LEDs (level from 3.1), gated by
  `LED_GLOW=<level>`.

### 3.3 App-state feedback (incremental, days)
- Poll rbp state addresses for play/cue per deck (pattern of the existing
  memory pokes) and drive the LED notes. Start with PLAY/CUE, then pads/loops.
- Full-fidelity stretch: parse rbp's EUP/SUB panel writes, which accumulate in
  the subucom SPI stub FIFOs (`docs/02` §3); `EUP.mot`/`SUB.mot` are
  extractable from the image (`docs/01`) for cross-checking the protocol.

**Anchors:** seq fd `knobshim.c:273`; surface `16:0`; `docs/02:98`;
`docs/01:122`.

**State:** TODO

---

## Phase 4 — second USB source via hub

rbp natively supports a second source: `/tmp/udev_usb2` is patched in
(`tools/patch-rbp/rbp_patch.py:107-110`), the kind-3 detect flag/property block
exist (`0x03256944` / `+4`, `docs/10` §2), but `usb_auto_thread` actively
suppresses them (`knobshim.c:956-959`).

1. **RE (4.1):** extract the RX3's expected **usb2 mount path** (strings near
   the udev handler / `PathDecider`; likely `/media/usb2/sdb1`) and
   **`UiKey_Usb2`** in `BrowseKeyTable` (`0x0327488c`, slot next to
   `UiKey_Usb1` at slot 3).
2. **usb-watch.sh (4.2):** `find_rear_sd()` returns only the first disk
   (`usb-watch.sh:51-62`) — extend to a list; mount/bind the second disk and
   notify `/tmp/udev_usb2` (umount→mount order, as for usb1).
3. **knobshim (4.2/4.3):** stop suppressing kind 3 when disk 2 is mounted
   (`usb_auto_thread`); generalise the Source-menu knob-push (currently
   hard-forced to USB1, `knobshim.c:448-499`) to select the highlighted entry /
   USB2.
4. **Hardware:** use a **powered hub** (or SSDs) — the GO's single port powers
   everything; both sticks must be rekordbox-exported.

**State:** TODO

---

## Phase 5 — render CPU (optional, do last)

Current: **46.9 fps, rbp ≈ 69 % of one core** (`HANDOFF.md` §1, session 3).
Ceiling is rbp's own software RGB565 blit (`Bop_16_Kto_Aop`) + our software
rotate/convert (~5 ms).

- **5.1 NEON (1–2 days):** NEON-ize the DirectFB generic blit and the fbdev
  rotate (`fbdev_rotate_primary` in `tools/build-directfb/`). Profile with
  `PRIMEGO_TIMING=1` and `pcprof.so`. Biggest single win.
- **5.2 RGA2 (2–4 days):** if `/dev/rga` exists on the 6.1 kernel, offload
  rotate + RGB565→RGB32 in the patched fbdev module.
- **Skip Mali GPU:** no DirectFB 1.4 gfxdriver; rbp renders in software anyway,
  so the GPU could only do the final compose — RGA is cheaper.

**State:** TODO

---

## Closed

- **rekordbox LINK — won't do.** The XDJ-RX3 itself has no Ethernet / no Pro DJ
  Link; the "kind 4 = LINK" entry in `docs/06` is CDJ-3000-inherited plumbing,
  non-functional even on real RX3 hardware. Optional: a 30-min strings/gate
  check to document it.

---

## Findings log

Append newest first. Include the exact command and observed output.

- **2026-09-25** — Plan written; decisions locked (see top). No device work
  started. Tempo scope constrained to the **left deck only** (user's fader has
  the hardware glitch; right fader healthy).
