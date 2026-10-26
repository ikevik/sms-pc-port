// UWP memory probe for the sms-pc-port Xbox build.
//
// The port needs every address the game can see below 4 GiB (docs/64-BIT.md):
// the image, MEM1 at 0x80000000, thread stacks, and the GX register sink at
// 0xCC000000. This app tries each of those inside the UWP sandbox and writes
// the results to <LocalState>\probe.log and a dialog. It runs before any
// window or graphics exists, as the port's first allocations do.

#include <windows.h>
#include <intrin.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <unknwn.h>

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <string>
#include <vector>

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
#ifdef PROBE_ANGLE
void run_angle_probe(winrt::Windows::UI::Core::CoreWindow const& window);  // angle_probe.cpp
#endif
std::string g_extra;  // lines for probe.log only (angle_probe.cpp)

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
    return (uintptr_t)_AddressOfReturnAddress();  // an address on the current stack
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

size_t low_report(const char* tag)
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
    line("[INFO] low range, %s: %zu MiB free, largest %zu MiB", tag, freeTotal >> 20, largest >> 20);
    return freeTotal;
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

    // 1. The image itself (low-base variant: /BASE:0x400000 /HIGHENTROPYVA:NO; ASLR stays on).
    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    result(base < 0x100000000ull, "image below 4 GiB", "module base %p", (void*)base);

    low_report("at start");

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

    low_report("after setup");
}

bool hr_ok(HRESULT hr, const char* what)
{
    if (FAILED(hr)) {
        result(false, what, "HRESULT 0x%08lX", (unsigned long)hr);
        return false;
    }
    return true;
}

// A fresh 128 MiB reserve in the low range, a stack in it, code run on a worker
// thread: what the port does for each new game thread.
void low_stack_test(const char* tag)
{
    char* pool = nullptr;
    for (uintptr_t at = kLowLo; at + kPoolSize <= kLowHi && !pool; at += 0x100000u)
        pool = (char*)VirtualAlloc((void*)at, kPoolSize, MEM_RESERVE, PAGE_NOACCESS);
    if (!pool) {
        result(false, tag, "no free 128 MiB window left in the low range");
        return;
    }
    const size_t kStack = 1u << 20;
    void* s = VirtualAlloc(pool, kStack, MEM_COMMIT, PAGE_READWRITE);
    StackJob job = {s ? (char*)s + kStack : nullptr, 0};
    HANDLE h = s ? CreateThread(nullptr, 0, stack_thread, &job, 0, nullptr) : nullptr;
    if (h) {
        WaitForSingleObject(h, 5000);
        CloseHandle(h);
    }
    result(h && job.rsp >= (uintptr_t)s && job.rsp < (uintptr_t)s + kStack, tag, "pool %p, stack ran at %p", (void*)pool,
           (void*)job.rsp);
    VirtualFree(pool, 0, MEM_RELEASE);
}

// D3D11 device, a 4K swap chain on the CoreWindow and ~512 MiB of textures, then
// the low-address checks again: does the GPU stack take the space the port needs?
void run_gpu_probe(CoreWindow const& window)
{
    line("--- GPU phase (D3D11) ---");
    try {
        const size_t freeBefore = low_report("before D3D11");

        com_ptr<ID3D11Device> dev;
        com_ptr<ID3D11DeviceContext> ctx;
        D3D_FEATURE_LEVEL fl = {};
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2,
                                       D3D11_SDK_VERSION, dev.put(), &fl, ctx.put());
        if (!hr_ok(hr, "D3D11CreateDevice"))
            return;
        result(true, "D3D11 device", "feature level 0x%x", (unsigned)fl);
        low_report("after device");

        com_ptr<IDXGIDevice> dxgiDev = dev.as<IDXGIDevice>();
        com_ptr<IDXGIAdapter> adapter;
        dxgiDev->GetAdapter(adapter.put());
        DXGI_ADAPTER_DESC ad = {};
        adapter->GetDesc(&ad);
        char name[128] = {};
        WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, name, sizeof name, nullptr, nullptr);
        line("[INFO] GPU %s: dedicated %zu MiB, shared %zu MiB", name, (size_t)(ad.DedicatedVideoMemory >> 20),
             (size_t)(ad.SharedSystemMemory >> 20));
        if (auto a3 = adapter.try_as<IDXGIAdapter3>()) {
            DXGI_QUERY_VIDEO_MEMORY_INFO vi = {};
            if (SUCCEEDED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vi)))
                line("[INFO] GPU local budget %llu MiB, in use %llu MiB", vi.Budget >> 20, vi.CurrentUsage >> 20);
        }

        com_ptr<IDXGIFactory2> factory;
        adapter->GetParent(IID_PPV_ARGS(factory.put()));
        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.Width = 3840;
        sd.Height = 2160;
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        com_ptr<IDXGISwapChain1> sc;
        hr = factory->CreateSwapChainForCoreWindow(dev.get(), winrt::get_unknown(window), &sd, nullptr, sc.put());
        if (hr_ok(hr, "swap chain 3840x2160")) {
            com_ptr<ID3D11Texture2D> bb;
            sc->GetBuffer(0, IID_PPV_ARGS(bb.put()));
            com_ptr<ID3D11RenderTargetView> rtv;
            dev->CreateRenderTargetView(bb.get(), nullptr, rtv.put());
            const float green[4] = {0.f, 0.6f, 0.f, 1.f};
            ctx->ClearRenderTargetView(rtv.get(), green);
            result(SUCCEEDED(sc->Present(1, 0)), "swap chain present", "ok");
        }
        low_report("after swap chain");

        // ~512 MiB of 2048x2048 RGBA8 render targets, cleared so the memory is really used.
        std::vector<com_ptr<ID3D11Texture2D>> tex;
        std::vector<com_ptr<ID3D11RenderTargetView>> views;
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = td.Height = 2048;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        for (int i = 0; i < 32; i++) {
            com_ptr<ID3D11Texture2D> t;
            if (FAILED(dev->CreateTexture2D(&td, nullptr, t.put())))
                break;
            com_ptr<ID3D11RenderTargetView> v;
            dev->CreateRenderTargetView(t.get(), nullptr, v.put());
            const float c[4] = {i / 32.f, 0.f, 0.f, 1.f};
            ctx->ClearRenderTargetView(v.get(), c);
            tex.push_back(t);
            views.push_back(v);
        }
        ctx->Flush();
        result(tex.size() >= 32, "GPU textures", "%zu x 16 MiB created", tex.size());

        // CPU-visible mappings: where the driver puts them (the port streams vertices this way).
        D3D11_TEXTURE2D_DESC sdsc = td;
        sdsc.Usage = D3D11_USAGE_STAGING;
        sdsc.BindFlags = 0;
        sdsc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        com_ptr<ID3D11Texture2D> staging;
        D3D11_MAPPED_SUBRESOURCE m = {};
        if (SUCCEEDED(dev->CreateTexture2D(&sdsc, nullptr, staging.put())) &&
            SUCCEEDED(ctx->Map(staging.get(), 0, D3D11_MAP_WRITE, 0, &m))) {
            line("[INFO] staging texture mapped at %p%s", m.pData, (uintptr_t)m.pData < 0x100000000ull ? " (below 4 GiB)" : "");
            ctx->Unmap(staging.get(), 0);
        }
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = 16u << 20;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        com_ptr<ID3D11Buffer> vb;
        if (SUCCEEDED(dev->CreateBuffer(&bd, nullptr, vb.put())) &&
            SUCCEEDED(ctx->Map(vb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            line("[INFO] dynamic buffer mapped at %p%s", m.pData, (uintptr_t)m.pData < 0x100000000ull ? " (below 4 GiB)" : "");
            ctx->Unmap(vb.get(), 0);
        }
        try {
            line("[INFO] app memory usage %llu MiB (limit %llu MiB)", Windows::System::MemoryManager::AppMemoryUsage() >> 20,
                 Windows::System::MemoryManager::AppMemoryUsageLimit() >> 20);
        } catch (...) {
        }
        const size_t freeAfter = low_report("with GPU resources");
        result(freeAfter + (64u << 20) >= freeBefore, "GPU took little low space", "%lld MiB used by D3D11",
               ((long long)freeBefore - (long long)freeAfter) >> 20);
        low_stack_test("new low pool + stack after GPU init");

        views.clear();
        tex.clear();
        staging = nullptr;
        vb = nullptr;
        ctx->ClearState();
        ctx->Flush();
        low_report("after releasing GPU resources");
    } catch (winrt::hresult_error const& e) {
        result(false, "GPU phase", "exception 0x%08X", (unsigned)(int32_t)e.code());
    }
}

void show_page(CoreDispatcher const& d, std::vector<std::wstring> pages, size_t i)
{
    std::wstring title = L"sms uwp-probe " + std::to_wstring(i + 1) + L"/" + std::to_wstring(pages.size());
    Windows::UI::Popups::MessageDialog dlg(pages[i], title);
    dlg.ShowAsync().Completed([d, pages, i](auto const&, auto) {
        if (i + 1 < pages.size())
            d.RunAsync(CoreDispatcherPriority::Normal, [d, pages, i] { show_page(d, pages, i + 1); });
    });
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

        window.Activate();
        window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
#ifdef PROBE_ANGLE
        run_angle_probe(window);  // the render thread keeps drawing behind the dialogs
#else
        run_gpu_probe(window);
#endif
        line("%s: %d check(s) failed", g_fail ? "RESULT FAIL" : "RESULT OK", g_fail);

        try {
            std::wstring path = std::wstring(Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str()) +
                                L"\\probe.log";
            std::ofstream(path, std::ios::binary) << g_report << "\n--- log only ---\n" << g_extra;
        } catch (...) {
        }

        // A dialog clips at about 7 lines on the console, so show the report in pages.
        std::vector<std::wstring> pages;
        std::string cur;
        int n = 0;
        for (char c : g_report) {
            cur += c;
            if (c == '\n' && ++n == 6) {
                pages.emplace_back(cur.begin(), cur.end());
                cur.clear();
                n = 0;
            }
        }
        if (!cur.empty())
            pages.emplace_back(cur.begin(), cur.end());
        show_page(window.Dispatcher(), pages, 0);
        window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessUntilQuit);
    }
};

}  // namespace

// angle_probe.cpp reports through this: page lines go to the dialog pages and the log, the rest to the log only.
void probe_report(const char* text, bool page)
{
    if (page) {
        line("%s", text);
    } else {
        g_extra += text;
        g_extra += '\n';
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    CoreApplication::Run(make<App>());
    return 0;
}
