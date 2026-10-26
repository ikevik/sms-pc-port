// ANGLE on the CoreWindow for the UWP build; see gx_uwp_egl.h. The steps match what tools/uwp-probe/angle_probe.cpp
// did on the console: display on the D3D11 platform, an ES 3.0 context (the most the console's feature level gives),
// and a window surface made on the UI thread.
#include "gx_uwp_egl.h"

#include "gx_internal.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>

#include <string.h>
#include <functional>
#include <string>
#include <vector>

extern "C" void port_uwp_backtrace_text(char* out, size_t size, int skip, int count);  // src/win_posix/uwp_stubs.cpp

namespace {

EGLDisplay s_dpy = EGL_NO_DISPLAY;
EGLConfig s_cfg = nullptr;
EGLContext s_ctx = EGL_NO_CONTEXT;
EGLSurface s_surf = EGL_NO_SURFACE;

// ANGLE's own messages (EGL_KHR_debug) into the log.
void EGLAPIENTRY onEglMessage(EGLenum error, const char* command, EGLint type, EGLLabelKHR, EGLLabelKHR, const char* message) {
    gx::logmsg("[ANGLE %d, 0x%X] %s: %s", (int)type, (unsigned)error, command ? command : "?", message ? message : "");
}

// GL_KHR_debug messages (ANGLE says which call it rejected and why) with the caller's stack: offsets in sms.exe, which
// the link map (sms.map, with the build's artifact) turns into names. One line for each distinct message, a limited
// number of them: the same ones come every frame.
void onGlMessage(unsigned source, unsigned type, unsigned id, unsigned severity, int length, const char* message, const void*) {
    static size_t seen[96];
    static int nseen = 0;
    const std::string text(message ? message : "", message ? (length > 0 ? (size_t)length : strlen(message)) : 0);
    const size_t key = std::hash<std::string>()(text);
    for (int i = 0; i < nseen; i++)
        if (seen[i] == key) return;
    if (nseen >= 96) return;
    seen[nseen++] = key;
    char stack[900];
    port_uwp_backtrace_text(stack, sizeof stack, 1, 12);
    gx::logmsg("[GL 0x%X/0x%X id %u sev 0x%X] %s\n    at %s", source, type, id, severity, text.c_str(), stack);
}

}  // namespace

extern "C" void GXPC_UwpSetInput(void (*poll)(void (*emit)(const void*)), int (*rumble)(unsigned short)) {
    gx::uwp::registerInput(poll, rumble);
}

extern "C" int GXPC_UwpAttachWindow(void* coreWindow) {
    using namespace gx;
    if (s_surf != EGL_NO_SURFACE) return 1;
    if (auto debugControl = reinterpret_cast<PFNEGLDEBUGMESSAGECONTROLKHRPROC>(eglGetProcAddress("eglDebugMessageControlKHR"))) {
        const EGLAttrib attribs[] = {EGL_DEBUG_MSG_CRITICAL_KHR, EGL_TRUE, EGL_DEBUG_MSG_ERROR_KHR, EGL_TRUE,
                                     EGL_DEBUG_MSG_WARN_KHR, EGL_TRUE, EGL_NONE};
        debugControl(onEglMessage, attribs);
    }
    auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (!getPlatformDisplay) {
        logmsg("UWP: eglGetPlatformDisplayEXT is missing");
        return 0;
    }
    const EGLint dispAttribs[] = {EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE, EGL_NONE};
    s_dpy = getPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, (void*)EGL_DEFAULT_DISPLAY, dispAttribs);
    EGLint major = 0, minor = 0;
    if (s_dpy == EGL_NO_DISPLAY || !eglInitialize(s_dpy, &major, &minor)) {
        logmsg("UWP: ANGLE display on D3D11 failed: 0x%X", eglGetError());
        return 0;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint cfgAttribs[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                 EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
                                 EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE};
    EGLint n = 0;
    if (!eglChooseConfig(s_dpy, cfgAttribs, &s_cfg, 1, &n) || n < 1) {
        logmsg("UWP: no ES 3 window config: 0x%X", eglGetError());
        return 0;
    }
    const EGLint ctxAttribs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
    s_ctx = eglCreateContext(s_dpy, s_cfg, EGL_NO_CONTEXT, ctxAttribs);
    if (s_ctx == EGL_NO_CONTEXT) {
        logmsg("UWP: no ES 3.0 context: 0x%X", eglGetError());
        return 0;
    }
    const EGLint surfAttribs[] = {EGL_NONE};
    s_surf = eglCreateWindowSurface(s_dpy, s_cfg, reinterpret_cast<EGLNativeWindowType>(coreWindow), surfAttribs);
    if (s_surf == EGL_NO_SURFACE) {
        logmsg("UWP: window surface on the CoreWindow failed: 0x%X", eglGetError());
        return 0;
    }
    int w = 0, h = 0;
    gx::uwp::surfaceSize(&w, &h);
    logmsg("UWP: ANGLE EGL %d.%d, OpenGL ES 3.0 context, %dx%d window surface", major, minor, w, h);
    return 1;
}

namespace gx {
namespace uwp {

bool available() { return s_surf != EGL_NO_SURFACE && s_ctx != EGL_NO_CONTEXT; }

static void (*s_eventCb)(const void*);
static void (*s_poll)(void (*emit)(const void*));
static int (*s_rumble)(unsigned short);

void setEventCallback(void (*cb)(const void*)) { s_eventCb = cb; }
void registerInput(void (*poll)(void (*emit)(const void*)), int (*rumble)(unsigned short)) {
    s_poll = poll;
    s_rumble = rumble;
}
void pumpInput() {
    if (s_poll && s_eventCb) s_poll(s_eventCb);
}
int rumble(unsigned short strength) { return s_rumble ? s_rumble(strength) : 0; }

// The context's extensions (glGetString, read once in enableGlDebug): ANGLE hands out entry points for extensions the
// Direct3D 11 backend does not have, and a call to one raises GL_INVALID_OPERATION instead of failing to load.
static std::string s_extensions;
static bool s_baseVertex = false;
static bool hasExtension(const char* name) {
    const std::string list = " " + s_extensions + " ";
    return list.find(std::string(" ") + name + " ") != std::string::npos;
}
bool nativeBaseVertex() { return s_baseVertex; }

// Desktop GL takes doubles where ES has float versions under other names.
static void (*s_clearDepthf)(float);
static void (*s_depthRangef)(float, float);
static void clearDepthDouble(double d) { s_clearDepthf((float)d); }
static void depthRangeDouble(double n, double f) { s_depthRangef((float)n, (float)f); }

void* getProc(const char* name) {
    if (!strcmp(name, "glBufferStorage") && !hasExtension("GL_EXT_buffer_storage")) return nullptr;  // the renderer then streams through plain buffers
    if (!strcmp(name, "glClearDepth")) {
        s_clearDepthf = reinterpret_cast<void (*)(float)>(eglGetProcAddress("glClearDepthf"));
        return s_clearDepthf ? reinterpret_cast<void*>(&clearDepthDouble) : nullptr;
    }
    if (!strcmp(name, "glDepthRange")) {
        s_depthRangef = reinterpret_cast<void (*)(float, float)>(eglGetProcAddress("glDepthRangef"));
        return s_depthRangef ? reinterpret_cast<void*>(&depthRangeDouble) : nullptr;
    }
    if (void* p = reinterpret_cast<void*>(eglGetProcAddress(name))) return p;
    for (const char* suffix : {"EXT", "OES", "KHR"}) {
        const std::string alt = std::string(name) + suffix;
        if (void* p = reinterpret_cast<void*>(eglGetProcAddress(alt.c_str()))) return p;
    }
    return nullptr;
}

void enableGlDebug() {
    typedef void (*DebugProc)(unsigned, unsigned, unsigned, unsigned, int, const char*, const void*);
    auto callback = reinterpret_cast<void (*)(DebugProc, const void*)>(getProc("glDebugMessageCallback"));
    auto control = reinterpret_cast<void (*)(unsigned, unsigned, unsigned, int, const unsigned*, unsigned char)>(
        getProc("glDebugMessageControl"));
    auto enable = reinterpret_cast<void (*)(unsigned)>(getProc("glEnable"));
    if (!callback || !enable) {
        logmsg("UWP: GL_KHR_debug is not available (callback %p)", reinterpret_cast<void*>(callback));
        return;
    }
    if (auto getString = reinterpret_cast<const unsigned char* (*)(unsigned)>(getProc("glGetString"))) {
        if (const unsigned char* e = getString(0x1F03 /* GL_EXTENSIONS */)) s_extensions = reinterpret_cast<const char*>(e);
    }
    s_baseVertex = hasExtension("GL_EXT_draw_elements_base_vertex") || hasExtension("GL_OES_draw_elements_base_vertex");
    logmsg("UWP: extensions: %s", s_extensions.c_str());
    logmsg("UWP: base vertex draws are %s", s_baseVertex ? "native" : "emulated (attribute offsets)");
    callback(onGlMessage, nullptr);
    if (control) control(0x1100 /* GL_DONT_CARE */, 0x1100, 0x1100, 0, nullptr, 1);
    enable(0x92E0);  // GL_DEBUG_OUTPUT
    enable(0x8242);  // GL_DEBUG_OUTPUT_SYNCHRONOUS: the callback runs inside the call that failed
    logmsg("UWP: GL debug messages on");
}

bool makeCurrent() { return eglMakeCurrent(s_dpy, s_surf, s_surf, s_ctx) == EGL_TRUE; }
void releaseCurrent() { eglMakeCurrent(s_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT); }

void surfaceSize(int* w, int* h) {
    EGLint ew = 0, eh = 0;
    if (s_surf != EGL_NO_SURFACE) {
        eglQuerySurface(s_dpy, s_surf, EGL_WIDTH, &ew);
        eglQuerySurface(s_dpy, s_surf, EGL_HEIGHT, &eh);
    }
    *w = ew;
    *h = eh;
}

void swap() {
    eglSwapInterval(s_dpy, 1);
    eglSwapBuffers(s_dpy, s_surf);
}

static bool startsWith(const std::string& s, size_t at, const char* what) { return s.compare(at, strlen(what), what) == 0; }

std::string toEs300(const char* source) {
    const std::string src(source);
    std::vector<std::string> lines;
    for (size_t i = 0; i <= src.size();) {
        const size_t nl = src.find('\n', i);
        if (nl == std::string::npos) {
            if (i < src.size()) lines.push_back(src.substr(i));
            break;
        }
        lines.push_back(src.substr(i, nl - i));
        i = nl + 1;
    }
    const bool clip = src.find("gl_ClipDistance") != std::string::npos;
    std::string head, body;
    bool inHead = true;
    for (const std::string& line : lines) {
        size_t at = line.find_first_not_of(" \t");
        const std::string trimmed = at == std::string::npos ? std::string() : line.substr(at);
        if (inHead && (trimmed.empty() || trimmed[0] == '#')) {
            if (startsWith(trimmed, 0, "#version")) head += "#version 300 es\n";
            else if (startsWith(trimmed, 0, "#extension GL_ARB_")) {}  // desktop only
            else head += line + "\n";
            continue;
        }
        if (inHead) {
            inHead = false;
            if (clip) head += "#extension GL_EXT_clip_cull_distance : require\n";
            head += "precision highp float;\nprecision highp int;\nprecision highp sampler2D;\nprecision highp isampler2D;\n"
                    "precision highp usampler2D;\nprecision highp sampler2DArray;\nprecision highp sampler3D;\n"
                    "precision highp samplerCube;\nprecision highp sampler2DShadow;\n";
        }
        if (trimmed == "out vec4 o_color;" || trimmed == "out vec4 o;" || trimmed == "out uvec4 o_color;")
            body += "layout(location = 0) " + trimmed + "\n";
        else
            body += line + "\n";
    }
    return head + body;
}

}  // namespace uwp
}  // namespace gx
