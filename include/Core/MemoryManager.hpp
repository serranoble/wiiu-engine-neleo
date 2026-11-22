#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <utility>
#include <mutex>
#include <vector>
#include <algorithm>
#include "Core/ResourceHandle.hpp"
#include "Core/ArenaVector.hpp"

namespace WiiU {

/**
 * @brief Categorías de memoria para auditoría, rastreo y diagnóstico en silicio Wii U.
 */
enum class MemoryTag : uint8_t {
    General = 0,
    Scene,
    ShaderProgram,   // Memoria de instrucciones de shader alineada a 256 bytes
    VertexBuffer,    // Búferes de vértices (VBOs en MEM1/MEM2)
    IndexBuffer,     // Búferes de índices (IBOs)
    Texture,         // Texturas y superficies de color
    SurfaceDepth,    // Búfer de profundidad / Z-Buffer en eDRAM
    ContextState,    // Estados de contexto de hardware GX2 (alineados a GX2_CONTEXT_STATE_ALIGNMENT)
    Audio,           // Streaming y decodificación de audio
    UI,              // Primitivas de interfaz de usuario
    Count
};

/**
 * @brief Obtiene el nombre textual de una categoría de memoria.
 */
inline const char* GetMemoryTagName(MemoryTag tag) noexcept {
    switch (tag) {
        case MemoryTag::General:       return "GENERAL";
        case MemoryTag::Scene:         return "ESCENA";
        case MemoryTag::ShaderProgram: return "SHADER_PGM";
        case MemoryTag::VertexBuffer:  return "VBO";
        case MemoryTag::IndexBuffer:   return "IBO";
        case MemoryTag::Texture:       return "TEXTURA";
        case MemoryTag::SurfaceDepth:  return "EDRAM_DEPTH";
        case MemoryTag::ContextState:  return "CONTEXT_STATE";
        case MemoryTag::Audio:         return "AUDIO";
        case MemoryTag::UI:            return "UI_2D";
        default:                       return "DESCONOCIDO";
    }
}

/**
 * @brief Registro individual de una alocación activa en el motor.
 */
struct MemoryRecord {
    void* address{nullptr};
    size_t size{0};
    MemoryTag tag{MemoryTag::General};
    const char* label{nullptr};
    bool isAligned{false};
};

/**
 * @brief [OPT-43 / OPT-113] Arena lineal de cuadro (Transient Linear Frame Scratchpad).
 * 
 * Permite realizar micro-alocaciones temporales de altísima velocidad durante el fotograma
 * activo mediante un puntero bump, garantizando CERO llamadas a malloc/free en el hot path.
 * Se limpia automáticamente al inicio de cada fotograma.
 */
class LinearFrameArena {
public:
    using Marker = size_t;
    static constexpr size_t DEFAULT_CAPACITY = 128 * 1024; // 128 KB en memoria rápida

    explicit LinearFrameArena(size_t capacity = DEFAULT_CAPACITY);
    ~LinearFrameArena();

    LinearFrameArena(const LinearFrameArena&) = delete;
    LinearFrameArena& operator=(const LinearFrameArena&) = delete;
    LinearFrameArena(LinearFrameArena&&) = delete;
    LinearFrameArena& operator=(LinearFrameArena&&) = delete;

    void* Alloc(size_t bytes, size_t alignment = 8) noexcept;

    template<typename T, typename... Args>
    T* Create(Args&&... args) {
        void* mem = Alloc(sizeof(T), alignof(T));
        if (!mem) return nullptr;
        return new (mem) T(std::forward<Args>(args)...);
    }

    [[nodiscard]] Marker GetMarker() const noexcept { return m_offset; }
    void FreeToMarker(Marker marker) noexcept {
        if (marker <= m_offset) {
            m_offset = marker;
        }
    }

    void Reset() noexcept;

    [[nodiscard]] size_t GetUsedBytes() const noexcept { return m_offset; }
    [[nodiscard]] size_t GetCapacity() const noexcept { return m_capacity; }
    [[nodiscard]] size_t GetPeakUsedBytes() const noexcept { return m_peakUsed; }

private:
    uint8_t* m_buffer{nullptr};
    size_t m_capacity{0};
    size_t m_offset{0};
    size_t m_peakUsed{0};
};

/**
 * @brief [OPT-214] Arena de Escena / Nivel (Scoped Level Arena).
 * 
 * Bloque de memoria continua dedicado a la escena activa (2 MB por defecto).
 * Permite que cualquier recurso, geometría procedimental, buffers de colisión o
 * estructuras de nivel se aloquen a máxima velocidad sin fragmentar el heap.
 * Al descargarse la escena, se resetea en O(1), haciendo imposible olvidar liberar
 * memoria entre niveles. Soporta marcadores jerárquicos (Marker / Rollback).
 */
class SceneArena {
public:
    using Marker = size_t;
    static constexpr size_t DEFAULT_CAPACITY = 2 * 1024 * 1024; // 2 MB para datos de nivel

    explicit SceneArena(size_t capacity = DEFAULT_CAPACITY);
    ~SceneArena();

    SceneArena(const SceneArena&) = delete;
    SceneArena& operator=(const SceneArena&) = delete;
    SceneArena(SceneArena&&) = delete;
    SceneArena& operator=(SceneArena&&) = delete;

    void* Alloc(size_t bytes, size_t alignment = 32) noexcept;

    template<typename T, typename... Args>
    T* Create(Args&&... args) {
        void* mem = Alloc(sizeof(T), alignof(T));
        if (!mem) return nullptr;
        return new (mem) T(std::forward<Args>(args)...);
    }

    [[nodiscard]] Marker GetMarker() const noexcept { return m_offset; }
    void FreeToMarker(Marker marker) noexcept {
        if (marker <= m_offset) {
            m_offset = marker;
        }
    }

    void Reset() noexcept;

    [[nodiscard]] size_t GetUsedBytes() const noexcept { return m_offset; }
    [[nodiscard]] size_t GetCapacity() const noexcept { return m_capacity; }
    [[nodiscard]] size_t GetPeakUsedBytes() const noexcept { return m_peakUsed; }

private:
    uint8_t* m_buffer{nullptr};
    size_t m_capacity{0};
    size_t m_offset{0};
    size_t m_peakUsed{0};
};

/**
 * @brief Gestor central de memoria, ciclo de vida y erradicación de fugas (MemoryManager).
 * 
 * Implementa:
 * 1. Asignaciones regulares y alineadas (256B para shaders, GX2_CONTEXT_STATE_ALIGNMENT).
 * 2. Supresión absoluta de punteros sueltos (*Anti-Dangling Pointer Guarantee*):
 *    las funciones de liberación aceptan referencias a puntero (`T*& ptr`) y los ponen a `nullptr`.
 * 3. Auditoría en tiempo real de bytes consumidos por categoría, conteo de bloques activos y pico.
 * 4. Verificación determinista de descarga de escena (`AuditSceneTeardown`), asegurando
 *    que al retornar al menú o cambiar de demo no queden fugas de recursos en Cafe OS.
 * 5. Acceso al LinearFrameArena (cuadro) y SceneArena (nivel).
 */
class MemoryManager {
public:
    static MemoryManager& Instance() {
        static MemoryManager instance;
        return instance;
    }

    // Alocaciones estándar en Heap
    void* Alloc(size_t size, MemoryTag tag = MemoryTag::General, const char* label = nullptr);
    void Free(void*& ptr);

    // Alocaciones con alineación específica (ej. 256 bytes para shaders CafeGLSL, 256 para GX2ContextState)
    void* AllocAligned(size_t alignment, size_t size, MemoryTag tag = MemoryTag::General, const char* label = nullptr);
    void FreeAligned(void*& ptr);

    // Construcción y destrucción de objetos tipados con garantía anti-puntero suelto
    template<typename T, typename... Args>
    T* New(MemoryTag tag, const char* label, Args&&... args) {
        void* mem = Alloc(sizeof(T), tag, label);
        if (!mem) return nullptr;
        return new (mem) T(std::forward<Args>(args)...);
    }

    template<typename T>
    void Delete(T*& ptr) {
        if (!ptr) return;
        ptr->~T();
        void* raw = static_cast<void*>(ptr);
        Free(raw);
        ptr = nullptr;
    }

    // Registro de recursos de hardware GX2 (VBOs, IBOs, DepthBuffers en eDRAM) para telemetría
    void RegisterGX2Resource(const void* handle, size_t bytes, MemoryTag tag, const char* label);
    void UnregisterGX2Resource(const void* handle);

    // Telemetría y auditoría
    [[nodiscard]] size_t GetActiveAllocationCount() const noexcept;
    [[nodiscard]] size_t GetTotalAllocatedBytes() const noexcept;
    [[nodiscard]] size_t GetPeakAllocatedBytes() const noexcept;
    [[nodiscard]] size_t GetAllocatedBytesByTag(MemoryTag tag) const noexcept;

    // Auditoría de fin de escena: reporta o verifica la liberación completa de recursos de la escena
    void AuditSceneTeardown(const char* sceneName);

    // Validación final al apagar el motor
    void AssertNoLeaks() const;

    // Autodiagnóstico de arquitectura de memoria (ResourcePool, SceneArena, FrameArena)
    [[nodiscard]] bool VerifyMemoryIntegrity();

    // Acceso al Scoped Frame Arena (por cuadro) y Scene Arena (por nivel)
    [[nodiscard]] LinearFrameArena& GetFrameArena() noexcept { return m_frameArena; }
    [[nodiscard]] SceneArena& GetSceneArena() noexcept { return m_sceneArena; }

private:
    MemoryManager();
    ~MemoryManager();

    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;
    MemoryManager(MemoryManager&&) = delete;
    MemoryManager& operator=(MemoryManager&&) = delete;

    mutable std::mutex m_mutex;
    std::vector<MemoryRecord> m_records;
    std::atomic<size_t> m_activeAllocationCount{0};
    std::atomic<size_t> m_totalAllocatedBytes{0};
    std::atomic<size_t> m_peakAllocatedBytes{0};
    std::atomic<size_t> m_bytesByTag[static_cast<size_t>(MemoryTag::Count)]{};

    LinearFrameArena m_frameArena;
    SceneArena m_sceneArena;
};

} // namespace WiiU
