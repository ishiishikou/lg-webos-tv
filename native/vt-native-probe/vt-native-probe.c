#define _GNU_SOURCE
#include <dlfcn.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLSurface;
typedef void *EGLContext;
typedef int32_t EGLint;
typedef uint32_t EGLBoolean;

#define EGL_NONE 0x3038
#define EGL_SURFACE_TYPE 0x3033
#define EGL_PBUFFER_BIT 0x0001
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_OPENGL_ES_API 0x30A0

typedef EGLDisplay (*egl_get_display_fn)(void *);
typedef EGLBoolean (*egl_initialize_fn)(EGLDisplay, EGLint *, EGLint *);
typedef EGLBoolean (*egl_choose_config_fn)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
typedef EGLBoolean (*egl_bind_api_fn)(EGLint);
typedef EGLSurface (*egl_create_pbuffer_fn)(EGLDisplay, EGLConfig, const EGLint *);
typedef EGLContext (*egl_create_context_fn)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
typedef EGLBoolean (*egl_make_current_fn)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
typedef EGLDisplay (*egl_get_current_display_fn)(void);
typedef EGLContext (*egl_get_current_context_fn)(void);
typedef EGLint (*egl_get_error_fn)(void);
typedef EGLBoolean (*egl_destroy_surface_fn)(EGLDisplay, EGLSurface);
typedef EGLBoolean (*egl_destroy_context_fn)(EGLDisplay, EGLContext);
typedef EGLBoolean (*egl_terminate_fn)(EGLDisplay);

typedef int (*vt_is_supported_fn)(unsigned int *);
typedef int (*vt_get_max_buffer_fn)(unsigned int *);
typedef int (*vt_create_window_fn)(int);
typedef int (*vt_acquire_fn)(int, int32_t *);
typedef int32_t (*vt_create_context_fn)(int32_t, unsigned int);
typedef int (*vt_context_valid_fn)(int32_t);
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

int main(void) {
    FILE *log = fopen("/tmp/vt_native_probe.log", "w");
    if (!log) return 2;
    setvbuf(log, NULL, _IONBF, 0);
    fprintf(log, "uid=%d euid=%d gid=%d\n", getuid(), geteuid(), getgid());

    void *egl_lib = dlopen("/usr/lib/libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!egl_lib) {
        fprintf(log, "dlopen libEGL failed: %s\n", dlerror());
        fclose(log);
        return 3;
    }

#define EGLSYM(name, type) type name = (type)dlsym(egl_lib, #name)
    EGLSYM(eglGetDisplay, egl_get_display_fn);
    EGLSYM(eglInitialize, egl_initialize_fn);
    EGLSYM(eglChooseConfig, egl_choose_config_fn);
    EGLSYM(eglBindAPI, egl_bind_api_fn);
    EGLSYM(eglCreatePbufferSurface, egl_create_pbuffer_fn);
    EGLSYM(eglCreateContext, egl_create_context_fn);
    EGLSYM(eglMakeCurrent, egl_make_current_fn);
    EGLSYM(eglGetCurrentDisplay, egl_get_current_display_fn);
    EGLSYM(eglGetCurrentContext, egl_get_current_context_fn);
    EGLSYM(eglGetError, egl_get_error_fn);
    EGLSYM(eglDestroySurface, egl_destroy_surface_fn);
    EGLSYM(eglDestroyContext, egl_destroy_context_fn);
    EGLSYM(eglTerminate, egl_terminate_fn);

    if (!eglGetDisplay || !eglInitialize || !eglChooseConfig || !eglBindAPI ||
        !eglCreatePbufferSurface || !eglCreateContext || !eglMakeCurrent ||
        !eglGetCurrentDisplay || !eglGetCurrentContext || !eglGetError) {
        fprintf(log, "missing EGL symbol\n");
        dlclose(egl_lib);
        fclose(log);
        return 4;
    }

    EGLDisplay display = eglGetDisplay(NULL);
    EGLint major = 0, minor = 0;
    EGLBoolean ok = display ? eglInitialize(display, &major, &minor) : 0;
    fprintf(log, "eglInitialize ok=%u display=%p version=%d.%d err=0x%x\n",
            ok, display, major, minor, eglGetError());
    if (!ok) goto egl_cleanup;

    const EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig config = NULL;
    EGLint num_config = 0;
    ok = eglChooseConfig(display, config_attrs, &config, 1, &num_config);
    fprintf(log, "eglChooseConfig ok=%u num=%d config=%p err=0x%x\n",
            ok, num_config, config, eglGetError());
    if (!ok || num_config < 1 || !config) goto egl_cleanup;

    ok = eglBindAPI(EGL_OPENGL_ES_API);
    fprintf(log, "eglBindAPI ok=%u err=0x%x\n", ok, eglGetError());
    if (!ok) goto egl_cleanup;

    const EGLint pbuffer_attrs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, pbuffer_attrs);
    fprintf(log, "eglCreatePbufferSurface surface=%p err=0x%x\n", surface, eglGetError());
    if (!surface) goto egl_cleanup;

    const EGLint context_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext egl_context = eglCreateContext(display, config, NULL, context_attrs);
    fprintf(log, "eglCreateContext context=%p err=0x%x\n", egl_context, eglGetError());
    if (!egl_context) goto surface_cleanup;

    ok = eglMakeCurrent(display, surface, surface, egl_context);
    fprintf(log, "eglMakeCurrent ok=%u currentDisplay=%p currentContext=%p err=0x%x\n",
            ok, eglGetCurrentDisplay(), eglGetCurrentContext(), eglGetError());
    if (!ok) goto context_cleanup;

    void *lib = dlopen("/usr/lib/libvt.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        fprintf(log, "dlopen libvt failed: %s\n", dlerror());
        goto current_cleanup;
    }

#define LOAD(name, type) \
    type name = (type)dlsym(lib, #name); \
    if (!(name)) { fprintf(log, "dlsym %s failed: %s\n", #name, dlerror()); goto vt_out; }

    LOAD(VT_IsSystemSupported, vt_is_supported_fn);
    LOAD(VT_GetMaxTextureBufferSize, vt_get_max_buffer_fn);
    LOAD(VT_CreateVideoWindow, vt_create_window_fn);
    LOAD(VT_AcquireVideoWindowResource, vt_acquire_fn);
    LOAD(VT_CreateContext, vt_create_context_fn);
    LOAD(VT_CONTEXT_IsContextIDValid, vt_context_valid_fn);
    LOAD(VT_SetTextureSourceRegion, vt_set_region_fn);
    LOAD(VT_RegisterEventHandler, vt_register_fn);
    LOAD(VT_UnRegisterEventHandler, vt_unregister_fn);
    LOAD(VT_DeleteContext, vt_delete_context_fn);
    LOAD(VT_ReleaseVideoWindowResource, vt_release_fn);

    int have_resource = 0;
    int have_context = 0;
    int have_handler = 0;
    int32_t resource_id = 0;
    int32_t context_id = 0;
    int rc;

    unsigned int supported = 0;
    rc = VT_IsSystemSupported(&supported);
    fprintf(log, "VT_IsSystemSupported rc=%d supported=%u\n", rc, supported);
    if (rc != 0 || !supported) goto vt_cleanup;

    unsigned int max_buffers = 0;
    rc = VT_GetMaxTextureBufferSize(&max_buffers);
    fprintf(log, "VT_GetMaxTextureBufferSize rc=%d max=%u\n", rc, max_buffers);
    if (rc != 0 || max_buffers == 0) max_buffers = 2;

    int window_id = VT_CreateVideoWindow(0);
    fprintf(log, "VT_CreateVideoWindow window_id=%d\n", window_id);
    if (window_id == -1) goto vt_cleanup;

    rc = VT_AcquireVideoWindowResource(window_id, &resource_id);
    fprintf(log, "VT_AcquireVideoWindowResource rc=%d resource_id=%d hex=0x%x\n",
            rc, resource_id, (unsigned int)resource_id);
    if (rc != 0) goto vt_cleanup;
    have_resource = 1;

    context_id = VT_CreateContext(resource_id, max_buffers);
    fprintf(log, "VT_CreateContext context_id=%d valid=%d\n",
            context_id, VT_CONTEXT_IsContextIDValid(context_id));
    if (context_id == 0) goto vt_cleanup;
    have_context = 1;

    rc = VT_SetTextureSourceRegion(context_id, 1);
    fprintf(log, "VT_SetTextureSourceRegion rc=%d\n", rc);
    if (rc != 0) goto vt_cleanup;

    rc = VT_RegisterEventHandler(context_id, on_vt_event, (void *)&g_events);
    fprintf(log, "VT_RegisterEventHandler rc=%d\n", rc);
    if (rc != 0) goto vt_cleanup;
    have_handler = 1;

    for (int i = 1; i <= 50; ++i) {
        usleep(100000);
        if ((i % 10) == 0)
            fprintf(log, "t=%ds events=%d last_type=%d\n",
                    i / 10, (int)g_events, (int)g_last_type);
    }

vt_cleanup:
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
vt_out:
    dlclose(lib);

current_cleanup:
    eglMakeCurrent(display, NULL, NULL, NULL);
context_cleanup:
    if (egl_context && eglDestroyContext)
        eglDestroyContext(display, egl_context);
surface_cleanup:
    if (surface && eglDestroySurface)
        eglDestroySurface(display, surface);
egl_cleanup:
    if (display && eglTerminate)
        eglTerminate(display);
    dlclose(egl_lib);
    fprintf(log, "done events=%d last_type=%d\n", (int)g_events, (int)g_last_type);
    fclose(log);
    return 0;
}
