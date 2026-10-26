// The Xbox controller for the UWP build (Windows.Gaming.Input), as the SDL game controller events the port's pad code
// (platform/pad/pad.cpp) reads: it takes SDL_CONTROLLERAXISMOTION, _BUTTONDOWN, _BUTTONUP and _DEVICEREMOVED through
// sms_gx_set_event_callback, which the graphics layer calls on every pad read (sms_gx_pump_events). Polling the
// gamepads from the game's thread is allowed: only the CoreWindow needs the UI thread.
#include <windows.h>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Gaming.Input.h>

#include <algorithm>
#include <cstdint>

using namespace winrt::Windows::Gaming::Input;

extern "C" void GXPC_UwpSetInput(void (*poll)(void (*emit)(const void*)), int (*rumble)(unsigned short));  // gx_uwp_egl.cpp

namespace {

// The layouts pad.cpp's namespace sdl declares (SDL_gamecontroller.h's events).
struct ControllerAxisEvent {
    uint32_t type, timestamp;
    int32_t which;
    uint8_t axis, p1, p2, p3;
    int16_t value;
    uint16_t p4;
};
struct ControllerButtonEvent {
    uint32_t type, timestamp;
    int32_t which;
    uint8_t button, state, p1, p2;
};
struct ControllerDeviceEvent {
    uint32_t type, timestamp;
    int32_t which;
};
enum : uint32_t { CAXIS = 0x650, CBUTTONDOWN = 0x651, CBUTTONUP = 0x652, CDEVICEREMOVED = 0x654 };

// SDL_CONTROLLER_BUTTON_* for each GamepadButtons flag.
struct ButtonMap {
    uint32_t flag;
    uint8_t sdl;
};
const ButtonMap kButtons[] = {
    {0x00000004, 0},  {0x00000008, 1},  {0x00000010, 2},  {0x00000020, 3},   // A B X Y
    {0x00000002, 4},  {0x00000001, 6},                                        // View (back), Menu (start)
    {0x00001000, 7},  {0x00002000, 8},                                        // stick presses
    {0x00000400, 9},  {0x00000800, 10},                                       // shoulders
    {0x00000040, 11}, {0x00000080, 12}, {0x00000100, 13}, {0x00000200, 14},   // dpad up down left right
};

constexpr int kMaxPads = 4;
struct State {
    bool present = false;
    uint32_t buttons = 0;
    int16_t axis[6] = {};
};
State g_last[kMaxPads];

int16_t scale(double v) { return (int16_t)std::max(-32767.0, std::min(32767.0, v * 32767.0)); }

void poll(void (*emit)(const void*))
{
    uint32_t count = 0;
    Gamepad pads[kMaxPads] = {nullptr, nullptr, nullptr, nullptr};
    try {
        auto all = Gamepad::Gamepads();
        count = std::min<uint32_t>(all.Size(), kMaxPads);
        for (uint32_t i = 0; i < count; i++)
            pads[i] = all.GetAt(i);
    } catch (...) {
        count = 0;
    }
    for (int i = 0; i < kMaxPads; i++) {
        State& last = g_last[i];
        if (i >= (int)count) {
            if (last.present) {
                ControllerDeviceEvent d = {CDEVICEREMOVED, 0, i};
                emit(&d);
                last = State();
            }
            continue;
        }
        GamepadReading r{};
        try {
            r = pads[i].GetCurrentReading();
        } catch (...) {
            continue;
        }
        last.present = true;
        const uint32_t buttons = (uint32_t)r.Buttons;
        for (const ButtonMap& b : kButtons) {
            const bool now = (buttons & b.flag) != 0, was = (last.buttons & b.flag) != 0;
            if (now != was) {
                ControllerButtonEvent e = {now ? CBUTTONDOWN : CBUTTONUP, 0, i, b.sdl, (uint8_t)(now ? 1 : 0), 0, 0};
                emit(&e);
            }
        }
        last.buttons = buttons;
        // SDL's axes: left x, left y, right x, right y (y grows downwards), then the triggers 0..32767.
        const int16_t axes[6] = {scale(r.LeftThumbstickX), scale(-r.LeftThumbstickY), scale(r.RightThumbstickX),
                                 scale(-r.RightThumbstickY), scale(r.LeftTrigger), scale(r.RightTrigger)};
        for (uint8_t a = 0; a < 6; a++) {
            if (axes[a] != last.axis[a]) {
                ControllerAxisEvent e = {CAXIS, 0, i, a, 0, 0, 0, axes[a], 0};
                emit(&e);
                last.axis[a] = axes[a];
            }
        }
    }
}

// The motors take 0..1; the port's strength is 0..65535 and drives both.
int rumble(unsigned short strength)
{
    int applied = 0;
    try {
        auto all = Gamepad::Gamepads();
        for (uint32_t i = 0; i < all.Size(); i++) {
            GamepadVibration v{};
            v.LeftMotor = v.RightMotor = strength / 65535.0;
            all.GetAt(i).Vibration(v);
            applied = 1;
        }
    } catch (...) {
    }
    return applied;
}

}  // namespace

void sms_uwp_input_register() { GXPC_UwpSetInput(poll, rumble); }
