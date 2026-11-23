#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace WiiU::Diagnostics {

/**
 * @brief Resultado individual de una prueba automatizada de invariante del motor (POD Zero-Alloc).
 */
struct SelfTestItem {
    const char* subsystem{"CORE"};
    const char* name{"UNNAMED"};
    const char* detail{""};
    bool passed{false};
};

/**
 * @brief Reporte de capacidad fija en stack para pruebas de subsistemas o del motor completo.
 */
template <size_t MaxItems = 64u>
struct SelfTestReport {
    static constexpr size_t CAPACITY = MaxItems;

    std::array<SelfTestItem, MaxItems> items{};
    uint32_t totalCount{0u};
    uint32_t passedCount{0u};
    uint32_t failedCount{0u};
    uint32_t droppedCount{0u};

    void Reset() noexcept {
        totalCount = 0u;
        passedCount = 0u;
        failedCount = 0u;
        droppedCount = 0u;
    }

    void Record(const char* subsystem, const char* name, bool passed, const char* detail = "") noexcept {
        if (totalCount < MaxItems) {
            items[totalCount] = SelfTestItem{subsystem, name, detail, passed};
        } else {
            ++droppedCount;
        }
        ++totalCount;
        if (passed) {
            ++passedCount;
        } else {
            ++failedCount;
        }
    }

    [[nodiscard]] bool AllPassed() const noexcept {
        return (totalCount > 0u) && (failedCount == 0u);
    }

    [[nodiscard]] const SelfTestItem* GetFirstFailure() const noexcept {
        const uint32_t stored = (totalCount < MaxItems) ? totalCount : static_cast<uint32_t>(MaxItems);
        for (uint32_t i = 0u; i < stored; ++i) {
            if (!items[i].passed) {
                return &items[i];
            }
        }
        return nullptr;
    }
};

using EngineSelfTestReport = SelfTestReport<64u>;

} // namespace WiiU::Diagnostics
