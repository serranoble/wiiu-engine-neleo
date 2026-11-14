#include "Core/ECS/Systems/StudioTurntableSystem.hpp"
#include "Core/ECS/Components/CameraComponent.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include <cmath>
#include <algorithm>

namespace WiiU {

void StudioTurntableSystem::Update(ECSWorld& world, float dt) {
    if (dt <= 0.0f) return;

    constexpr float TWO_PI = 6.283185307f;

    world.ForEach<StudioTurntableComponent>([dt, TWO_PI](Entity /*entity*/, StudioTurntableComponent& tt) {
        // 1. Integración de rotación automática si no está siendo arrastrado interactivamente
        if (tt.autoRotate && !tt.isDragging) {
            tt.targetYaw += dt * tt.autoRotateSpeed;
        }

        // 2. Suavizado exponencial amortiguado (OPT-164 / Jason Gregory Cap. 16)
        float smoothAlpha = std::clamp(1.0f - std::exp(-tt.smoothFactor * dt), 0.0f, 1.0f);
        tt.target.x += (tt.targetTarget.x - tt.target.x) * smoothAlpha;
        tt.target.y += (tt.targetTarget.y - tt.target.y) * smoothAlpha;
        tt.target.z += (tt.targetTarget.z - tt.target.z) * smoothAlpha;
        tt.yaw += (tt.targetYaw - tt.yaw) * smoothAlpha;
        tt.pitch += (tt.targetPitch - tt.pitch) * smoothAlpha;
        tt.distance += (tt.targetDistance - tt.distance) * smoothAlpha;

        // 3. Renormalización acoplada de fase angular en el grupo circular SO(2) (OPT-303)
        if (tt.yaw > TWO_PI && tt.targetYaw > TWO_PI) {
            tt.yaw -= TWO_PI;
            tt.targetYaw -= TWO_PI;
        } else if (tt.yaw < -TWO_PI && tt.targetYaw < -TWO_PI) {
            tt.yaw += TWO_PI;
            tt.targetYaw += TWO_PI;
        }
    });

    // Sincronizar entidades que posean conjuntamente StudioTurntableComponent y CameraComponent
    world.ForEach<StudioTurntableComponent, CameraComponent>([](Entity /*e*/, StudioTurntableComponent& tt, CameraComponent& cam) {
        cam.target = tt.target;
        cam.MarkDirty();
    });

    // Sincronizar entidades que posean conjuntamente StudioTurntableComponent y TransformComponent (Cámara o Modelo)
    world.ForEach<StudioTurntableComponent, TransformComponent>([](Entity /*e*/, StudioTurntableComponent& tt, TransformComponent& trans) {
        Point3D eye = tt.ComputeEyePosition();
        trans.SetPosition(eye);
    });
}

} // namespace WiiU
