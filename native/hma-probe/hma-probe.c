#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

typedef uint32_t (*hma_alloc_fn)(const char *name, uint32_t size, uint32_t align, void **vaddr);
typedef int (*hma_free_fn)(uint32_t paddr);

int main(void) {
    FILE *log = fopen("/tmp/hma_probe.log", "w");
    if (!log) return 2;
    setvbuf(log, NULL, _IONBF, 0);
    fprintf(log, "uid=%d euid=%d gid=%d\n", getuid(), geteuid(), getgid());

    void *lib = dlopen("/usr/lib/libhma.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        fprintf(log, "dlopen failed: %s\n", dlerror());
        fclose(log);
        return 3;
    }

    hma_alloc_fn hma_alloc = (hma_alloc_fn)dlsym(lib, "libhma_alloc");
    hma_free_fn hma_free = (hma_free_fn)dlsym(lib, "libhma_free");
    if (!hma_alloc || !hma_free) {
        fprintf(log, "missing symbol alloc=%p free=%p err=%s\n",
                (void*)hma_alloc, (void*)hma_free, dlerror());
        dlclose(lib);
        fclose(log);
        return 4;
    }

    const char *names[] = {"sdec", "sdec_cma", "pvr", "te_shared"};
    for (unsigned i = 0; i < sizeof(names)/sizeof(names[0]); ++i) {
        void *vaddr = NULL;
        uint32_t paddr = hma_alloc(names[i], 64 * 1024, 0x1000, &vaddr);
        fprintf(log, "pool=%s paddr=0x%08x vaddr=%p\n", names[i], paddr, vaddr);
        if (paddr != 0) {
            int rc = hma_free(paddr);
            fprintf(log, "pool=%s free_rc=%d\n", names[i], rc);
        }
    }

    dlclose(lib);
    fprintf(log, "done\n");
    fclose(log);
    return 0;
}
