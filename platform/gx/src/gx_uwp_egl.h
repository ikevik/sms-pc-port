// The OpenGL ES context of the UWP build: ANGLE (OpenGL ES on Direct3D 11) on the app's CoreWindow
// (docs/UWP-GRAPHICS.md, section 2). Built only with SMS_UWP.
#pragma once

#include <string>

namespace gx {
namespace uwp {

// True once GXPC_UwpAttachWindow made the display, context and window surface.
bool available();
// GL entry points by name (eglGetProcAddress, retrying with the EXT, OES and KHR suffixes ES gives some).
void* getProc(const char* name);
// Make the context current on the calling thread / release it there.
bool makeCurrent();
void releaseCurrent();
// The window surface's size in pixels.
void surfaceSize(int* w, int* h);
// eglSwapBuffers (waits for the display's vertical sync).
void swap();
// The renderer's GLSL 3.30 core shaders as GLSL ES 3.00: the version line, default precisions, the extension for clip
// distances, no desktop-only extensions, and an explicit location for the fragment output (ES has no
// glBindFragDataLocation).
std::string toEs300(const char* src);

// The controller, supplied by the app (src/uwp/uwp_input.cpp) as SDL-style controller events: pad.cpp registers its event
// handler through sms_gx_set_event_callback, and every pad read pumps the poll.
void setEventCallback(void (*cb)(const void* sdlEvent));
// Diagnostics on the GL thread (gx_render.cpp): after the picture is drawn and before the swap, log GL errors and what
// the back buffer holds (a few frames, then now and then); or draw a test colour in place of the game's picture
// (SMS_UWP_TEST_PATTERN=1) to tell the display path from the renderer.
void frameProbe(int w, int h, unsigned frame, int presented);
void testPattern(unsigned frame);
void registerInput(void (*poll)(void (*emit)(const void*)), int (*rumble)(unsigned short));
void pumpInput();
int rumble(unsigned short strength);  // 1 if a controller took it

}  // namespace uwp
}  // namespace gx

// Called once on the app's UI thread, with the CoreWindow's ABI pointer (winrt::get_abi): a CoreWindow can only be used
// from its own thread, and ANGLE reads its size and subscribes to its events when the surface is made. Returns 1 if
// the window surface and context exist.
extern "C" int GXPC_UwpAttachWindow(void* coreWindow);
// The app's controller poll (calls emit with SDL controller events) and rumble.
extern "C" void GXPC_UwpSetInput(void (*poll)(void (*emit)(const void*)), int (*rumble)(unsigned short));
