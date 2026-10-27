// XAudio2 output for the UWP build (SMS_UWP). platform/audio/ai.cpp keeps the mixed audio in a FIFO and drains it through
// a callback that fills 16-bit stereo at 32 kHz (the SDL audio callback on the other hosts). This opens the console's
// default output and calls that callback from a thread of its own, one buffer at a time.
//
// The device can go away under a UWP app: the console is suspended, or the output changes. The engine says so
// (OnCriticalError) or the next submit fails, and the output is built again; the game's audio clock keeps running on the
// host's clock while there is no device, so JAudio's thread does not stall waiting for a DMA block.
#include <windows.h>
#include <xaudio2.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

typedef void (*SmsAudioFill)(void* user, uint8_t* stream, int bytes);

namespace {

const UINT32 kFrames = 512;  // frames in a buffer: 16 ms at 32 kHz
const int kBuffers = 4;      // buffers queued ahead of the device: 64 ms

struct Output {
    SmsAudioFill fill = nullptr;
    unsigned rate = 32000;
    HANDLE wake = nullptr;            // a buffer finished, the engine failed, or the app asks for a look
    std::atomic<bool> abandoned{false};  // the game stopped waiting for the first answer
    std::atomic<bool> lost{false};
    std::atomic<long> lostCode{0};
    std::mutex mu;                    // guards the answer below
    std::condition_variable cv;
    bool answered = false;
    bool opened = false;
    char error[256] = "";
    std::mutex engineMu;              // guards engine, for suspend and resume from the UI thread
    IXAudio2* engine = nullptr;
    int underruns = 0;
} g;

int16_t s_pcm[kBuffers][kFrames * 2];

struct VoiceCallback : IXAudio2VoiceCallback {
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnStreamEnd() override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}
    void STDMETHODCALLTYPE OnBufferEnd(void*) override { SetEvent(g.wake); }
    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}
} s_voiceCallback;

struct EngineCallback : IXAudio2EngineCallback {
    void STDMETHODCALLTYPE OnProcessingPassStart() override {}
    void STDMETHODCALLTYPE OnProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnCriticalError(HRESULT error) override
    {
        g.lostCode = (long)error;
        g.lost = true;
        SetEvent(g.wake);
    }
} s_engineCallback;

struct Device {
    IXAudio2* xa = nullptr;
    IXAudio2MasteringVoice* master = nullptr;
    IXAudio2SourceVoice* src = nullptr;
    int next = 0;  // the buffer to fill next: the oldest one, which the device has finished with

    HRESULT submit()
    {
        g.fill(nullptr, (uint8_t*)s_pcm[next], (int)(kFrames * 4));
        XAUDIO2_BUFFER b = {};
        b.AudioBytes = kFrames * 4;
        b.pAudioData = (const BYTE*)s_pcm[next];
        const HRESULT hr = src->SubmitSourceBuffer(&b);
        next = (next + 1) % kBuffers;
        return hr;
    }

    HRESULT open(const char** step)
    {
        *step = "XAudio2Create";
        HRESULT hr = XAudio2Create(&xa, 0, XAUDIO2_DEFAULT_PROCESSOR);
        if (FAILED(hr))
            return hr;
        xa->RegisterForCallbacks(&s_engineCallback);
        *step = "CreateMasteringVoice";
        // Two channels in: the engine mixes them to whatever the output has (stereo, or the front pair of a surround set).
        hr = xa->CreateMasteringVoice(&master, 2, XAUDIO2_DEFAULT_SAMPLERATE, 0, nullptr, nullptr, AudioCategory_GameMedia);
        if (FAILED(hr))
            return hr;
        *step = "CreateSourceVoice";
        WAVEFORMATEX wf = {};
        wf.wFormatTag = WAVE_FORMAT_PCM;
        wf.nChannels = 2;
        wf.nSamplesPerSec = g.rate;
        wf.wBitsPerSample = 16;
        wf.nBlockAlign = 4;
        wf.nAvgBytesPerSec = g.rate * 4;
        hr = xa->CreateSourceVoice(&src, &wf, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &s_voiceCallback);
        if (FAILED(hr))
            return hr;
        *step = "SubmitSourceBuffer";
        for (int i = 0; i < kBuffers; i++) {
            hr = submit();
            if (FAILED(hr))
                return hr;
        }
        *step = "Start";
        hr = src->Start(0);
        if (FAILED(hr))
            return hr;
        {
            std::lock_guard<std::mutex> l(g.engineMu);
            g.engine = xa;
        }
        return S_OK;
    }

    void describe()
    {
        XAUDIO2_VOICE_DETAILS vd = {};
        master->GetVoiceDetails(&vd);
        DWORD mask = 0;
        master->GetChannelMask(&mask);
        fprintf(stderr, "[audio] XAudio2 output: %u Hz stereo in %d x %u-frame buffers; the device mixes at %u Hz (speakers 0x%lx)\n",
                g.rate, kBuffers, (unsigned)kFrames, (unsigned)vd.InputSampleRate, (unsigned long)mask);
    }

    void close()
    {
        {
            std::lock_guard<std::mutex> l(g.engineMu);
            g.engine = nullptr;
        }
        if (src) {
            src->Stop(0);
            src->DestroyVoice();
        }
        if (master)
            master->DestroyVoice();
        if (xa)
            xa->Release();
        xa = nullptr;
        master = nullptr;
        src = nullptr;
        next = 0;
    }

    // Keeps the buffers full until the device is lost or the game stops waiting.
    void run()
    {
        for (;;) {
            WaitForSingleObjectEx(g.wake, 20, FALSE);
            if (g.lost.load())
                return;
            XAUDIO2_VOICE_STATE st = {};
            src->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if (st.BuffersQueued == 0 && g.underruns < 5)
                fprintf(stderr, "[audio] XAudio2 ran out of buffers (%d)\n", ++g.underruns);
            while (st.BuffersQueued < (UINT32)kBuffers) {
                const HRESULT hr = submit();
                if (FAILED(hr)) {
                    g.lostCode = (long)hr;
                    g.lost = true;
                    return;
                }
                st.BuffersQueued++;
            }
        }
    }
};

// No device: the buffers the game's audio thread asks for are still taken, at the pace a device would.
void drainFor(unsigned ms)
{
    static int16_t tmp[kFrames * 2];
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    auto at = std::chrono::steady_clock::now();
    while (at < end) {
        at += std::chrono::microseconds((long long)kFrames * 1000000 / g.rate);
        g.fill(nullptr, (uint8_t*)tmp, (int)(kFrames * 4));
        std::this_thread::sleep_until(at);
    }
}

void audioThread()
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // already set up differently is fine
    bool first = true;
    for (;;) {
        Device d;
        const char* step = "";
        g.lost = false;  // before the open: an engine error during it is then seen by run
        const HRESULT hr = d.open(&step);
        if (first) {
            first = false;
            // The first answer is given under the lock the game's wait for it holds, so a run the game gave up on
            // (abandoned) is never also answered: the host clock paces its audio instead.
            std::unique_lock<std::mutex> l(g.mu);
            const bool late = g.abandoned.load();
            if (late || FAILED(hr)) {
                if (!late) {
                    snprintf(g.error, sizeof g.error, "XAudio2 %s failed: 0x%08lx", step, (unsigned long)hr);
                    g.answered = true;
                    g.cv.notify_all();
                }
                l.unlock();
                d.close();
                if (late)
                    fprintf(stderr, "[audio] the XAudio2 output answered too late: this run stays without sound\n");
                return;
            }
            g.answered = g.opened = true;
            g.cv.notify_all();
            l.unlock();
            d.describe();
        } else if (FAILED(hr)) {
            fprintf(stderr, "[audio] XAudio2 %s failed: 0x%08lx; trying again\n", step, (unsigned long)hr);
            d.close();
            drainFor(500);
            continue;
        } else {
            fprintf(stderr, "[audio] XAudio2 output is back\n");
        }
        d.run();
        fprintf(stderr, "[audio] XAudio2 output lost (0x%08lx); opening it again\n", (unsigned long)g.lostCode.load());
        d.close();
        drainFor(250);
    }
}

}  // namespace

// Opens the output and starts feeding it from fill; false with a reason in error when there is no device, or none
// answered within timeoutSeconds (0 waits for ever). Called by platform/audio/ai.cpp.
extern "C" int sms_uwp_audio_open(SmsAudioFill fill, unsigned rate, int timeoutSeconds, char* error, size_t errorSize)
{
    g.fill = fill;
    g.rate = rate;
    g.wake = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    std::thread(audioThread).detach();
    std::unique_lock<std::mutex> l(g.mu);
    if (timeoutSeconds > 0)
        g.cv.wait_for(l, std::chrono::seconds(timeoutSeconds), [] { return g.answered; });
    else
        g.cv.wait(l, [] { return g.answered; });
    if (!g.answered) {
        g.abandoned = true;
        snprintf(error, errorSize, "no answer from XAudio2 after %d s: the game goes on without sound", timeoutSeconds);
        return 0;
    }
    if (!g.opened) {
        snprintf(error, errorSize, "%s", g.error);
        return 0;
    }
    return 1;
}

// The app is suspended or resumed: XAudio2 asks for its engine to be stopped and started around it.
extern "C" void sms_uwp_audio_suspend()
{
    std::lock_guard<std::mutex> l(g.engineMu);
    if (g.engine)
        g.engine->StopEngine();
}

extern "C" void sms_uwp_audio_resume()
{
    std::lock_guard<std::mutex> l(g.engineMu);
    if (g.engine) {
        const HRESULT hr = g.engine->StartEngine();
        if (FAILED(hr)) {
            g.lostCode = (long)hr;
            g.lost = true;
            SetEvent(g.wake);
        }
    }
}
