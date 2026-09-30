#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef void *(*te_open_fn)(void);
typedef int (*te_close_fn)(void *);
typedef int (*te_get_tpi_status_fn)(uint32_t, void *);

static void dump_hex(FILE *f, const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if ((i % 16) == 0) fprintf(f, "%04zx:", i);
        fprintf(f, " %02x", p[i]);
        if ((i % 16) == 15 || i + 1 == n) fputc('\n', f);
    }
}

int main(void) {
    FILE *log = fopen("/tmp/kadp_tpi_status_probe.log", "w");
    if (!log) return 2;
    setvbuf(log, NULL, _IONBF, 0);
    fprintf(log, "uid=%d euid=%d gid=%d\n", getuid(), geteuid(), getgid());

    void *lib = dlopen("/usr/lib/libkadaptor.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        fprintf(log, "dlopen failed: %s\n", dlerror());
        fclose(log);
        return 3;
    }

    te_open_fn KADP_TE_Open = (te_open_fn)dlsym(lib, "KADP_TE_Open");
    te_close_fn KADP_TE_Close = (te_close_fn)dlsym(lib, "KADP_TE_Close");
    te_get_tpi_status_fn KADP_TE_GetTPIStatus =
        (te_get_tpi_status_fn)dlsym(lib, "KADP_TE_GetTPIStatus");

    if (!KADP_TE_Open || !KADP_TE_Close || !KADP_TE_GetTPIStatus) {
        fprintf(log, "missing symbol: %s\n", dlerror());
        dlclose(lib);
        fclose(log);
        return 4;
    }

    void *ctx = KADP_TE_Open();
    fprintf(log, "KADP_TE_Open ctx=%p\n", ctx);
    if (!ctx) {
        dlclose(lib);
        fclose(log);
        return 5;
    }

    static const uint32_t ports[] = {
        0x00, 0x10, 0x11, 0x12, 0x14, 0x15, 0x30, 0x31, 0x40
    };
    for (size_t i = 0; i < sizeof(ports) / sizeof(ports[0]); ++i) {
        uint8_t out[256];
        memset(out, 0, sizeof(out));
        int rc = KADP_TE_GetTPIStatus(ports[i], out);
        fprintf(log, "port=0x%02x rc=%d\n", ports[i], rc);
        dump_hex(log, out, 64);
    }

    int rc = KADP_TE_Close(ctx);
    fprintf(log, "KADP_TE_Close rc=%d\n", rc);
    dlclose(lib);
    fclose(log);
    return 0;
}
