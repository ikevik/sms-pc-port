// ANGLE probe (PROBE_ANGLE): what OpenGL ES the console gives the port, through ANGLE on Direct3D 11.
//
// Creates an ES context on the app's CoreWindow (docs/UWP-GRAPHICS.md, section 2), clears to a colour that
// changes every frame, and reports: the EGL and GL versions, the renderer, the limits the port's renderer
// cares about, which extensions it would use are present, whether shaders like the renderer's compile
// (this also proves d3dcompiler_47.dll works in the sandbox), and the frame rate. The report goes to the
// dialog pages (key lines) and probe.log (everything, including the full extension lists).
#include <windows.h>
#include <unknwn.h>

#include <winrt/Windows.UI.Core.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <thread>
#include <vector>

void probe_report(const char* text, bool page);  // probe.cpp

namespace {

std::atomic<bool> g_ready{false};

void rep(bool page, const char* fmt, ...)
{
    char buf[700];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    probe_report(buf, page);
}

std::string first_line(std::string s, size_t max = 90)
{
    const size_t nl = s.find('\n');
    if (nl != std::string::npos)
        s.resize(nl);
    if (s.size() > max)
        s.resize(max);
    return s;
}

GLuint compile(GLenum type, const char* src, std::string* log)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint n = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &n);
        std::string l(n > 1 ? n : 1, '\0');
        glGetShaderInfoLog(sh, n, nullptr, &l[0]);
        *log = l.c_str();
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

// Compiles and links a program; one report line with the result and the first line of any error.
void test_program(const char* name, const char* vs, const char* fs, const char* gs = nullptr)
{
    std::string log;
    GLuint v = compile(GL_VERTEX_SHADER, vs, &log);
    GLuint g = (v && gs) ? compile(GL_GEOMETRY_SHADER, gs, &log) : 1;
    GLuint f = (v && g) ? compile(GL_FRAGMENT_SHADER, fs, &log) : 0;
    if (!v || !g || !f) {
        rep(true, "[FAIL] shader %s: %s", name, first_line(log).c_str());
        rep(false, "  full log: %s", log.c_str());
        return;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    if (gs)
        glAttachShader(p, g);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (ok) {
        rep(true, "[PASS] shader %s compiles and links", name);
    } else {
        char l[1024] = {};
        glGetProgramInfoLog(p, sizeof l - 1, nullptr, l);
        rep(true, "[FAIL] shader %s link: %s", name, first_line(l).c_str());
    }
    glDeleteProgram(p);
}

struct Egl {
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLConfig cfg = nullptr;
    EGLContext ctx = EGL_NO_CONTEXT;
    EGLSurface surf = EGL_NO_SURFACE;
    int es = 0;  // the ES 3.x minor version granted
};

// ANGLE's own messages (EGL_KHR_debug): errors to the page, everything to the log.
void EGLAPIENTRY on_egl_message(EGLenum error, const char* command, EGLint type, EGLLabelKHR, EGLLabelKHR, const char* message)
{
    const bool bad = type == EGL_DEBUG_MSG_CRITICAL_KHR || type == EGL_DEBUG_MSG_ERROR_KHR;
    rep(false, "[EGL message %d, error 0x%X] %s: %s", (int)type, (unsigned)error, command ? command : "?", message ? message : "");
    if (bad)
        rep(true, "[EGL] %s: %s", command ? command : "?", first_line(message ? message : "", 70).c_str());
}

// Display, config, context and window surface. This runs on the UI thread: a CoreWindow can only be used from
// the thread that owns it, and ANGLE reads its size and subscribes to its events when the surface is made.
bool setup_egl(winrt::Windows::UI::Core::CoreWindow const& window, Egl& e)
{
    auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (!getPlatformDisplay) {
        rep(true, "[FAIL] eglGetPlatformDisplayEXT is missing");
        return false;
    }
    if (auto debugControl = reinterpret_cast<PFNEGLDEBUGMESSAGECONTROLKHRPROC>(eglGetProcAddress("eglDebugMessageControlKHR"))) {
        const EGLAttrib attribs[] = {EGL_DEBUG_MSG_CRITICAL_KHR, EGL_TRUE, EGL_DEBUG_MSG_ERROR_KHR, EGL_TRUE,
                                     EGL_DEBUG_MSG_WARN_KHR, EGL_TRUE, EGL_DEBUG_MSG_INFO_KHR, EGL_TRUE, EGL_NONE};
        debugControl(on_egl_message, attribs);
    }
    const EGLint dispAttribs[] = {EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE, EGL_NONE};
    e.dpy = getPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, (void*)EGL_DEFAULT_DISPLAY, dispAttribs);
    EGLint major = 0, minor = 0;
    if (e.dpy == EGL_NO_DISPLAY || !eglInitialize(e.dpy, &major, &minor)) {
        rep(true, "[FAIL] ANGLE display on D3D11: error 0x%X", eglGetError());
        return false;
    }
    rep(true, "[PASS] ANGLE display: EGL %d.%d", major, minor);
    rep(false, "EGL vendor: %s", eglQueryString(e.dpy, EGL_VENDOR));
    rep(false, "EGL version: %s", eglQueryString(e.dpy, EGL_VERSION));
    rep(false, "EGL extensions: %s", eglQueryString(e.dpy, EGL_EXTENSIONS));
    rep(false, "EGL client extensions: %s", eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS));

    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint cfgAttribs[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                 EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
                                 EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE};
    EGLint nCfg = 0;
    if (!eglChooseConfig(e.dpy, cfgAttribs, &e.cfg, 1, &nCfg) || nCfg < 1) {
        rep(true, "[FAIL] no ES3 window config: error 0x%X", eglGetError());
        return false;
    }

    // The highest ES version the context gives: 3.2, 3.1, then 3.0 (what each try said goes to the log).
    for (int minorTry : {2, 1, 0}) {
        const EGLint ctxAttribs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, minorTry, EGL_NONE};
        e.ctx = eglCreateContext(e.dpy, e.cfg, EGL_NO_CONTEXT, ctxAttribs);
        if (e.ctx != EGL_NO_CONTEXT) {
            e.es = minorTry;
            break;
        }
        rep(false, "ES 3.%d context: refused, error 0x%X", minorTry, eglGetError());
    }
    if (e.ctx == EGL_NO_CONTEXT) {
        rep(true, "[FAIL] no ES 3.x context: error 0x%X", eglGetError());
        return false;
    }
    rep(true, "[PASS] context created: ES 3.%d requested and granted", e.es);

    const EGLint surfAttribs[] = {EGL_NONE};
    e.surf = eglCreateWindowSurface(e.dpy, e.cfg, reinterpret_cast<EGLNativeWindowType>(winrt::get_abi(window)), surfAttribs);
    if (e.surf == EGL_NO_SURFACE) {
        rep(true, "[FAIL] window surface on the CoreWindow: error 0x%X", eglGetError());
    } else {
        EGLint w = 0, h = 0;
        eglQuerySurface(e.dpy, e.surf, EGL_WIDTH, &w);
        eglQuerySurface(e.dpy, e.surf, EGL_HEIGHT, &h);
        rep(true, "[PASS] window surface %dx%d on the CoreWindow", w, h);
    }
    return true;
}

// The report on the current context: versions, limits, extensions, shader compiles. Needs no surface (EGL_KHR_surfaceless).
void report_gl(const Egl& e)
{
    const int got = e.es;
    rep(true, "[INFO] GL_VERSION: %s", glGetString(GL_VERSION));
    rep(true, "[INFO] GL_RENDERER: %s", first_line((const char*)glGetString(GL_RENDERER), 80).c_str());
    rep(false, "GL_VENDOR: %s", glGetString(GL_VENDOR));
    rep(true, "[INFO] GLSL: %s", glGetString(GL_SHADING_LANGUAGE_VERSION));

    // Limits the port's renderer uses.
    struct Limit { const char* name; GLenum e; };
    const Limit limits[] = {
        {"MAX_TEXTURE_SIZE", GL_MAX_TEXTURE_SIZE}, {"MAX_COLOR_ATTACHMENTS", GL_MAX_COLOR_ATTACHMENTS},
        {"MAX_DRAW_BUFFERS", GL_MAX_DRAW_BUFFERS}, {"MAX_VERTEX_ATTRIBS", GL_MAX_VERTEX_ATTRIBS},
        {"MAX_UNIFORM_BLOCK_SIZE", GL_MAX_UNIFORM_BLOCK_SIZE}, {"MAX_COMBINED_UNIFORM_BLOCKS", GL_MAX_COMBINED_UNIFORM_BLOCKS},
        {"MAX_TEXTURE_IMAGE_UNITS", GL_MAX_TEXTURE_IMAGE_UNITS}, {"MAX_SAMPLES", GL_MAX_SAMPLES},
        {"MAX_ARRAY_TEXTURE_LAYERS", GL_MAX_ARRAY_TEXTURE_LAYERS}, {"MAX_VARYING_VECTORS", GL_MAX_VARYING_VECTORS},
        {"MAX_VERTEX_UNIFORM_VECTORS", GL_MAX_VERTEX_UNIFORM_VECTORS}, {"MAX_FRAGMENT_UNIFORM_VECTORS", GL_MAX_FRAGMENT_UNIFORM_VECTORS},
        {"NUM_PROGRAM_BINARY_FORMATS", GL_NUM_PROGRAM_BINARY_FORMATS}, {"NUM_COMPRESSED_TEXTURE_FORMATS", GL_NUM_COMPRESSED_TEXTURE_FORMATS},
    };
    std::string row;
    for (const Limit& l : limits) {
        GLint v = 0;
        glGetIntegerv(l.e, &v);
        rep(false, "limit %s = %d", l.name, v);
        if (!strcmp(l.name, "MAX_TEXTURE_SIZE") || !strcmp(l.name, "MAX_COLOR_ATTACHMENTS") || !strcmp(l.name, "MAX_SAMPLES") ||
            !strcmp(l.name, "MAX_UNIFORM_BLOCK_SIZE") || !strcmp(l.name, "NUM_PROGRAM_BINARY_FORMATS")) {
            char b[80];
            snprintf(b, sizeof b, "%s=%d  ", l.name, v);
            row += b;
        }
    }
    rep(true, "[INFO] %s", row.c_str());

    // Extensions: all to the log, the ones the port's renderer would use to the page.
    std::set<std::string> ext;
    GLint nExt = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &nExt);
    std::string all;
    for (GLint i = 0; i < nExt; i++) {
        const char* e = (const char*)glGetStringi(GL_EXTENSIONS, i);
        if (e) {
            ext.insert(e);
            all += e;
            all += ' ';
        }
    }
    rep(false, "GL extensions (%d): %s", (int)nExt, all.c_str());
    struct Want { const char* label; const char* a; const char* b; };
    const Want wants[] = {
        {"clip distance", "GL_EXT_clip_cull_distance", "GL_ANGLE_clip_cull_distance"},
        {"base vertex draws", "GL_EXT_draw_elements_base_vertex", "GL_OES_draw_elements_base_vertex"},
        {"geometry shader", "GL_EXT_geometry_shader", "GL_OES_geometry_shader"},
        {"S3TC (BC1-3)", "GL_EXT_texture_compression_s3tc", "GL_EXT_texture_compression_dxt1"},
        {"BC7 (BPTC)", "GL_EXT_texture_compression_bptc", "GL_ARB_texture_compression_bptc"},
        {"dual-source blend", "GL_EXT_blend_func_extended", nullptr},
        {"logic op", "GL_ANGLE_logic_op", nullptr},
        {"depth clamp", "GL_EXT_depth_clamp", nullptr},
        {"border clamp", "GL_EXT_texture_border_clamp", "GL_OES_texture_border_clamp"},
        {"buffer storage", "GL_EXT_buffer_storage", nullptr},
        {"parallel compile", "GL_KHR_parallel_shader_compile", nullptr},
        {"anisotropic", "GL_EXT_texture_filter_anisotropic", nullptr},
        {"BGRA textures", "GL_EXT_texture_format_BGRA8888", "GL_APPLE_texture_format_BGRA8888"},
        {"polygon offset clamp", "GL_EXT_polygon_offset_clamp", nullptr},
        {"framebuffer fetch", "GL_EXT_shader_framebuffer_fetch", "GL_ARM_shader_framebuffer_fetch"},
    };
    std::string have, lack;
    const char* clipExt = nullptr;
    for (const Want& wnt : wants) {
        const char* hit = ext.count(wnt.a) ? wnt.a : (wnt.b && ext.count(wnt.b)) ? wnt.b : nullptr;
        (hit ? have : lack) += std::string(wnt.label) + ", ";
        if (hit && !strcmp(wnt.label, "clip distance"))
            clipExt = hit;
    }
    rep(true, "[INFO] has: %s", have.empty() ? "(none)" : have.c_str());
    rep(true, "[INFO] lacks: %s", lack.empty() ? "(none)" : lack.c_str());

    // Shaders like the renderer's (docs/UWP-GRAPHICS.md, section 4); compiling needs d3dcompiler_47.dll.
    test_program("ES 3.00 renderer-like",
                 "#version 300 es\nprecision highp float;\nprecision highp int;\n"
                 "layout(std140) uniform XFBlock { uvec4 xf[184]; };\n"
                 "in vec4 a_pos;\nout vec2 v_uv;\n"
                 "void main() { uvec4 w = xf[gl_VertexID & 127]; v_uv = vec2(float(w.x & 255u), float(w.y)); gl_Position = a_pos; }\n",
                 "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n"
                 "uniform sampler2D u_tex;\nin vec2 v_uv;\nlayout(location = 0) out vec4 o_color;\n"
                 "void main() { o_color = texelFetch(u_tex, ivec2(v_uv), 0) + texture(u_tex, v_uv); }\n");
    if (clipExt) {
        std::string vs = std::string("#version 300 es\n#extension ") + clipExt + " : require\n"
                         "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); gl_ClipDistance[0] = 1.0; gl_ClipDistance[1] = 1.0; }\n";
        test_program("clip distance", vs.c_str(), "#version 300 es\nprecision highp float;\nout vec4 o;\nvoid main() { o = vec4(1.0); }\n");
    }
    if (got >= 1 && (ext.count("GL_EXT_geometry_shader") || got >= 2)) {
        test_program("geometry (wide lines)",
                     "#version 310 es\nprecision highp float;\nvoid main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n",
                     "#version 310 es\nprecision highp float;\nout vec4 o;\nvoid main() { o = vec4(1.0); }\n",
                     "#version 310 es\n#extension GL_EXT_geometry_shader : require\n"
                     "layout(lines) in;\nlayout(triangle_strip, max_vertices = 4) out;\n"
                     "void main() { gl_Position = gl_in[0].gl_Position; EmitVertex(); EndPrimitive(); }\n");
    }

}

// Draws: clear to a colour that changes every frame, at the display's rate. Runs on its own thread, which takes the
// context from the UI thread.
void frame_loop(Egl e)
{
    if (!eglMakeCurrent(e.dpy, e.surf, e.surf, e.ctx)) {
        rep(true, "[FAIL] eglMakeCurrent on the render thread: error 0x%X", eglGetError());
        g_ready = true;
        return;
    }
    const EGLDisplay dpy = e.dpy;
    const EGLSurface surf = e.surf;
    eglSwapInterval(dpy, 1);
    LARGE_INTEGER freq, t0, t;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    unsigned frames = 0;
    bool reported = false;
    for (;;) {
        const float k = (frames % 180) / 180.0f;
        glClearColor(k, 1.0f - k, 0.4f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (!eglSwapBuffers(dpy, surf)) {
            if (!reported) {
                rep(true, "[FAIL] eglSwapBuffers: error 0x%X", eglGetError());
                g_ready = true;
                reported = true;
            }
            return;
        }
        frames++;
        if (!reported) {
            QueryPerformanceCounter(&t);
            const double s = double(t.QuadPart - t0.QuadPart) / double(freq.QuadPart);
            if (s >= 2.0) {
                rep(true, "[PASS] %u frames in %.1f s = %.1f fps (vsync)", frames, s, frames / s);
                reported = true;
                g_ready = true;
            }
        }
    }
}

}

}  // namespace

// Sets up EGL on this (the UI) thread, reports, then hands the context to a render thread that keeps drawing for as
// long as the app runs.
void run_angle_probe(winrt::Windows::UI::Core::CoreWindow const& window)
{
    rep(true, "--- ANGLE (OpenGL ES on D3D11) ---");
    Egl e;
    if (!setup_egl(window, e))
        return;
    // Without a surface the context still works (EGL_KHR_surfaceless): the report does not need one.
    if (eglMakeCurrent(e.dpy, e.surf, e.surf, e.ctx) || eglMakeCurrent(e.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, e.ctx)) {
        report_gl(e);
        eglMakeCurrent(e.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    } else {
        rep(true, "[FAIL] eglMakeCurrent: error 0x%X", eglGetError());
        return;
    }
    if (e.surf == EGL_NO_SURFACE)
        return;
    std::thread([e] { frame_loop(e); }).detach();
    for (int i = 0; i < 400 && !g_ready; i++)  // 20 s
        Sleep(50);
    if (!g_ready)
        rep(true, "[FAIL] no frames in 20 s");
}
