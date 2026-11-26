#pragma once

#include "Core/MemoryManager.hpp"
#include "Core/StackString.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Neleo {

/**
 * @brief [OPT-30 / OPT-184 / OPT-308 / OPT-399] Estado de una solicitud de lectura VFS asincrona.
 */
enum class VFSRequestStatus : uint8_t {
    Free = 0,
    Pending,
    Completed,
    Failed
};

/**
 * @brief Bloque alineado a 256 bytes para carga Zero-Copy en MEM2 / superficies GPU GX2.
 */
struct VFSAlignedBuffer {
    void* data{nullptr};
    size_t size{0};
    size_t alignment{256};
    MemoryTag tag{MemoryTag::General};

    [[nodiscard]] bool IsValid() const noexcept { return data != nullptr && size > 0; }
};

/**
 * @brief Handle generacional ligero para solicitudes de E/S asincrona en Core 2.
 */
struct VFSRequestHandle {
    uint16_t slot{0xFFFFu};
    uint16_t generation{0};

    [[nodiscard]] bool IsValid() const noexcept { return slot != 0xFFFFu; }
};

/**
 * @brief [OPT-30 / OPT-184 / OPT-308 / OPT-399] Virtual File System (VFS) Asincrono Zero-Copy.
 *
 * Unifica los puntos de montaje logicos del motor:
 * - "content:/" -> Assets empaquetados (resueltos via AssetManager con cache O(1))
 * - "save:/"    -> Espacio de guardado persistente (resuelto via StorageManager)
 *
 * Permite streaming asincrono en Core 2 (via JobSystem) directamente hacia memoria
 * alineada a 256 bytes (MemoryManager::AllocAligned), evitando copias intermedias
 * y bloqueos del hilo principal (Core 1) a 60 FPS.
 */
class VirtualFileSystem {
public:
    static constexpr size_t MAX_ASYNC_REQUESTS = 16;
    static constexpr size_t DEFAULT_ALIGNMENT  = 256;
    static constexpr size_t STREAM_CHUNK_SIZE  = 256 * 1024;

    static VirtualFileSystem& Instance() noexcept;

    VirtualFileSystem(const VirtualFileSystem&) = delete;
    VirtualFileSystem& operator=(const VirtualFileSystem&) = delete;
    VirtualFileSystem(VirtualFileSystem&&) = delete;
    VirtualFileSystem& operator=(VirtualFileSystem&&) = delete;

    void Initialize() noexcept;
    void Shutdown() noexcept;

    /**
     * @brief Resuelve una ruta virtual ("content:/...", "save:/...") hacia un bufer de salida fijo.
     * @note Para rutas "save:/" opera 100% en pila; para "content:/" delega en AssetManager con proteccion noexcept.
     */
    static bool ResolveVirtualPath(std::string_view virtualPath, char* outPath, size_t maxLen,
                                   int64_t* outFileSize = nullptr) noexcept;

    /**
     * @brief Lectura sincrona Zero-Copy directamente hacia un bufer alineado a 256B.
     */
    static bool ReadFileAligned(std::string_view virtualPath, VFSAlignedBuffer& outBuffer,
                                size_t alignment = DEFAULT_ALIGNMENT,
                                MemoryTag tag = MemoryTag::General,
                                const char* label = "VFS_Aligned") noexcept;

    /**
     * @brief Libera de forma simetrica un bufer asignado por VirtualFileSystem.
     */
    static void FreeAlignedBuffer(VFSAlignedBuffer& buffer) noexcept;

    /**
     * @brief Despacha una lectura asincrona Zero-Copy en Core 2 mediante JobSystem.
     */
    [[nodiscard]] VFSRequestHandle ReadFileAsync(std::string_view virtualPath,
                                                 size_t alignment = DEFAULT_ALIGNMENT,
                                                 MemoryTag tag = MemoryTag::General,
                                                 const char* label = "VFS_Async") noexcept;

    [[nodiscard]] VFSRequestStatus GetRequestStatus(VFSRequestHandle handle) const noexcept;

    /**
     * @brief Transfiere la propiedad del bufer alineado cuando el estado es Completed y libera la ranura.
     */
    bool TakeCompletedBuffer(VFSRequestHandle handle, VFSAlignedBuffer& outBuffer) noexcept;

    /**
     * @brief Cancela o descarta una solicitud completada/fallida liberando su memoria si no fue tomada.
     */
    void ReleaseRequest(VFSRequestHandle handle) noexcept;

    /**
     * @brief Espera a que finalicen todas las solicitudes en vuelo (ej. al cerrar escena o motor).
     */
    void WaitAll() noexcept;

    [[nodiscard]] uint32_t GetActiveRequestCount() const noexcept;

private:
    VirtualFileSystem() noexcept = default;
    ~VirtualFileSystem() noexcept;

    struct alignas(32) AsyncSlot {
        StackString<256> virtualPath{};
        VFSAlignedBuffer buffer{};
        size_t alignment{DEFAULT_ALIGNMENT};
        MemoryTag tag{MemoryTag::General};
        const char* label{"VFS_Async"};
        std::atomic<uint8_t> status{static_cast<uint8_t>(VFSRequestStatus::Free)};
        std::atomic<uint16_t> generation{1};
        std::atomic<uint32_t> jobCounter{0};
    };

    static void AsyncReadJobEntry(void* userData);

    std::array<AsyncSlot, MAX_ASYNC_REQUESTS> m_slots{};
    std::atomic_flag m_allocLock = ATOMIC_FLAG_INIT;
    std::atomic<bool> m_initialized{false};
};

} // namespace Neleo
