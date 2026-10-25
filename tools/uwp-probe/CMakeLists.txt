cmake_minimum_required(VERSION 3.20)
project(uwp_probe C CXX ASM_MASM)

# The Windows build of the port links at 0x400000 with ASLR off, but UWP's
# /APPCONTAINER rejects /DYNAMICBASE:NO (LNK1295). PROBE_LOW_BASE=ON keeps ASLR
# and sets /HIGHENTROPYVA:NO, which should make the loader place the image below
# 4 GiB at a random address. OFF is the default high-entropy image, as a control.
option(PROBE_LOW_BASE "Link with /BASE:0x400000 /HIGHENTROPYVA:NO" ON)

if(PROBE_LOW_BASE)
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

# Loose package layout for makeappx: the exe, AppxManifest.xml and Assets/.
configure_file(AppxManifest.xml.in ${CMAKE_BINARY_DIR}/AppxManifest.xml @ONLY)
set(PROBE_LAYOUT ${CMAKE_BINARY_DIR}/layout)
add_custom_command(TARGET uwp-probe POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${PROBE_LAYOUT}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:uwp-probe> ${PROBE_LAYOUT}/uwp-probe.exe
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/AppxManifest.xml ${PROBE_LAYOUT}/AppxManifest.xml
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/Assets ${PROBE_LAYOUT}/Assets
  VERBATIM)
