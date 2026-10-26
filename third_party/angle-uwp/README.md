# ANGLE for UWP x64

Prebuilt `libEGL.dll` and `libGLESv2.dll` (OpenGL ES on Direct3D 11) for the UWP build of the port, with
their import libraries and the EGL, GLES2, GLES3 and KHR headers. ANGLE is BSD-3-Clause licensed by The
ANGLE Project Authors (https://chromium.googlesource.com/angle/angle/+/main/LICENSE).

`d3dcompiler_47.dll`, which ANGLE's Direct3D backend needs at run time, is not here: copy it from the
Windows SDK (`Redist\D3D\x64`) into the package.

## How these were built

By `azure-pipelines-angle.yml` (`tools/ci/build_angle.ps1`), on a windows-2022 runner, at ANGLE commit
`39087422740112ef2f3be57a6af1ed8399459f69` (`ANGLE_REF.txt` has the commit and the gn args):

```
target_os = "winuwp"
target_cpu = "x64"
is_debug = false
is_component_build = false
is_clang = false
use_custom_libcxx = false
symbol_level = 0
```

with `autoninja libEGL libGLESv2`. The script changes the checkout in three ways before building, because
Chromium's build files expect a newer Windows SDK than the runner has and clang-only code:

1. the pinned Windows SDK version (`SDK_VERSION` in `build/toolchain/win/setup_toolchain.py` and
   `build/vs_toolchain.py`) is replaced by the newest complete SDK installed (10.0.26100.0);
2. `NTDDI_WIN11_*` names in `build/config/win/BUILD.gn` that the SDK does not define are replaced by the
   newest it does (`NTDDI_WIN11_GE`);
3. `ScopedSyncDestroy` in `src/libANGLE/EGLSync.h` gets a move constructor, which MSVC needs where clang
   elides a copy.

See `docs/UWP-GRAPHICS.md` for how the port uses them.
