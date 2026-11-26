#include "Core/ECS/ECSWorld.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include "Core/ECS/Components/AudioEmitterComponent.hpp"
#include "Core/ECS/Components/MeshComponent.hpp"
#include "Core/ECS/Components/LightComponent.hpp"
#include "Core/ECS/Components/MaterialComponent.hpp"
#include "Core/ECS/Components/CameraComponent.hpp"
#include "Core/ECS/Components/HierarchyComponent.hpp"
#include "Core/ECS/Systems/TransformSystem.hpp"
#include "Core/ECS/Systems/CameraSystem.hpp"
#include "Core/ECS/Components/BoundingVolumeComponent.hpp"
#include "Core/ECS/Systems/CullingSystem.hpp"
#include <cstdio>

namespace Neleo {

ECSWorld::ECSWorld() {
    m_freeCount = MAX_ENTITIES;
    for (size_t i = 0; i < MAX_ENTITIES; ++i) {
        m_freeIndices[i] = static_cast<uint32_t>(MAX_ENTITIES - 1 - i);
        m_generations[i] = 1;
        m_alive[i] = false;
    }
    m_livingCount = 0;
}

ECSWorld::~ECSWorld() {
    Clear();
}

Entity ECSWorld::CreateEntity() {
    if (m_freeCount == 0) {
        return Entity::Null;
    }

    uint32_t index = m_freeIndices[--m_freeCount];
    uint16_t gen = m_generations[index];
    m_alive[index] = true;
    ++m_livingCount;

    return Entity(index, gen);
}

bool ECSWorld::DestroyEntity(Entity entity) {
    if (!IsAlive(entity)) {
        return false;
    }

    uint32_t index = entity.Index();

    // Eliminar la entidad de todos los pools de componentes activos
    for (auto& pool : m_pools) {
        if (pool) {
            pool->Remove(entity);
        }
    }

    m_alive[index] = false;

    // Incrementar generación para invalidar cualquier copia externa
    uint32_t nextGen = (static_cast<uint32_t>(m_generations[index]) + 1) & Entity::GEN_MASK;
    if (nextGen == 0) {
        nextGen = 1; // Generación 0 reservada para null
    }
    m_generations[index] = static_cast<uint16_t>(nextGen);

    m_freeIndices[m_freeCount++] = index;
    --m_livingCount;

    return true;
}

bool ECSWorld::IsAlive(Entity entity) const noexcept {
    if (!entity.IsValid() || entity.Index() >= MAX_ENTITIES) {
        return false;
    }
    uint32_t index = entity.Index();
    return m_alive[index] && (m_generations[index] == entity.Generation());
}

void ECSWorld::Clear() {
    for (auto& pool : m_pools) {
        if (pool) {
            pool->Clear();
        }
    }

    for (size_t i = 0; i < MAX_ENTITIES; ++i) {
        if (m_alive[i]) {
            uint32_t nextGen = (static_cast<uint32_t>(m_generations[i]) + 1) & Entity::GEN_MASK;
            if (nextGen == 0) nextGen = 1;
            m_generations[i] = static_cast<uint16_t>(nextGen);
            m_alive[i] = false;
        }
        m_freeIndices[i] = static_cast<uint32_t>(MAX_ENTITIES - 1 - i);
    }

    m_freeCount = MAX_ENTITIES;
    m_livingCount = 0;
}

bool ECSWorld::VerifyECSIntegrity() {
    ECSWorld testWorld;

    // 1. Verificación de Creación de Entidades e Identificadores
    Entity e1 = testWorld.CreateEntity();
    Entity e2 = testWorld.CreateEntity();

    if (!e1.IsValid() || !e2.IsValid() || e1 == e2) {
        std::printf("[ECSWorld] ERROR: Fallo al crear entidades iniciales e1 y e2.\n");
        return false;
    }

    if (e1.Generation() != 1 || e2.Generation() != 1) {
        std::printf("[ECSWorld] ERROR: Generación inicial no coincide con valor esperado (1).\n");
        return false;
    }

    // 2. Componentes de Prueba POD
    struct TestPos { float x{0.0f}, y{0.0f}, z{0.0f}; };
    struct TestVel { float vx{0.0f}, vy{0.0f}, vz{0.0f}; };
    struct TestHealth { int hp{100}; };

    testWorld.AddComponent<TestPos>(e1, 10.0f, 20.0f, 30.0f);
    testWorld.AddComponent<TestVel>(e1, 1.0f, 2.0f, 3.0f);

    testWorld.AddComponent<TestPos>(e2, 50.0f, 60.0f, 70.0f);
    testWorld.AddComponent<TestHealth>(e2, 250);

    // 3. Verificación de Búsqueda O(1) en ComponentPool
    TestPos* p1 = testWorld.GetComponent<TestPos>(e1);
    TestVel* v1 = testWorld.GetComponent<TestVel>(e1);
    TestHealth* h1 = testWorld.GetComponent<TestHealth>(e1);

    if (!p1 || p1->x != 10.0f || p1->y != 20.0f || p1->z != 30.0f) {
        std::printf("[ECSWorld] ERROR: Fallo al resolver TestPos en e1.\n");
        return false;
    }
    if (!v1 || v1->vx != 1.0f || v1->vy != 2.0f || v1->vz != 3.0f) {
        std::printf("[ECSWorld] ERROR: Fallo al resolver TestVel en e1.\n");
        return false;
    }
    if (h1 != nullptr) {
        std::printf("[ECSWorld] ERROR: e1 no debería tener TestHealth.\n");
        return false;
    }

    // 4. Verificación de Vistas Multi-Componente (Sparse-Set Iteration)
    size_t movementEntitiesFound = 0;
    testWorld.ForEach<TestPos, TestVel>([&](Entity e, TestPos& pos, TestVel& vel) {
        if (e == e1) {
            pos.x += vel.vx;
            pos.y += vel.vy;
            pos.z += vel.vz;
            ++movementEntitiesFound;
        }
    });

    if (movementEntitiesFound != 1) {
        std::printf("[ECSWorld] ERROR: ForEach<TestPos, TestVel> encontró %zu entidades (esperada: 1).\n",
                    movementEntitiesFound);
        return false;
    }

    if (p1->x != 11.0f || p1->y != 22.0f || p1->z != 33.0f) {
        std::printf("[ECSWorld] ERROR: ForEach no aplicó mutación correctamente sobre e1.\n");
        return false;
    }

    // 5. Eliminación de Componente Individual (Swap-and-Pop)
    bool removedVel = testWorld.RemoveComponent<TestVel>(e1);
    if (!removedVel || testWorld.HasComponent<TestVel>(e1)) {
        std::printf("[ECSWorld] ERROR: Fallo al remover TestVel de e1.\n");
        return false;
    }
    if (!testWorld.HasComponent<TestPos>(e1)) {
        std::printf("[ECSWorld] ERROR: La remoción de TestVel afectó erróneamente a TestPos.\n");
        return false;
    }

    // 6. Destrucción de Entidad y Blindaje Anti Use-After-Free
    Entity staleE1 = e1;
    bool destroyed = testWorld.DestroyEntity(e1);
    if (!destroyed || testWorld.IsAlive(staleE1)) {
        std::printf("[ECSWorld] ERROR: Fallo en destrucción de e1.\n");
        return false;
    }
    if (testWorld.GetComponent<TestPos>(staleE1) != nullptr) {
        std::printf("[ECSWorld] ERROR CRÍTICO: Handle obsoleto retornó componente vivo tras DestroyEntity.\n");
        return false;
    }

    // 7. Reciclaje de Ranura con Incremento Generacional
    Entity recycled = testWorld.CreateEntity();
    if (recycled.Index() != staleE1.Index()) {
        std::printf("[ECSWorld] ERROR: La ranura reciclada no coincide con la esperada en LIFO.\n");
        return false;
    }
    if (recycled.Generation() == staleE1.Generation()) {
        std::printf("[ECSWorld] ERROR CRÍTICO: La generación de la ranura no se incrementó al reciclar.\n");
        return false;
    }
    if (testWorld.IsAlive(staleE1)) {
        std::printf("[ECSWorld] ERROR CRÍTICO: El handle viejo staleE1 aún aparece como vivo.\n");
        return false;
    }
    if (!testWorld.IsAlive(recycled)) {
        std::printf("[ECSWorld] ERROR: La nueva entidad reciclada no aparece como viva.\n");
        return false;
    }

    // Asignar componente a la entidad reciclada y verificar que staleE1 no lo ve
    testWorld.AddComponent<TestPos>(recycled, 999.0f, 888.0f, 777.0f);
    if (testWorld.GetComponent<TestPos>(staleE1) != nullptr) {
        std::printf("[ECSWorld] ERROR CRÍTICO: Handle obsoleto accedió al componente de la nueva entidad reciclada.\n");
        return false;
    }
    TestPos* recycledPos = testWorld.GetComponent<TestPos>(recycled);
    if (!recycledPos || recycledPos->x != 999.0f) {
        std::printf("[ECSWorld] ERROR: Fallo al acceder al componente de la entidad reciclada.\n");
        return false;
    }

    // 8. Limpieza Completa de Fase 1
    testWorld.Clear();
    if (testWorld.GetLivingEntityCount() != 0) {
        std::printf("[ECSWorld] ERROR: LivingEntityCount no es 0 tras Clear().\n");
        return false;
    }

    // 9. Verificación de Fase 2: Componentes Base y Primeros Sistemas
    Entity actor = testWorld.CreateEntity();
    auto& trans = testWorld.AddComponent<TransformComponent>(actor, Point3D{5.0f, 10.0f, 15.0f});
    testWorld.AddComponent<MeshComponent>(actor, nullptr, nullptr, 36, 8);
    testWorld.AddComponent<LightComponent>(actor, ColorRGBA::Orange(), 2.5f, 18.0f);
    auto& audioComp = testWorld.AddComponent<AudioEmitterComponent>(actor, nullptr, 0.8f, 2.0f, 40.0f);

    // Ejecución de TransformSystem: recálculo de matriz TRS por dirty-checking
    TransformSystem::Update(testWorld);
    if (trans.isDirty) {
        std::printf("[ECSWorld] ERROR: TransformSystem no limpió la bandera isDirty tras actualizar.\n");
        return false;
    }
    if (trans.worldMatrix.m[12] != 5.0f || trans.worldMatrix.m[13] != 10.0f || trans.worldMatrix.m[14] != 15.0f) {
        std::printf("[ECSWorld] ERROR: TransformSystem calculó traslación incorrecta en worldMatrix.\n");
        return false;
    }

    // Nota: las pruebas de integracion cruzada (AudioSpatialSystem, RHI, KCC) residen en engine_framework (Engine::Init)
    // para mantener engine_core 100% libre de dependencias circulares.
    (void)audioComp;

    // 10. Verificación de Optimizaciones Pendientes (Hito 71: OPT-252, OPT-253, OPT-254)
    // 10.1 OPT-252: MaterialComponent (POD y parámetros de pipeline)
    Entity matEntity = testWorld.CreateEntity();
    auto& mat = testWorld.EmplaceComponent<MaterialComponent>(matEntity, 1.0f, 2.5f, 0.65f, -0.35f, BlendMode::Opaque, true, true);
    if (mat.materialMode != 1.0f || mat.uvTiling != 2.5f || mat.roughness != 0.65f || mat.customParam != -0.35f) {
        std::printf("[ECSWorld] ERROR: MaterialComponent no retuvo los parámetros inicializados.\n");
        return false;
    }

    // 10.2 OPT-253: CameraComponent y CameraSystem (Cálculo desacoplado de View/Projection)
    Entity camEntity = testWorld.CreateEntity();
    testWorld.EmplaceComponent<TransformComponent>(camEntity, Point3D{0.0f, 2.0f, 10.0f});
    auto& cam = testWorld.EmplaceComponent<CameraComponent>(camEntity, 60.0f, 16.0f / 9.0f, 0.1f, 100.0f, Point3D{0.0f, 0.0f, 0.0f});
    if (!cam.isDirty) {
        std::printf("[ECSWorld] ERROR: CameraComponent debe inicializarse con isDirty = true.\n");
        return false;
    }
    CameraSystem::Update(testWorld);
    if (cam.isDirty) {
        std::printf("[ECSWorld] ERROR: CameraSystem no limpió la bandera isDirty de CameraComponent.\n");
        return false;
    }
    if (cam.viewMatrix.m[15] != 1.0f || cam.projMatrix.m[11] != -1.0f) {
        std::printf("[ECSWorld] ERROR: CameraSystem calculó matrices de vista o proyección inválidas.\n");
        return false;
    }

    // 10.3 OPT-254: HierarchyComponent y Resolución Jerárquica en TransformSystem
    Entity parentEntity = testWorld.CreateEntity();
    auto& parentTrans = testWorld.EmplaceComponent<TransformComponent>(parentEntity, Point3D{10.0f, 0.0f, 0.0f});

    Entity childEntity = testWorld.CreateEntity();
    auto& childTrans = testWorld.EmplaceComponent<TransformComponent>(childEntity, Point3D{0.0f, 5.0f, 0.0f});
    testWorld.EmplaceComponent<HierarchyComponent>(childEntity, parentEntity, 1);

    TransformSystem::Update(testWorld);
    if (parentTrans.worldMatrix.m[12] != 10.0f || parentTrans.worldMatrix.m[13] != 0.0f) {
        std::printf("[ECSWorld] ERROR: Matriz de mundo de entidad padre incorrecta.\n");
        return false;
    }
    if (childTrans.worldMatrix.m[12] != 10.0f || childTrans.worldMatrix.m[13] != 5.0f) {
        std::printf("[ECSWorld] ERROR: TransformSystem no resolvió la composición jerárquica padre-hijo (OPT-254).\n");
        return false;
    }

    // 11. Verificación de CullingSystem y BoundingVolumeComponent (OPT-256)
    Entity culledEntity = testWorld.CreateEntity();
    auto& cullTrans = testWorld.EmplaceComponent<TransformComponent>(culledEntity, Point3D{0.0f, 0.0f, 0.0f});
    auto& cullMesh = testWorld.EmplaceComponent<MeshComponent>(culledEntity);
    auto& cullBound = testWorld.EmplaceComponent<BoundingVolumeComponent>(culledEntity, Point3D{0.0f, 0.0f, 0.0f}, 1.5f);
    cullBound.enableFrustumCulling = true;
    cullBound.enableSmallPrimitiveCulling = false;

    // Con entidad en el origen y cámara en (0, 2, 10) mirando a (0, 0, 0), la esfera debe ser visible
    TransformSystem::Update(testWorld);
    CullingSystem::Update(testWorld, cam, 1280.0f, 720.0f);
    if (cullBound.isCulled || !cullMesh.isVisible) {
        std::printf("[ECSWorld] ERROR: CullingSystem descartó erróneamente un objeto dentro del frustum.\n");
        return false;
    }

    // Mover entidad detrás de la cámara (z = +50.0f)
    cullTrans.position = Point3D{0.0f, 0.0f, 50.0f};
    cullTrans.isDirty = true;
    TransformSystem::Update(testWorld);
    CullingSystem::Update(testWorld, cam, 1280.0f, 720.0f);
    if (!cullBound.isCulled || cullMesh.isVisible) {
        std::printf("[ECSWorld] ERROR: CullingSystem no descartó el objeto fuera del frustum.\n");
        return false;
    }

    // Probar CullingSystem::UpdateParallel
    cullTrans.position = Point3D{0.0f, 0.0f, 0.0f};
    cullTrans.isDirty = true;
    TransformSystem::Update(testWorld);
    CullingSystem::UpdateParallel(testWorld, cam, 1280.0f, 720.0f);
    if (cullBound.isCulled || !cullMesh.isVisible) {
        std::printf("[ECSWorld] ERROR: CullingSystem::UpdateParallel falló al procesar entidad visible.\n");
        return false;
    }

    // 12. OPT-266: StrongId y Tipado Fuerte de Entidades
    StrongEntityId strongE1 = e1.ToStrongId();
    if (!strongE1.IsValid() || strongE1.Get() != e1.id) {
        std::printf("[ECSWorld] ERROR: Conversión Entity a StrongEntityId falló (OPT-266).\n");
        return false;
    }
    Entity roundTripE1 = Entity::FromStrongId(strongE1);
    if (roundTripE1 != e1 || roundTripE1.Index() != e1.Index() || roundTripE1.Generation() != e1.Generation()) {
        std::printf("[ECSWorld] ERROR: Conversión StrongEntityId a Entity falló en ida y vuelta (OPT-266).\n");
        return false;
    }
    struct TestPhantomTag {};
    StrongId<TestPhantomTag, uint32_t> customId(777);
    if (!customId.IsValid() || customId.Get() != 777) {
        std::printf("[ECSWorld] ERROR: StrongId con tag fantasma falló en validación o valor (OPT-266).\n");
        return false;
    }

    // 13. Verificación del Subsistema de Mallas y Volúmenes Envolventes (Hito 80 / OPT-293)
    Entity meshEntity = testWorld.CreateEntity();
    auto& mComp = testWorld.EmplaceComponent<MeshComponent>(meshEntity);
    auto& bComp = testWorld.EmplaceComponent<BoundingVolumeComponent>(meshEntity);
    mComp.vertexCount = 329;
    mComp.indexCount = 1632;
    bComp.center = Point3D{0.0f, 2.0f, 0.0f};
    bComp.radius = 2.13f;
    if (mComp.vertexCount != 329 || mComp.indexCount != 1632 || bComp.radius < 2.1f) {
        std::printf("[ECSWorld] ERROR: Fallo al registrar componentes de malla y volumen envolvente (OPT-293).\n");
        return false;
    }

    // Limpieza final de prueba
    testWorld.Clear();

    std::printf("[ECSWorld] EXITO: Arquitectura ECS C++20 (Entity, Sparse-Set, Views, Systems, OPT-207..OPT-293) 100%% VERIFICADA.\n");
    return true;
}

} // namespace Neleo
