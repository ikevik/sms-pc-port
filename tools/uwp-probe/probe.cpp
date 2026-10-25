// UWP memory probe for the sms-pc-port Xbox build.
//
// The port needs every address the game can see below 4 GiB (docs/64-BIT.md):
// the image, MEM1 at 0x80000000, thread stacks, and the GX register sink at
// 0xCC000000. This app tries each of those inside the UWP sandbox and writes
// the results to <LocalState>\probe.log and a dialog. It runs before any
// window or graphics exists, as the port's first allocations do.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <string>

#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Popups.h>

using namespace winrt;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::UI::Core;

extern "C" uintptr_t probe_call_on_stack(uintptr_t (*fn)(void*), void* arg, void* stackTop);

namespace {

constexpr uintptr_t kMem1At = 0x80000000u;
constexpr size_t kMem1Size = 64u << 20;
constexpr uintptr_t kSinkAt = 0xCC000000u;
constexpr size_t kSinkSize = 0x10000;
constexpr size_t kPoolSize = 128u << 20;
constexpr uintptr_t kLowLo = 0x10000000u;
constexpr uintptr_t kLowHi = 0x80000000u;

std::string g_report;
int g_fail = 0;

void line(const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    g_report += buf;
    g_report += '\n';
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

void result(bool ok, const char* name, const char* fmt, ...)
{
    char buf[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (!ok)
        g_fail++;
    line("[%s] %s: %s", ok ? "PASS" : "FAIL", name, buf);
}

// Fixed-address reserve+commit. Tries VirtualAlloc (what the port calls), then
// VirtualAllocFromApp, and says which one worked.
void* alloc_at(uintptr_t at, size_t size, DWORD type, const char** api)
{
    void* p = VirtualAlloc((void*)at, size, type, PAGE_READWRITE);
    if (p) {
        *api = "VirtualAlloc";
        return p;
    }
    p = VirtualAllocFromApp((void*)at, size, type, PAGE_READWRITE);
    *api = p ? "VirtualAllocFromApp" : "none";
    return p;
}

uintptr_t where_am_i(void*)
{
    volatile int local = 0;
    return (uintptr_t)&local;
}

struct StackJob {
    void* top;
    uintptr_t rsp;
};

DWORD WINAPI stack_thread(void* arg)
{
    StackJob* j = (StackJob*)arg;
    j->rsp = probe_call_on_stack(where_am_i, nullptr, j->top);
    return 0;
}

void survey_low_space()
{
    // How much of 0x10000000..0x80000000 is still free: what the loader, the
    // runtime and the system already took before this code ran.
    uintptr_t a = kLowLo;
    size_t freeTotal = 0, largest = 0;
    while (a < kLowHi) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((void*)a, &mbi, sizeof mbi))
            break;
        uintptr_t end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (end > kLowHi)
            end = kLowHi;
        if (mbi.State == MEM_FREE) {
            size_t n = end - a;
            freeTotal += n;
            if (n > largest)
                largest = n;
        }
        if (end <= a)
            break;
        a = end;
    }
    line("[INFO] low 0x10000000..0x80000000: %zu MiB free, largest block %zu MiB", freeTotal >> 20, largest >> 20);
}

void run_probe()
{
    line("sms uwp-probe, built %s %s", __DATE__, __TIME__);
    line("pointer size: %zu", sizeof(void*));

    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    line("[INFO] user VA range: %p .. %p", si.lpMinimumApplicationAddress, si.lpMaximumApplicationAddress);

    MEMORYSTATUSEX ms = {sizeof ms};
    if (GlobalMemoryStatusEx(&ms))
        line("[INFO] phys total %llu MiB, avail %llu MiB; virtual avail %llu MiB", ms.ullTotalPhys >> 20,
             ms.ullAvailPhys >> 20, ms.ullAvailVirtual >> 20);
    try {
        auto limit = Windows::System::MemoryManager::AppMemoryUsageLimit();
        auto usage = Windows::System::MemoryManager::AppMemoryUsage();
        line("[INFO] app memory limit %llu MiB, usage %llu MiB", limit >> 20, usage >> 20);
    } catch (...) {
        line("[INFO] MemoryManager not available");
    }

    // 1. The image itself (needs a fixed low base: /BASE:0x400000 /DYNAMICBASE:NO).
    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    result(base < 0x100000000ull, "image below 4 GiB", "module base %p", (void*)base);

    survey_low_space();

    // 2. MEM1 at exactly 0x80000000.
    const char* api = "";
    void* mem1 = alloc_at(kMem1At, kMem1Size, MEM_RESERVE | MEM_COMMIT, &api);
    bool mem1ok = mem1 == (void*)kMem1At;
    if (mem1ok) {
        ((volatile uint8_t*)mem1)[0] = 0xA5;
        ((volatile uint8_t*)mem1)[kMem1Size - 1] = 0x5A;
        mem1ok = ((volatile uint8_t*)mem1)[0] == 0xA5 && ((volatile uint8_t*)mem1)[kMem1Size - 1] == 0x5A;
    }
    result(mem1ok, "MEM1 0x80000000 (64 MiB)", "got %p via %s, GetLastError %lu", mem1, api, mem1 ? 0 : GetLastError());

    // 3. Pool below 2 GiB for thread stacks: first free 128 MiB window.
    char* pool = nullptr;
    for (uintptr_t at = kLowLo; at + kPoolSize <= kLowHi && !pool; at += 0x100000u)
        pool = (char*)VirtualAlloc((void*)at, kPoolSize, MEM_RESERVE, PAGE_NOACCESS);
    result(pool != nullptr, "low pool (128 MiB reserve)", "at %p", (void*)pool);

    // 4. GX write-gather register sink at 0xCC000000.
    void* sink = alloc_at(kSinkAt, kSinkSize, MEM_RESERVE | MEM_COMMIT, &api);
    bool sinkok = sink == (void*)kSinkAt;
    if (sinkok) {
        ((volatile uint32_t*)((char*)sink + 0x8000))[0] = 0x12345678;
        sinkok = ((volatile uint32_t*)((char*)sink + 0x8000))[0] == 0x12345678;
    }
    result(sinkok, "GX sink 0xCC000000", "got %p via %s", sink, api);

    // 5/6. Run code on a stack committed from the low pool, on this thread and
    // on a worker thread. Does not test the TEB stack-bounds fix-up the port does.
    if (pool) {
        const size_t kStack = 1u << 20;
        void* s1 = VirtualAlloc(pool, kStack, MEM_COMMIT, PAGE_READWRITE);
        if (s1) {
            uintptr_t rsp = probe_call_on_stack(where_am_i, nullptr, (char*)s1 + kStack);
            result(rsp >= (uintptr_t)s1 && rsp < (uintptr_t)s1 + kStack && rsp < 0x80000000u,
                   "low stack, UI thread", "ran at %p", (void*)rsp);
        } else {
            result(false, "low stack, UI thread", "commit in pool failed, error %lu", GetLastError());
        }
        void* s2 = VirtualAlloc(pool + kStack, kStack, MEM_COMMIT, PAGE_READWRITE);
        if (s2) {
            StackJob job = {(char*)s2 + kStack, 0};
            HANDLE h = CreateThread(nullptr, 0, stack_thread, &job, 0, nullptr);
            if (h) {
                WaitForSingleObject(h, 5000);
                CloseHandle(h);
            }
            result(h && job.rsp >= (uintptr_t)s2 && job.rsp < (uintptr_t)s2 + kStack, "low stack, worker thread",
                   "ran at %p", (void*)job.rsp);
        } else {
            result(false, "low stack, worker thread", "commit in pool failed, error %lu", GetLastError());
        }
    } else {
        result(false, "low stacks", "skipped, no pool");
    }

    survey_low_space();
    line("%s: %d check(s) failed", g_fail ? "RESULT FAIL" : "RESULT OK", g_fail);
}

struct App : implements<App, IFrameworkViewSource, IFrameworkView> {
    CoreWindow window{nullptr};

    IFrameworkView CreateView() { return *this; }
    void Initialize(CoreApplicationView const&) {}
    void Load(hstring const&) {}
    void Uninitialize() {}
    void SetWindow(CoreWindow const& w) { window = w; }

    void Run()
    {
        run_probe();  // before Activate(): the earliest the port would allocate too

        try {
            std::wstring path = std::wstring(Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str()) +
                                L"\\probe.log";
            std::ofstream(path, std::ios::binary) << g_report;
        } catch (...) {
        }

        window.Activate();
        std::wstring text(g_report.begin(), g_report.end());
        Windows::UI::Popups::MessageDialog dlg(text, L"sms uwp-probe");
        dlg.ShowAsync();
        window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessUntilQuit);
    }
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    CoreApplication::Run(make<App>());
    return 0;
}
