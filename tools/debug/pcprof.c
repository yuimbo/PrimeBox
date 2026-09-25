/* pcprof.c — LD_PRELOAD sampling profiler for a named thread.
 *
 * After PCPROF_DELAY seconds (default 15), a helper thread sends SIGPROF to
 * every thread whose comm matches PCPROF_THREAD (default "gui_task") every
 * 2 ms for PCPROF_SAMPLES samples, and records pc/lr of the interrupted
 * context. Results go to /tmp/pcprof.txt, the address space to
 * /tmp/pcprof.maps.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

#define MAXS 4000

#define DEPTH 6
static volatile unsigned int pcs[MAXS], lrs[MAXS], fps_[MAXS][DEPTH];
static volatile int nsamp;

static void on_prof(int sig, siginfo_t *si, void *uc)
{
    ucontext_t *u = uc;
    int i = __sync_fetch_and_add(&nsamp, 1);
    if (i < MAXS) {
        pcs[i] = u->uc_mcontext.arm_pc;
        lrs[i] = u->uc_mcontext.arm_lr;
        /* best-effort APCS frame walk: [fp] = saved lr, [fp-4] = saved fp */
        unsigned int fp = u->uc_mcontext.arm_fp, sp = u->uc_mcontext.arm_sp;
        for (int d = 0; d < DEPTH; d++) {
            if (fp < sp || fp > sp + 0x100000 || (fp & 3)) {
                fps_[i][d] = 0;
                continue;
            }
            fps_[i][d] = ((unsigned int *)fp)[0];
            sp = fp;
            fp = ((unsigned int *)fp)[-1];
        }
    }
}

static void copy_file(const char *src, const char *dst)
{
    char buf[4096];
    int n, in = open(src, O_RDONLY), out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    while (in >= 0 && out >= 0 && (n = read(in, buf, sizeof(buf))) > 0)
        (void)write(out, buf, n);
    if (in >= 0) close(in);
    if (out >= 0) close(out);
}

static int find_tids(const char *want, int *tids, int max)
{
    char path[64], comm[32];
    int n = 0;
    DIR *d = opendir("/proc/self/task");
    struct dirent *e;
    while (d && (e = readdir(d)) && n < max) {
        if (e->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "/proc/self/task/%s/comm", e->d_name);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;
        if (fgets(comm, sizeof(comm), f)) {
            comm[strcspn(comm, "\n")] = 0;
            if (!strcmp(comm, want))
                tids[n++] = atoi(e->d_name);
        }
        fclose(f);
    }
    if (d) closedir(d);
    return n;
}

static void *sampler(void *arg)
{
    const char *want = getenv("PCPROF_THREAD") ? getenv("PCPROF_THREAD") : "gui_task";
    int delay = getenv("PCPROF_DELAY") ? atoi(getenv("PCPROF_DELAY")) : 15;
    int total = getenv("PCPROF_SAMPLES") ? atoi(getenv("PCPROF_SAMPLES")) : 2000;
    int tids[16], nt, i, k;
    pid_t pid = getpid();

    if (total > MAXS) total = MAXS;
    sleep(delay);
    nt = find_tids(want, tids, 16);
    for (i = 0; i < total; i++) {
        for (k = 0; k < nt; k++)
            syscall(SYS_tgkill, pid, tids[k], SIGPROF);
        usleep(2000);
    }
    usleep(20000);
    copy_file("/proc/self/maps", "/tmp/pcprof.maps");
    FILE *f = fopen("/tmp/pcprof.txt", "w");
    if (f) {
        int m = nsamp < MAXS ? nsamp : MAXS;
        fprintf(f, "# thread=%s tids=%d samples=%d\n", want, nt, m);
        for (i = 0; i < m; i++)
        {
            fprintf(f, "%08x %08x", pcs[i], lrs[i]);
            for (k = 0; k < DEPTH; k++)
                fprintf(f, " %08x", fps_[i][k]);
            fprintf(f, "\n");
        }
        fclose(f);
    }
    return NULL;
}

__attribute__((constructor)) static void init(void)
{
    struct sigaction sa;
    pthread_t t;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = on_prof;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGPROF, &sa, NULL);
    pthread_create(&t, NULL, sampler, NULL);
    pthread_detach(t);
}
