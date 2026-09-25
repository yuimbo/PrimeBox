/*
 * compat.c - LD_PRELOAD shim for the RX3 glibc 2.13 runtime.
 *
 * DirectFB 1.4.16 built with a modern cross toolchain references two libc
 * symbols the RX3 glibc 2.13 does not provide:
 *
 *   fcntl@GLIBC_2.28        -> syscall(SYS_fcntl, ...)
 *   __fdelt_chk@GLIBC_2.15   -> inline FD_SET bounds helper
 *
 * Built with compat.map, which exports them under exactly those version
 * names so libdirect's versioned references resolve on the Prime GO.
 * See docs/03-display.md.
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <sys/syscall.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>

int fcntl (int fd, int cmd, ...)
{
    va_list ap;
    void *arg;

    va_start (ap, cmd);
    arg = va_arg (ap, void *);
    va_end (ap);

    return (int) syscall (SYS_fcntl, fd, cmd, arg);
}

long int __fdelt_chk (long int d)
{
    if (d < 0 || d >= FD_SETSIZE)
        return 0;

    return d / (8 * (long int) sizeof (long));
}
