#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "Core/ECS/ECSWorld.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include "Core/ECS/Components/ColliderComponent.hpp"
#include "Core/ECS/Components/RigidBodyComponent.hpp"
#include "Core/ECS/Components/KinematicCharacterComponent.hpp"

namespace WiiU {

/**
 * @brief Contacto físico generado en la fase estrecha entre dos entidades rígidas o estáticas.
 * Almacena impulsos acumulados para Sequential Impulses con fricción de Coulomb (Erin Catto GDC).
 */
struct RigidBodyContact {
    Entity entityA{Entity::Null};
    Entity entityB{Entity::Null};
    Point3D normal{0.0f, 1.0f, 0.0f};       ///< Vector unitario normal que apunta desde A hacia B.
    Point3D contactPoint{0.0f, 0.0f, 0.0f}; ///< Punto representativo de contacto en coordenadas de mundo.
    float penetration{0.0f};                ///< Profundidad de interpenetración métrica (>= 0).
    float normalImpulseSum{0.0f};           ///< Impulso normal acumulado (clamping >= 0).
    float tangentImpulseSum1{0.0f};         ///< Impulso tangencial acumulado eje 1 (fricción).
    float tangentImpulseSum2{0.0f};         ///< Impulso tangencial acumulado eje 2 (fricción).
};

/**
 * @brief Clasificación de articulaciones y restricciones físicas entre cuerpos rígidos.
 */
enum class PhysicsConstraintType : uint8_t {
    Distance      = 0, ///< Restricción de distancia XPBD entre dos anclajes (cadenas, barras).
    BallSocketPin = 1, ///< Junta esférica/anclaje puntual (péndulos, pivotes, ragdolls).
    Spring        = 2  ///< Muelle elástico amortiguado (Ley de Hooke + amortiguamiento viscoso).
};

/**
 * @brief Restricción bilateral o elástica entre entityA y entityB (o mundo si entityB == Entity::Null).
 */
struct PhysicsConstraint {
    PhysicsConstraintType type{PhysicsConstraintType::Distance};
    Entity entityA{Entity::Null};
    Entity entityB{Entity::Null};           ///< Si es Entity::Null, localAnchorB representa la posición fija en mundo.
    Point3D localAnchorA{0.0f, 0.0f, 0.0f}; ///< Desplazamiento en espacio local de entityA.
    Point3D localAnchorB{0.0f, 0.0f, 0.0f}; ///< Desplazamiento en espacio local de entityB (o mundo si entityB es inválido).
    float restLength{1.0f};
    float compliance{0.0f};                 ///< Inversa de rigidez XPBD (0.0 = unión rígida).
    float stiffness{40.0f};                 ///< Constante elástica k para Spring (N/m).
    float damping{4.0f};                    ///< Amortiguamiento viscoso c para Spring (N*s/m).
    bool active{true};
};

/**
 * @brief Métricas instantáneas de rendimiento y estado del solver físico (Cap. 14).
 */
struct PhysicsTelemetry {
    uint32_t activeBodies{0};
    uint32_t sleepingBodies{0};
    uint32_t activeContacts{0};
    uint32_t activeConstraints{0};
    uint32_t solverIterations{8};
    uint32_t substeps{2};
};

/**
 * @brief [Hito 131] Subsistema Completo de Dinámica de Cuerpos Rígidos, Restricciones y Personajes (Cap. 14).
 *
 * Implementación Data-Oriented C++20 sin asignaciones dinámicas por frame (0 heap allocs):
 * - Sub-stepping fijo con integración Semi-Implicit Euler e integración de cuaterniones SO(3).
 * - Detección estrecha analítica: Sphere-Sphere, Sphere-AABB y AABB-AABB (SAT 3 ejes).
 * - Solver iterativo Sequential Impulses (Erin Catto) con fricción de Coulomb y Baumgarte stabilization.
 * - Restricciones posicionales XPBD (Distance, BallSocketPin) y muelles elásticos (Hooke + Damping).
 * - Hibernación automática (Sleeping / Island Deactivation) y acoplamiento bidireccional con KCC.
 */
class RigidBodyPhysicsSystem {
public:
    static constexpr size_t MAX_BODY_PROXIES = 128;
    static constexpr size_t MAX_CONTACTS     = 256;
    static constexpr size_t MAX_CONSTRAINTS  = 64;

    RigidBodyPhysicsSystem() = default;

    /**
     * @brief Avanza los personajes cinemáticos (KCC) sobre un plano de laboratorio en groundY
     *        antes del acoplamiento bidireccional con cuerpos rígidos.
     */
    static void UpdateKinematicCharactersFlat(ECSWorld& world, float dt, float groundY = 0.0f) noexcept;

    /**
     * @brief Avanza la simulación física sobre el mundo ECS usando sub-stepping e impulsos secuenciales.
     */
    void Update(ECSWorld& world, float dt) noexcept;

    /**
     * @brief Elimina todas las restricciones y contactos activos almacenados.
     */
    void Clear() noexcept;

    /**
     * @brief Registra una restricción (distancia, pivote esférico o muelle) en el buffer pre-alocado.
     * @return Índice de restricción si se registró correctamente, o -1 si la capacidad está llena.
     */
    int32_t AddConstraint(const PhysicsConstraint& constraint) noexcept;

    /**
     * @brief Despierta todos los cuerpos rígidos dinámicos del mundo (útil al alterar gravedad o reiniciar).
     */
    static void WakeAllBodies(ECSWorld& world) noexcept;

    /**
     * @brief Aplica una onda expansiva radial impulsiva sobre todos los cuerpos dinámicos dentro de un radio.
     */
    static void ApplyRadialExplosion(ECSWorld& world,
                                     const Point3D& center,
                                     float radius,
                                     float peakImpulse,
                                     float upwardBoost = 0.35f) noexcept;

    /**
     * @brief Prueba estrecha AABB vs AABB (SAT 3 ejes de mínima penetración) con punto de contacto.
     */
    [[nodiscard]] static bool IntersectAABBAABB(const Point3D& centerA, const Point3D& halfExtA,
                                                const Point3D& centerB, const Point3D& halfExtB,
                                                Point3D& outNormalAB,
                                                Point3D& outContactPoint,
                                                float& outPenetration) noexcept;

    /**
     * @brief Prueba estrecha Esfera vs Esfera con cálculo de punto de contacto.
     */
    [[nodiscard]] static bool IntersectSphereSphere(const Point3D& centerA, float radiusA,
                                                    const Point3D& centerB, float radiusB,
                                                    Point3D& outNormalAB,
                                                    Point3D& outContactPoint,
                                                    float& outPenetration) noexcept;

    /**
     * @brief Prueba estrecha Esfera vs AABB con normal desde la esfera (A) hacia la caja (B).
     */
    [[nodiscard]] static bool IntersectSphereAABB(const Point3D& sphereCenter, float sphereRadius,
                                                  const Point3D& boxCenter, const Point3D& boxHalfExtents,
                                                  Point3D& outNormalAB,
                                                  Point3D& outContactPoint,
                                                  float& outPenetration) noexcept;

    void SetGravity(const Point3D& gravity) noexcept { m_gravity = gravity; }
    [[nodiscard]] const Point3D& GetGravity() const noexcept { return m_gravity; }

    void SetSolverIterations(uint32_t iterations) noexcept {
        m_solverIterations = (iterations > 0U && iterations <= 24U) ? iterations : 8U;
    }
    void SetSubsteps(uint32_t substeps) noexcept {
        m_substeps = (substeps > 0U && substeps <= 8U) ? substeps : 2U;
    }

    [[nodiscard]] const PhysicsTelemetry& GetTelemetry() const noexcept { return m_telemetry; }
    [[nodiscard]] size_t GetContactCount() const noexcept { return m_contactCount; }
    [[nodiscard]] const RigidBodyContact* GetContacts() const noexcept { return m_contacts.data(); }
    [[nodiscard]] size_t GetConstraintCount() const noexcept { return m_constraintCount; }
    [[nodiscard]] const PhysicsConstraint* GetConstraints() const noexcept { return m_constraints.data(); }

private:
    struct BodyProxy {
        Entity entity{Entity::Null};
        TransformComponent* transform{nullptr};
        ColliderComponent* collider{nullptr};
        RigidBodyComponent* rigidBody{nullptr};
        bool isDynamic{false};
        bool isStaticImmovable{true};
    };

    void BuildProxyList(ECSWorld& world) noexcept;
    [[nodiscard]] int32_t FindProxyIndex(Entity entity) const noexcept;
    void ApplySpringForces(float dt) noexcept;
    void IntegrateVelocities(float dt) noexcept;
    void GenerateContacts() noexcept;
    void ResolveKCCInteractions(ECSWorld& world, float dt) noexcept;
    void SolveVelocityContacts(float dt) noexcept;
    void SolveXPBDConstraints(float dt) noexcept;
    void IntegratePositions(float dt) noexcept;
    void UpdateSleepingAndClearAccumulators(ECSWorld& world, float dt) noexcept;

    Point3D m_gravity{0.0f, -9.81f, 0.0f};
    uint32_t m_solverIterations{8};
    uint32_t m_substeps{2};

    std::array<BodyProxy, MAX_BODY_PROXIES> m_proxies{};
    size_t m_proxyCount{0};

    std::array<RigidBodyContact, MAX_CONTACTS> m_contacts{};
    size_t m_contactCount{0};

    std::array<PhysicsConstraint, MAX_CONSTRAINTS> m_constraints{};
    size_t m_constraintCount{0};

    PhysicsTelemetry m_telemetry{};
};

} // namespace WiiU
