#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>

typedef int (*init_fn)(void);
typedef void *(*open_fn)(void);
typedef int (*close_fn)(void);
typedef int (*shutdown_fn)(void);

int main(void) {
    FILE *log = fopen("/tmp/sdec_open_probe.log", "w");
    if (!log) return 2;
    setvbuf(log, NULL, _IONBF, 0);
    fprintf(log, "uid=%d euid=%d gid=%d\n", getuid(), geteuid(), getgid());

    void *lib = dlopen("/usr/lib/libkadaptor.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        fprintf(log, "dlopen failed: %s\n", dlerror());
        fclose(log);
        return 3;
    }

    init_fn init = (init_fn)dlsym(lib, "KADP_SDEC_Initialize");
    open_fn open_sdec = (open_fn)dlsym(lib, "KADP_SDEC_Open");
    close_fn close_sdec = (close_fn)dlsym(lib, "KADP_SDEC_Close");
    shutdown_fn shutdown_sdec = (shutdown_fn)dlsym(lib, "KADP_SDEC_Shutdown");
    fprintf(log, "symbols init=%p open=%p close=%p shutdown=%p\n",
            (void*)init, (void*)open_sdec, (void*)close_sdec, (void*)shutdown_sdec);
    if (!init || !open_sdec || !close_sdec || !shutdown_sdec) {
        dlclose(lib);
        fclose(log);
        return 4;
    }

    int init_rc = init();
    fprintf(log, "initialize rc=%d\n", init_rc);

    void *ctx = open_sdec();
    fprintf(log, "open ctx=%p\n", ctx);

    if (ctx) {
        int close_rc = close_sdec();
        fprintf(log, "close rc=%d\n", close_rc);
    }

    int shutdown_rc = shutdown_sdec();
    fprintf(log, "shutdown rc=%d\n", shutdown_rc);

    dlclose(lib);
    fprintf(log, "done\n");
    fclose(log);
    return ctx ? 0 : 5;
}
