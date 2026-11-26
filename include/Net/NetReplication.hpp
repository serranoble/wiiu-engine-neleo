#pragma once

#include "Graphics/Math3D.hpp"
#include "Net/NetBitStream.hpp"
#include "Net/NetTypes.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace Neleo {

/**
 * @brief [Jason Gregory Cap. 20.4] Instantánea compacta de transformación para replicación de red.
 */
struct NetTransformSnapshot {
    uint32_t netId{0};
    uint16_t tick{0};
    double timestampSec{0.0};
    Point3D position{0.0f, 0.0f, 0.0f};
    Quaternion rotation{0.0f, 0.0f, 0.0f, 1.0f};
    Point3D velocity{0.0f, 0.0f, 0.0f};
};

bool SerializeSnapshot(NetBitWriter& writer, const NetTransformSnapshot& snap) noexcept;
bool DeserializeSnapshot(NetBitReader& reader, NetTransformSnapshot& outSnap) noexcept;

/**
 * @brief [Jason Gregory Cap. 20.4] Interpolador determinista de snapshots para entidades remotas.
 * Almacena hasta 16 instantáneas ordenadas en un arreglo contiguo (0 alocaciones en heap),
 * interpolando posición (LERP) y orientación (SLERP) con extrapolación acotada anti-jitter.
 */
class SnapshotInterpolator {
public:
    static constexpr size_t MAX_SNAPSHOTS = 16u;
    static constexpr double MAX_EXTRAPOLATION_SEC = 0.15; // Tope 150 ms ante pérdida puntual

    SnapshotInterpolator() noexcept = default;

    void Reset() noexcept;
    void PushSnapshot(const NetTransformSnapshot& snap) noexcept;

    /**
     * @brief Muestrea la pose interpolada para un tiempo de render dado (habitualmente now - 0.10s).
     */
    [[nodiscard]] bool Sample(double renderTimeSec, NetTransformSnapshot& outPose) const noexcept;

    [[nodiscard]] size_t GetCount() const noexcept { return m_count; }
    [[nodiscard]] const NetTransformSnapshot* GetLatest() const noexcept {
        return (m_count > 0) ? &m_snapshots[m_count - 1] : nullptr;
    }

private:
    std::array<NetTransformSnapshot, MAX_SNAPSHOTS> m_snapshots{};
    size_t m_count{0};
};

} // namespace Neleo
