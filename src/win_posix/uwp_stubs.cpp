// Stand-ins for the parts of the platform layer that need desktop-only Windows APIs, for the
// UWP target (SMS_UWP). CMake leaves out platform/os/windows_crash.cpp (a crash reporter that
// starts a helper process and installs vectored handlers) and platform/pad/gc_adapter.cpp (the
// GameCube USB adapter, opened with CreateFileW) there, and builds this in their place.
#include <windows.h>

#include <stdio.h>

#include "os/crash.h"
#include "pad/gc_adapter.h"

void port_windows_report_current(const char* what)
{
	char text[256];
	snprintf(text, sizeof text, "[port] fatal: %s\n", what);
	OutputDebugStringA(text);
	fputs(text, stderr);
}

void port_install_windows_exception_logger() {}

void gc_adapter_start() {}
bool gc_adapter_read(GcAdapterInput*) { return false; }
bool gc_adapter_read_second(GcAdapterInput*) { return false; }
void gc_adapter_rumble(bool) {}
void gc_adapter_flush_rumble() {}
