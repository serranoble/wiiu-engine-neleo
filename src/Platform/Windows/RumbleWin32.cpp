#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xinput.h>
#include <cstdint>

namespace WiiU::Platform {

namespace {

using PFNXINPUTSETSTATE = DWORD (WINAPI*)(DWORD, XINPUT_VIBRATION*);

PFNXINPUTSETSTATE ResolveXInputSetState() noexcept {
    static bool s_probed = false;
    static PFNXINPUTSETSTATE s_fn = nullptr;
    if (!s_probed) {
        s_probed = true;
        HMODULE mod = LoadLibraryW(L"xinput1_4.dll");
        if (!mod) {
            mod = LoadLibraryW(L"xinput9_1_0.dll");
        }
        if (mod) {
            FARPROC raw = GetProcAddress(mod, "XInputSetState");
            if (raw) {
                s_fn = reinterpret_cast<PFNXINPUTSETSTATE>(reinterpret_cast<void*>(raw));
            }
        }
    }
    return s_fn;
}

} // namespace

void SetWin32HardwareRumble(int playerIdx, float intensity, bool active) noexcept {
    if (playerIdx < 0 || playerIdx >= 4) return;
    const PFNXINPUTSETSTATE fn = ResolveXInputSetState();
    if (!fn) return;

    XINPUT_VIBRATION vib{};
    if (active && intensity > 0.0f) {
        const float clamped = (intensity > 1.0f) ? 1.0f : intensity;
        const WORD speed = static_cast<WORD>(clamped * 65535.0f);
        vib.wLeftMotorSpeed = speed;
        vib.wRightMotorSpeed = speed;
    }
    fn(static_cast<DWORD>(playerIdx), &vib);
}

} // namespace WiiU::Platform
