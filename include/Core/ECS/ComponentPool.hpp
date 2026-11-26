#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <array>
#include "Core/ECS/Entity.hpp"

namespace Neleo {

/**
 * @brief Interfaz polimórfica para contenedores de componentes heterogéneos en ECSWorld.
 */
class IComponentPool {
public:
    virtual ~IComponentPool() = default;

    [[nodiscard]] virtual bool Has(Entity entity) const noexcept = 0;
    virtual bool Remove(Entity entity) noexcept = 0;
    virtual void Clear() noexcept = 0;
    [[nodiscard]] virtual size_t Size() const noexcept = 0;
    [[nodiscard]] virtual size_t Capacity() const noexcept = 0;
};

/**
 * @brief [OPT-236] Contenedor de componentes Sparse-Set orientado a datos (Data-Oriented Design).
 * 
 * Estructura de almacenamiento dual:
 * 1. Arreglo Denso (Dense Data & Dense Entities): Almacena los componentes de forma compacta y contigua.
 *    Garantiza que la iteración de los sistemas en el bucle a 60 FPS sature al 100% las líneas de caché L1D
 *    (32 bytes) del procesador PowerPC 750CL Espresso mediante hardware prefetch secuencial.
 * 2. Arreglo Disperso (Sparse Array): Mapea el índice de la entidad a su ranura en el arreglo denso.
 * 
 * Complejidad de Operaciones:
 * - Has(Entity): O(1) tiempo constante (doble verificación de índice e identidad).
 * - Get(Entity): O(1) acceso directo.
 * - Emplace(Entity, ...): O(1) inserción al final del arreglo denso.
 * - Remove(Entity): O(1) eliminación mediante intercambio con el último elemento (Swap-and-Pop).
 * - Iteración: O(N) lineal y contigua sin huecos ni saltos indirectos de punteros.
 */
template <typename T, size_t MaxCapacity = 2048>
class ComponentPool final : public IComponentPool {
public:
    static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFF;

    ComponentPool() {
        m_sparse.fill(INVALID_INDEX);
    }

    ~ComponentPool() override = default;

    ComponentPool(const ComponentPool&) = delete;
    ComponentPool& operator=(const ComponentPool&) = delete;
    ComponentPool(ComponentPool&&) noexcept = default;
    ComponentPool& operator=(ComponentPool&&) noexcept = default;

    [[nodiscard]] bool Has(Entity entity) const noexcept override {
        if (!entity.IsValid() || entity.Index() >= MaxCapacity) {
            return false;
        }
        uint32_t denseIdx = m_sparse[entity.Index()];
        return (denseIdx < m_denseCount) && (m_denseToEntity[denseIdx] == entity);
    }

    [[nodiscard]] T* Get(Entity entity) noexcept {
        if (!Has(entity)) {
            return nullptr;
        }
        return &m_denseData[m_sparse[entity.Index()]];
    }

    [[nodiscard]] const T* Get(Entity entity) const noexcept {
        if (!Has(entity)) {
            return nullptr;
        }
        return &m_denseData[m_sparse[entity.Index()]];
    }

    template <typename... Args>
    T& Emplace(Entity entity, Args&&... args) {
        if (Has(entity)) {
            uint32_t denseIdx = m_sparse[entity.Index()];
            m_denseData[denseIdx] = T(std::forward<Args>(args)...);
            return m_denseData[denseIdx];
        }

        if (m_denseCount >= MaxCapacity || entity.Index() >= MaxCapacity) {
            static T fallback{};
            return fallback;
        }

        uint32_t denseIdx = m_denseCount++;
        m_denseData[denseIdx] = T(std::forward<Args>(args)...);
        m_denseToEntity[denseIdx] = entity;
        m_sparse[entity.Index()] = denseIdx;

        return m_denseData[denseIdx];
    }

    bool Remove(Entity entity) noexcept override {
        if (!Has(entity)) {
            return false;
        }

        uint32_t denseIdxToRemove = m_sparse[entity.Index()];
        uint32_t lastDenseIdx = m_denseCount - 1;

        if (denseIdxToRemove != lastDenseIdx) {
            // Mover el último componente al hueco liberado
            m_denseData[denseIdxToRemove] = std::move(m_denseData[lastDenseIdx]);
            Entity lastEntity = m_denseToEntity[lastDenseIdx];
            m_denseToEntity[denseIdxToRemove] = lastEntity;
            m_sparse[lastEntity.Index()] = denseIdxToRemove;
        }

        m_sparse[entity.Index()] = INVALID_INDEX;
        --m_denseCount;
        return true;
    }

    void Clear() noexcept override {
        m_sparse.fill(INVALID_INDEX);
        m_denseCount = 0;
    }

    [[nodiscard]] size_t Size() const noexcept override { return m_denseCount; }
    [[nodiscard]] size_t Capacity() const noexcept override { return MaxCapacity; }

    [[nodiscard]] T* Data() noexcept { return m_denseData.data(); }
    [[nodiscard]] const T* Data() const noexcept { return m_denseData.data(); }
    [[nodiscard]] const Entity* Entities() const noexcept { return m_denseToEntity.data(); }

    // Iteradores estándar para bucles basados en rango
    [[nodiscard]] T* begin() noexcept { return m_denseData.data(); }
    [[nodiscard]] T* end() noexcept { return m_denseData.data() + m_denseCount; }
    [[nodiscard]] const T* begin() const noexcept { return m_denseData.data(); }
    [[nodiscard]] const T* end() const noexcept { return m_denseData.data() + m_denseCount; }

private:
    std::array<T, MaxCapacity> m_denseData{};
    std::array<Entity, MaxCapacity> m_denseToEntity{};
    std::array<uint32_t, MaxCapacity> m_sparse{};
    uint32_t m_denseCount{0};
};

} // namespace Neleo
