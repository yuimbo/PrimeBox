# 05 — Controls (buttons, knobs, faders, jog)

The Prime GO's control surface is a **USB MIDI device** (`15e4:800c`,
"PRIME GO Control Surface"), exposed as ALSA sequencer client **`16:0`**.
`rbp` knows nothing about MIDI — it reads keycodes from Pioneer front-panel
microcontrollers. `knobshim.so` bridges the two.

## 1. The control surface

* The device only streams MIDI while an **ALSA sequencer subscription** is
  active. Pure `rawmidi` reads get nothing (this is why stock Engine "just
  works" and a naive reader does not). `knobshim` subscribes itself.
* All channels are **0-based** in the firmware sources:
  * global `15` → MIDI ch 16,
  * decks `2`/`3` → MIDI ch 3/4,
  * mixer `0`/`1`,
  * FX `4`.

## 2. How the shim injects keys

`knobshim.so` is `LD_PRELOAD`ed into `rbp`. It resolves the live
`ui::KeyManager` singleton from `rbp`'s BSS:

```
uiObjectManager global  = 0x2685f2c
KeyManager              = *( *(0x2685f2c) + 100 )
```

then calls the key manager's virtual `sendKey`:

```c
typedef void (*sendkey_fn)(void *km, int keycode, int op, int ch,
                           long param, float fval, long lval);
/* vtable slot resolved as vtable[?] */
fn(km, keycode, op, ch, param, fval, lval);
```

Operation codes (`op`):

| op | name | payload |
|---|---|---|
| 0 | `OP_PRESS` | — |
| 2 | `OP_RELEASE` | — |
| 4 | `OP_ROTATE` | relative rotation; `f` = speed, `l` = position |
| 5 | `OP_VALUE` | absolute; `f` = normalised float, `param` = 10-bit int |

A background `usb_auto_thread` also drives source selection / USB state and
clears the browse-caution id; a `bfx_init_thread` locks Beat FX routing.

## 3. Prime GO MIDI → RX3 keycode map

### 3.1 Global channel (MIDI ch 16)

| Prime GO | MIDI | RX3 key (`knobshim`) |
|---|---|---|
| VIEW | Note 7 | `K_BROWSE 0x0202`; `K_USB1 0x0209` while no source is selected and a stick is mounted |
| FWD | Note 4 | `K_SOURCE 0x0201` |
| BACK | Note 3 | `K_BACK 0x420d` |
| Browse knob push | Note 6 | `K_SELECTOR 0x420c` press |
| Browse knob turn | CC 5 | `K_SELECTOR 0x420c` rotate (±1) |
| SHIFT | Note 8 | `K_TAGLIST 0x0203` |
| MEDIA / EJECT | Note 20 | `K_MENU 0x0206` |
| CUE MIX | CC 12 | `K_HPMIX 0x4405` |
| CUE GAIN | CC 13 | `K_HPLEVEL 0x4406` |
| CROSSFADER | CC 14 | `K_XFADER 0x6017` |
| LOAD 1 / 2 | Note 1 / 2 | `K_LOAD 0x4311` (deck 1 / 2) |

> The LOAD buttons stream on the **global** channel (ch 15) on the Prime GO,
> even though they are deck controls.

### 3.2 Deck channels (ch 2 = deck 1, ch 3 = deck 2)

| Prime GO | MIDI | RX3 key |
|---|---|---|
| SYNC | Note 8 | `0x4112` |
| CUE | Note 9 | `0x4102` |
| PLAY / PAUSE | Note 10 | `0x4101` |
| Pad mode CUES / STEMS | Note 11 | `0x4113` HOTCUE |
| Pad mode LOOPS / AUTO | Note 12 | `0x4114` ALOOP |
| Pad mode ROLL / SAMPLER | Note 13 | `0x4115` SLIPLOOP |
| Pads 1–8 | Notes 15–22 | `0x4117`–`0x411e` |
| PITCH BEND − | Note 29 | `0x4107` TEMPO RANGE toggle |
| PITCH BEND + | Note 30 | `0x4108` MASTER TEMPO (key lock) |
| JOG touch | Note 33 | `0x4306` |
| JOG rotate | CC `0x37` hi + `0x4D` lo (14-bit) | `0x4305` |
| VINYL | Note 35 | `0x4104` |
| AUTO LOOP push | Note 39 | `0x4114` |
| AUTO LOOP turn | CC 32 | `0x4114` rotate |
| PITCH fader | CC `0x1F` hi + `0x4B` lo (14-bit) | `0x4109` TEMPO SLIDER |

### 3.3 Mixer channels (ch 0 = ch 1, ch 1 = ch 2)

| Prime GO | MIDI | RX3 key |
|---|---|---|
| TRIM | CC 3 | `0x5019` |
| TREBLE | CC 4 | `0x501a` |
| MID | CC 6 | `0x501b` |
| BASS | CC 8 | `0x501c` |
| CHANNEL FADER | CC 14 | `0x501e` |
| SWEEP FX knob | CC 11 | `0x509d` COLOR (float 0..1, 0.5 centre) |
| SWEEP select A (DualFilter) | Note 14 | `0x50a6` FILTER |
| SWEEP select B (Wash) | Note 15 | `0x50a3` SWEEP |
| PFL | Note 13 | logged only |

### 3.4 FX channel (ch 4)

| Prime GO | MIDI | RX3 key |
|---|---|---|
| FX ON/OFF | Note 6 | `0x448d` Beat FX enable |
| FX wet/dry | CC 4 | `0x448f` Beat FX depth |
| ASSIGN 1 | Note 11 | `0x4490` Beat `<` (halve) |
| ASSIGN 2 | Note 12 | `0x4491` Beat `>` (double) |
| FX select push | Note 7 | `0x448b` type |
| FX time push | Note 8 | `0x4492` tap |
| FX time turn | CC 34 | `0x448e` time |
| (startup) | — | `0x448c` channel = MASTER (5) |

The full RX3 keycode list (decoded from `rbp`'s debug tables and verified
against the handlers):

```
0x0202 BROWSE   0x0203 TAGLIST  0x0206 MENU     0x0207 SOURCE
0x020b INFO     0x420d BACK     0x420c SELECTOR
0x4311 LOAD     0x4101 PLAY     0x4102 CUE      0x4112 SYNC
0x4104 VINYL    0x4306 JOGTOUCH 0x4305 JOGROT
0x4107 TEMPORANGE 0x4108 MT     0x4109 TEMPOSLIDER
0x4114 ALOOP    0x4113 HOTCUE   0x4115 SLIPLOOP 0x4116 BEATJUMP
0x4117..0x411e PAD1..PAD8
0x410c LOOPIN   0x410d LOOPOUT  0x410e RELOOP   0x410f REV
0x4110 SLIP     0x4111 MASTER   0x4407 MASTERCUE
0x5019 TRIM     0x501a EQH      0x501b EQM      0x501c EQL
0x501e FADER    0x6017 XFADER   0x4405 HPMIX    0x4406 HPLEVEL
0x509d COLOR    0x50a6 FILTER   0x50a2 DUBECHO  0x50a3 SWEEP
0x50a4 NOISE    0x448d EFFECT   0x0493 EFFECTQUANT
0x448e TIME     0x4492 TAP      0x448f DEPTH    0x448b BFX
0x448c BFXCH    0x0814 MIC
```

## 4. Encoders, jog wheels, faders

### Browse encoder

CC 5 carries a **relative two's-complement delta** on the Prime GO
(`1` = CW, `127` = CCW). The shim decodes:

```c
int delta = (v >= 64) ? v - 128 : v;
rot_clamped(K_SELECTOR, CH_GLOBAL, delta * knob_scale);
```

### Jog wheel

14-bit absolute position (CC `0x37` hi, CC `0x4D` lo). `rbp`'s
`JogSpeedGuesser` turns samples into a speed in rev/s (clamped ±8) and the shim
sends:

```c
send_rx_key_fl(0x4305, OP_ROTATE, deck, 0, speed, position);
```

* touch = `0x4306` press/release,
* idle timeout sends speed 0 (`JOG_IDLE_MS`, default 120 ms),
* `JOG_PPR` (pulses per revolution) and `JOG_REV` are tunable.

### Pitch (tempo) fader

14-bit CC `0x1F`/`0x4B`. The Prime GO slider runs `0x3FFF` (top/slower) →
`0x2000` (detent) → `0x0000` (bottom/faster). `rbp`'s `TempoSlider` wants a
float in `[-1..+1]` (0 = detent), so:

```c
float norm = ((float)0x2000 - (float)pos) / 8192.0f;   /* -1..+1 */
int   v10  = (int)((norm + 1.0f) * 511.5f);            /* 0..1023 display */
send_rx_key_fl(0x4109, OP_VALUE, deck, v10, norm, pos);
```

> Beware the historical bug: `0x2000f` in C is the **integer** `131087`, not a
> float literal. Use `8192.0f`.

### Mixer values

Analogue controls arrive as 7-bit CCs and are scaled to 10-bit and a
normalised float:

```c
int   v10  = v * 1023 / 127;
float fval = (float)v10 / 1023.0f;
send_rx_key_f(key, OP_VALUE, mixer_ch, v10, fval);
```

Startup defaults (so the mixer is audible immediately): faders = 1.0,
trims/EQs = 0.5, crossfader = 0.5, Sound Color FX = FILTER @ 0.5.

## 5. Startup initialisation

`knobshim` performs these once, when `KeyManager` becomes ready:

* mixer route: channel 1 → deck 1, channel 2 → deck 2
  (`*(0x01149f54) = 0x01149f10`),
* Sound Color FX set to Filter on both channels, knob at 0.5,
* Beat FX channel locked to MASTER (`0x448c` = 5) by a retry thread,
* phantom USB2 suppressed.

## 6. Environment variables

| Variable | Default | Meaning |
|---|---|---|
| `KNOB_SCALE` | 1 | selector ticks per knob step |
| `JOG_SCALE` | 1 | jog ticks per 14-bit step |
| `JOG_PPR` | 128 | Prime GO jog counts per revolution |
| `JOG_REV` | 0 | reverse jog direction |
| `JOG_IDLE_MS` | 120 | jog speed-0 timeout |
| `JOG_VERBOSE` | 0 | log jog keys |
| `TEMPO_VERBOSE` | 0 | log pitch-fader values |
| `KNOB_VERBOSE` | 0 | log every received MIDI event to `/tmp/knobshim.log` |

## 7. Testing without hardware

`scripts/shims/seqinject2.c` is a static ARM helper that injects notes/CCs/jog/
pitch onto any sequencer channel, so the mapping can be exercised without
touching the unit:

```sh
/data/seqinject2 note 15 7 127     # VIEW press
/data/seqinject2 note 15 7 0       # VIEW release
/data/seqinject2 cc   15 5 1       # browse knob CW one tick
/data/seqinject2 jog  3 100        # jog deck 2
```

With `KNOB_VERBOSE=1`, every event and the resulting keycode is logged to
`/tmp/knobshim.log`.
