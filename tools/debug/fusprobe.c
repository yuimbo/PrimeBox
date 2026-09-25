#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <stdarg.h>

static void logf_(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    FILE *f = fopen("/tmp/fus.log", "a");
    if (f) { vfprintf(f, fmt, ap); fclose(f); }
    va_end(ap);
}

void *fusion_reactor_new(void *a, void *b, void *c) {
    static void *(*real)(void*,void*,void*);
    if (!real) real = dlsym(RTLD_NEXT, "fusion_reactor_new");
    void *r = real(a,b,c);
    logf_("reactor_new -> %p (caller %p)\n", r, __builtin_return_address(0));
    return r;
}

int fusion_reactor_attach(void *reactor, void *func, void *ctx, void *reaction) {
    static int (*real)(void*,void*,void*,void*);
    if (!real) real = dlsym(RTLD_NEXT, "fusion_reactor_attach");
    logf_("attach reactor=%p caller=%p\n", reactor, __builtin_return_address(0));
    return real(reactor, func, ctx, reaction);
}

void *fusion_shm_pool_create(void *a, void *b, void *c, void *d) {
    static void *(*real)(void*,void*,void*,void*);
    if (!real) real = dlsym(RTLD_NEXT, "fusion_shm_pool_create");
    void *r = real(a,b,c,d);
    logf_("shm_pool_create -> %p (caller %p)\n", r, __builtin_return_address(0));
    return r;
}

void *fusion_object_create(void *a, void *b) {
    static void *(*real)(void*,void*);
    if (!real) real = dlsym(RTLD_NEXT, "fusion_object_create");
    void *r = real(a,b);
    logf_("object_create -> %p (caller %p)\n", r, __builtin_return_address(0));
    return r;
}
