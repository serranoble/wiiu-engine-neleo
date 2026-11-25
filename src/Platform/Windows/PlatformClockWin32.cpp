#include "Platform/PlatformClock.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace WiiU::PlatformClock {

uint64_t GetPerformanceFrequency() noexcept {
    LARGE_INTEGER freq{};
    if (QueryPerformanceFrequency(&freq) && freq.QuadPart > 0) {
        return static_cast<uint64_t>(freq.QuadPart);
    }
    return 1000ULL;
}

uint64_t GetPerformanceCounter() noexcept {
    LARGE_INTEGER counter{};
    if (QueryPerformanceCounter(&counter) && counter.QuadPart >= 0) {
        return static_cast<uint64_t>(counter.QuadPart);
    }
    return 0ULL;
}

void SleepMs(uint32_t milliseconds) noexcept {
    Sleep(static_cast<DWORD>(milliseconds));
}

} // namespace WiiU::PlatformClock
