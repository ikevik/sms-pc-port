// The OpenGL ES context of the UWP build: ANGLE (OpenGL ES on Direct3D 11) on the app's CoreWindow
// (docs/UWP-GRAPHICS.md, section 2). Built only with SMS_UWP.
#pragma once

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

}  // namespace uwp
}  // namespace gx

// Called once on the app's UI thread, with the CoreWindow's ABI pointer (winrt::get_abi): a CoreWindow can only be used
// from its own thread, and ANGLE reads its size and subscribes to its events when the surface is made. Returns 1 if
// the window surface and context exist.
extern "C" int GXPC_UwpAttachWindow(void* coreWindow);
