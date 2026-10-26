cmake_minimum_required(VERSION 3.20)
project(uwp_probe C CXX ASM_MASM)

# The Windows build of the port links at 0x400000 with ASLR off, but UWP's
# /APPCONTAINER rejects /DYNAMICBASE:NO (LNK1295). PROBE_LOW_BASE=ON keeps ASLR
# and sets /HIGHENTROPYVA:NO, which should make the loader place the image below
# 4 GiB at a random address. OFF is the default high-entropy image, as a control.
option(PROBE_LOW_BASE "Link with /BASE:0x400000 /HIGHENTROPYVA:NO" ON)
# PROBE_ANGLE=ON replaces the D3D11 phase with ANGLE (third_party/angle-uwp): an OpenGL ES context on the
# CoreWindow, its version, limits, extensions, shader compiles and frame rate.
option(PROBE_ANGLE "Probe ANGLE (OpenGL ES on D3D11) instead of plain D3D11" OFF)

if(PROBE_ANGLE)
  set(PROBE_ID    "SmsUwpProbeAngle")
  set(PROBE_TITLE "SMS UWP Probe (ANGLE)")
elseif(PROBE_LOW_BASE)
  set(PROBE_ID    "SmsUwpProbeLow")
  set(PROBE_TITLE "SMS UWP Probe (low base)")
else()
  set(PROBE_ID    "SmsUwpProbeDefault")
  set(PROBE_TITLE "SMS UWP Probe (default base)")
endif()

# UWP rejects the static CRT (MSB8024), so the app uses the DLL CRT and the manifest
# depends on the Microsoft.VCLibs.140.00 framework package.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

add_executable(uwp-probe WIN32 probe.cpp stack_switch.asm)
target_link_libraries(uwp-probe PRIVATE d3d11 dxgi)
target_compile_features(uwp-probe PRIVATE cxx_std_17)
target_compile_options(uwp-probe PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/permissive->)

# C++/WinRT headers ship with the Windows SDK; add the folder when the VS
# include path does not already have it.
if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
  set(_sdk "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt")
  if(EXISTS "${_sdk}")
    target_include_directories(uwp-probe SYSTEM PRIVATE "${_sdk}")
  endif()
endif()

if(PROBE_LOW_BASE)
  target_link_options(uwp-probe PRIVATE /BASE:0x400000 /HIGHENTROPYVA:NO)
endif()

if(PROBE_ANGLE)
  set(ANGLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/angle-uwp)
  target_sources(uwp-probe PRIVATE angle_probe.cpp)
  target_compile_definitions(uwp-probe PRIVATE PROBE_ANGLE=1)
  target_include_directories(uwp-probe PRIVATE ${ANGLE_DIR}/include)
  target_link_libraries(uwp-probe PRIVATE ${ANGLE_DIR}/lib/libEGL.dll.lib ${ANGLE_DIR}/lib/libGLESv2.dll.lib)
  # ANGLE's Direct3D backend compiles shaders with the D3D compiler, which the app has to carry.
  find_file(PROBE_D3DCOMPILER d3dcompiler_47.dll PATHS "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/Redist/D3D/x64" NO_DEFAULT_PATH)
  if(NOT PROBE_D3DCOMPILER)
    message(WARNING "d3dcompiler_47.dll not found in the Windows SDK redistributables")
  endif()
endif()

# Loose package layout for makeappx: the exe, AppxManifest.xml and Assets/.
configure_file(AppxManifest.xml.in ${CMAKE_BINARY_DIR}/AppxManifest.xml @ONLY)
set(PROBE_LAYOUT ${CMAKE_BINARY_DIR}/layout)
add_custom_command(TARGET uwp-probe POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${PROBE_LAYOUT}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:uwp-probe> ${PROBE_LAYOUT}/uwp-probe.exe
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/AppxManifest.xml ${PROBE_LAYOUT}/AppxManifest.xml
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/Assets ${PROBE_LAYOUT}/Assets
  VERBATIM)
if(PROBE_ANGLE)
  add_custom_command(TARGET uwp-probe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${ANGLE_DIR}/bin/libEGL.dll ${PROBE_LAYOUT}/libEGL.dll
    COMMAND ${CMAKE_COMMAND} -E copy ${ANGLE_DIR}/bin/libGLESv2.dll ${PROBE_LAYOUT}/libGLESv2.dll
    VERBATIM)
  if(PROBE_D3DCOMPILER)
    add_custom_command(TARGET uwp-probe POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy ${PROBE_D3DCOMPILER} ${PROBE_LAYOUT}/d3dcompiler_47.dll
      VERBATIM)
  endif()
endif()
