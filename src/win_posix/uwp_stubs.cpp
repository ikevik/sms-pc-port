// Stand-ins for the parts of the platform layer that need desktop-only Windows APIs, for the
// UWP target (SMS_UWP). CMake leaves out platform/os/windows_crash.cpp (a crash reporter that
// starts a helper process and installs vectored handlers) and platform/pad/gc_adapter.cpp (the
// GameCube USB adapter, opened with CreateFileW) there, and builds this in their place.
#include <windows.h>

#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exception>

#include "os/crash.h"
#include "pad/gc_adapter.h"

namespace {

// The log is the game's stderr (a file in LocalState, opened by uwp_main.cpp). A crash report writes to its handle
// directly: the C runtime's stream may be locked by the thread that faulted.
HANDLE s_logHandle;
volatile LONG s_faultReports = 30;
volatile LONG s_throwReports = 3;
volatile LONG s_reporting;

void put(const char* text)
{
    OutputDebugStringA(text);
    if (s_logHandle && s_logHandle != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(s_logHandle, text, (DWORD)strlen(text), &written, nullptr);
    } else {
        fputs(text, stderr);
    }
}

// "sms.exe+0x1234" for an address in a loaded module, else the bare address. The offset is the RVA: the link map
// (sms.map in the build's artifact) lists symbols by RVA.
void describe(char* out, size_t size, uintptr_t address)
{
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)address,
                           &module) && module) {
        wchar_t path[MAX_PATH];
        const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
        const wchar_t* name = path;
        for (DWORD i = 0; i < length && i < MAX_PATH; i++)
            if (path[i] == L'\\')
                name = path + i + 1;
        snprintf(out, size, "%ls+0x%llx", length ? name : L"?", (unsigned long long)(address - (uintptr_t)module));
    } else {
        snprintf(out, size, "0x%llx", (unsigned long long)address);
    }
}

typedef USHORT(NTAPI* CaptureBackTrace)(ULONG, ULONG, PVOID*, PULONG);
typedef void(NTAPI* CaptureContext)(PCONTEXT);

// ntdll's, looked up by name: the import library for a UWP app does not list everything ntdll has, and a lookup does
// not need a lock the way a function-local static does (a fault can come with the loader's lock held).
CaptureBackTrace s_capture;
CaptureContext s_captureContext;

CaptureBackTrace captureBackTrace()
{
    if (!s_capture)
        s_capture = (CaptureBackTrace)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlCaptureStackBackTrace");
    return s_capture;
}

void backtrace(const CONTEXT* context)
{
    char line[256], where[200];
    if (CaptureBackTrace capture = captureBackTrace()) {
        void* frames[40];
        const USHORT n = capture(0, 40, frames, nullptr);
        snprintf(line, sizeof line, "[port] backtrace (%u frames, the handler first):\n", (unsigned)n);
        put(line);
        for (USHORT i = 0; i < n; i++) {
            describe(where, sizeof where, (uintptr_t)frames[i]);
            snprintf(line, sizeof line, "[port]   #%u %s\n", (unsigned)i, where);
            put(line);
        }
    }
    // The unwinder stops at code without unwind data; the words on the stack that point into a module are the
    // return addresses it would have found.
    if (context) {
        const uintptr_t sp = (uintptr_t)context->Rsp;
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery((void*)sp, &info, sizeof info) && info.State == MEM_COMMIT) {
            const uintptr_t end = (uintptr_t)info.BaseAddress + info.RegionSize;
            const uintptr_t stop = sp + 4096 < end ? sp + 4096 : end;
            put("[port] stack words in a module:\n");
            int shown = 0;
            for (uintptr_t at = sp; at + sizeof(uintptr_t) <= stop && shown < 24; at += sizeof(uintptr_t)) {
                const uintptr_t word = *(const uintptr_t*)at;
                HMODULE module = nullptr;
                if (word > 0x10000 && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                         (LPCWSTR)word, &module) && module) {
                    describe(where, sizeof where, word);
                    snprintf(line, sizeof line, "[port]   sp+0x%llx %s\n", (unsigned long long)(at - sp), where);
                    put(line);
                    shown++;
                }
            }
        }
    }
}

void report(const EXCEPTION_POINTERS* exception, const char* what)
{
    if (InterlockedCompareExchange(&s_reporting, 1, 0))
        return;
    const EXCEPTION_RECORD* record = exception->ExceptionRecord;
    const CONTEXT* c = exception->ContextRecord;
    char line[512], where[200];
    describe(where, sizeof where, (uintptr_t)record->ExceptionAddress);
    snprintf(line, sizeof line, "\n[port] %s: exception 0x%08lx at %s, thread %lu\n", what, record->ExceptionCode, where,
             GetCurrentThreadId());
    put(line);
    if ((record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
        record->NumberParameters >= 2) {
        snprintf(line, sizeof line, "[port] access %llu (0 read, 1 write, 8 execute) at 0x%llx\n",
                 (unsigned long long)record->ExceptionInformation[0], (unsigned long long)record->ExceptionInformation[1]);
        put(line);
    }
    if (c) {
        snprintf(line, sizeof line,
                 "[port] rip %016llx rsp %016llx rbp %016llx\n[port] rax %016llx rbx %016llx rcx %016llx rdx %016llx\n"
                 "[port] rsi %016llx rdi %016llx r8  %016llx r9  %016llx\n[port] r10 %016llx r11 %016llx r12 %016llx r13 %016llx\n"
                 "[port] r14 %016llx r15 %016llx\n",
                 c->Rip, c->Rsp, c->Rbp, c->Rax, c->Rbx, c->Rcx, c->Rdx, c->Rsi, c->Rdi, c->R8, c->R9, c->R10, c->R11, c->R12,
                 c->R13, c->R14, c->R15);
        put(line);
    }
    backtrace(c);
    InterlockedExchange(&s_reporting, 0);
}

LONG CALLBACK onException(PEXCEPTION_POINTERS exception)
{
    const DWORD code = exception->ExceptionRecord->ExceptionCode;
    if (code == 0xE06D7363) {  // a C++ throw: usually caught, so only the first few are worth a line
        if (InterlockedDecrement(&s_throwReports) >= 0)
            report(exception, "C++ throw");
    } else if (code == 0x40010006 || code == 0x4001000A || code == 0x406D1388 || code == 0x80000004 || code == 0x40010005 ||
               code == 0xE0434352) {
        // debug-print, thread-naming, single-step and control-C exceptions: routine, not reported
    } else if ((code & 0xC0000000) == 0xC0000000 || (code & 0xC0000000) == 0x80000000 || code >= 0xE0000000) {
        // faults, guard page, stack overflow, heap corruption, and the breakpoint (0x80000003) that ends a process without a
        // debugger: anything with a warning or error severity
        if (InterlockedDecrement(&s_faultReports) >= 0)
            report(exception, "fault");
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI onUnhandled(PEXCEPTION_POINTERS exception)
{
    put("[port] unhandled exception: the process ends\n");
    InterlockedExchange(&s_reporting, 0);
    report(exception, "unhandled");
    return EXCEPTION_CONTINUE_SEARCH;
}

// The C runtime ends a process at once, with no exception, for a call it finds invalid (a closed file descriptor, a
// bad mode string, a format it rejects): the vectored handler never sees it. Logging it and returning lets the call fail
// with EINVAL, as it does in the desktop build's runtime.
void __cdecl onInvalidParameter(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned line, uintptr_t)
{
    char text[600];
    snprintf(text, sizeof text, "\n[port] the C runtime rejected a call: %ls in %ls (%ls:%u), thread %lu\n", expression ? expression : L"?",
             function ? function : L"?", file ? file : L"?", line, GetCurrentThreadId());
    put(text);
    backtrace(nullptr);
}

void onTerminate()
{
    put("\n[port] std::terminate: an exception nothing caught, or a throw from a noexcept function\n");
    backtrace(nullptr);
    abort();
}

}  // namespace

// The backtrace of the calling thread as text, for diagnostics elsewhere (the GL debug messages).
extern "C" void port_uwp_backtrace_text(char* out, size_t size, int skip, int count)
{
    out[0] = 0;
    CaptureBackTrace capture = captureBackTrace();
    if (!capture)
        return;
    void* frames[32];
    if (count > 32)
        count = 32;
    const USHORT n = capture((ULONG)skip, (ULONG)count, frames, nullptr);
    size_t used = 0;
    for (USHORT i = 0; i < n && used + 64 < size; i++) {
        char where[96];
        describe(where, sizeof where, (uintptr_t)frames[i]);
        used += snprintf(out + used, size - used, "%s%s", i ? " < " : "", where);
    }
}

void port_windows_report_current(const char* what)
{
    CONTEXT context = {};
    if (!s_captureContext)
        s_captureContext = (CaptureContext)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlCaptureContext");
    if (s_captureContext)
        s_captureContext(&context);
    char text[256];
    snprintf(text, sizeof text, "\n[port] fatal: %s in thread %lu\n", what, GetCurrentThreadId());
    put(text);
    backtrace(context.Rsp ? &context : nullptr);
}

// A UWP process that faults ends at once, with no report; this puts the fault (address, registers, the stack's return
// addresses) in the log first.
void port_install_windows_exception_logger()
{
    static bool installed;
    if (installed)
        return;
    installed = true;
    s_logHandle = (HANDLE)_get_osfhandle(_fileno(stderr));
    typedef PVOID(WINAPI * AddVectored)(ULONG, PVECTORED_EXCEPTION_HANDLER);
    typedef LPTOP_LEVEL_EXCEPTION_FILTER(WINAPI * SetUnhandled)(LPTOP_LEVEL_EXCEPTION_FILTER);
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    AddVectored add = (AddVectored)GetProcAddress(kernel, "AddVectoredExceptionHandler");
    if (!add)
        add = (AddVectored)GetProcAddress(ntdll, "RtlAddVectoredExceptionHandler");
    SetUnhandled setUnhandled = (SetUnhandled)GetProcAddress(kernel, "SetUnhandledExceptionFilter");
    _set_invalid_parameter_handler(onInvalidParameter);
    std::set_terminate(onTerminate);
    const bool vectored = add && add(1, onException);
    if (setUnhandled)
        setUnhandled(onUnhandled);
    fprintf(stderr, "[port] UWP exception logger: %s, %s\n", vectored ? "vectored handler on" : "no vectored handler",
            setUnhandled ? "unhandled-exception filter on" : "no unhandled-exception filter");
}

void gc_adapter_start() {}
bool gc_adapter_read(GcAdapterInput*) { return false; }
bool gc_adapter_read_second(GcAdapterInput*) { return false; }
void gc_adapter_rumble(bool) {}
void gc_adapter_flush_rumble() {}

// The extension overlay (text panels drawn over the frame) is built with the SDL2 window code,
// which UWP does not have yet; multiplayer and co-op call this one function directly.
extern "C" void GXPC_ExtOverlaySet(int, const char*) {}
