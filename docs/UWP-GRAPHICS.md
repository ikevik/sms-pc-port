# UWP graphics: ANGLE and a CoreWindow EGL surface

Status: plan. The UWP build runs the game's logic on the Xbox (see the `[uwp]` heartbeat in
`LocalState\sms.log`) but draws nothing: `[sms_gx] no OpenGL context could be created`.

The renderer (`platform/gx`) is OpenGL 3.3 core, created by SDL2 (`openWindow`) or by desktop EGL
(`openHeadless`). UWP has no desktop OpenGL and SDL2 has no clang-buildable WinRT backend, so the plan
is ANGLE (OpenGL ES on Direct3D 11) with the context made on the app's `CoreWindow`.
The probe measured Direct3D feature level 11.0 on the console (`docs/64-BIT.md` notes on memory:
1024 MiB app limit, 0 MiB of low address space taken by D3D11).

## 1. Build ANGLE for UWP (`azure-pipelines-angle.yml`)

`tools/ci/build_angle.ps1` follows ANGLE's `doc/DevSetup.md`: depot_tools, `fetch angle`, `gn gen` with
`target_os = "winuwp"`, `is_component_build = false`, `is_clang = false`, then
`autoninja libEGL libGLESv2`. The `angle-uwp` artifact holds `bin/` (libEGL.dll, libGLESv2.dll,
d3dcompiler_47.dll), `lib/` (import libraries), `include/` (EGL, GLES2, GLES3, KHR), `ANGLE_REF.txt`
(the commit and gn args) and the logs.

Unknowns, in the order they will show up:
- ANGLE's UWP support is documented but the Chromium build files it uses have a "WinUWP is out of date"
  history. If `main` does not build, pass an older branch or commit as `angleRef`.
- A hosted job stops at 60 minutes. The script stops ninja at 52 and says so in `RESULT.txt`. If the
  build is cut short, the next step is caching the source and `out/` between runs.
- Which OpenGL ES version the D3D11 backend reports at feature level 11.0 (3.0 or 3.1): step 3 prints it.

## 2. The context on a CoreWindow

New `platform/gx/src/gx_uwp_egl.cpp`, built only with `SMS_UWP`:

1. Link `libEGL.dll.lib` and `libGLESv2.dll.lib`; the DLLs sit in the package, so no `LoadPackagedLibrary`.
2. `eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, {EGL_PLATFORM_ANGLE_TYPE_ANGLE,
   EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE, EGL_NONE})`, `eglInitialize`.
3. `eglChooseConfig` for RGBA8 with `EGL_RENDERABLE_TYPE = EGL_OPENGL_ES3_BIT` (the renderer draws into
   its own framebuffers and only needs a colour back buffer), `eglCreateContext` with
   `EGL_CONTEXT_CLIENT_VERSION 3` (or 3.1 if section 4 needs it).
4. `eglCreateWindowSurface(dpy, cfg, window, {EGL_NONE})`, where `window` is the `CoreWindow` as an
   `IInspectable*` (`winrt::get_abi`). ANGLE's WinRT path takes a `CoreWindow`, a `SwapChainPanel` or an
   `IPropertySet`; check against the built ANGLE's headers.
5. `eglSwapInterval(dpy, 1)`; all GL entry points through `eglGetProcAddress` into the existing
   `gx::gl::load` table (`gl_loader.cpp`).

Wiring into `gx_platform.cpp`:
- `uwp_main.cpp` keeps the `CoreWindow` and exports `void* sms_uwp_core_window()`.
- A new mode, `MODE_ANGLE_WINDOW`, beside `MODE_WINDOW` and `MODE_HEADLESS` (`MODE_EXTERNAL` is declared but
  unused). `GXPC_InitAuto` tries it first under `SMS_UWP`. It implements only open, present, vsync and
  size; the SDL-only parts (mouse, fullscreen, display modes, menus) stay out.
- `GXPC_Present`: `eglSwapBuffers(dpy, surf)` where it calls `SDL_GL_SwapWindow`.
- The renderer already has a GL thread (`glt::start` with a make-current callback, as `openHeadless`
  does); the ANGLE context is made current there.
- Size: `eglQuerySurface(EGL_WIDTH/HEIGHT)`. The console gives 1920x1080 or 3840x2160 depending on its
  video setting; the renderer's internal resolution scale stays as it is.
- Suspend and resume: UWP apps must trim the D3D device when suspended. Check whether ANGLE does it
  (`IDXGIDevice3::Trim`) or expose a hook; drop and recreate the surface if the window is replaced.

## 3. Before the renderer: a probe (do this first)

Extend `tools/uwp-probe` with ANGLE: create the ES context on the probe's `CoreWindow`, clear to a
colour, swap, and write `GL_VERSION`, `GL_RENDERER`, `GL_SHADING_LANGUAGE_VERSION`, every extension and
the main limits to `probe.log`. It answers the open questions in section 4 on the real console, in one
short build, and shows a colour on screen: the first picture of the port.

## 4. What the renderer needs changed (from reading `platform/gx`)

| Today (GL 3.3 core) | On ANGLE's ES | Plan |
| --- | --- | --- |
| `#version 330 core` shaders (about 10 in `gx_render.cpp`, `gx_hires.cpp`, `gx_mod_layer.cpp`, plus the generated ones in `gx_shader.cpp`) | `#version 300 es` (or 310 es) with precision qualifiers | one prefix function, plus fixes where the compiler objects |
| `glBindFragDataLocation` (6 calls) | not in ES | `layout(location = N)` in the shaders |
| `gl_ClipDistance` (GX clip planes) | `EXT_clip_cull_distance`, if ANGLE offers it on D3D11 | check the extension list; else clip in the fragment shader with `discard` |
| `GL_COLOR_LOGIC_OP` (12 uses) | none | emulate the few GX logic ops in the shader or blend state; most scenes use none |
| geometry shader for wide lines (`gx_shader.cpp`) | `EXT_geometry_shader` only at ES 3.1+ with the extension | draw lines as instanced quads, or skip the extension on UWP first |
| `GL_SAMPLES_PASSED` | `ANY_SAMPLES_PASSED` only | the game's pixel-count queries need a count; check how the count is used |
| `GL_DEPTH_CLAMP` | none | clamp `gl_Position.z` in the vertex shader |
| `glDrawElementsBaseVertex`, `glMultiDrawElementsBaseVertex` | `EXT_draw_elements_base_vertex` | check; else rebase indices on the CPU |
| `glBufferStorage` persistent mapping | none | already optional in `gl_funcs.h` (falls back) |
| S3TC and BC7 (`gx_hires.cpp`) | S3TC extensions likely; BC7 not | S3TC if reported, else decode on the CPU (`gx_bcenc.cpp` has BC encoders) |
| program binary cache | `glGetProgramBinary` is ES 3.0 | works as is |
| HDR (`gx_hdr.cpp`) | excluded on UWP already | later, with a D3D11 swap chain of its own |

Every item is testable one at a time: the renderer logs shader compile errors (`glGetShaderInfoLog`),
and `sms.log` can be read after each run.

## 5. Packaging

The UWP survey downloads the `angle-uwp` artifact (a pipeline resource), links its import libraries,
and puts `libEGL.dll`, `libGLESv2.dll` and `d3dcompiler_47.dll` in the layout next to `sms.exe`. The
manifest needs no new capability. The rising package version keeps `LocalState` (the disc image) on update.

## Order of work

1. Run the ANGLE pipeline; fix what stops it.
2. Probe with an ANGLE context (section 3): the first colour on screen, and the extension list.
3. `gx_uwp_egl.cpp` and `MODE_ANGLE_WINDOW`: a context for the renderer, a black frame, the game's logs.
4. Shaders, then features from section 4, in the order the logs complain.
5. Input (`Windows.Gaming.Input`, `src/uwp/uwp_input.cpp`) and audio (XAudio2, `src/uwp/uwp_audio.cpp`): both done.

## Notes from running on the console

- OpenGL ES 3.0 cannot read depth back with `glReadPixels` (colour formats only). `GXPeekZ`, which the sun's lens-flare
  test uses 17 times a frame, draws the EFB depth texture as colour with the EFB copy shader's Z24X8 mode into the peek
  buffer and reads that (`peekSource`, `depthFromRgba` in `gx_render.cpp`).
- Audio: `ai.cpp` fills its FIFO as on every host and drains it through the same callback the SDL output uses; on UWP
  `uwp_audio.cpp` calls it from an XAudio2 streaming thread (32 kHz stereo, four 512-frame buffers, the mastering voice
  does the rate conversion). The engine is stopped and started around a suspend, and rebuilt if the device is lost.
- Saves and settings live under the app's `LocalState\sms-port` (Device Portal file explorer): `card-a` (the memory
  card), `savestates`, `hud.txt`, and `menu-settings.txt`, the pause menu's changes in its own words (`msaa = 4x`),
  put back when the game is drawing (`SMS_MENU_SAVE`, `port_settings_restore`). `%APPDATA%` in an app container is
  the user's own Roaming folder, which the app cannot write to. Delete `menu-settings.txt` to go back to the defaults.
- The B button is the system's Back on an Xbox. The app marks the back request handled and swallows the gamepad-B key
  events, so B reaches the game; the Xbox button is the way out.
