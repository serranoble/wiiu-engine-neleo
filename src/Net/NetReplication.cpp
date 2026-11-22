#include "Net/NetReplication.hpp"
#include <algorithm>
#include <cmath>

namespace WiiU {

bool SerializeSnapshot(NetBitWriter& writer, const NetTransformSnapshot& snap) noexcept {
    return writer.WriteU32(snap.netId) &&
           writer.WriteU16(snap.tick) &&
           writer.WriteFloat(static_cast<float>(snap.timestampSec)) &&
           writer.WriteFloat(snap.position.x) &&
           writer.WriteFloat(snap.position.y) &&
           writer.WriteFloat(snap.position.z) &&
           writer.WriteFloat(snap.rotation.x) &&
           writer.WriteFloat(snap.rotation.y) &&
           writer.WriteFloat(snap.rotation.z) &&
           writer.WriteFloat(snap.rotation.w) &&
           writer.WriteFloat(snap.velocity.x) &&
           writer.WriteFloat(snap.velocity.y) &&
           writer.WriteFloat(snap.velocity.z);
}

bool DeserializeSnapshot(NetBitReader& reader, NetTransformSnapshot& outSnap) noexcept {
    float tsFloat = 0.0f;
    const bool ok = reader.ReadU32(outSnap.netId) &&
                    reader.ReadU16(outSnap.tick) &&
                    reader.ReadFloat(tsFloat) &&
                    reader.ReadFloat(outSnap.position.x) &&
                    reader.ReadFloat(outSnap.position.y) &&
                    reader.ReadFloat(outSnap.position.z) &&
                    reader.ReadFloat(outSnap.rotation.x) &&
                    reader.ReadFloat(outSnap.rotation.y) &&
                    reader.ReadFloat(outSnap.rotation.z) &&
                    reader.ReadFloat(outSnap.rotation.w) &&
                    reader.ReadFloat(outSnap.velocity.x) &&
                    reader.ReadFloat(outSnap.velocity.y) &&
                    reader.ReadFloat(outSnap.velocity.z);
    if (!ok) {
        return false;
    }
    const bool allFinite =
        std::isfinite(tsFloat) &&
        std::isfinite(outSnap.position.x) && std::isfinite(outSnap.position.y) && std::isfinite(outSnap.position.z) &&
        std::isfinite(outSnap.rotation.x) && std::isfinite(outSnap.rotation.y) &&
        std::isfinite(outSnap.rotation.z) && std::isfinite(outSnap.rotation.w) &&
        std::isfinite(outSnap.velocity.x) && std::isfinite(outSnap.velocity.y) && std::isfinite(outSnap.velocity.z);
    if (!allFinite) {
        return false;
    }

    const float qLenSq = outSnap.rotation.x * outSnap.rotation.x +
                         outSnap.rotation.y * outSnap.rotation.y +
                         outSnap.rotation.z * outSnap.rotation.z +
                         outSnap.rotation.w * outSnap.rotation.w;
    if (qLenSq > 1e-6f) {
        const float invLen = 1.0f / std::sqrt(qLenSq);
        outSnap.rotation.x *= invLen;
        outSnap.rotation.y *= invLen;
        outSnap.rotation.z *= invLen;
        outSnap.rotation.w *= invLen;
    } else {
        outSnap.rotation = Quaternion{0.0f, 0.0f, 0.0f, 1.0f};
    }

    outSnap.timestampSec = static_cast<double>(tsFloat);
    return true;
}

void SnapshotInterpolator::Reset() noexcept {
    m_count = 0;
}

void SnapshotInterpolator::PushSnapshot(const NetTransformSnapshot& snap) noexcept {
    if (!std::isfinite(snap.timestampSec)) {
        return;
    }
    if (m_count > 0u) {
        const auto& latest = m_snapshots[m_count - 1u];
        // Descartar paquetes duplicados, fuera de orden o con timestamp no monotónico
        if (snap.tick == latest.tick ||
            !SequenceGreaterThan(snap.tick, latest.tick) ||
            snap.timestampSec <= latest.timestampSec) {
            return;
        }
    }

    if (m_count < MAX_SNAPSHOTS) {
        m_snapshots[m_count++] = snap;
    } else {
        for (size_t i = 1u; i < MAX_SNAPSHOTS; ++i) {
            m_snapshots[i - 1u] = m_snapshots[i];
        }
        m_snapshots[MAX_SNAPSHOTS - 1u] = snap;
    }
}

bool SnapshotInterpolator::Sample(double renderTimeSec, NetTransformSnapshot& outPose) const noexcept {
    if (m_count == 0u) {
        return false;
    }

    if (m_count == 1u || renderTimeSec <= m_snapshots[0].timestampSec) {
        outPose = m_snapshots[0];
        return true;
    }

    const auto& newest = m_snapshots[m_count - 1u];
    if (renderTimeSec >= newest.timestampSec) {
        // Extrapolación lineal acotada (máx. 150 ms) para absorber micro-pérdidas UDP
        const double extraDt = std::min(renderTimeSec - newest.timestampSec, MAX_EXTRAPOLATION_SEC);
        outPose = newest;
        const float dtF = static_cast<float>(extraDt);
        outPose.position.x += newest.velocity.x * dtF;
        outPose.position.y += newest.velocity.y * dtF;
        outPose.position.z += newest.velocity.z * dtF;
        outPose.timestampSec = newest.timestampSec + extraDt;
        return true;
    }

    // Buscar intervalo [a, b] tal que a.timestampSec <= renderTimeSec <= b.timestampSec
    for (size_t i = 0u; i + 1u < m_count; ++i) {
        const auto& a = m_snapshots[i];
        const auto& b = m_snapshots[i + 1u];
        if (renderTimeSec >= a.timestampSec && renderTimeSec <= b.timestampSec) {
            const double span = b.timestampSec - a.timestampSec;
            const float t = (span > 1e-6)
                ? std::clamp(static_cast<float>((renderTimeSec - a.timestampSec) / span), 0.0f, 1.0f)
                : 0.0f;

            outPose.netId = b.netId;
            outPose.tick = b.tick;
            outPose.timestampSec = renderTimeSec;
            outPose.position.x = a.position.x + (b.position.x - a.position.x) * t;
            outPose.position.y = a.position.y + (b.position.y - a.position.y) * t;
            outPose.position.z = a.position.z + (b.position.z - a.position.z) * t;
            outPose.rotation = Quaternion::Slerp(a.rotation, b.rotation, t);
            outPose.velocity.x = a.velocity.x + (b.velocity.x - a.velocity.x) * t;
            outPose.velocity.y = a.velocity.y + (b.velocity.y - a.velocity.y) * t;
            outPose.velocity.z = a.velocity.z + (b.velocity.z - a.velocity.z) * t;
            return true;
        }
    }

    outPose = newest;
    return true;
}

} // namespace WiiU
