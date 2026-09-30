#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

typedef void *(*sdec_open_fn)(void);
typedef int (*sdec_close_fn)(void);
typedef void (*void_fn)(void);

int main(void) {
    FILE *log = fopen("/tmp/kadp_tsdump_probe.log", "w");
    if (!log) return 2;
    setvbuf(log, NULL, _IONBF, 0);
    fprintf(log, "uid=%d euid=%d gid=%d\n", getuid(), geteuid(), getgid());

    void *kadp = dlopen("/usr/lib/libkadaptor.so.2", RTLD_NOW | RTLD_LOCAL);
    void *hal = dlopen("/usr/lib/libhal_lg115x.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!kadp || !hal) {
        fprintf(log, "dlopen failed kadp=%p hal=%p err=%s\n", kadp, hal, dlerror());
        return 3;
    }

    sdec_open_fn KADP_SDEC_Open = (sdec_open_fn)dlsym(kadp, "KADP_SDEC_Open");
    sdec_close_fn KADP_SDEC_Close = (sdec_close_fn)dlsym(kadp, "KADP_SDEC_Close");
    void *debug_sym = dlsym(hal, "HAL_SDEC_DebugMenu");
    if (!KADP_SDEC_Open || !KADP_SDEC_Close || !debug_sym) {
        fprintf(log, "missing symbols err=%s\n", dlerror());
        return 4;
    }

    uintptr_t debug_addr = ((uintptr_t)debug_sym) & ~(uintptr_t)1;
    uintptr_t base = debug_addr - 0x39848u;
    void_fn ts_start = (void_fn)(base + 0x362d4u + 1u);
    void_fn ts_stop  = (void_fn)(base + 0x35d8cu + 1u);

    fprintf(log, "HAL base=%p debug=%p start=%p stop=%p\n",
            (void *)base, debug_sym, (void *)ts_start, (void *)ts_stop);

    void *ctx = KADP_SDEC_Open();
    fprintf(log, "KADP_SDEC_Open ctx=%p\n", ctx);
    if (!ctx) return 5;

    fprintf(log, "calling TS Dump Start (interactive stdin)\n");
    ts_start();
    fprintf(log, "TS Dump Start returned; sleeping 2s\n");
    sleep(2);

    fprintf(log, "calling TS Dump Stop\n");
    ts_stop();
    fprintf(log, "TS Dump Stop returned\n");

    int rc = KADP_SDEC_Close();
    fprintf(log, "KADP_SDEC_Close rc=%d\n", rc);

    dlclose(hal);
    dlclose(kadp);
    fprintf(log, "done\n");
    fclose(log);
    return 0;
}
