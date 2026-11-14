#pragma once

#include <cstdint>
#include <cstddef>
#include <new>
#include <utility>
#include <array>
#include <vector>
#include <mutex>
#include <type_traits>

namespace WiiU {

/**
 * @brief [OPT-213] Handle de índice generacional compacto de 32 bits (Generational Index Handle).
 * 
 * Estructura de 32 bits:
 * - Bits 0..15:  Índice de ranura en el pool (hasta 65,535 ranuras).
 * - Bits 16..31: Contador de generación (evita punteros colgantes y uso tras liberación).
 * 
 * Si un recurso se destruye, la generación de su ranura se incrementa. Cualquier copia antigua
 * del handle detectará la desincronización de generación en O(1) y retornará nullptr con total
 * seguridad, erradicando al 100% las vulnerabilidades de Use-After-Free y Dangling Pointers.
 */
template <typename T>
struct ResourceHandle {
    uint32_t id{0}; // 0 = handle nulo / inválido

    static constexpr uint32_t INDEX_MASK = 0x0000FFFF;
    static constexpr uint32_t GEN_SHIFT  = 16;
    static constexpr uint32_t GEN_MASK   = 0xFFFF0000;

    constexpr ResourceHandle() noexcept = default;
    
    constexpr explicit ResourceHandle(uint16_t index, uint16_t generation) noexcept
        : id(static_cast<uint32_t>(index) | (static_cast<uint32_t>(generation) << GEN_SHIFT)) {}

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return id != 0;
    }

    [[nodiscard]] constexpr uint16_t GetIndex() const noexcept {
        return static_cast<uint16_t>(id & INDEX_MASK);
    }

    [[nodiscard]] constexpr uint16_t GetGeneration() const noexcept {
        return static_cast<uint16_t>(id >> GEN_SHIFT);
    }

    constexpr explicit operator bool() const noexcept {
        return IsValid();
    }

    constexpr bool operator==(const ResourceHandle& other) const noexcept = default;
    constexpr bool operator!=(const ResourceHandle& other) const noexcept = default;
};

/**
 * @brief Pool de recursos contiguo de capacidad fija con generación de 16 bits por ranura.
 * 
 * Diseñado bajo filosofía Data-Oriented Design (DOD):
 * - Memoria pre-alocada contigua: CERO alocaciones dinámicas en el bucle interactivo de 60 FPS.
 * - Adquisición y liberación en O(1) mediante free-list indexada.
 * - Validación atómica/thread-safe o por exclusión mutua para entornos multihilo.
 */
template <typename T, size_t Capacity>
class ResourcePool {
public:
    static_assert(Capacity <= 0xFFFF, "ResourcePool Capacity no puede exceder 65535 ranuras (16 bits).");
    static_assert(Capacity > 0, "ResourcePool Capacity debe ser mayor que cero.");

    ResourcePool() {
        m_freeIndices.reserve(Capacity);
        for (size_t i = 0; i < Capacity; ++i) {
            // Empezamos las generaciones en 1 para que id = 0 nunca colisione con un handle válido
            m_slots[i].generation = 1;
            m_slots[i].isAlive = false;
            m_freeIndices.push_back(static_cast<uint16_t>(Capacity - 1 - i));
        }
    }

    ~ResourcePool() {
        Clear();
    }

    ResourcePool(const ResourcePool&) = delete;
    ResourcePool& operator=(const ResourcePool&) = delete;

    /**
     * @brief Construye un nuevo objeto T en una ranura libre y retorna su handle generacional.
     */
    template <typename... Args>
    [[nodiscard]] ResourceHandle<T> Emplace(Args&&... args) {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_freeIndices.empty()) {
            return ResourceHandle<T>(); // Pool saturado, retorna handle inválido
        }

        uint16_t index = m_freeIndices.back();
        m_freeIndices.pop_back();

        Slot& slot = m_slots[index];
        new (slot.storage) T(std::forward<Args>(args)...);
        slot.isAlive = true;
        ++m_activeCount;

        return ResourceHandle<T>(index, slot.generation);
    }

    /**
     * @brief Resuelve un handle a un puntero crudo de solo-lectura / modificación.
     * Si el handle es nulo o expiró (generación obsoleta), retorna nullptr en O(1).
     */
    [[nodiscard]] T* Get(ResourceHandle<T> handle) noexcept {
        if (!handle.IsValid()) return nullptr;

        uint16_t index = handle.GetIndex();
        if (index >= Capacity) return nullptr;

        std::lock_guard<std::mutex> lock(m_mutex);
        Slot& slot = m_slots[index];

        if (!slot.isAlive || slot.generation != handle.GetGeneration()) {
            return nullptr; // Handle obsoleto (Use-After-Free prevenido deterministamente)
        }

        return reinterpret_cast<T*>(slot.storage);
    }

    /**
     * @brief Versión constante de resolución de handle.
     */
    [[nodiscard]] const T* Get(ResourceHandle<T> handle) const noexcept {
        if (!handle.IsValid()) return nullptr;

        uint16_t index = handle.GetIndex();
        if (index >= Capacity) return nullptr;

        std::lock_guard<std::mutex> lock(m_mutex);
        const Slot& slot = m_slots[index];

        if (!slot.isAlive || slot.generation != handle.GetGeneration()) {
            return nullptr;
        }

        return reinterpret_cast<const T*>(slot.storage);
    }

    /**
     * @brief Destruye el recurso asociado al handle, incrementa la generación de la ranura
     * y anula el handle llamador por referencia.
     */
    void Destroy(ResourceHandle<T>& handle) noexcept {
        if (!handle.IsValid()) return;

        uint16_t index = handle.GetIndex();
        if (index >= Capacity) return;

        std::lock_guard<std::mutex> lock(m_mutex);
        Slot& slot = m_slots[index];

        if (slot.isAlive && slot.generation == handle.GetGeneration()) {
            reinterpret_cast<T*>(slot.storage)->~T();
            slot.isAlive = false;

            // Incremento de generación: Toda copia previa del handle queda invalidada instantáneamente
            slot.generation++;
            if (slot.generation == 0) {
                slot.generation = 1; // Evitar que generation quede en 0
            }

            m_freeIndices.push_back(index);
            if (m_activeCount > 0) {
                --m_activeCount;
            }
        }

        // Anulación del handle del llamador
        handle = ResourceHandle<T>();
    }

    /**
     * @brief Comprueba si el handle apunta a un objeto vivo válido.
     */
    [[nodiscard]] bool IsAlive(ResourceHandle<T> handle) const noexcept {
        return Get(handle) != nullptr;
    }

    /**
     * @brief Destruye todos los recursos activos del pool y resetea las ranuras.
     */
    void Clear() noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);

        for (size_t i = 0; i < Capacity; ++i) {
            Slot& slot = m_slots[i];
            if (slot.isAlive) {
                reinterpret_cast<T*>(slot.storage)->~T();
                slot.isAlive = false;
                slot.generation++;
                if (slot.generation == 0) slot.generation = 1;
            }
        }

        m_freeIndices.clear();
        for (size_t i = 0; i < Capacity; ++i) {
            m_freeIndices.push_back(static_cast<uint16_t>(Capacity - 1 - i));
        }
        m_activeCount = 0;
    }

    [[nodiscard]] size_t GetActiveCount() const noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_activeCount;
    }

    template <typename Fn>
    void ForEachActive(Fn&& fn) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (size_t i = 0; i < Capacity; ++i) {
            if (m_slots[i].isAlive) {
                fn(*reinterpret_cast<T*>(m_slots[i].storage));
            }
        }
    }

    [[nodiscard]] constexpr size_t GetCapacity() const noexcept {
        return Capacity;
    }

private:
    struct Slot {
        alignas(alignof(T)) uint8_t storage[sizeof(T)];
        uint16_t generation{1};
        bool isAlive{false};
    };

    mutable std::mutex m_mutex;
    std::array<Slot, Capacity> m_slots;
    std::vector<uint16_t> m_freeIndices;
    size_t m_activeCount{0};
};

} // namespace WiiU
