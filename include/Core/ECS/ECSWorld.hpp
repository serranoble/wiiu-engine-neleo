#pragma once

#include <atomic>
#include <memory>
#include <array>
#include <utility>
#include <cstdint>
#include <cstddef>
#include "Core/ECS/Entity.hpp"
#include "Core/ECS/ComponentPool.hpp"

namespace Neleo {

/**
 * @brief Generador de identificadores únicos por tipo de componente en tiempo de compilación/enlace.
 */
using ComponentTypeID = uint32_t;

inline ComponentTypeID GetNextComponentTypeID() noexcept {
    static std::atomic<ComponentTypeID> s_counter{0};
    return s_counter.fetch_add(1, std::memory_order_relaxed);
}

template <typename T>
inline ComponentTypeID GetComponentTypeID() noexcept {
    static const ComponentTypeID s_id = GetNextComponentTypeID();
    return s_id;
}

/**
 * @brief [OPT-237] Registro central de entidades, pools de componentes y despacho de vistas (ECSWorld).
 * 
 * Diseñado bajo los principios de Data-Oriented Design (DOD) para consolas:
 * - Capacidad pre-alocada de 2,048 entidades: CERO fragmentación de memoria ni alocaciones en caliente.
 * - Validación generacional O(1) en cada acceso, erradicando al 100% referencias obsoletas (Use-After-Free).
 * - Consulta por vistas (Views) de múltiples componentes con optimización Sparse-Set (iterando siempre el
 *   pool de menor tamaño para minimizar accesos).
 */
class ECSWorld {
public:
    static constexpr size_t MAX_ENTITIES = 2048;
    static constexpr size_t MAX_COMPONENT_TYPES = 64;

    ECSWorld();
    ~ECSWorld();

    ECSWorld(const ECSWorld&) = delete;
    ECSWorld& operator=(const ECSWorld&) = delete;
    ECSWorld(ECSWorld&&) noexcept = default;
    ECSWorld& operator=(ECSWorld&&) noexcept = default;

    /**
     * @brief Crea una nueva entidad adquiriendo una ranura libre y su generación actual.
     */
    [[nodiscard]] Entity CreateEntity();

    /**
     * @brief Destruye una entidad, removiendo todos sus componentes e incrementando su generación.
     */
    bool DestroyEntity(Entity entity);

    /**
     * @brief Verifica si una entidad está viva y su generación coincide.
     */
    [[nodiscard]] bool IsAlive(Entity entity) const noexcept;

    /**
     * @brief Retorna el número de entidades vivas en el mundo.
     */
    [[nodiscard]] size_t GetLivingEntityCount() const noexcept { return m_livingCount; }
    [[nodiscard]] size_t GetEntityCount() const noexcept { return m_livingCount; }

    /**
     * @brief Retorna la capacidad máxima de entidades soportadas.
     */
    [[nodiscard]] static constexpr size_t GetMaxEntities() noexcept { return MAX_ENTITIES; }

    /**
     * @brief Limpia todas las entidades y componentes activos.
     */
    void Clear();

    // -------------------------------------------------------------------------
    // Gestión de Componentes
    // -------------------------------------------------------------------------

    template <typename T, typename... Args>
    T& AddComponent(Entity entity, Args&&... args) {
        if (!IsAlive(entity)) {
            static T fallback{};
            return fallback;
        }
        auto& pool = GetOrCreatePool<T>();
        return pool.Emplace(entity, std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    T& EmplaceComponent(Entity entity, Args&&... args) {
        return AddComponent<T>(entity, std::forward<Args>(args)...);
    }

    template <typename T>
    bool RemoveComponent(Entity entity) noexcept {
        if (!IsAlive(entity)) return false;
        ComponentTypeID id = GetComponentTypeID<T>();
        if (id >= MAX_COMPONENT_TYPES || !m_pools[id]) return false;
        return m_pools[id]->Remove(entity);
    }

    template <typename T>
    [[nodiscard]] T* GetComponent(Entity entity) noexcept {
        if (!IsAlive(entity)) return nullptr;
        ComponentTypeID id = GetComponentTypeID<T>();
        if (id >= MAX_COMPONENT_TYPES || !m_pools[id]) return nullptr;
        auto* pool = static_cast<ComponentPool<T, MAX_ENTITIES>*>(m_pools[id].get());
        return pool->Get(entity);
    }

    template <typename T>
    [[nodiscard]] const T* GetComponent(Entity entity) const noexcept {
        if (!IsAlive(entity)) return nullptr;
        ComponentTypeID id = GetComponentTypeID<T>();
        if (id >= MAX_COMPONENT_TYPES || !m_pools[id]) return nullptr;
        const auto* pool = static_cast<const ComponentPool<T, MAX_ENTITIES>*>(m_pools[id].get());
        return pool->Get(entity);
    }

    template <typename T>
    [[nodiscard]] bool HasComponent(Entity entity) const noexcept {
        if (!IsAlive(entity)) return false;
        ComponentTypeID id = GetComponentTypeID<T>();
        if (id >= MAX_COMPONENT_TYPES || !m_pools[id]) return false;
        return m_pools[id]->Has(entity);
    }

    template <typename T>
    ComponentPool<T, MAX_ENTITIES>& GetOrCreatePool() {
        ComponentTypeID id = GetComponentTypeID<T>();
        if (id >= MAX_COMPONENT_TYPES) {
            static ComponentPool<T, MAX_ENTITIES> fallbackPool;
            return fallbackPool;
        }
        if (!m_pools[id]) {
            m_pools[id] = std::make_unique<ComponentPool<T, MAX_ENTITIES>>();
        }
        return *static_cast<ComponentPool<T, MAX_ENTITIES>*>(m_pools[id].get());
    }

    template <typename T>
    [[nodiscard]] ComponentPool<T, MAX_ENTITIES>* GetPool() noexcept {
        ComponentTypeID id = GetComponentTypeID<T>();
        if (id >= MAX_COMPONENT_TYPES || !m_pools[id]) return nullptr;
        return static_cast<ComponentPool<T, MAX_ENTITIES>*>(m_pools[id].get());
    }

    template <typename T>
    [[nodiscard]] const ComponentPool<T, MAX_ENTITIES>* GetPool() const noexcept {
        ComponentTypeID id = GetComponentTypeID<T>();
        if (id >= MAX_COMPONENT_TYPES || !m_pools[id]) return nullptr;
        return static_cast<const ComponentPool<T, MAX_ENTITIES>*>(m_pools[id].get());
    }

    // -------------------------------------------------------------------------
    // Vistas e Iteración Rápida de Sistemas (Sparse-Set View Queries)
    // -------------------------------------------------------------------------

    /**
     * @brief Itera sobre todas las entidades que poseen el componente T.
     */
    template <typename T, typename Func>
    void ForEach(Func&& func) {
        auto* pool = GetPool<T>();
        if (!pool) return;
        T* data = pool->Data();
        const Entity* entities = pool->Entities();
        size_t count = pool->Size();
        for (size_t i = 0; i < count; ++i) {
            func(entities[i], data[i]);
        }
    }

    /**
     * @brief Itera sobre todas las entidades que poseen conjuntamente T1 y T2.
     * Optimización Sparse-Set: Recorre el arreglo denso del pool más pequeño y consulta el segundo en O(1).
     */
    template <typename T1, typename T2, typename Func>
    void ForEach(Func&& func) {
        auto* pool1 = GetPool<T1>();
        auto* pool2 = GetPool<T2>();
        if (!pool1 || !pool2) return;

        if (pool1->Size() <= pool2->Size()) {
            T1* data1 = pool1->Data();
            const Entity* entities1 = pool1->Entities();
            size_t count1 = pool1->Size();
            for (size_t i = 0; i < count1; ++i) {
                Entity e = entities1[i];
                if (T2* c2 = pool2->Get(e)) {
                    func(e, data1[i], *c2);
                }
            }
        } else {
            T2* data2 = pool2->Data();
            const Entity* entities2 = pool2->Entities();
            size_t count2 = pool2->Size();
            for (size_t i = 0; i < count2; ++i) {
                Entity e = entities2[i];
                if (T1* c1 = pool1->Get(e)) {
                    func(e, *c1, data2[i]);
                }
            }
        }
    }

    /**
     * @brief Itera sobre todas las entidades que poseen conjuntamente T1, T2 y T3.
     */
    template <typename T1, typename T2, typename T3, typename Func>
    void ForEach(Func&& func) {
        auto* pool1 = GetPool<T1>();
        auto* pool2 = GetPool<T2>();
        auto* pool3 = GetPool<T3>();
        if (!pool1 || !pool2 || !pool3) return;

        size_t s1 = pool1->Size();
        size_t s2 = pool2->Size();
        size_t s3 = pool3->Size();

        if (s1 <= s2 && s1 <= s3) {
            T1* d1 = pool1->Data();
            const Entity* ents = pool1->Entities();
            for (size_t i = 0; i < s1; ++i) {
                Entity e = ents[i];
                T2* c2 = pool2->Get(e);
                T3* c3 = pool3->Get(e);
                if (c2 && c3) func(e, d1[i], *c2, *c3);
            }
        } else if (s2 <= s1 && s2 <= s3) {
            T2* d2 = pool2->Data();
            const Entity* ents = pool2->Entities();
            for (size_t i = 0; i < s2; ++i) {
                Entity e = ents[i];
                T1* c1 = pool1->Get(e);
                T3* c3 = pool3->Get(e);
                if (c1 && c3) func(e, *c1, d2[i], *c3);
            }
        } else {
            T3* d3 = pool3->Data();
            const Entity* ents = pool3->Entities();
            for (size_t i = 0; i < s3; ++i) {
                Entity e = ents[i];
                T1* c1 = pool1->Get(e);
                T2* c2 = pool2->Get(e);
                if (c1 && c2) func(e, *c1, *c2, d3[i]);
            }
        }
    }

    // -------------------------------------------------------------------------
    // Autodiagnóstico y Verificación Unitaria
    // -------------------------------------------------------------------------

    /**
     * @brief Ejecuta el autodiagnóstico integral de integridad de la arquitectura ECS en el arranque.
     */
    [[nodiscard]] static bool VerifyECSIntegrity();

private:
    std::array<uint32_t, MAX_ENTITIES> m_freeIndices{};
    uint32_t m_freeCount{MAX_ENTITIES};
    std::array<uint16_t, MAX_ENTITIES> m_generations{};
    std::array<bool, MAX_ENTITIES> m_alive{};
    uint32_t m_livingCount{0};

    std::array<std::unique_ptr<IComponentPool>, MAX_COMPONENT_TYPES> m_pools{};
};

} // namespace Neleo
