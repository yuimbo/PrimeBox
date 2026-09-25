/*
 * knobshim2.c — LD_PRELOAD shim mapping the ENTIRE Denon Prime GO control
 * surface (ALSA sequencer client 16:0 "PRIME GO Control Surface") onto the
 * XDJ-RX3's rekordbox player (rbp) key space.
 *
 * Prime GO side (Engine OS JP11_Controller_Assignments.qml — the file Engine
 * loads for the Prime GO's hardware->MIDI assignment; all channels 0-based):
 *   Global  (ch 15): Back note3, FWD note4, Browse push note6/CC5, View note7,
 *                    Shift note8, Media note20, cueMix CC12, cueGain CC13,
 *                    crossfader CC14
 *   Decks   (ch 2/3): Load note1/2, Sync n8, Cue n9, Play n10, modes n11..13,
 *                    pads n15..22, pitch bend n29/30, jog touch n33,
 *                    jog CC 0x37/0x4D (14-bit abs), vinyl n35, autoloop
 *                    push n39/CC32, pitch fader CC 0x1F/0x4B (14-bit, invert)
 *   Mixer   (ch 0/1): pfl n13, trim CC3, treble CC4, mid CC6, bass CC8,
 *                    fader CC14, sweep fx knob CC11, sweep select n14/15
 *   FX      (ch 4): select push n7/touch n9/CC33, time push n8/CC34,
 *                    wet/dry CC4, activate n6, assign n11/12
 *
 * RX3 side (keycodes decoded from rbp's allinone_debug button/knob tables,
 * shift-corrected + live-verified + reverse-verified against the handlers in
 * rbp itself — see primego-mapping/MAPPING.md and SESSION_STATE_14):
 *   sendKey(keycode, op, ch, param, float f, long l)
 *   op 0=push 2=release 4=relative-rotate 5=absolute-value
 *   Verified live: BROWSE 0x0202, SOURCE 0x0207, SELECTOR 0x420c.
 *
 * Jog / tempo model (decompiled from rbp — do NOT change casually):
 *   ui::JogSpeedGuesser  (0x366180) turns jog wheel samples into keys.
 *   JogSpeedGuesser::timerCallback (0x3663cc) sends:
 *     sendKey(0x4305, op 4, ch, 0, f, l)   key 0x4305 = jog WHEEL rotate,
 *     f = jog speed in rev/s (clamped +-8), l = jog position.
 *   ui::PlayerInnards::onKey_Jog (0x302c9c): op must be 4; it reads the f
 *     and l args out of the IKeyInput, then calls:
 *       DjEngineIF::setJogPulse(ch, l)   (0x4736c)
 *       DjEngineIF::setJogSpeed(ch, f)   (0x46d2c)  [when not searching]
 *     Player::setJogSpeed clamps to +-8.0 and treats units as rev/s.
 *   jog TOUCH = key 0x4306 push/release -> PlayerInnards::onKey_JogTouch
 *     (0x3042d8) -> DjEngineIF::touchJog(ch, bool) (0x46de4).
 *   Tempo (pitch) fader = key 0x4107 with op 5 and f = fader position in
 *     [-1..+1] (0 = detent center) -> onKey_TempoSlider (0x302a30) ->
 *     DjEngineIF::setTempoSlider(ch, f) (0x45e6c).
 *
 * Build (soft-float, glibc-2.13 chroot compatible):
 *   # sysroot from the extracted RX3 rootfs (glibc 2.13 — /tmp is ephemeral):
 *   mkdir -p /tmp/arm213sysroot/lib /tmp/arm213sysroot/usr/lib
 *   cp extracted/XDJRX3-rootfs/lib/{libc.so.6,libpthread.so.0,ld-linux.so.3} \
 *      /tmp/arm213sysroot/lib/
 *   cd /tmp/arm213sysroot/lib && for f in libc libpthread libm libdl librt; do
 *     ln -sf $f.so.6 $f.so; done
 *   arm-linux-gnueabi-gcc -O2 -mfloat-abi=soft -fno-stack-protector \
 *       -fPIC -shared -o knobshim2.so knobshim2.c -lpthread -lc \
 *       -L/tmp/arm213sysroot/lib -Wl,-rpath-link,/tmp/arm213sysroot/lib
 *   (must resolve to GLIBC_2.4-only symbols — verify with
 *    arm-linux-gnueabi-objdump -T knobshim2.so | grep GLIBC)
 *
 * Env:
 *   KNOB_SCALE=n    selector ticks per knob step (default 1)
 *   JOG_SCALE=n     jog ticks per 14-bit delta step (default 1)
 *   JOG_PPR=n       Prime GO jog counts per full revolution (default 128)
 *   JOG_REV=1       reverse jog direction (default 0)
 *   JOG_IDLE_MS=n   ms of inactivity before a speed-0 jog key is sent
 *                   (default 120)
 *   JOG_VERBOSE=1   log jog keys
 *   KNOB_VERBOSE=1  log every received MIDI event to /tmp/knobshim.log
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <sound/asequencer.h>

/* ---- real syscalls (bypass libc interposition) ---- */
static int real_open(const char *p, int flags)
{
     return syscall(SYS_openat, AT_FDCWD, p, flags, 0);
}
static int real_ioctl(int fd, unsigned long req, void *arg)
{
     return syscall(SYS_ioctl, fd, req, arg);
}
static ssize_t real_read(int fd, void *buf, size_t n)
{
     return syscall(SYS_read, fd, buf, n);
}
static int real_close(int fd)
{
     return syscall(SYS_close, fd);
}

/* ---- rbp integration ---- */
static int is_rbp_process(void)
{
     char cmd[128];
     int fd;
     ssize_t n;
     fd = real_open("/proc/self/cmdline", O_RDONLY);
     if (fd < 0)
          return 0;
     n = real_read(fd, cmd, sizeof(cmd) - 1);
     real_close(fd);
     if (n <= 0)
          return 0;
     cmd[n] = '\0';
     for (ssize_t i = 0; i < n; i++)
          if (cmd[i] == '\0')
               cmd[i] = ' ';
     return strstr(cmd, "rbp") != NULL;
}

#define UI_OBJ_MGR_GLOBAL 0x2685f2cUL
#define KEY_MANAGER_OFF   100
#define SENDKEY_VTABLE_WORD 2

#define OP_PRESS      0
#define OP_RELEASE    2
#define OP_ROTATE     4
#define OP_VALUE      5        /* absolute value: payload is the float f arg */

#define CH_GLOBAL     1        /* browse/source/selector used ch 1 (deck1) live */

/* ---- RX3 keycodes (see MAPPING.md) ---- */
#define K_SELECTOR   0x420c
#define K_BROWSE     0x0202
#define K_SOURCE     0x0201    /* RX3 SOURCE menu key (was wrongly 0x0207 in earlier sessions) */
#define K_USB1       0x0209    /* RX3 USB1 direct browse key */
#define K_LINK       0x0207    /* RX3 LINK key */
#define K_REKORDBOX  0x0208    /* RX3 REKORDBOX key */
#define K_TAGLIST    0x0203
#define K_MENU       0x0206
#define K_INFO       0x020b
#define K_BACK       0x420d    /* RX3 BACK key (was wrongly 0x4103 = SHIFT) */
#define K_LOAD       0x4311
#define K_PLAY       0x4101
#define K_CUE        0x4102
#define K_SYNC       0x4112
#define K_VINYL      0x4104
#define K_JOG_TOUCH  0x4306    /* jog plate touch  (push/release) */
#define K_JOG_ROT    0x4305    /* jog wheel rotate: f=speed(rev/s) l=pos */
#define K_TEMPO_RANGE 0x4107   /* tempo range select (+-6/10/16/WIDE) */
#define K_MT         0x4108    /* master tempo (key lock) toggle */
#define K_TEMPO_SLIDER 0x4109  /* tempo slider (pitch fader) */
#define K_ALOOP      0x4114
#define K_HOTCUE     0x4113
#define K_SLIPLOOP   0x4115
#define K_BEATJUMP   0x4116
#define K_PAD1       0x4117
#define K_LOOPIN     0x410c
#define K_LOOPOUT    0x410d
#define K_RELOOP     0x410e
#define K_REV        0x410f
#define K_SLIP       0x4110
#define K_MASTER     0x4111
#define K_MASTERCUE  0x4407
#define K_TRIM       0x5019
#define K_EQH        0x501a
#define K_EQM        0x501b
#define K_EQL        0x501c
#define K_FADER      0x501e
#define K_XFADER     0x6017
#define K_HPMIX      0x4405
#define K_HPLEVEL    0x4406
#define K_COLOR      0x509d    /* Sound Color FX knob (per channel) */
#define K_FILTER     0x50a6    /* Sound Color FX: Filter button */
#define K_DUBECHO    0x50a2    /* Sound Color FX: Dub Echo button */
#define K_SWEEP      0x50a3    /* Sound Color FX: Sweep button */
#define K_NOISE      0x50a4    /* Sound Color FX: Noise button */
#define K_SPACE      0x50a5    /* Sound Color FX: Space button */
#define K_CRUSH      0x50a1    /* Sound Color FX: Crush button */
#define K_BFXTYPE    0x448b    /* Beat FX Type Select (0=Delay, 1=Echo, etc.) */
#define K_BFXCH      0x448c    /* Beat FX Channel Assign (5 = MASTER) */
#define BFX_CH_MASTER 5
#define K_BFX        0x448d    /* Beat FX Enable (ON / OFF toggle) */
#define K_TIME       0x448e    /* Beat FX Time */
#define K_DEPTH      0x448f    /* Beat FX Level/Depth (Wet/Dry intensity) */
#define K_BEATPREV   0x4490    /* Beat FX Beat Fraction < (halve) */
#define K_BEATNEXT   0x4491    /* Beat FX Beat Fraction > (double) */
#define K_TAP        0x4492    /* Beat FX Tap */
#define K_EFFECTQUANT 0x0493
#define K_MIC        0x0814

static int is_rbp_checked = -1;

static void *get_key_manager(void)
{
     void **p;
     void *mgr;
     if (is_rbp_checked < 0)
          is_rbp_checked = is_rbp_process();
     if (!is_rbp_checked)
          return NULL;
     p = (void **)UI_OBJ_MGR_GLOBAL;
     if (!p)
          return NULL;
     mgr = *p;
     if (!mgr)
          return NULL;
     return *(void **)((char *)mgr + KEY_MANAGER_OFF);
}

typedef void (*sendkey_fn)(void *km, int keycode, int op, int ch,
                           long param, float f, long l);

static void send_rx_key_fl(int keycode, int op, int ch, long param,
                           float fval, long lval)
{
     void *km = get_key_manager();
     if (!km)
          return;
     void **vt = *(void ***)km;
     sendkey_fn fn = (sendkey_fn)vt[SENDKEY_VTABLE_WORD];
     if (!fn)
          return;
     fn(km, keycode, op, ch, param, fval, lval);
}

static void send_rx_key_f(int keycode, int op, int ch, long param, float fval)
{
     send_rx_key_fl(keycode, op, ch, param, fval, 0);
}

static void send_rx_key(int keycode, int op, int ch, long param)
{
     send_rx_key_fl(keycode, op, ch, param, 0.0f, 0);
}

#define LOG_PATH "/tmp/knobshim.log"
static void klog(const char *fmt, ...)
{
     char buf[256];
     va_list ap;
     va_start(ap, fmt);
     int n = vsnprintf(buf, sizeof(buf), fmt, ap);
     va_end(ap);
     if (n > 0) {
          int fd = real_open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND);
          if (fd >= 0) {
               (void)write(fd, buf, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1));
               real_close(fd);
          }
     }
}

/* ---- ALSA sequencer ---- */
#define SEQ_DEV        "/dev/snd/seq"
#define SURFACE_CLIENT 16
#define SURFACE_PORT   0

static int seq_fd = -1;
static int seq_client = -1;
static int seq_port = -1;
static int knob_scale = 1;
static int jog_scale = 1;
static int verbose = 0;

static void seq_setup(void)
{
     struct snd_seq_client_info cinfo;
     struct snd_seq_port_info pinfo;
     struct snd_seq_port_subscribe sub;
     int ver;

     seq_fd = real_open(SEQ_DEV, O_RDWR | O_NONBLOCK);
     if (seq_fd < 0) {
          klog("knobshim2: open %s failed: %s\n", SEQ_DEV, strerror(errno));
          return;
     }
     if (real_ioctl(seq_fd, SNDRV_SEQ_IOCTL_PVERSION, &ver) < 0) {
          klog("knobshim2: PVERSION failed: %s\n", strerror(errno));
          real_close(seq_fd); seq_fd = -1; return;
     }
     if (real_ioctl(seq_fd, SNDRV_SEQ_IOCTL_CLIENT_ID, &seq_client) < 0) {
          klog("knobshim2: CLIENT_ID failed: %s\n", strerror(errno));
          real_close(seq_fd); seq_fd = -1; return;
     }

     memset(&cinfo, 0, sizeof(cinfo));
     cinfo.client = seq_client;
     cinfo.type = USER_CLIENT;
     snprintf(cinfo.name, sizeof(cinfo.name), "rbp-knob2");
     if (real_ioctl(seq_fd, SNDRV_SEQ_IOCTL_SET_CLIENT_INFO, &cinfo) < 0) {
          klog("knobshim2: SET_CLIENT_INFO failed: %s\n", strerror(errno));
          real_close(seq_fd); seq_fd = -1; return;
     }

     memset(&pinfo, 0, sizeof(pinfo));
     pinfo.addr.client = seq_client;
     snprintf(pinfo.name, sizeof(pinfo.name), "rbp-knob2-in");
     pinfo.capability = SNDRV_SEQ_PORT_CAP_WRITE | SNDRV_SEQ_PORT_CAP_SUBS_WRITE;
     pinfo.type = SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC | SNDRV_SEQ_PORT_TYPE_APPLICATION;
     pinfo.midi_channels = 16;
     if (real_ioctl(seq_fd, SNDRV_SEQ_IOCTL_CREATE_PORT, &pinfo) < 0) {
          klog("knobshim2: CREATE_PORT failed: %s\n", strerror(errno));
          real_close(seq_fd); seq_fd = -1; return;
     }
     seq_port = pinfo.addr.port;

     memset(&sub, 0, sizeof(sub));
     sub.sender.client = SURFACE_CLIENT;
     sub.sender.port = SURFACE_PORT;
     sub.dest.client = seq_client;
     sub.dest.port = seq_port;
     sub.queue = SNDRV_SEQ_QUEUE_DIRECT;
     if (real_ioctl(seq_fd, SNDRV_SEQ_IOCTL_SUBSCRIBE_PORT, &sub) < 0) {
          klog("knobshim2: SUBSCRIBE %d:%d -> %d:%d failed: %s\n",
               SURFACE_CLIENT, SURFACE_PORT, seq_client, seq_port,
               strerror(errno));
          real_close(seq_fd); seq_fd = -1; return;
     }
     klog("knobshim2: seq ok client=%d port=%d subscribed to %d:%d\n",
          seq_client, seq_port, SURFACE_CLIENT, SURFACE_PORT);
}

/* ---------- generic helpers ---------- */

/* 7-bit CC (0..127) -> RX3 10-bit knob value (0..1023) */
static int cc_to_10bit(int v)
{
     if (v < 0) v = 0;
     if (v > 127) v = 127;
     return (v << 3) | (v >> 4);
}

/* clamp rotate burst to avoid flooding the key queue */
static void rot_clamped(int key, int ch, int n)
{
     if (n > 16) n = 16;
     if (n < -16) n = -16;
     for (int i = 0; i < (n < 0 ? -n : n); i++)
          send_rx_key(key, OP_ROTATE, ch, (n < 0) ? -1 : 1);
}

/* ---------- per-control state ---------- */
#define NKEYS 96
static struct {
     int rch;         /* receive channel (0-based seq channel) */
     int note;
     int key;
     int sch;         /* send channel for sendKey */
     int pressed;
} note_map[NKEYS];
static int note_map_n = 0;

static void add_note(int rch, int note, int key, int sch)
{
     if (note_map_n >= NKEYS) return;
     note_map[note_map_n].rch = rch;
     note_map[note_map_n].note = note;
     note_map[note_map_n].key = key;
     note_map[note_map_n].sch = sch;
     note_map[note_map_n].pressed = 0;
     note_map_n++;
}

/* absolute CC knob: {rch, cc, key, sch}; send 10-bit value on change */
#define NABS 40
static struct {
     int rch, cc, key, sch;
     int last;
} abs_map[NABS];
static int abs_map_n = 0;

static void add_abs(int rch, int cc, int key, int sch)
{
     if (abs_map_n >= NABS) return;
     abs_map[abs_map_n].rch = rch;
     abs_map[abs_map_n].cc = cc;
     abs_map[abs_map_n].key = key;
     abs_map[abs_map_n].sch = sch;
     abs_map[abs_map_n].last = -1;
     abs_map_n++;
}

/* jog wheel: 14-bit absolute position assembled from CC 0x37 (hi) + 0x4D (lo).
 * The RX3 wants per-move keys 0x4305/op4 carrying:
 *   f = jog speed in revolutions/sec (Player::setJogSpeed clamps to ±8)
 *   l = jog position as a wrap-around counter (JogPulse::update expects a
 *       16-bit counter wrapping at 65536, JOG_POS_TH = 0xFB7F).
 * The Prime GO jog reports an absolute 14-bit position that wraps at 16384;
 * we unwrap it into a continuous virtual u16 counter (vpos) and compute
 * speed from the delta per sample time. */
struct jog_ctrl {
     int rch;
     int pos;            /* last full 14-bit position */
     int have_hi;
     int have_lo;
     int prev;           /* previous full position for delta */
     int ready;          /* saw at least one full sample */
     unsigned long long last_ms;  /* monotonic ms of previous completed sample */
     unsigned int vpos;  /* continuous virtual jog counter (u16 space) */
     int moving;         /* jog currently moving / nonzero speed sent */
     float speed;        /* last computed speed (rev/s) */
     int sch;
};
static struct jog_ctrl jog_state[2] = { {2,0,0,0,0,0,0,0,0,0.0f,1},
                                        {3,0,0,0,0,0,0,0,0,0.0f,2} };

static int jog_ppr = 128;      /* Prime GO counts per revolution (calibrate) */
static int jog_rev = 0;        /* invert jog direction */
static int jog_idle_ms = 120;
static int jog_verbose = 0;

/* pitch fader: 14-bit, CC 0x1F (hi) + 0x4B (lo), inverted */
struct pitch_ctrl {
     int rch;
     int pos;
     int have_hi;
     int have_lo;
     int ready;
};
static struct pitch_ctrl pitch_state[2] = { {2,0,0,0,0}, {3,0,0,0,0} };
static int tempo_verbose = 0;

/* monotonic ms */
static unsigned long long now_ms(void)
{
     struct timespec ts;
     clock_gettime(CLOCK_MONOTONIC, &ts);
     return (unsigned long long)ts.tv_sec * 1000ULL +
            (unsigned long long)ts.tv_nsec / 1000000ULL;
}

/* browse knob position (global CC5) */
static int knob_pos = -1;
static int shift_down = 0;

/* ---------- event handlers ---------- */

/* Source-menu -> USB1-open remap.
 * On the real RX3 the drive is opened from the Source menu by the dedicated
 * hardware USB1 source button (key 0x0209 -> BrowseUiIfDpl::onKey case 0x0209
 * -> BrowseUiIf::InputKey(UKEY_USB1=3) -> UiKey_Usb1 -> ChangeBrowseDevice(3)
 * -> DEV_SEL R232c messages -> browse list population).  The RX3 engine
 * deliberately IGNORES the browse-encoder push in the Source menu (mode 12),
 * and the Prime GO has no USB1 button, so pushing the browse knob (note 6) or
 * pressing FWD (note 4) while the Source menu is shown is remapped to key
 * 0x0209 so the drive can actually be opened. */
static int source_menu_with_usb1(void)
{
     if (*(volatile uint32_t *)0x326f8b8 != 12)   /* browseMode != 12 */
          return 0;
     if (access("/media/usb1/sda1/PIONEER/rekordbox/export.pdb", F_OK) != 0)
          return 0;
     return 1;
}

/* VIEW with a mounted Rekordbox stick but no browse source selected yet
 * (browseDevice 0) would show the empty "Please select a source" screen: the
 * RX3 expects the user to pick a source with its USB1 button first, which the
 * Prime GO does not have. Treat that VIEW as the USB1 button. */
static int view_needs_usb1(void)
{
     if (*(volatile uint32_t *)0x326f8bc != 0)      /* browseDevice already set */
          return 0;
     return access("/media/usb1/sda1/PIONEER/rekordbox/export.pdb", F_OK) == 0;
}

static void handle_note(int ch, int note, int on)
{
     static int view_as_usb1;

     if (ch == 15 && note == 8)
          shift_down = on;

     if (ch == 15 && note == 7) {
          if (on)
               view_as_usb1 = view_needs_usb1();
          if (view_as_usb1) {
               send_rx_key(K_USB1, on ? OP_PRESS : OP_RELEASE, CH_GLOBAL, 0);
               klog("knobshim2: VIEW %s -> USB1 select 0x0209 (no source yet)\n",
                    on ? "on" : "off");
               if (!on)
                    view_as_usb1 = 0;
               return;
          }
     }

     /* Source menu + mounted Rekordbox stick: knob push and FWD become the
      * USB1 source button.  Swallow BOTH edges so the generic SELECTOR/SOURCE
      * mapping below never fires for this gesture. */
     if (ch == 15 && (note == 4 || note == 6)) {
          if (source_menu_with_usb1()) {
               send_rx_key(K_USB1, on ? OP_PRESS : OP_RELEASE, CH_GLOBAL, 0);
               if (verbose)
                    klog("knobshim2: ch15 note%d %s -> USB1 select 0x0209\n",
                         note, on ? "on" : "off");
               return;
          }
     }

     for (int i = 0; i < note_map_n; i++) {
          if (note_map[i].rch == ch && note_map[i].note == note) {
               if (note_map[i].key == 0) {
                    if (verbose)
                         klog("knobshim2: ch%d note%d (log-only)\n", ch, note);
                    return;
               }
               int key = note_map[i].key;
               int *p = &note_map[i].pressed;
               if (on && !*p) {
                    *p = 1;
                    send_rx_key(key, OP_PRESS, note_map[i].sch, 0);
                    if (verbose || key == K_FILTER || key == K_SWEEP ||
                        key == K_BFX || key == K_BEATPREV || key == K_BEATNEXT)
                         klog("knobshim2: ch%d note%d -> 0x%04x press (sch%d)\n",
                              ch, note, key, note_map[i].sch);
               } else if (!on && *p) {
                    *p = 0;
                    send_rx_key(key, OP_RELEASE, note_map[i].sch, 0);
                    if (verbose || key == K_FILTER || key == K_SWEEP ||
                        key == K_BFX || key == K_BEATPREV || key == K_BEATNEXT)
                         klog("knobshim2: ch%d note%d -> 0x%04x release (sch%d)\n",
                              ch, note, key, note_map[i].sch);
               }
               return;
          }
     }
     if (verbose)
          klog("knobshim2: unmapped ch%d note%d %s\n", ch, note, on ? "on" : "off");
}

static void handle_cc_abs(int ch, int cc, int val)
{
     for (int i = 0; i < abs_map_n; i++) {
          if (abs_map[i].rch == ch && abs_map[i].cc == cc) {
               int v = cc_to_10bit(val);
               if (v != abs_map[i].last) {
                    abs_map[i].last = v;
                    float fval = (float)v / 1023.0f;
                    int op = (abs_map[i].key == K_COLOR || abs_map[i].key == K_DEPTH) ? OP_VALUE : OP_ROTATE;
                    send_rx_key_f(abs_map[i].key, op, abs_map[i].sch, v, fval);
                    if (verbose || (abs_map[i].key == K_COLOR && tempo_verbose) || abs_map[i].key == K_DEPTH)
                         klog("knobshim2: ch%d cc%d -> 0x%04x val=%d f=%.3f (sch%d)\n",
                              ch, cc, abs_map[i].key, v, (double)fval, abs_map[i].sch);
               }
               return;
          }
     }
     if (verbose)
          klog("knobshim2: unmapped ch%d cc%d val=%d\n", ch, cc, val);
}

/* browse knob (global CC5): relative delta (1 = +1 step, 127 = -1 step) */
static void handle_knob_pos(int v)
{
     if (v < 0 || v > 127)
          return;
     int delta = (v >= 64) ? (v - 128) : v;
     if (delta == 0)
          return;
     rot_clamped(K_SELECTOR, CH_GLOBAL, delta * knob_scale);
     if (verbose)
          klog("knobshim2: browse knob v=%d delta=%d\n", v, delta);
}

/* jog: assemble 14-bit pos; on each completed sample emit the RX3 jog
 * wheel key 0x4305 / op4 with f = speed (rev/s) and l = virtual position.
 * Speed sign follows the jog direction; jog_rev flips it. */
static void handle_jog(int ch, int cc, int val)
{
     int idx = -1;
     for (int i = 0; i < 2; i++)
          if (jog_state[i].rch == ch) { idx = i; break; }
     if (idx < 0)
          return;
     struct jog_ctrl *s = &jog_state[idx];
     if (cc == 0x37) {
          s->have_hi = 1;
          s->pos = (s->pos & 0x7f) | (val << 7);   /* store hi half now */
     } else if (cc == 0x4D) {
          s->have_lo = 1;
          s->pos = (s->pos & 0x3f80) | val;        /* store lo half now */
     } else {
          return;
     }
     if (!s->have_hi || !s->have_lo)
          return;                  /* need both halves */
     int pos = s->pos;
     s->have_hi = s->have_lo = 0;  /* consume the pair */
     if (!s->ready) {
          s->ready = 1;
          s->prev = pos;
          s->last_ms = now_ms();
          return;
     }
     int d = pos - s->prev;
     s->prev = pos;
     if (d > 8192) d -= 16384;
     if (d < -8192) d += 16384;
     if (d == 0) {
          s->last_ms = now_ms();
          return;
     }
     unsigned long long t = now_ms();
     float dt = (float)(long long)(t - s->last_ms) / 1000.0f;
     s->last_ms = t;
     if (dt < 0.0005f) dt = 0.0005f;
     if (jog_rev)
          d = -d;
     int dp = d * jog_scale;               /* scaled pulse delta */
     if (dp > 4096) dp = 4096;
     if (dp < -4096) dp = -4096;
     /* continuous virtual counter in 16-bit space (wrap 65536) */
     s->vpos = (unsigned int)(s->vpos + (unsigned int)dp) & 0xFFFFu;
     /* speed in rev/s of the Prime GO jog */
     float speed = (float)dp / (float)(jog_ppr * jog_scale) / dt;
     if (speed > 8.0f) speed = 8.0f;
     if (speed < -8.0f) speed = -8.0f;
     s->moving = 1;
     s->speed = speed;
     send_rx_key_fl(K_JOG_ROT, OP_ROTATE, s->sch, 0, speed, (long)s->vpos);
     if (jog_verbose)
          klog("knobshim2: jog ch%d delta=%d speed=%.2f pos=%u (sch%d)\n",
               ch, d, (double)speed, s->vpos, s->sch);
}

/* jog idle watcher: when the wheel has not moved for jog_idle_ms, send a
 * speed-0 key so the engine ends the pitch bend / jog state. */
static void *jog_idle_thread(void *arg)
{
     (void)arg;
     for (;;) {
          usleep(30000);   /* 30 ms */
          unsigned long long t = now_ms();
          for (int i = 0; i < 2; i++) {
               struct jog_ctrl *s = &jog_state[i];
               if (!s->ready || !s->moving)
                    continue;
               if ((unsigned long long)(long long)(t - s->last_ms) <
                   (unsigned long long)jog_idle_ms)
                    continue;
               s->moving = 0;
               s->speed = 0.0f;
               send_rx_key_fl(K_JOG_ROT, OP_ROTATE, s->sch, 0, 0.0f, (long)s->vpos);
               if (jog_verbose)
                    klog("knobshim2: jog ch%d idle -> speed 0\n", s->rch);
          }
     }
     return NULL;
}

static int tempo_rev = 0;

/* pitch fader: 14-bit, CC 0x1F (hi) + 0x4B (lo).
 * Prime GO hardware: 0x0000 = bottom (+), 0x3FFF = top (-)
 * RX3 tempo slider = key 0x4109 op 5, payload = float fader position in
 * [-1.0 .. +1.0] (0 = detent center).
 * -1.0 = slower (top), +1.0 = faster (bottom).
 */
static void handle_pitch(int ch, int cc, int val)
{
     int idx = -1;
     for (int i = 0; i < 2; i++)
          if (pitch_state[i].rch == ch) { idx = i; break; }
     if (idx < 0)
          return;
     struct pitch_ctrl *s = &pitch_state[idx];
     if (cc == 0x1F) {
          s->have_hi = 1;
          s->pos = (s->pos & 0x7f) | (val << 7);
     } else if (cc == 0x4B) {
          s->have_lo = 1;
          s->pos = (s->pos & 0x3f80) | val;
     } else {
          return;
     }
     if (!s->ready) {
          if (s->have_hi && s->have_lo)
               s->ready = 1;
          else
               return;
     }
     /* Only dispatch on CC 0x4B (the low byte, which always arrives right after 0x1F) */
     if (cc != 0x4B)
          return;

     int pos = s->pos;
     if (pos < 0) pos = 0;
     if (pos > 0x3FFF) pos = 0x3FFF;

     /* Prime GO:
      * physical top (slower): pos = 0x3FFF (16383)
      * physical detent (center): pos = ~0x2000 (8192)
      * physical bottom (faster): pos = 0x0000 (0)
      * Pioneer:
      * float: -1.0 at top (slower), 0.0 at center, +1.0 at bottom (faster)
      */
     float norm = ((float)0x2000 - (float)pos) / 8192.0f;
     if (norm > 1.0f) norm = 1.0f;
     if (norm < -1.0f) norm = -1.0f;
     if (tempo_rev)
          norm = -norm;

     int sch = (ch == 2) ? 1 : 2;
     int v10 = (int)((norm + 1.0f) * 511.5f);
     if (v10 < 0) v10 = 0;
     if (v10 > 1023) v10 = 1023;

     send_rx_key_fl(K_TEMPO_SLIDER, OP_VALUE, sch, (long)v10, norm, (long)pos);
     if (tempo_verbose)
          klog("knobshim2: pitch ch%d (deck %d) pos=%d -> tempo norm=%.3f v10=0x%03x\n",
               ch, sch, pos, (double)norm, v10);
}

static void handle_event(const struct snd_seq_event *ev)
{
     if (!get_key_manager())
          return;
     switch (ev->type) {
     case SNDRV_SEQ_EVENT_CONTROLLER: {
          int ch = ev->data.control.channel;
          int cc = ev->data.control.param;
          int val = ev->data.control.value;
          if (ch == 15 && cc == 5)
               handle_knob_pos(val);
          else if (cc == 0x37 || cc == 0x4D)
               handle_jog(ch, cc, val);
          else if (cc == 0x1F || cc == 0x4B)
               handle_pitch(ch, cc, val);
          else
               handle_cc_abs(ch, cc, val);
          break;
     }
     case SNDRV_SEQ_EVENT_NOTEON: {
          int on = ev->data.note.velocity > 0;
          handle_note(ev->data.note.channel, ev->data.note.note, on);
          break;
     }
     case SNDRV_SEQ_EVENT_NOTEOFF:
          handle_note(ev->data.note.channel, ev->data.note.note, 0);
          break;
     default:
          break;
     }
}

static void *bfx_init_thread(void *arg)
{
     (void)arg;
     for (int i = 0; i < 6; i++) {
          usleep(500000); /* 500ms */
          send_rx_key(K_BFXCH, OP_VALUE, CH_GLOBAL, BFX_CH_MASTER);
     }
     klog("knobshim2: Beat FX Channel master enforcement complete\n");
     return NULL;
}

static void build_maps(void)
{
     /* ---- global (ch 15) — send ch 1 ---- */
     add_note(15, 3,  K_BACK, CH_GLOBAL);      /* BACK */
     add_note(15, 4,  K_SOURCE, CH_GLOBAL);    /* FWD -> source */
     add_note(15, 6,  K_SELECTOR, CH_GLOBAL);  /* browse knob push */
     add_note(15, 7,  K_BROWSE, CH_GLOBAL);    /* VIEW */
     add_note(15, 8,  K_TAGLIST, CH_GLOBAL);   /* SHIFT -> tag list */
     add_note(15, 20, K_MENU, CH_GLOBAL);      /* MEDIA/EJECT -> menu */
     /* observed live: the Prime GO kernel surface streams the deck LOAD
      * buttons on ch15 (notes 1/2 = left/right deck), not on the deck
      * channels — map both here and on the deck channels (harmless). */
     add_note(15, 1,  K_LOAD, 1);
     add_note(15, 2,  K_LOAD, 2);
     add_abs(15, 12, K_HPMIX, CH_GLOBAL);      /* cue mix */
     add_abs(15, 13, K_HPLEVEL, CH_GLOBAL);    /* cue gain */
     add_abs(15, 14, K_XFADER, CH_GLOBAL);     /* crossfader */

     /* ---- decks (rch 2 = left -> deck1, rch 3 = right -> deck2) ---- */
     for (int d = 0; d < 2; d++) {
          int rch = 2 + d;
          int sch = 1 + d;
          add_note(rch, (d == 0) ? 1 : 2, K_LOAD, sch);
          add_note(rch, 8,  K_SYNC, sch);
          add_note(rch, 9,  K_CUE, sch);
          add_note(rch, 10, K_PLAY, sch);
          add_note(rch, 11, K_HOTCUE, sch);    /* mode CUES/STEMS */
          add_note(rch, 12, K_ALOOP, sch);     /* mode LOOPS/AUTO */
          add_note(rch, 13, K_SLIPLOOP, sch);  /* mode ROLL/SAMPLER */
          for (int p = 0; p < 8; p++)
               add_note(rch, 15 + p, K_PAD1 + p, sch);
          add_note(rch, 29, K_TEMPO_RANGE, sch); /* pitch bend - -> tempo range toggle */
          add_note(rch, 30, K_MT, sch);          /* pitch bend + -> master tempo (key lock) toggle */
          add_note(rch, 33, K_JOG_TOUCH, sch); /* jog touch */
          add_note(rch, 35, K_VINYL, sch);
          add_note(rch, 39, K_ALOOP, sch);     /* auto loop push */
          add_abs(rch, 32, K_ALOOP, sch);      /* auto loop knob */
     }

     /* ---- mixer channels (rch 0 = ch1 -> sch1, rch 1 = ch2 -> sch2) ---- */
     for (int m = 0; m < 2; m++) {
          int rch = m;
          int sch = 1 + m;
          add_abs(rch, 3,  K_TRIM, sch);
          add_abs(rch, 4,  K_EQH, sch);
          add_abs(rch, 6,  K_EQM, sch);
          add_abs(rch, 8,  K_EQL, sch);
          add_abs(rch, 14, K_FADER, sch);
          add_abs(rch, 11, K_COLOR, sch);      /* sweep fx knob -> Color knob (0x509d) */
          add_note(rch, 14, K_FILTER, sch);    /* sweep select DualFilter -> Filter (0x50a6) */
          add_note(rch, 15, K_SWEEP, sch);     /* sweep select Wash -> Sweep (0x50a3) */
          add_note(rch, 13, 0, sch);           /* PFL: no RX3 code, log-only */
     }

     /* ---- FX (rch 4) — send ch 1 ---- */
     add_note(4, 6,  K_BFX, CH_GLOBAL);        /* FX ON/OFF toggle (0x448d) */
     add_abs(4, 4,   K_DEPTH, CH_GLOBAL);      /* FX Intensity / Wet-Dry knob (0x448f) */
     add_note(4, 11, K_BEATPREV, CH_GLOBAL);   /* ASSIGN 1 -> Beat < (halve beat) (0x4490) */
     add_note(4, 12, K_BEATNEXT, CH_GLOBAL);   /* ASSIGN 2 -> Beat > (double beat) (0x4491) */
     add_note(4, 7,  K_BFXTYPE, CH_GLOBAL);    /* select push */
     add_note(4, 8,  K_TAP, CH_GLOBAL);        /* time push -> tap */
     add_abs(4, 34,  K_TIME, CH_GLOBAL);       /* time turn */
}

static void *midi_thread(void *arg)
{
     struct snd_seq_event ev;
     const char *s;
     (void)arg;

     if (!is_rbp_process())
          return NULL;

     s = getenv("KNOB_SCALE");
     if (s) knob_scale = atoi(s);
     if (knob_scale < 1) knob_scale = 1;
     s = getenv("JOG_SCALE");
     if (s) jog_scale = atoi(s);
     if (jog_scale < 1) jog_scale = 1;
     s = getenv("JOG_PPR");
     if (s) jog_ppr = atoi(s);
     if (jog_ppr < 1) jog_ppr = 1;
     jog_rev = getenv("JOG_REV") != NULL;
     s = getenv("JOG_IDLE_MS");
     if (s) jog_idle_ms = atoi(s);
     if (jog_idle_ms < 10) jog_idle_ms = 10;
     jog_verbose = getenv("JOG_VERBOSE") != NULL;
     tempo_verbose = getenv("TEMPO_VERBOSE") != NULL;
     tempo_rev = getenv("TEMPO_REV") != NULL;
     verbose = getenv("KNOB_VERBOSE") != NULL;

     build_maps();
     klog("knobshim2: thread started (maps: %d notes, %d abs knobs)\n",
          note_map_n, abs_map_n);

     for (int i = 0; i < 300; i++) {
          if (get_key_manager())
               break;
          usleep(100000);
     }
     if (!get_key_manager()) {
          klog("knobshim2: KeyManager never became ready\n");
          return NULL;
     }
     klog("knobshim2: KeyManager ready, opening sequencer...\n");

     /* Ensure audio routing in djengine::MixerRouteMngr:
      * On real RX3, physical DECK/LINE switches assign input routing.
      * On Prime GO without subucom switches, default routes left Channel 2 to Player 0.
      * Fix: permanently route Input 0 -> Player 0 (Deck 1) and Input 1 -> Player 1 (Deck 2).
      */
     *(volatile uint32_t *)0x01149f50 = 0x01149f08; /* Input 0 -> Player 0 */
     *(volatile uint32_t *)0x01149f54 = 0x01149f10; /* Input 1 -> Player 1 */
     klog("knobshim2: routed Mixer Ch1 -> Deck1, Ch2 -> Deck2\n");

     /* Initialize Sound Color FX to Filter on both channels so Sweep FX knob works out of the box */
     send_rx_key(K_FILTER, OP_PRESS, 1, 0);
     send_rx_key(K_FILTER, OP_RELEASE, 1, 0);
     send_rx_key(K_FILTER, OP_PRESS, 2, 0);
     send_rx_key(K_FILTER, OP_RELEASE, 2, 0);
     send_rx_key_f(K_COLOR, OP_VALUE, 1, 512, 0.5f);
     send_rx_key_f(K_COLOR, OP_VALUE, 2, 512, 0.5f);
     klog("knobshim2: Sound Color FX initialized to Filter on Ch1 & Ch2\n");

     /* Always set Beat FX Channel to MASTER (channel 5) */
     send_rx_key(K_BFXCH, OP_VALUE, CH_GLOBAL, BFX_CH_MASTER);
     klog("knobshim2: Beat FX Channel set to MASTER (5)\n");

     pthread_t bfx_tid;
     pthread_create(&bfx_tid, NULL, bfx_init_thread, NULL);
     pthread_detach(bfx_tid);

     seq_setup();
     if (seq_fd < 0) {
          klog("knobshim2: sequencer setup failed, giving up\n");
          return NULL;
     }
     klog("knobshim2: reading sequencer events (full Prime GO surface)\n");

     for (;;) {
          struct pollfd pfd;
          pfd.fd = seq_fd;
          pfd.events = POLLIN;
          int pr = poll(&pfd, 1, 1000);
          if (pr <= 0)
               continue;
          ssize_t n = real_read(seq_fd, &ev, sizeof(ev));
          if (n == (ssize_t)sizeof(ev))
               handle_event(&ev);
          else if (n < 0 && errno == EINTR)
               continue;
     }
     return NULL;
}

/* Stub out Pioneer PowerManager callbacks.
 * Prime GO lacks Pioneer's power manager hardware, so [UsbStorageManager+80] is NULL.
 * When a USB drive mounts/unmounts, rbp calls notifyPermissionChanged(NULL), etc. which segfaults. */
void _ZN3uif13IPowerManager23notifyPermissionChangedEv(void *this) { (void)this; }
void _ZN3uif13IPowerManager23notifyPreparedToStandbyEi(void *this, int a) { (void)this; (void)a; }
void _ZN3uif13IPowerManager21notifyAutoStandbyTimeEii(void *this, int a, int b) { (void)this; (void)a; (void)b; }
void _ZN3uif13IPowerManager22notifyScreenSaverModeEb(void *this, int a) { (void)this; (void)a; }
void _ZN3uif13IPowerManager12regPermitterEPNS_21IAutoStandbyPermitterE(void *this, void *a) { (void)this; (void)a; }
void _ZN3uif13IPowerManager15removePermitterEPNS_21IAutoStandbyPermitterE(void *this, void *a) { (void)this; (void)a; }
void _ZN3uif13IPowerManager12reqStandbyOnEv(void *this) { (void)this; }
void _ZN3uif13IPowerManager16prepareToStandyEv(void *this) { (void)this; }

/* USB stick auto-detection watcher thread.
 * On the Denon Prime GO there is only ONE rear USB-A port, but the Pioneer RX3
 * firmware has 2 USB ports: USB1 (kind 2 in UI, device 3) and USB2 (kind 3 in UI, device 2).
 * Pioneer's internal engine (Total_MainUsbMessageProc) writes to kind 3 (USB2), which causes
 * the UI to display a blank/phantom "USB2" and hides the USB1 stick label.
 * This thread continuously:
 *   1. Monitors for mounted Rekordbox stick (/media/usb1/sda1/PIONEER/rekordbox/export.pdb).
 *   2. Automatically redirects any kind 3 (USB2) detect flags and property info into kind 2 (USB1).
 *   3. Keeps kind 3 cleared to 0 so phantom USB2 is NEVER reported.
 *   4. Ensures USB1 detect flag = 2, uiConnectedMedia = 2 (USB1 only), browseDevice = 3 (USB1).
 *   5. Opens the Source menu on fresh attach, and clears state on detach.
 */
static void *usb_auto_thread(void *arg)
{
     (void)arg;
     int last_mounted = 0;
     for (;;) {
          usleep(100000); /* 100 ms */
          if (!get_key_manager())
               continue;

          int mounted = access("/media/usb1/sda1/PIONEER/rekordbox/export.pdb", F_OK) == 0;
          if (mounted) {
               volatile uint32_t *p_det_usb1 = (volatile uint32_t *)0x03256888;
               volatile uint32_t *p_det_usb2 = (volatile uint32_t *)0x03256944;
               volatile uint32_t *p_media    = (volatile uint32_t *)0x326f8b4;
               volatile uint32_t *p_mode     = (volatile uint32_t *)0x326f8b8;
               volatile uint32_t *p_dev      = (volatile uint32_t *)0x326f8bc;
               volatile uint32_t *p_refresh  = (volatile uint32_t *)0x326e128;

               /* Suppress phantom USB2 (kind 3) on Prime GO since it has only 1 physical port */
               if (*p_det_usb2 != 0) {
                    *p_det_usb2 = 0;
                    *p_refresh = 1;
               }

               /* When USB1 is ready (kind2=2), ensure UI knows media 2 is connected */
               if (*p_det_usb1 == 2) {
                    if (*p_media != 2) {
                         *p_media = 2;
                         *p_refresh = 1;
                    }
                    /* Ensure browse caution message is cleared so touchscreen is active */
                    volatile uint32_t *p_caution = (volatile uint32_t *)0x05a191fc;
                    if (*p_caution != 0) {
                         *p_caution = 0;
                    }
               }

               if (!last_mounted) {
                    *p_det_usb1 = 2;
                    *p_det_usb2 = 0;
                    *p_media = 2;
                    *p_dev = 3;   /* Device 3 = USB 1 */
                    *p_refresh = 1;
                    klog("knobshim2: USB1 detected -> registered (dev=3)\n");
               } else if (*p_mode == 12 && *p_dev == 0) {
                    /* On Source menu: keep USB1 (device 3) active so the stick label shows */
                    *p_media = 2;
                    *p_dev = 3;
                    *p_refresh = 1;
               }
          } else if (last_mounted) {
               /* Stick unplugged: clear detect flags */
               *(volatile uint32_t *)0x03256888 = 0;
               *(volatile uint32_t *)0x03256944 = 0;
               *(volatile uint32_t *)0x326f8b4 = 0;
               *(volatile uint32_t *)0x326e128 = 1;
               klog("knobshim2: USB removed\n");
          }
          last_mounted = mounted;
     }
     return NULL;
}

__attribute__((constructor))
static void knobshim2_init(void)
{
     pthread_t tid;
     if (pthread_create(&tid, NULL, midi_thread, NULL) == 0)
          pthread_detach(tid);
     if (pthread_create(&tid, NULL, jog_idle_thread, NULL) == 0)
          pthread_detach(tid);
     if (pthread_create(&tid, NULL, usb_auto_thread, NULL) == 0)
          pthread_detach(tid);
}
