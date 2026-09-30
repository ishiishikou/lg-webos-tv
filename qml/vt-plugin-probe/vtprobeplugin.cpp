#include <QtQml/QQmlExtensionPlugin>
#include <dlfcn.h>
#include <pthread.h>
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

static volatile int g_events = 0;
static volatile int g_last_type = -1;

static void on_vt_event(int type, void *, void *) {
    __sync_add_and_fetch(&g_events, 1);
    g_last_type = type;
}

static void *worker(void *) {
    FILE *log = fopen("/tmp/vt_qml_plugin.log", "a");
    if (!log) return 0;
    setvbuf(log, 0, _IONBF, 0);
    fprintf(log, "worker uid=%d euid=%d gid=%d\n", getuid(), geteuid(), getgid());

    void *lib = dlopen("/usr/lib/libvt.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        fprintf(log, "dlopen failed: %s\n", dlerror());
        fclose(log);
        return 0;
    }

#define LOAD(var, type, sym) \
    type var = reinterpret_cast<type>(dlsym(lib, sym)); \
    if (!var) { fprintf(log, "dlsym %s failed: %s\n", sym, dlerror()); goto done; }

    LOAD(is_supported, vt_is_supported_fn, "VT_IsSystemSupported");
    LOAD(create_window, vt_create_window_fn, "VT_CreateVideoWindow");
    LOAD(acquire, vt_acquire_fn, "VT_AcquireVideoWindowResource");
    LOAD(create_context, vt_create_context_fn, "VT_CreateContext");
    LOAD(set_region, vt_set_region_fn, "VT_SetTextureSourceRegion");
    LOAD(reg_handler, vt_register_fn, "VT_RegisterEventHandler");
    LOAD(unreg_handler, vt_unregister_fn, "VT_UnRegisterEventHandler");
    LOAD(delete_context, vt_delete_context_fn, "VT_DeleteContext");
    LOAD(release, vt_release_fn, "VT_ReleaseVideoWindowResource");

    {
        unsigned int supported = 0;
        int32_t resource_id = 0;
        int32_t context_id = -1;
        int have_resource = 0, have_context = 0, have_handler = 0;
        int rc = is_supported(&supported);
        fprintf(log, "VT_IsSystemSupported rc=%d supported=%u\n", rc, supported);
        if (rc != 0 || !supported) goto cleanup;

        int window_id = create_window(0);
        fprintf(log, "VT_CreateVideoWindow window_id=%d\n", window_id);
        if (window_id == -1) goto cleanup;

        rc = acquire(window_id, &resource_id);
        fprintf(log, "VT_AcquireVideoWindowResource rc=%d resource_id=%d\n", rc, resource_id);
        if (rc != 0) goto cleanup;
        have_resource = 1;

        context_id = create_context(resource_id, 2);
        fprintf(log, "VT_CreateContext context_id=%d\n", context_id);
        if (context_id == -1) goto cleanup;
        have_context = 1;

        rc = set_region(context_id, 1);
        fprintf(log, "VT_SetTextureSourceRegion rc=%d\n", rc);
        if (rc != 0) goto cleanup;

        rc = reg_handler(context_id, on_vt_event, 0);
        fprintf(log, "VT_RegisterEventHandler rc=%d\n", rc);
        if (rc != 0) goto cleanup;
        have_handler = 1;

        for (int i = 1; i <= 30; ++i) {
            usleep(100000);
            if ((i % 10) == 0)
                fprintf(log, "t=%ds events=%d last_type=%d\n",
                        i / 10, g_events, g_last_type);
        }

cleanup:
        if (have_handler)
            fprintf(log, "VT_UnRegisterEventHandler rc=%d\n", unreg_handler(context_id));
        if (have_context)
            fprintf(log, "VT_DeleteContext rc=%d\n", delete_context(context_id));
        if (have_resource)
            fprintf(log, "VT_ReleaseVideoWindowResource rc=%d\n", release(resource_id));
    }

done:
    fprintf(log, "done events=%d last_type=%d\n", g_events, g_last_type);
    dlclose(lib);
    fclose(log);
    return 0;
}

class VtProbePlugin : public QQmlExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)
public:
    void registerTypes(const char *uri) override {
        FILE *log = fopen("/tmp/vt_qml_plugin.log", "a");
        if (log) {
            fprintf(log, "registerTypes uri=%s uid=%d euid=%d gid=%d\n",
                    uri ? uri : "(null)", getuid(), geteuid(), getgid());
            fclose(log);
        }
        pthread_t thread;
        if (pthread_create(&thread, 0, worker, 0) == 0)
            pthread_detach(thread);
    }
};

#include "vtprobeplugin.moc"
