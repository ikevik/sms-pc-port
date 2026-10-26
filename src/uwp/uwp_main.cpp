// WinRT entry for the UWP build (SMS_UWP). A UWP app starts through CoreApplication, not main().
// This runs the game's own main (renamed sms_game_main in port_main.cpp) on a thread, keeps the
// window's event loop going, and puts the game's stderr and stdout in LocalState\sms.log.
//
// What the game needs, placed in the app's LocalState folder (Device Portal file explorer):
//   game.iso (or game.gcm, game.ciso)   the GMSE01 Rev 0 disc image, passed as SMS_DISC_IMAGE
//   args.txt (optional)                 one command-line argument per line
//
// There is no window, audio or controller backend yet (SDL2 has no clang-buildable WinRT port),
// so this shows what the game's code does on the console before any of those exist.
#include <windows.h>

#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Popups.h>

#include <stdio.h>
#include <stdlib.h>

#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace winrt;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::UI::Core;

int sms_game_main(int argc, char** argv);
extern "C" unsigned int VIGetRetraceCount(void);  // platform/vi/vi.cpp
extern "C" unsigned int GXPC_FrameCount(void);    // platform/gx/src/gx_platform.cpp: pictures presented
extern "C" int GXPC_UwpAttachWindow(void* coreWindow);       // platform/gx/src/gx_uwp_egl.cpp

namespace {

std::string utf8(std::wstring const& w)
{
    if (w.empty())
        return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

bool exists(std::string const& path)
{
    std::ifstream f(path, std::ios::binary);
    return f.good();
}

std::vector<std::string> g_args;
std::vector<char*> g_argv;

struct App : implements<App, IFrameworkViewSource, IFrameworkView> {
    CoreWindow window{nullptr};

    IFrameworkView CreateView() { return *this; }
    void Initialize(CoreApplicationView const&) {}
    void Load(hstring const&) {}
    void Uninitialize() {}
    void SetWindow(CoreWindow const& w) { window = w; }

    void Run()
    {
        window.Activate();
        auto dispatcher = window.Dispatcher();

        const std::string dir = utf8(std::wstring(Windows::Storage::ApplicationData::Current().LocalFolder().Path()));
        FILE* log = nullptr;
        freopen_s(&log, (dir + "\\sms.log").c_str(), "w", stderr);
        freopen_s(&log, (dir + "\\sms.out.log").c_str(), "w", stdout);
        setvbuf(stderr, nullptr, _IONBF, 0);
        fprintf(stderr, "[uwp] LocalState: %s\n[uwp] image base %p\n", dir.c_str(), (void*)GetModuleHandleW(nullptr));

        for (const char* name : {"game.iso", "game.gcm", "game.ciso"}) {
            if (exists(dir + "\\" + name)) {
                _putenv_s("SMS_DISC_IMAGE", (dir + "\\" + name).c_str());
                fprintf(stderr, "[uwp] disc image: %s\n", name);
                break;
            }
        }
        // ANGLE's window surface has to be made here, on the UI thread that owns the CoreWindow; the renderer takes
        // the context over on the game's GL thread later.
        const int attached = GXPC_UwpAttachWindow(winrt::get_abi(window));
        fprintf(stderr, "[uwp] ANGLE window context: %s\n", attached ? "ready" : "not created (see above)");

        // env.txt (optional): NAME=VALUE lines set as environment variables for the game, for trying its settings
        // without a new build, e.g. SMS_GX_SHADER_WARMUP=off or SMS_GX_SCALE=1.
        {
            std::ifstream env(dir + "\\env.txt");
            for (std::string line; std::getline(env, line);) {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                    line.pop_back();
                const size_t eq = line.find('=');
                if (eq == std::string::npos || eq == 0 || line[0] == '#')
                    continue;
                _putenv_s(line.substr(0, eq).c_str(), line.substr(eq + 1).c_str());
                fprintf(stderr, "[uwp] env.txt: %s\n", line.c_str());
            }
        }
        g_args.push_back("sms");
        std::ifstream args(dir + "\\args.txt");
        for (std::string line; std::getline(args, line);) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (!line.empty())
                g_args.push_back(line);
        }
        for (auto& a : g_args)
            g_argv.push_back(&a[0]);
        g_argv.push_back(nullptr);

        std::thread([dispatcher] {
            fprintf(stderr, "[uwp] starting the game\n");
            const int rc = sms_game_main((int)g_args.size(), g_argv.data());
            fprintf(stderr, "[uwp] the game's main returned %d\n", rc);
            dispatcher.RunAsync(CoreDispatcherPriority::Normal, [rc] {
                std::wstring text = L"The game's main returned " + std::to_wstring(rc) + L". See sms.log in the app's LocalState folder.";
                Windows::UI::Popups::MessageDialog(text, L"sms").ShowAsync();
            });
        }).detach();

        // A heartbeat, so the log shows whether the game keeps running (its retrace count moves) and how
        // much of the app's memory limit it uses, long after the last line the game prints.
        std::thread([] {
            unsigned last = VIGetRetraceCount();
            unsigned lastFrames = GXPC_FrameCount();
            for (int i = 1;; i++) {
                Sleep(5000);
                const unsigned now = VIGetRetraceCount();
                const unsigned frames = GXPC_FrameCount();
                unsigned long long usage = 0, limit = 0;
                try {
                    usage = Windows::System::MemoryManager::AppMemoryUsage() >> 20;
                    limit = Windows::System::MemoryManager::AppMemoryUsageLimit() >> 20;
                } catch (...) {
                }
                fprintf(stderr, "[uwp] %4d s: retrace %u (+%u), pictures %u (+%u), memory %llu of %llu MiB\n", i * 5, now, now - last,
                        frames, frames - lastFrames, usage, limit);
                last = now;
                lastFrames = frames;
            }
        }).detach();

        dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessUntilQuit);
    }
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    CoreApplication::Run(make<App>());
    return 0;
}
