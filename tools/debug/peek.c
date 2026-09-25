/* peek.c — read words from another process's memory (/proc/PID/mem).
 * Static ARM helper for inspecting rbp state without stopping it.
 *   peek PID ADDR [COUNT]      hex ADDR, COUNT 32-bit words (default 1)
 */
#define _FILE_OFFSET_BITS 64
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    char path[64];
    uint32_t w;
    if (argc < 3) { fprintf(stderr, "usage: peek PID ADDR [COUNT]\n"); return 2; }
    unsigned long a = strtoul(argv[2], NULL, 16);
    int n = argc > 3 ? atoi(argv[3]) : 1;
    snprintf(path, sizeof(path), "/proc/%s/mem", argv[1]);
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror(path); return 1; }
    for (int i = 0; i < n; i++, a += 4) {
        if (pread(fd, &w, 4, (off_t)a) != 4) { perror("pread"); return 1; }
        printf("%08lx: %08x (%u)\n", a, w, w);
    }
    return 0;
}
