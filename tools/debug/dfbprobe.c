/* dfbprobe.c — LD_PRELOAD probe: un-silence DirectFB and log core-part init.
 *
 * rbp sets DirectFB "quiet", so DirectFBCreate() failures are invisible.
 * This probe clears direct_config->quiet before DirectFBCreate() and after
 * every DirectFBSetOption(), and logs each dfb_core_part_initialize()/join()
 * with its result to /tmp/dfbprobe.log.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

typedef struct { const char *name; } CorePartHead;
struct DirectConfigHead { unsigned int quiet; };
extern struct DirectConfigHead *direct_config;

static void plog(const char *fmt, ...)
{
    va_list ap;
    FILE *f = fopen("/tmp/dfbprobe.log", "a");
    if (!f)
        return;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fclose(f);
}

static void unquiet(void)
{
    if (direct_config)
        direct_config->quiet = 0;
}

int DirectFBSetOption(const char *name, const char *value)
{
    static int (*real)(const char *, const char *);
    if (!real)
        real = dlsym(RTLD_NEXT, "DirectFBSetOption");
    int r = real(name, value);
    plog("SetOption %s=%s -> %d\n", name, value ? value : "(null)", r);
    unquiet();
    return r;
}

int DirectFBCreate(void **iface)
{
    static int (*real)(void **);
    if (!real)
        real = dlsym(RTLD_NEXT, "DirectFBCreate");
    unquiet();
    int r = real(iface);
    plog("DirectFBCreate -> %d iface=%p\n", r, iface ? *iface : 0);
    return r;
}

static const char *part_name(void *part)
{
    CorePartHead *p = part;
    return (p && p->name) ? p->name : "?";
}

int dfb_core_part_initialize(void *core, void *part)
{
    static int (*real)(void *, void *);
    if (!real)
        real = dlsym(RTLD_NEXT, "dfb_core_part_initialize");
    int r = real(core, part);
    plog("part_initialize %s -> %d\n", part_name(part), r);
    return r;
}

int dfb_core_part_join(void *core, void *part)
{
    static int (*real)(void *, void *);
    if (!real)
        real = dlsym(RTLD_NEXT, "dfb_core_part_join");
    int r = real(core, part);
    plog("part_join %s -> %d\n", part_name(part), r);
    return r;
}

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

/* per-flip timing: time spent inside flip vs. between flips */
static double flip_in, flip_gap, flip_last_end;
static unsigned int flip_n;

int dfb_layer_region_flip_update(void *region, const void *update, unsigned int flags)
{
    static int (*real)(void *, const void *, unsigned int);
    static unsigned int calls;
    if (!real)
        real = dlsym(RTLD_NEXT, "dfb_layer_region_flip_update");
    double t0 = now_s();
    int r = real(region, update, flags);
    double t1 = now_s();
    calls++;
    if (flip_last_end > 0) {
        flip_in += t1 - t0;
        flip_gap += t0 - flip_last_end;
        flip_n++;
    }
    flip_last_end = t1;
    if ((calls % 120) == 0) {
        static struct timespec t0;
        static unsigned int c0;
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        if (t0.tv_sec) {
            double dt = (t.tv_sec - t0.tv_sec) + (t.tv_nsec - t0.tv_nsec) / 1e9;
            plog("fps %.1f  in_flip %.1f ms  between %.1f ms\n", (calls - c0) / dt,
                 flip_n ? 1000 * flip_in / flip_n : 0, flip_n ? 1000 * flip_gap / flip_n : 0);
            flip_in = flip_gap = 0;
            flip_n = 0;
        }
        t0 = t;
        c0 = calls;
    }
    if (calls <= 5 || r)
    {
        const int *u = update;
        plog("flip_update #%u update=%d,%d-%d,%d flags=0x%x -> %d\n", calls,
             u ? u[0] : -1, u ? u[1] : -1, u ? u[2] : -1, u ? u[3] : -1, flags, r);
    }
    return r;
}

void dfb_back_to_front_copy_rotation(void *surface, const void *region, int rotation)
{
    static void (*real)(void *, const void *, int);
    static unsigned int calls;
    if (!real)
        real = dlsym(RTLD_NEXT, "dfb_back_to_front_copy_rotation");
    real(surface, region, rotation);
    if (++calls <= 3 || (calls % 300) == 0)
        plog("back_to_front_copy #%u rotation=%d\n", calls, rotation);
}

int dfb_surface_flip(void *surface, int swap)
{
    static int (*real)(void *, int);
    static unsigned int calls;
    if (!real)
        real = dlsym(RTLD_NEXT, "dfb_surface_flip");
    int r = real(surface, swap);
    if (++calls <= 3 || (calls % 300) == 0)
        plog("surface_flip #%u swap=%d -> %d\n", calls, swap, r);
    return r;
}
