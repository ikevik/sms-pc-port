cmake_minimum_required(VERSION 3.20)
project(uwp_probe C CXX ASM_MASM)

# PROBE_FIXED_BASE=ON links the image at 0x400000 with ASLR off, as the Windows
# build of the port does (CMakeLists.txt in the repo root). OFF leaves the
# default dynamic base, so the other checks still run if the loader refuses
# the fixed image.
option(PROBE_FIXED_BASE "Link at 0x400000 with /DYNAMICBASE:NO" ON)

if(PROBE_FIXED_BASE)
  set(PROBE_ID    "SmsUwpProbeFixed")
  set(PROBE_TITLE "SMS UWP Probe (fixed base)")
else()
  set(PROBE_ID    "SmsUwpProbeDynamic")
  set(PROBE_TITLE "SMS UWP Probe (dynamic base)")
endif()

# Static CRT: no Microsoft.VCLibs framework package to sideload on the console.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(uwp-probe WIN32 probe.cpp stack_switch.asm)
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

if(PROBE_FIXED_BASE)
  target_link_options(uwp-probe PRIVATE /BASE:0x400000 /DYNAMICBASE:NO /HIGHENTROPYVA:NO)
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
