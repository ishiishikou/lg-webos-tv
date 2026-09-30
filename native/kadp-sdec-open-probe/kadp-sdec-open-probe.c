#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

typedef void *(*kadp_sdec_open_fn)(void);
typedef int (*kadp_sdec_open_channel_fn)(int);

int main(void) {
    FILE *log = fopen("/tmp/kadp_sdec_open_probe.log", "w");
    if (!log) return 2;
    setvbuf(log, NULL, _IONBF, 0);
    fprintf(log, "uid=%d euid=%d gid=%d\n", getuid(), geteuid(), getgid());

    void *lib = dlopen("/usr/lib/libkadaptor.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        fprintf(log, "dlopen failed: %s\n", dlerror());
        fclose(log);
        return 3;
    }

    kadp_sdec_open_fn KADP_SDEC_Open =
        (kadp_sdec_open_fn)dlsym(lib, "KADP_SDEC_Open");
    kadp_sdec_open_channel_fn KADP_SDEC_OpenChannel =
        (kadp_sdec_open_channel_fn)dlsym(lib, "KADP_SDEC_OpenChannel");

    fprintf(log, "symbols Open=%p OpenChannel=%p\n",
            (void*)KADP_SDEC_Open, (void*)KADP_SDEC_OpenChannel);
    if (!KADP_SDEC_Open || !KADP_SDEC_OpenChannel) {
        fprintf(log, "missing symbol: %s\n", dlerror());
        dlclose(lib);
        fclose(log);
        return 4;
    }

    void *ctx = KADP_SDEC_Open();
    fprintf(log, "KADP_SDEC_Open ctx=%p\n", ctx);
    if (!ctx) {
        dlclose(lib);
        fclose(log);
        return 5;
    }

    int rc = KADP_SDEC_OpenChannel(0);
    fprintf(log, "KADP_SDEC_OpenChannel(0) rc=%d\n", rc);

    /* Do not start filters, TS dump, mmap, or other state-changing operations.
       Process exit closes any descriptors opened by this short-lived probe. */
    dlclose(lib);
    fprintf(log, "done\n");
    fclose(log);
    return 0;
}
