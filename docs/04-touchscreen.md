# 04 — Touchscreen

`rbp` expects the XDJ-RX3's resistive **tsc2007** touch controller on
`/dev/tsc2007_2-0048`. The Prime GO has a capacitive **ILI2117** panel that
appears as a normal Linux evdev device. A shim translates one into the other.

## 1. Hardware / protocol mismatch

| | XDJ-RX3 | Prime GO |
|---|---|---|
| Panel | resistive, tsc2007 | capacitive, ILI2117 (i2c-4 @ 0x26) |
| Node | `/dev/tsc2007_2-0048` | `/dev/input/event0` (evdev) |
| Range | controller-specific (`max_x=3`, `max_y=3900`) | raw axes `[0,2048)²` |
| Events | custom 6-byte `ts_data` reads | Type-B multitouch + legacy ABS/BTN_TOUCH |

### The RX3 `ts_data` protocol (from `rbp` disassembly)

`TouchPanelComm::openDevice` opens the device and negotiates the maximums:

```
ioctl(fd, 0x80046b00, &x)   # read max X; if != 3, ioctl(fd,0x40046b00,&3)
ioctl(fd, 0x80026b01, &y)   # read max Y; if != 3900, ioctl(fd,0x40026b01,&3900)
read (fd, buf, 6)           # ts_data
```

`ts_data` (6 bytes little-endian):

| Offset | Field |
|---|---|
| 0 | `flag` — 1 = touch down, 0 = lift |
| 1 | padding |
| 2–3 | `x` (u16) |
| 4–5 | `y` (u16) |

`commRxDataProc` (`0x2d74b0`) decodes it, runs `TouchAdValueHysteresis`, applies
calibration, then `solveCoordToKey` maps the coordinate to a rekordbox UI
action.

## 2. `fbshim.so` — PART 2 (touch)

The same preload library that carries the fb shim also emulates the tsc2007.
It intercepts `open("/dev/tsc2007_2-0048")` and returns the read end of an
internal pipe. A background thread reads `/dev/input/event0`, transforms the
coordinates, and writes 6-byte `ts_data` frames into the pipe.

```
ILI2117 evdev ──► reader_thread ──transform──► pipe ──► rbp TouchPanel
```

### Coordinate transform

DirectFB is rotating 1280×800 → 800×1280 with `DFB_ROTATE=left`, i.e.
`px = ly`, `py = 1279 − lx`. Additionally Pioneer's firmware has `invertX = 1`,
so it computes `calX = 1280 − rawX` internally. Cancelling both gives the
working transform:

```c
int px = (rx * 800)  / 2048;   /* raw → physical portrait */
int py = (ry * 1280) / 2048;
*lx = py;                      /* value written as rawX to the firmware */
*ly = px;                      /* value written as rawY */
```

### Debounce burst

`TouchAdValueHysteresis` deliberately **zeroes the first touch-down frame**
(state 0→1 transition). A quick tap therefore delivered a down frame that was
discarded, followed immediately by a release → the tap vanished. The shim
pushes the down frame **twice** so the hysteresis advances 0→1→2 within the
same contact:

```c
if (cur_flag && !last_flag) {
    push_touch(1, lx, ly);   /* eaten by hysteresis */
    push_touch(1, lx, ly);   /* becomes the real down frame */
} else {
    push_touch(cur_flag, lx, ly);
}
```

### Multitouch decoding

The ILI2117 reports Type-B multitouch. The reader accepts all common encodings:

| evdev | meaning |
|---|---|
| `ABS_X` (`0x00`), `ABS_MT_POSITION_X` (`0x35`) | x |
| `ABS_Y` (`0x01`), `ABS_MT_POSITION_Y` (`0x36`) | y |
| `ABS_MT_TRACKING_ID` (`0x39`) | `>= 0` down, `-1` lift |
| `BTN_TOUCH` (`0x14a`) | down/up (legacy) |

## 3. Calibration

`rbp` reads `settings/TouchCalib_User.dat` (falling back to
`TouchCalib_Factory.dat`). The shim's transform assumes an **identity**
calibration, so PrimeBox installs:

```
0
0
320
200
1280
800
```

`TouchPanelFileManager::loadFactor` uses `getline`×6 + `strtol`×6 and reads the
buffers in a different order than it fills them, so the **line order is
load-bearing**:

| line | meaning |
|---|---|
| 1 | `offX` |
| 2 | `offY` |
| 3 | `scaleX` |
| 4 | `scaleY` |
| 5 | validation vs base X (must be `1280`) |
| 6 | validation vs base Y (must be `800`) |

## 4. `rbp` patches required for touch

Three firmware-level problems are fixed by
[`tools/patch-rbp/rbp_patch.py`](../tools/patch-rbp/rbp_patch.py):

1. **Browse-caution gate.** While any modal caution is active,
   `Pub_Total_GetBrowseDispMessage()` returns non-zero and both
   `TouchPanelHandler::touchOn` and `solveCoordToKey` discard every touch.

   | VA | change |
   |---|---|
   | `0x2dc228` | `mov r7, r0` → `mov r7, #0` (ignore caution id) |
   | `0x2dc46c` | `beq` → `b` (always enter dispatch) |

   Additionally `knobshim` clears the caution id at `0x05a191fc` while a USB
   drive is present.

2. **Playlist drag-scroll deadlock.** `TouchAreaProc_ListScroll::holdTouch`
   zeroes its own state on the first hold frame, so the counter can never reach
   the threshold that emits `sendKey(0x02b2, op=6, y)`.

   | VA | change |
   |---|---|
   | `0x363774` | force branch to the `sendKey` path |
   | `0x363794` | NOP the state-clear |

3. **Waveform window gate** (see [03 — Display](03-display.md)) also matters
   for the touch layout: the caution id gates the scrolling waveform.

## 5. Touch areas (logical 1280×800)

`BrowseListWindow` registers eight hit regions; useful when debugging:

| Region | x range | y range | Action |
|---|---|---|---|
| KeyboardAppear | 175–887 | 11–44 | search bar |
| KeywordAllClear | 728–758 | 12–62 | clear search |
| InfoBtn | 1180–1260 | 10–57 | INFO |
| ListScroll | 100–737 | 49–756 | track list (tap + drag) |
| ListLayerDown | 737–1280 | 100–705 | right column |
| CategoryBtn | 0–100 | 0–748 | left category sidebar |
| TimeMode1 | 18–97 | 713–784 | deck 1 time/remain |
| TimeMode2 | 658–737 | 713–784 | deck 2 time/remain |

Each hit dispatches a native key (`0x02a9` INFO, `0x02b2` list scroll,
`0x02b4` category) with normalised float coordinates.

## 6. Verifying

```sh
# on the device: raw events from the panel
cat /dev/input/event0 | od -An -tx1 | head      # touch the screen

# inside the chroot: the shim's own log
cat /tmp/fbshim.log        # only if verbose logging is enabled
```

Symptoms and fixes:

| Symptom | Cause |
|---|---|
| Nothing responds, all taps eaten | caution gate patches missing, or caution id non-zero |
| Taps work, drag-scroll doesn't | drag-scroll patches missing |
| Everything mirrored horizontally | firmware `invertX` not cancelled (transform wrong) |
| Only slow drags work | debounce burst missing |
