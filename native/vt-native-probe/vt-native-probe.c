#define _GNU_SOURCE
#include <dlfcn.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

typedef int (*vt_is_supported_fn)(unsigned int *);
typedef int (*vt_create_window_fn)(int);
typedef int (*vt_acquire_fn)(int, int32_t *);
typedef int32_t (*vt_create_context_fn)(int32_t, unsigned int);
typedef int (*vt_set_region_fn)(int32_t, int);
typedef int (*vt_register_fn)(int32_t, void (*)(int, void *, void *), void *);
typedef int (*vt_unregister_fn)(int32_t);
typedef int (*vt_delete_context_fn)(int32_t);
typedef int (*vt_release_fn)(int32_t);

static volatile sig_atomic_t g_events;
static volatile sig_atomic_t g_last_type = -1;

static void on_vt_event(int type, void *data, void *user_data) {
    (void)data;
    (void)user_data;
    ++g_events;
    g_last_type = type;
}

#define LOAD(name, type) \
    type name = (type)dlsym(lib, #name); \
    if (!(name)) { fprintf(log, "dlsym %s failed: %s\n", #name, dlerror()); goto out; }

int main(void) {
    const char *report = "/tmp/vt_native_probe.log";
    FILE *log = fopen(report, "w");
    if (!log) return 2;
    setvbuf(log, NULL, _IONBF, 0);\n    fprintf(log, "uid=%d euid=%d gid=%d\\n", getuid(), geteuid(), getgid());

    void *lib = dlopen("/usr/lib/libvt.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        fprintf(log, "dlopen failed: %s\n", dlerror());
        fclose(log);
        return 3;
    }

    int have_resource = 0;
    int have_context = 0;
    int have_handler = 0;
    int32_t resource_id = 0;
    int32_t context_id = -1;

    LOAD(VT_IsSystemSupported, vt_is_supported_fn);
    LOAD(VT_CreateVideoWindow, vt_create_window_fn);
    LOAD(VT_AcquireVideoWindowResource, vt_acquire_fn);
    LOAD(VT_CreateContext, vt_create_context_fn);
    LOAD(VT_SetTextureSourceRegion, vt_set_region_fn);
    LOAD(VT_RegisterEventHandler, vt_register_fn);
    LOAD(VT_UnRegisterEventHandler, vt_unregister_fn);
    LOAD(VT_DeleteContext, vt_delete_context_fn);
    LOAD(VT_ReleaseVideoWindowResource, vt_release_fn);

    unsigned int supported = 0;
    int rc = VT_IsSystemSupported(&supported);
    fprintf(log, "VT_IsSystemSupported rc=%d supported=%u\n", rc, supported);
    if (rc != 0 || !supported) goto cleanup;

    int window_id = VT_CreateVideoWindow(0);
    fprintf(log, "VT_CreateVideoWindow window_id=%d\n", window_id);
    if (window_id == -1) goto cleanup;

    rc = VT_AcquireVideoWindowResource(window_id, &resource_id);
    fprintf(log, "VT_AcquireVideoWindowResource rc=%d resource_id=%d\n", rc, resource_id);
    if (rc != 0) goto cleanup;
    have_resource = 1;

    context_id = VT_CreateContext(resource_id, 2);
    fprintf(log, "VT_CreateContext context_id=%d\n", context_id);
    if (context_id == -1) goto cleanup;
    have_context = 1;

    rc = VT_SetTextureSourceRegion(context_id, 1);
    fprintf(log, "VT_SetTextureSourceRegion rc=%d\n", rc);
    if (rc != 0) goto cleanup;

    rc = VT_RegisterEventHandler(context_id, on_vt_event, NULL);
    fprintf(log, "VT_RegisterEventHandler rc=%d\n", rc);
    if (rc != 0) goto cleanup;
    have_handler = 1;

    for (int i = 1; i <= 50; ++i) {
        usleep(100000);
        if ((i % 10) == 0)
            fprintf(log, "t=%ds events=%d last_type=%d\n", i / 10,
                    (int)g_events, (int)g_last_type);
    }

cleanup:
    if (have_handler) {
        rc = VT_UnRegisterEventHandler(context_id);
        fprintf(log, "VT_UnRegisterEventHandler rc=%d\n", rc);
    }
    if (have_context) {
        rc = VT_DeleteContext(context_id);
        fprintf(log, "VT_DeleteContext rc=%d\n", rc);
    }
    if (have_resource) {
        rc = VT_ReleaseVideoWindowResource(resource_id);
        fprintf(log, "VT_ReleaseVideoWindowResource rc=%d\n", rc);
    }

out:
    fprintf(log, "done events=%d last_type=%d\n", (int)g_events, (int)g_last_type);
    dlclose(lib);
    fclose(log);
    return 0;
}
