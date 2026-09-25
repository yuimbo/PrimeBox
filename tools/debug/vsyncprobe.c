/* vsyncprobe.c — time FBIOPAN_DISPLAY and FBIO_WAITFORVSYNC on /dev/fb0.
 * Static ARM helper; run with Engine OS stopped. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <time.h>
#include <unistd.h>

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(void)
{
    struct fb_var_screeninfo v;
    int fd = open("/dev/fb0", O_RDWR), z = 0, i, n = 60;
    double t;
    if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &v)) { perror("fb0"); return 1; }
    printf("fb %ux%u virt %ux%u bpp %u\n", v.xres, v.yres, v.xres_virtual, v.yres_virtual, v.bits_per_pixel);

    for (int mode = 0; mode < 2; mode++) {
        t = now();
        for (i = 0; i < n; i++) {
            v.yoffset = (i % 3) * v.yres;
            v.activate = mode ? FB_ACTIVATE_VBL : FB_ACTIVATE_NOW;
            if (ioctl(fd, FBIOPAN_DISPLAY, &v)) { perror("PAN"); break; }
        }
        printf("PAN %s: %.2f ms/call\n", mode ? "VBL" : "NOW", 1000 * (now() - t) / n);
    }
    t = now();
    for (i = 0; i < n && !ioctl(fd, FBIO_WAITFORVSYNC, &z); i++)
        ;
    printf("WAITFORVSYNC: %d ok, %.3f ms/call\n", i, i ? 1000 * (now() - t) / i : 0);
    v.yoffset = 0; ioctl(fd, FBIOPAN_DISPLAY, &v);
    return 0;
}
