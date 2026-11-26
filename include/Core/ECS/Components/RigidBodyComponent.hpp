#pragma once

#include <cstdint>
#include <algorithm>
#include "Graphics/Math3D.hpp"

namespace Neleo {

/**
 * @brief Clasificación cinemática del cuerpo rígido dentro del mundo físico.
 * (Jason Gregory, Game Engine Architecture, 3rd Ed., Cap. 14.3 "Rigid Body Dynamics").
 */
enum class RigidBodyMotionType : uint8_t {
    Static    = 0, ///< Masa infinita; nunca integrado ni movido por fuerzas o impulsos.
    Kinematic = 1, ///< Movido explícitamente por lógica o animación; masa infinita ante dinámicos.
    Dynamic   = 2  ///< Simulado completamente por fuerzas, torques, gravedad, contactos y restricciones.
};

/**
 * @brief [Hito 131] Componente de Cuerpo Rígido 3D para Simulación Física (Cap. 14).
 *
 * Almacena estado inercial (masa e inversa de tensor de inercia local), orientación
 * en SO(3) mediante cuaternión, velocidades lineal/angular, acumuladores de fuerza/torque,
 * propiedades de material (restitución y fricción de Coulomb) e hibernación (sleeping).
 */
struct RigidBodyComponent {
    RigidBodyMotionType motionType{RigidBodyMotionType::Dynamic};

    float mass{1.0f};
    float invMass{1.0f};
    Point3D invInertiaLocal{1.0f, 1.0f, 1.0f};

    Quaternion orientation{Quaternion::Identity()};

    Point3D linearVelocity{0.0f, 0.0f, 0.0f};
    Point3D angularVelocity{0.0f, 0.0f, 0.0f};

    Point3D forceAccum{0.0f, 0.0f, 0.0f};
    Point3D torqueAccum{0.0f, 0.0f, 0.0f};

    float restitution{0.35f};
    float friction{0.55f};
    float linearDamping{0.12f};
    float angularDamping{0.25f};
    float gravityScale{1.0f};

    float sleepTimer{0.0f};
    bool isSleeping{false};
    bool canSleep{true};

    /**
     * @brief Despierta el cuerpo rígido e invalida el contador de hibernación.
     */
    inline void WakeUp() noexcept {
        isSleeping = false;
        sleepTimer = 0.0f;
    }

    /**
     * @brief Aplica una fuerza lineal continua aplicada al centro de masa (Newtons).
     */
    inline void ApplyCentralForce(const Point3D& f) noexcept {
        if (motionType != RigidBodyMotionType::Dynamic || invMass <= 0.0f) return;
        WakeUp();
        forceAccum = forceAccum + f;
    }

    /**
     * @brief Aplica una fuerza en un punto del mundo generando fuerza lineal y torque r x F.
     */
    inline void ApplyForceAtPoint(const Point3D& f, const Point3D& worldPoint, const Point3D& centerOfMass) noexcept {
        if (motionType != RigidBodyMotionType::Dynamic || invMass <= 0.0f) return;
        WakeUp();
        forceAccum = forceAccum + f;
        const Point3D r = worldPoint - centerOfMass;
        torqueAccum = torqueAccum + Point3D::Cross(r, f);
    }

    /**
     * @brief Aplica un impulso lineal instantáneo J al centro de masa (N*s).
     */
    inline void ApplyLinearImpulse(const Point3D& j) noexcept {
        if (motionType != RigidBodyMotionType::Dynamic || invMass <= 0.0f) return;
        WakeUp();
        linearVelocity = linearVelocity + (j * invMass);
    }

    /**
     * @brief Aplica un impulso angular instantáneo L en espacio de mundo.
     */
    inline void ApplyAngularImpulse(const Point3D& lWorld) noexcept {
        if (motionType != RigidBodyMotionType::Dynamic || invMass <= 0.0f) return;
        WakeUp();
        angularVelocity = angularVelocity + ComputeWorldInvInertia(lWorld);
    }

    /**
     * @brief Aplica un impulso instantáneo J en un brazo de palanca rWorld desde el centro de masa.
     */
    inline void ApplyImpulseAtOffset(const Point3D& j, const Point3D& rWorld) noexcept {
        if (motionType != RigidBodyMotionType::Dynamic || invMass <= 0.0f) return;
        WakeUp();
        linearVelocity = linearVelocity + (j * invMass);
        angularVelocity = angularVelocity + ComputeWorldInvInertia(Point3D::Cross(rWorld, j));
    }

    /**
     * @brief Transforma un vector angular desde espacio de mundo a través de I_world^{-1} = R * I_local^{-1} * R^T.
     */
    [[nodiscard]] inline Point3D ComputeWorldInvInertia(const Point3D& vWorld) const noexcept {
        if (motionType != RigidBodyMotionType::Dynamic || invMass <= 0.0f) {
            return Point3D{0.0f, 0.0f, 0.0f};
        }
        const Quaternion qNorm = orientation.Normalized();
        const Point3D vLocal = qNorm.Conjugate().Rotate(vWorld);
        const Point3D scaledLocal{
            vLocal.x * invInertiaLocal.x,
            vLocal.y * invInertiaLocal.y,
            vLocal.z * invInertiaLocal.z
        };
        return qNorm.Rotate(scaledLocal);
    }

    /**
     * @brief Crea un cuerpo rígido estático (masa e inercia infinitas).
     */
    [[nodiscard]] static RigidBodyComponent MakeStatic(float rest = 0.35f, float fric = 0.65f) noexcept {
        RigidBodyComponent rb;
        rb.motionType = RigidBodyMotionType::Static;
        rb.mass = 0.0f;
        rb.invMass = 0.0f;
        rb.invInertiaLocal = Point3D{0.0f, 0.0f, 0.0f};
        rb.restitution = std::clamp(rest, 0.0f, 1.0f);
        rb.friction = std::clamp(fric, 0.0f, 1.5f);
        rb.canSleep = false;
        return rb;
    }

    /**
     * @brief Crea un cuerpo rígido cinemático (movido por juego/KCC, masa infinita ante cuerpos dinámicos).
     */
    [[nodiscard]] static RigidBodyComponent MakeKinematic(float rest = 0.3f, float fric = 0.5f) noexcept {
        RigidBodyComponent rb = MakeStatic(rest, fric);
        rb.motionType = RigidBodyMotionType::Kinematic;
        return rb;
    }

    /**
     * @brief Crea un cuerpo rígido dinámico con tensor de inercia de esfera sólida (I = 2/5 m r^2).
     */
    [[nodiscard]] static RigidBodyComponent MakeDynamicSphere(float massVal, float radius,
                                                              float rest = 0.45f, float fric = 0.45f) noexcept {
        const float m = (massVal > 0.001f) ? massVal : 1.0f;
        const float r = (radius > 0.01f) ? radius : 0.1f;
        const float inertia = 0.4f * m * r * r;
        const float invI = 1.0f / inertia;

        RigidBodyComponent rb;
        rb.motionType = RigidBodyMotionType::Dynamic;
        rb.mass = m;
        rb.invMass = 1.0f / m;
        rb.invInertiaLocal = Point3D{invI, invI, invI};
        rb.restitution = std::clamp(rest, 0.0f, 1.0f);
        rb.friction = std::clamp(fric, 0.0f, 1.5f);
        return rb;
    }

    /**
     * @brief Crea un cuerpo rígido dinámico con tensor de inercia de caja sólida homogénea.
     */
    [[nodiscard]] static RigidBodyComponent MakeDynamicBox(float massVal, const Point3D& halfExtents,
                                                           float rest = 0.25f, float fric = 0.65f) noexcept {
        const float m = (massVal > 0.001f) ? massVal : 1.0f;
        const float w = 2.0f * ((halfExtents.x > 0.01f) ? halfExtents.x : 0.1f);
        const float h = 2.0f * ((halfExtents.y > 0.01f) ? halfExtents.y : 0.1f);
        const float d = 2.0f * ((halfExtents.z > 0.01f) ? halfExtents.z : 0.1f);

        const float factor = m / 12.0f;
        const float ix = factor * (h * h + d * d);
        const float iy = factor * (w * w + d * d);
        const float iz = factor * (w * w + h * h);

        RigidBodyComponent rb;
        rb.motionType = RigidBodyMotionType::Dynamic;
        rb.mass = m;
        rb.invMass = 1.0f / m;
        rb.invInertiaLocal = Point3D{1.0f / ix, 1.0f / iy, 1.0f / iz};
        rb.restitution = std::clamp(rest, 0.0f, 1.0f);
        rb.friction = std::clamp(fric, 0.0f, 1.5f);
        return rb;
    }
};

} // namespace Neleo
