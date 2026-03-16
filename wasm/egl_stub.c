#include <EGL/egl.h>

static EGLDisplay g_display = (EGLDisplay)1;
static EGLSurface g_surface = (EGLSurface)1;
static EGLContext g_context = (EGLContext)1;

EGLDisplay eglGetDisplay(EGLNativeDisplayType) {
    return g_display;
}

EGLBoolean eglInitialize(EGLDisplay display, EGLint *major, EGLint *minor) {
    if (major) *major = 1;
    if (minor) *minor = 5;
    return EGL_TRUE;
}

EGLBoolean eglTerminate(EGLDisplay) {
    return EGL_TRUE;
}

EGLBoolean eglBindAPI(EGLenum) {
    return EGL_TRUE;
}

EGLenum eglQueryAPI(void) {
    return EGL_OPENGL_ES_API;
}

EGLBoolean eglChooseConfig(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *) {
    return EGL_TRUE;
}

EGLSurface eglCreateWindowSurface(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint *) {
    return g_surface;
}

EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const EGLint *) {
    return g_context;
}

EGLBoolean eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext) {
    return EGL_TRUE;
}

EGLBoolean eglSwapBuffers(EGLDisplay, EGLSurface) {
    return EGL_TRUE;
}

EGLBoolean eglDestroyContext(EGLDisplay, EGLContext) {
    return EGL_TRUE;
}

EGLBoolean eglDestroySurface(EGLDisplay, EGLSurface) {
    return EGL_TRUE;
}

EGLBoolean eglQueryContext(EGLDisplay, EGLContext, EGLint, EGLint *) {
    return EGL_TRUE;
}

EGLDisplay eglGetCurrentDisplay(void) {
    return g_display;
}

EGLContext eglGetCurrentContext(void) {
    return g_context;
}

EGLSurface eglGetCurrentSurface(EGLint) {
    return g_surface;
}

EGLBoolean eglSwapInterval(EGLDisplay, EGLint) {
    return EGL_TRUE;
}

EGLBoolean eglReleaseThread(void) {
    return EGL_TRUE;
}

EGLBoolean eglWaitClient(void) {
    return EGL_TRUE;
}

EGLBoolean eglWaitNative(EGLint) {
    return EGL_TRUE;
}
