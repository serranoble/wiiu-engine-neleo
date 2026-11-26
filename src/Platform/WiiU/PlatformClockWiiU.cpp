#include "Platform/PlatformClock.hpp"

#if defined(__WIIU__) || defined(__wiiu__)
#include <coreinit/systeminfo.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#endif

namespace Neleo::PlatformClock {

uint64_t GetPerformanceFrequency() noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    const uint64_t freq = static_cast<uint64_t>(OSTimerClockSpeed);
    return (freq > 0ULL) ? freq : 1ULL;
#else
    return 1ULL;
#endif
}

uint64_t GetPerformanceCounter() noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    return static_cast<uint64_t>(OSGetSystemTime());
#else
    return 0ULL;
#endif
}

void SleepMs(uint32_t milliseconds) noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    if (milliseconds == 0u) {
        OSSleepTicks(0);
        return;
    }
    OSSleepTicks(OSMillisecondsToTicks(milliseconds));
#else
    (void)milliseconds;
#endif
}

} // namespace Neleo::PlatformClock
