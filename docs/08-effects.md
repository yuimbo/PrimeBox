# 08 — Sound Color FX & Beat FX

The Prime GO's FX strip has fewer controls than a DJM/RX3 mixer, and the Prime
GO has no hardware channel-select switch. `knobshim` maps the available
controls onto `rbp`'s FX keycodes and pins Beat FX to MASTER.

## 1. Sound Color FX (per channel)

Each Prime GO mixer channel has a **SWEEP FX** knob plus two mode buttons.

| Prime GO | MIDI | `rbp` key | `rbp` event |
|---|---|---|---|
| SWEEP FX knob | CC 11 | `0x509d` COLOR | `onEv_ColorFxVolume(float)` |
| Button A (DualFilter) | Note 14 | `0x50a6` FILTER | select Filter |
| Button B (Wash) | Note 15 | `0x50a3` SWEEP | select Sweep |

The knob sends a normalised float:

```
0.0 = full CCW (low-pass / wash)
0.5 = 12 o'clock detent (neutral)
1.0 = full CW  (high-pass / filter)
```

`rbp` initialises `SoundColorFxType = 0` (off), so a knob turn alone is silent.
`knobshim` presses Filter once on each channel at startup and sets the knob to
`0.5`, then the physical Buttons A/B switch modes live. Each channel is
independent (`sendKey(..., ch=1|2)`).

> Gotcha from the field: `0x50a6` is the FILTER **button**; the per-channel
> **knob** is `0x509d`. Sending knob rotations as `0x50a6` is discarded because
> that handler requires `op == OP_PRESS`.

## 2. Beat FX

| Prime GO | MIDI | `rbp` key | EventCode | `rbp` handler |
|---|---|---|---|---|
| FX ON/OFF | Note 6 | `0x448d` | `0x2012` | `onEv_BeatEffectEnable` |
| FX Intensity (wet/dry) | CC 4 | `0x448f` | `0x2013` | `onEv_BeatFxLvDepth` |
| ASSIGN 1 | Note 11 | `0x4490` | `0x2015` | `onEv_BeatFxBeat` (−1, halve) |
| ASSIGN 2 | Note 12 | `0x4491` | `0x2016` | `onEv_BeatFxBeat` (+1, double) |
| FX select push | Note 7 | `0x448b` | `0x2011` | `onEv_AuxGainSW` |
| FX time push | Note 8 | `0x4492` | `0x2017` | `onEv_BeatFxBeat` / tap |
| FX time turn | CC 34 | `0x448e` | `0x2014` | `onEv_BeatFxTime` |
| channel assign | — | `0x448c` | `0x2010` | `onEv_BeatEffectCh` |

Full keycode → EventCode mapping (`ui::Mixer::asEventCode`):

```
0x448b -> 0x2011 onEv_AuxGainSW
0x448c -> 0x2010 onEv_BeatEffectCh
0x448d -> 0x2012 onEv_BeatEffectEnable
0x448e -> 0x2014 onEv_BeatFxTime
0x448f -> 0x2013 onEv_BeatFxLvDepth
0x4490 -> 0x2015 onEv_BeatFxBeat (-1)
0x4491 -> 0x2016 onEv_BeatFxBeat (+1)
0x4492 -> 0x2017 onEv_BeatFxBeat / Tap
```

### Master routing

`EnBeatEffectSelectChannel` (`djengine::c_str`, `0x50e54`):

| value | channel |
|---|---|
| 0 | PLAYER_0 |
| 1 | PLAYER_1 |
| 2 | MIC_0 |
| 3 | ASSIGN_A |
| 4 | ASSIGN_B |
| **5** | **MASTER** |
| 6 | AUX |

The shim sends `sendKey(0x448c, OP_VALUE, ch, 5)` from a retry thread at startup,
locking the on-screen indicator to `MST`.

### Beat fractions

ASSIGN 1 / ASSIGN 2 were unused on the Prime GO and are repurposed:

* ASSIGN 1 → `0x4490` → `onEv_BeatFxBeat(-1)` → halve (1/1 → 1/2 → 1/4 …),
* ASSIGN 2 → `0x4491` → `onEv_BeatFxBeat(+1)` → double (1/4 → 1/2 → 1/1 → 2/1 …).

### Startup enforcement

```c
static void *bfx_init_thread(void *arg) {
    for (int i = 0; i < 6; i++) {
        usleep(500000);
        send_rx_key(K_BFXCH, OP_VALUE, CH_GLOBAL, 5);  /* MASTER */
    }
}
```

This runs even if `rbp` restores a saved routing from disk.

## 3. Verification

```
FX ON/OFF        → effect engages / disengages
Intensity CCW→CW → depth 0.0 → 1.0 smoothly
ASSIGN 1         → 1/1 → 1/2 → 1/4 → 1/8 → 1/16
ASSIGN 2         → 1/16 → 1/8 → 1/4 → 1/2 → 1/1 → 2/1
CH indicator     → MST
```

With `KNOB_VERBOSE=1`, `/tmp/knobshim.log` shows the sent keycodes. If FX
ON/OFF does nothing, the wrong key (`0x448b`) is being sent — the enable toggle
is `0x448d`.
