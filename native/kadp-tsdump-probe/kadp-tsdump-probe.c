#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

typedef void *(*sdec_open_fn)(void);
typedef int (*sdec_close_fn)(void);
typedef int (*sdec_get_wptr_fn)(uint32_t *);
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
    sdec_get_wptr_fn KADP_SDEC_GetTsDumpWptr =
        (sdec_get_wptr_fn)dlsym(kadp, "KADP_SDEC_GetTsDumpWptr");
    void *debug_sym = dlsym(hal, "HAL_SDEC_DebugMenu");
    if (!KADP_SDEC_Open || !KADP_SDEC_Close || !KADP_SDEC_GetTsDumpWptr || !debug_sym) {
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
    fprintf(log, "TS Dump Start returned; polling Wptr for 2s\n");
    uint32_t first_wptr = 0, last_wptr = 0;
    int moving = 0;
    for (int i = 0; i < 20; ++i) {
        uint32_t wptr = 0;
        int wrc = KADP_SDEC_GetTsDumpWptr(&wptr);
        if (i == 0) first_wptr = wptr;
        if (i > 0 && wptr != last_wptr) moving = 1;
        last_wptr = wptr;
        fprintf(log, "wptr[%02d] rc=%d value=0x%08x\n", i, wrc, wptr);
        usleep(100000);
    }
    fprintf(log, "wptr_summary first=0x%08x last=0x%08x moving=%d\n",
            first_wptr, last_wptr, moving);

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
