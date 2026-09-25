/*
 * primebox-launcher — boot menu for the Denon Prime GO.
 *
 * Draws a landscape menu (1280x800 logical, rotated onto the 800x1280 panel)
 * from launcher.conf (default /data/primebox/launcher.conf), takes input from
 * the touchscreen and the browse knob, and prints the chosen entry's command
 * on stdout. It exits 0 when an entry was chosen, 1 on error. It launches
 * nothing itself: the caller (launcher.sh) runs the command, so this binary
 * stays free of process management.
 *
 * launcher.conf:
 *   # comment / section heading ("# DJ Apps" shows as a heading)
 *   NAME | command            entry; empty command = "boot Engine OS"
 *   default = NAME            entry chosen when the countdown expires
 *   timeout = SECONDS         countdown; 0 = wait forever
 *
 * Input:
 *   touch                     tap an entry
 *   browse knob (ch15 cc5)    move selection; push (ch15 note6) = choose
 *   any input                 cancels the countdown
 *
 * Build: arm-linux-gnueabi-gcc -O2 -static -o primebox-launcher launcher.c
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <sound/asequencer.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "font8x8_basic.h"

#define CONF_PATH   "/data/primebox/launcher.conf"
#define FB_PATH     "/dev/fb0"
#define TOUCH_PATH  "/dev/input/event0"
#define SEQ_PATH    "/dev/snd/seq"
#define SURFACE_CLIENT 16

#define LW 1280                 /* logical (landscape) size */
#define LH 800
#define TOUCH_RAW 2048

#define MAX_ITEMS 48
#define GRACE_MS  2000          /* ignore input this long after start: the control
                                  * surface and touch panel emit their initial
                                  * state at boot, which is not user input */
#define ROW_H     56
#define TOP       110
#define FONT_S    3             /* 8x8 glyphs scaled x3 = 24 px */

#define C_BG      0xff101418
#define C_FG      0xffe8e8e8
#define C_DIM     0xff7a8088
#define C_HEAD    0xff4fc3f7
#define C_SEL     0xff1f6fb2
#define C_BAR     0xff1a1f26

struct item {
     char name[64];
     char cmd[512];
     int  heading;              /* 1 = section heading, not selectable */
};

static struct item items[MAX_ITEMS];
static int         nitems;
static char        default_name[64];
static int         timeout_s = 5;

static uint32_t   *fb;
static uint32_t   *back;        /* logical LW x LH frame */
static int         fb_w, fb_h, fb_pitch, fb_yoff;

/* ---------------------------------------------------------------- config */

static char *trim(char *s)
{
     char *e;
     while (*s == ' ' || *s == '\t')
          s++;
     e = s + strlen(s);
     while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r'))
          *--e = 0;
     return s;
}

static void add_item(const char *name, const char *cmd, int heading)
{
     if (nitems >= MAX_ITEMS)
          return;
     snprintf(items[nitems].name, sizeof(items[nitems].name), "%s", name);
     snprintf(items[nitems].cmd, sizeof(items[nitems].cmd), "%s", cmd);
     items[nitems].heading = heading;
     nitems++;
}

static int load_conf(const char *path)
{
     char line[640];
     FILE *f = fopen(path, "r");
     if (!f)
          return -1;
     while (fgets(line, sizeof(line), f)) {
          char *l = trim(line), *bar;
          if (!*l)
               continue;
          if (*l == '#') {
               l = trim(l + 1);
               /* only short, capitalised comments are headings */
               if (*l >= 'A' && *l <= 'Z' && strlen(l) < 40 && !strchr(l, '('))
                    add_item(l, "", 1);
               continue;
          }
          if ((bar = strchr(l, '|'))) {
               *bar = 0;
               add_item(trim(l), trim(bar + 1), 0);
               continue;
          }
          if (!strncmp(l, "default", 7) && strchr(l, '='))
               snprintf(default_name, sizeof(default_name), "%s", trim(strchr(l, '=') + 1));
          else if (!strncmp(l, "timeout", 7) && strchr(l, '='))
               timeout_s = atoi(trim(strchr(l, '=') + 1));
     }
     fclose(f);
     /* drop headings that have no entries under them */
     for (int i = nitems - 1; i >= 0; i--)
          if (items[i].heading && (i == nitems - 1 || items[i + 1].heading)) {
               memmove(&items[i], &items[i + 1], (nitems - i - 1) * sizeof(items[0]));
               nitems--;
          }
     return 0;
}

static int next_selectable(int from, int dir)
{
     for (int i = from + dir; i >= 0 && i < nitems; i += dir)
          if (!items[i].heading)
               return i;
     return from;
}

/* --------------------------------------------------------------- drawing */

static void fill(int x, int y, int w, int h, uint32_t c)
{
     if (x < 0) { w += x; x = 0; }
     if (y < 0) { h += y; y = 0; }
     if (x + w > LW) w = LW - x;
     if (y + h > LH) h = LH - y;
     for (int j = 0; j < h; j++) {
          uint32_t *p = back + (y + j) * LW + x;
          for (int i = 0; i < w; i++)
               p[i] = c;
     }
}

static void text(int x, int y, int s, uint32_t c, const char *str)
{
     for (; *str; str++, x += 8 * s) {
          const unsigned char *g = font8x8_basic[(unsigned char)*str & 0x7f];
          for (int gy = 0; gy < 8; gy++)
               for (int gx = 0; gx < 8; gx++)
                    if (g[gy] & (1 << gx))
                         fill(x + gx * s, y + gy * s, s, s, c);
     }
}

/* rotate "left" like DFB_ROTATE=left: logical (x,y) -> panel (y, LW-1-x) */
static void present(void)
{
     for (int y = 0; y < LH; y++) {
          const uint32_t *s = back + y * LW;
          for (int x = 0; x < LW; x++)
               fb[(size_t)(LW - 1 - x) * (fb_pitch / 4) + y] = s[x];
     }
}

static int visible_rows(void) { return (LH - TOP - 70) / ROW_H; }

static void draw(int sel, int scroll, int countdown)
{
     char buf[96];

     fill(0, 0, LW, LH, C_BG);
     fill(0, 0, LW, 80, C_BAR);
     text(40, 28, 3, C_FG, "PRIMEBOX");
     text(40 + 9 * 24, 28, 3, C_DIM, "boot menu");

     for (int r = 0; r < visible_rows() && scroll + r < nitems; r++) {
          int i = scroll + r, y = TOP + r * ROW_H;
          if (items[i].heading) {
               text(40, y + 24, 2, C_HEAD, items[i].name);
               continue;
          }
          if (i == sel)
               fill(24, y + 4, LW - 48, ROW_H - 8, C_SEL);
          text(64, y + 16, FONT_S, C_FG, items[i].name);
          if (!items[i].cmd[0])
               text(LW - 64 - 16 * 11, y + 20, 2, C_DIM, "(Engine OS)");
     }

     fill(0, LH - 60, LW, 60, C_BAR);
     if (countdown > 0)
          snprintf(buf, sizeof(buf), "Starting %s in %d s - touch or turn the knob to choose",
                   items[sel].name, countdown);
     else
          snprintf(buf, sizeof(buf), "Tap an entry, or turn and push the browse knob");
     text(40, LH - 40, 2, C_DIM, buf);
     present();
}

/* ------------------------------------------------------------------ input */

static int seq_open(void)
{
     struct snd_seq_client_info ci;
     struct snd_seq_port_info pi;
     struct snd_seq_port_subscribe sub;
     int fd = open(SEQ_PATH, O_RDWR | O_NONBLOCK), me;

     if (fd < 0 || ioctl(fd, SNDRV_SEQ_IOCTL_CLIENT_ID, &me) < 0)
          goto fail;
     memset(&ci, 0, sizeof(ci));
     ci.client = me;
     ci.type = USER_CLIENT;
     snprintf(ci.name, sizeof(ci.name), "primebox-launcher");
     if (ioctl(fd, SNDRV_SEQ_IOCTL_SET_CLIENT_INFO, &ci) < 0)
          goto fail;
     memset(&pi, 0, sizeof(pi));
     pi.addr.client = me;
     snprintf(pi.name, sizeof(pi.name), "in");
     pi.capability = SNDRV_SEQ_PORT_CAP_WRITE | SNDRV_SEQ_PORT_CAP_SUBS_WRITE;
     pi.type = SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC | SNDRV_SEQ_PORT_TYPE_APPLICATION;
     if (ioctl(fd, SNDRV_SEQ_IOCTL_CREATE_PORT, &pi) < 0)
          goto fail;
     memset(&sub, 0, sizeof(sub));
     sub.sender.client = SURFACE_CLIENT;
     sub.dest.client = me;
     sub.dest.port = pi.addr.port;
     sub.queue = SNDRV_SEQ_QUEUE_DIRECT;
     if (ioctl(fd, SNDRV_SEQ_IOCTL_SUBSCRIBE_PORT, &sub) < 0)
          goto fail;
     return fd;
fail:
     if (fd >= 0)
          close(fd);
     return -1;
}

enum { EV_NONE, EV_UP, EV_DOWN, EV_PUSH, EV_TAP };

/* browse knob: relative CC5 on ch15, as knobshim decodes it:
 * 1..63 = +n steps (move down), 65..127 = -n steps (move up), 0 = none.
 * The surface also sends a turn at boot; GRACE_MS ignores that. Push = note 6. */
static int seq_read(int fd)
{
     struct snd_seq_event ev;
     while (read(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
          if (ev.type == SNDRV_SEQ_EVENT_CONTROLLER && ev.data.control.channel == 15 &&
              ev.data.control.param == 5) {
               int v = ev.data.control.value;
               fprintf(stderr, "launcher: cc5=%d\n", v);
               if (v == 0 || v == 64)      /* no movement */
                    continue;
               return v < 64 ? EV_DOWN : EV_UP;
          }
          if (ev.type == SNDRV_SEQ_EVENT_NOTEON && ev.data.note.channel == 15 &&
              ev.data.note.note == 6 && ev.data.note.velocity)
               return EV_PUSH;
     }
     return EV_NONE;
}

/* touch: map raw panel coords to logical landscape, report on finger-up */
static int touch_read(int fd, int *lx, int *ly)
{
     static int rx, ry, down;
     struct input_event ev;
     int ret = EV_NONE;
     while (read(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
          if (ev.type == EV_ABS && (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X))
               rx = ev.value;
          else if (ev.type == EV_ABS && (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y))
               ry = ev.value;
          else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
               if (down && !ev.value) {
                    int px = rx * fb_w / TOUCH_RAW, py = ry * fb_h / TOUCH_RAW;
                    *lx = LW - 1 - py;
                    *ly = px;
                    ret = EV_TAP;
               }
               down = ev.value;
          }
     }
     return ret;
}

/* ------------------------------------------------------------------- main */

static long now_ms(void)
{
     struct timespec t;
     clock_gettime(CLOCK_MONOTONIC, &t);
     return t.tv_sec * 1000L + t.tv_nsec / 1000000;
}

static int fb_open(void)
{
     struct fb_var_screeninfo v;
     struct fb_fix_screeninfo f;
     int fd = open(FB_PATH, O_RDWR);
     void *m;
     if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &v) || ioctl(fd, FBIOGET_FSCREENINFO, &f))
          return -1;
     if (v.bits_per_pixel != 32 || v.xres != LH || v.yres != LW) {
          fprintf(stderr, "launcher: unexpected fb %ux%u@%u\n", v.xres, v.yres, v.bits_per_pixel);
          return -1;
     }
     fb_w = v.xres;
     fb_h = v.yres;
     fb_pitch = f.line_length;
     fb_yoff = v.yoffset;
     m = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
     if (m == MAP_FAILED)
          return -1;
     fb = (uint32_t *)((char *)m + (size_t)fb_yoff * fb_pitch);
     return fd;
}

int main(int argc, char **argv)
{
     const char *conf = argc > 1 ? argv[1] : CONF_PATH;
     int sel = -1, scroll = 0, tfd, sfd, deadline_on;
     long deadline, started;

     if (load_conf(conf) || !nitems) {
          fprintf(stderr, "launcher: no entries in %s\n", conf);
          return 1;
     }
     for (int i = 0; i < nitems; i++)
          if (!items[i].heading && (sel < 0 || !strcmp(items[i].name, default_name))) {
               sel = i;
               if (!strcmp(items[i].name, default_name))
                    break;
          }
     if (sel < 0 || fb_open() < 0 || !(back = malloc(LW * LH * 4))) {
          fprintf(stderr, "launcher: cannot start (%s)\n", strerror(errno));
          return 1;
     }

     tfd = open(TOUCH_PATH, O_RDONLY | O_NONBLOCK);
     sfd = seq_open();
     started = now_ms();
     deadline_on = timeout_s > 0;
     deadline = started + timeout_s * 1000L;

     for (;;) {
          struct pollfd p[2] = { { tfd, POLLIN, 0 }, { sfd, POLLIN, 0 } };
          int ev = EV_NONE, lx = 0, ly = 0, left;

          if (sel < scroll)
               scroll = sel;
          if (sel >= scroll + visible_rows())
               scroll = sel - visible_rows() + 1;
          if (scroll > 0 && items[scroll - 1].heading && sel - scroll + 1 < visible_rows())
               scroll--;

          left = deadline_on ? (int)((deadline - now_ms() + 999) / 1000) : 0;
          if (deadline_on && left <= 0)
               break;
          draw(sel, scroll, left);

          if (poll(p, 2, deadline_on ? 250 : -1) <= 0)
               continue;
          if (p[0].revents & POLLIN)
               ev = touch_read(tfd, &lx, &ly);
          if (ev == EV_NONE && (p[1].revents & POLLIN))
               ev = seq_read(sfd);
          if (ev == EV_NONE)
               continue;
          if (now_ms() - started < GRACE_MS) {
               fprintf(stderr, "launcher: ignoring initial input (event %d)\n", ev);
               continue;
          }
          fprintf(stderr, "launcher: input event %d\n", ev);

          deadline_on = 0;
          if (ev == EV_UP)
               sel = next_selectable(sel, -1);
          else if (ev == EV_DOWN)
               sel = next_selectable(sel, 1);
          else if (ev == EV_PUSH)
               break;
          else if (ev == EV_TAP && ly >= TOP) {
               int i = scroll + (ly - TOP) / ROW_H;
               if (i < nitems && (ly - TOP) / ROW_H < visible_rows() && !items[i].heading) {
                    sel = i;
                    draw(sel, scroll, 0);
                    break;
               }
          }
     }

     fill(0, 0, LW, LH, C_BG);
     text(40, LH / 2 - 12, 3, C_FG, "Starting ");
     text(40 + 9 * 24, LH / 2 - 12, 3, C_HEAD, items[sel].name);
     present();

     printf("%s\n", items[sel].cmd);
     fprintf(stderr, "launcher: chose '%s'\n", items[sel].name);
     return 0;
}
