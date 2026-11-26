#include "Core/AI/SteeringBehaviors.hpp"
#include <cmath>

namespace Neleo {

Point3D SteeringBehaviors::Seek(const Point3D& currentPos, 
                                const Point3D& targetPos, 
                                const Point3D& currentVel, 
                                float maxSpeed) noexcept {
    const float dx = targetPos.x - currentPos.x;
    const float dz = targetPos.z - currentPos.z;
    const float distSq = dx * dx + dz * dz;

    if (distSq < 0.0001f) {
        return Point3D{ -currentVel.x, 0.0f, -currentVel.z };
    }

    const float dist = std::sqrt(distSq);
    const float desiredVx = (dx / dist) * maxSpeed;
    const float desiredVz = (dz / dist) * maxSpeed;

    return Point3D{ desiredVx - currentVel.x, 0.0f, desiredVz - currentVel.z };
}

Point3D SteeringBehaviors::Flee(const Point3D& currentPos, 
                                const Point3D& threatPos, 
                                const Point3D& currentVel, 
                                float maxSpeed) noexcept {
    const float dx = currentPos.x - threatPos.x;
    const float dz = currentPos.z - threatPos.z;
    const float distSq = dx * dx + dz * dz;

    if (distSq < 0.0001f) {
        return Point3D{ maxSpeed, 0.0f, 0.0f };
    }

    const float dist = std::sqrt(distSq);
    const float desiredVx = (dx / dist) * maxSpeed;
    const float desiredVz = (dz / dist) * maxSpeed;

    return Point3D{ desiredVx - currentVel.x, 0.0f, desiredVz - currentVel.z };
}

Point3D SteeringBehaviors::Arrive(const Point3D& currentPos, 
                                  const Point3D& targetPos, 
                                  const Point3D& currentVel, 
                                  float maxSpeed, 
                                  float slowingRadius) noexcept {
    const float dx = targetPos.x - currentPos.x;
    const float dz = targetPos.z - currentPos.z;
    const float distSq = dx * dx + dz * dz;

    if (distSq < 0.0001f) {
        return Point3D{ -currentVel.x, 0.0f, -currentVel.z };
    }

    const float dist = std::sqrt(distSq);
    float targetSpeed = maxSpeed;

    // Reducción proporcional de velocidad dentro del radio de amortiguamiento
    if (slowingRadius > 0.001f && dist < slowingRadius) {
        targetSpeed = maxSpeed * (dist / slowingRadius);
    }

    const float desiredVx = (dx / dist) * targetSpeed;
    const float desiredVz = (dz / dist) * targetSpeed;

    return Point3D{ desiredVx - currentVel.x, 0.0f, desiredVz - currentVel.z };
}

float SteeringBehaviors::VelocityToYaw(const Point3D& velocity, float fallbackYaw) noexcept {
    const float speedSq = velocity.x * velocity.x + velocity.z * velocity.z;
    if (speedSq < 0.0001f) {
        return fallbackYaw;
    }
    // Eje canónico del motor: Y-up, Z-fwd (+Z a yaw=0), X-right (+X a yaw=-90°)
    return std::atan2(-velocity.x, velocity.z);
}

bool SteeringBehaviors::IsInSightCone(const Point3D& observerPos, 
                                      float observerYawRad, 
                                      const Point3D& targetPos, 
                                      float maxRange, 
                                      float fovDeg) noexcept {
    const float dx = targetPos.x - observerPos.x;
    const float dz = targetPos.z - observerPos.z;
    const float distSq = dx * dx + dz * dz;

    if (distSq > maxRange * maxRange) {
        return false;
    }

    if (distSq < 0.001f) {
        return true;
    }

    const float dist = std::sqrt(distSq);
    const float toTargetX = dx / dist;
    const float toTargetZ = dz / dist;

    // Vector canónico de avance según orientación Yaw del observador
    const float fwdX = -std::sin(observerYawRad);
    const float fwdZ =  std::cos(observerYawRad);

    const float dot = fwdX * toTargetX + fwdZ * toTargetZ;
    constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
    const float halfFovRad = (fovDeg * 0.5f) * DEG_TO_RAD;
    const float cosHalfFov = std::cos(halfFovRad);

    return dot >= cosHalfFov;
}

} // namespace Neleo
