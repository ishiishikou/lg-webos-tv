#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef int (*sdec_init_fn)(void);
typedef void *(*sdec_open_fn)(void);
typedef int (*sdec_close_fn)(void);
typedef int (*sdec_shutdown_fn)(void);
typedef int (*sdec_start_dump_fn)(uint32_t cfg[8]);
typedef int (*sdec_get_wptr_fn)(uint32_t *wptr);
typedef int (*sdec_stop_dump_fn)(void);
typedef uint32_t (*hma_alloc_fn)(const char *name, uint32_t size, uint32_t align, void **vaddr);
typedef int (*hma_free_fn)(uint32_t paddr);

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int sig) { (void)sig; g_stop = 1; }

static uint64_t mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int sync_score(const unsigned char *buf, size_t n, int period, int *best_off) {
    int best = -1, off_best = 0;
    for (int off = 0; off < period && (size_t)off < n; ++off) {
        int score = 0, total = 0;
        for (size_t p = (size_t)off; p < n && total < 256; p += (size_t)period, ++total)
            if (buf[p] == 0x47) ++score;
        if (score > best) { best = score; off_best = off; }
    }
    *best_off = off_best;
    return best;
}

int main(int argc, char **argv) {
    const uint32_t HMA_SIZE = 0x753000u;
    const size_t MAX_OUT = 4u * 1024u * 1024u;
    const uint64_t RUN_MS = 500u;
    uint32_t input_port = 0x10u;
    if (argc >= 2) {
        char *endp = NULL;
        unsigned long v = strtoul(argv[1], &endp, 0);
        if (!endp || *endp != '\0' || !(v == 0x10u || v == 0x12u || v == 0x15u)) {
            fprintf(stderr, "usage: %s [0x10|0x12|0x15]\n", argv[0]);
            return 2;
        }
        input_port = (uint32_t)v;
    }
    char out_path[64];
    snprintf(out_path, sizeof(out_path), "/tmp/sdec_probe_%02x.ts", input_port);
    const char *OUT = out_path;

    FILE *log = fopen("/tmp/sdec_tsdump_probe.log", "w");
    if (!log) return 2;
    setvbuf(log, NULL, _IONBF, 0);
    fprintf(log, "uid=%d euid=%d gid=%d\n", getuid(), geteuid(), getgid());

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    void *kadp = dlopen("/usr/lib/libkadaptor.so.2", RTLD_NOW | RTLD_LOCAL);
    void *hma = dlopen("/usr/lib/libhma.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!kadp || !hma) {
        fprintf(log, "dlopen failed kadp=%p hma=%p err=%s\n", kadp, hma, dlerror());
        if (kadp) dlclose(kadp);
        if (hma) dlclose(hma);
        fclose(log);
        return 3;
    }

#define LOAD(lib, name, type) type name = (type)dlsym((lib), #name)
    LOAD(kadp, KADP_SDEC_Initialize, sdec_init_fn);
    LOAD(kadp, KADP_SDEC_Open, sdec_open_fn);
    LOAD(kadp, KADP_SDEC_Close, sdec_close_fn);
    LOAD(kadp, KADP_SDEC_Shutdown, sdec_shutdown_fn);
    LOAD(kadp, KADP_SDEC_StartInputTsDump, sdec_start_dump_fn);
    LOAD(kadp, KADP_SDEC_GetTsDumpWptr, sdec_get_wptr_fn);
    LOAD(kadp, KADP_SDEC_StopInputTsDump, sdec_stop_dump_fn);
    LOAD(hma, libhma_alloc, hma_alloc_fn);
    LOAD(hma, libhma_free, hma_free_fn);

    if (!KADP_SDEC_Initialize || !KADP_SDEC_Open || !KADP_SDEC_Close ||
        !KADP_SDEC_Shutdown || !KADP_SDEC_StartInputTsDump ||
        !KADP_SDEC_GetTsDumpWptr || !KADP_SDEC_StopInputTsDump ||
        !libhma_alloc || !libhma_free) {
        fprintf(log, "missing required symbol\n");
        dlclose(hma); dlclose(kadp); fclose(log); return 4;
    }

    int init_rc = KADP_SDEC_Initialize();
    fprintf(log, "initialize rc=%d\n", init_rc);
    void *ctx = KADP_SDEC_Open();
    fprintf(log, "open ctx=%p\n", ctx);
    if (!ctx) {
        KADP_SDEC_Shutdown();
        dlclose(hma); dlclose(kadp); fclose(log); return 5;
    }

    void *vaddr = NULL;
    uint32_t paddr = libhma_alloc("sdec", HMA_SIZE, 0x1000u, &vaddr);
    fprintf(log, "hma pool=sdec size=0x%x paddr=0x%08x vaddr=%p\n",
            HMA_SIZE, paddr, vaddr);
    if (!paddr || !vaddr) {
        KADP_SDEC_Close(); KADP_SDEC_Shutdown();
        dlclose(hma); dlclose(kadp); fclose(log); return 6;
    }

    uint32_t cfg[8] = {
        0u,              /* Dump Mode: ALL */
        0u,              /* Clock Source: AUTO */
        1u,              /* Clock Resolution: 27 MHz */
        input_port,      /* Input Port: evidence-backed TPI candidate */
        4u,              /* LG debug default Main channel selector */
        paddr,
        paddr + HMA_SIZE,
        0x4b00u          /* LG built-in fixed value */
    };
    fprintf(log, "input_port=0x%02x out=%s\n", input_port, OUT);
    fprintf(log, "cfg=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
            cfg[0],cfg[1],cfg[2],cfg[3],cfg[4],cfg[5],cfg[6],cfg[7]);

    int started = 0;
    FILE *out = NULL;
    size_t written = 0;
    int rc = KADP_SDEC_StartInputTsDump(cfg);
    fprintf(log, "start rc=%d\n", rc);
    if (rc == 0) {
        started = 1;
        out = fopen(OUT, "wb");
        if (!out) fprintf(log, "fopen output failed errno=%d %s\n", errno, strerror(errno));
    }

    if (started && out) {
        uint32_t rptr = paddr;
        uint32_t end = paddr + HMA_SIZE;
        uint64_t t0 = mono_ms();
        unsigned polls = 0, movements = 0;
        while (!g_stop && mono_ms() - t0 < RUN_MS && written < MAX_OUT) {
            uint32_t wptr = 0;
            int grc = KADP_SDEC_GetTsDumpWptr(&wptr);
            ++polls;
            if (grc != 0) {
                fprintf(log, "get_wptr rc=%d\n", grc);
                break;
            }
            if (wptr < paddr || wptr > end) {
                fprintf(log, "wptr out-of-range=0x%08x\n", wptr);
                break;
            }
            if (wptr != rptr) {
                size_t len = (wptr > rptr) ? (size_t)(wptr - rptr) : (size_t)(end - rptr);
                if (len > MAX_OUT - written) len = MAX_OUT - written;
                const unsigned char *src = (const unsigned char *)vaddr + (size_t)(rptr - paddr);
                size_t n = fwrite(src, 1, len, out);
                written += n;
                ++movements;
                if (n != len) {
                    fprintf(log, "fwrite short n=%zu len=%zu errno=%d\n", n, len, errno);
                    break;
                }
                rptr = (wptr > rptr) ? wptr : paddr;
                if (rptr >= end) rptr = paddr;
            } else {
                usleep(5000);
            }
        }
        fprintf(log, "capture polls=%u movements=%u written=%zu elapsed_ms=%llu\n",
                polls, movements, written, (unsigned long long)(mono_ms()-t0));
    }

    if (out) {
        fflush(out);
        fclose(out);
        out = NULL;
    }
    if (started) {
        int src = KADP_SDEC_StopInputTsDump();
        fprintf(log, "stop rc=%d\n", src);
    }

    int frc = libhma_free(paddr);
    fprintf(log, "hma free rc=%d\n", frc);
    int crc = KADP_SDEC_Close();
    int shrc = KADP_SDEC_Shutdown();
    fprintf(log, "close rc=%d shutdown rc=%d\n", crc, shrc);

    FILE *in = fopen(OUT, "rb");
    if (in) {
        unsigned char buf[65536];
        size_t n = fread(buf, 1, sizeof(buf), in);
        fclose(in);
        fprintf(log, "verify bytes=%zu first=", n);
        for (size_t i=0; i<n && i<32; ++i) fprintf(log, "%02x", buf[i]);
        fprintf(log, "\n");
        int o188=0,o192=0;
        int s188=sync_score(buf,n,188,&o188);
        int s192=sync_score(buf,n,192,&o192);
        fprintf(log, "sync188 score=%d off=%d sync192 score=%d off=%d\n",
                s188,o188,s192,o192);
    } else {
        fprintf(log, "verify no output file errno=%d\n", errno);
    }

    dlclose(hma); dlclose(kadp);
    fprintf(log, "done started=%d written=%zu\n", started, written);
    fclose(log);
    return (started && written > 0) ? 0 : 7;
}
