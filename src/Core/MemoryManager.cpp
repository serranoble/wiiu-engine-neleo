#include "Core/MemoryManager.hpp"
#include <cstdio>
#include <cstring>

#if defined(_MSC_VER)
#include <malloc.h>
#define WIIU_MEMALIGN(align, size) _aligned_malloc(size, align)
#define WIIU_FREEALIGNED(ptr) _aligned_free(ptr)
#elif defined(__WIIU__) || defined(__wiiu__) || defined(__GNUC__)
#include <malloc.h>
#define WIIU_MEMALIGN(align, size) memalign(align, size)
#define WIIU_FREEALIGNED(ptr) free(ptr)
#else
#include <cstdlib>
static inline void* WiiUAlignedAllocFallback(size_t align, size_t size) noexcept {
    const size_t safeAlign = (align < sizeof(void*) || (align & (align - 1)) != 0) ? sizeof(void*) : align;
    const size_t roundedSize = (size + safeAlign - 1) & ~(safeAlign - 1);
    return std::aligned_alloc(safeAlign, roundedSize);
}
#define WIIU_MEMALIGN(align, size) WiiUAlignedAllocFallback(align, size)
#define WIIU_FREEALIGNED(ptr) std::free(ptr)
#endif

namespace WiiU {

// ============================================================================
// LinearFrameArena [OPT-43 / OPT-113]
// ============================================================================

LinearFrameArena::LinearFrameArena(size_t capacity)
    : m_capacity(capacity)
    , m_offset(0)
    , m_peakUsed(0)
{
    if (m_capacity > 0) {
        // Alínea a 32 bytes para encajar con las líneas de caché L1D de Espresso (PPC 750)
        m_buffer = static_cast<uint8_t*>(WIIU_MEMALIGN(32, m_capacity));
    }
}

LinearFrameArena::~LinearFrameArena() {
    if (m_buffer) {
        WIIU_FREEALIGNED(m_buffer);
        m_buffer = nullptr;
    }
    m_capacity = 0;
    m_offset = 0;
}

void* LinearFrameArena::Alloc(size_t bytes, size_t alignment) noexcept {
    if (!m_buffer || bytes == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0) return nullptr;

    // Calcular alineación hacia arriba con protección anti-overflow
    const uintptr_t baseAddr = reinterpret_cast<uintptr_t>(m_buffer);
    const uintptr_t currentAddr = baseAddr + m_offset;
    const uintptr_t mask = static_cast<uintptr_t>(alignment - 1);
    if (currentAddr > UINTPTR_MAX - mask) return nullptr;
    const uintptr_t alignedAddr = (currentAddr + mask) & ~mask;
    const size_t alignedOffset = static_cast<size_t>(alignedAddr - baseAddr);
    if (alignedOffset > m_capacity || bytes > m_capacity - alignedOffset) {
        return nullptr;
    }

    m_offset = alignedOffset + bytes;
    if (m_offset > m_peakUsed) {
        m_peakUsed = m_offset;
    }

    return reinterpret_cast<void*>(alignedAddr);
}

void LinearFrameArena::Reset() noexcept {
    // Liberación O(1) de todas las alocaciones efímeras del cuadro
    m_offset = 0;
}

// ============================================================================
// SceneArena [OPT-214] (Scoped Level Arena)
// ============================================================================

SceneArena::SceneArena(size_t capacity)
    : m_capacity(capacity)
    , m_offset(0)
    , m_peakUsed(0)
{
    if (m_capacity > 0) {
        // Alínea a 256 bytes para compatibilidad nativa con hardware GX2 (shaders y VBOs)
        m_buffer = static_cast<uint8_t*>(WIIU_MEMALIGN(256, m_capacity));
    }
}

SceneArena::~SceneArena() {
    if (m_buffer) {
        WIIU_FREEALIGNED(m_buffer);
        m_buffer = nullptr;
    }
    m_capacity = 0;
    m_offset = 0;
}

void* SceneArena::Alloc(size_t bytes, size_t alignment) noexcept {
    if (!m_buffer || bytes == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0) return nullptr;

    const uintptr_t baseAddr = reinterpret_cast<uintptr_t>(m_buffer);
    const uintptr_t currentAddr = baseAddr + m_offset;
    const uintptr_t mask = static_cast<uintptr_t>(alignment - 1);
    if (currentAddr > UINTPTR_MAX - mask) return nullptr;
    const uintptr_t alignedAddr = (currentAddr + mask) & ~mask;
    const size_t alignedOffset = static_cast<size_t>(alignedAddr - baseAddr);
    if (alignedOffset > m_capacity || bytes > m_capacity - alignedOffset) {
        return nullptr;
    }

    m_offset = alignedOffset + bytes;
    if (m_offset > m_peakUsed) {
        m_peakUsed = m_offset;
    }

    return reinterpret_cast<void*>(alignedAddr);
}

void SceneArena::Reset() noexcept {
    // Liberación O(1) determinista de todo recurso asignado al nivel
    m_offset = 0;
}

// ============================================================================
// MemoryManager (Auditoría Central, Anti-Dangling Pointers y Ciclo de Vida)
// ============================================================================

MemoryManager::MemoryManager()
    : m_activeAllocationCount(0)
    , m_totalAllocatedBytes(0)
    , m_peakAllocatedBytes(0)
    , m_frameArena(LinearFrameArena::DEFAULT_CAPACITY)
    , m_sceneArena(SceneArena::DEFAULT_CAPACITY)
{
    for (auto& b : m_bytesByTag) {
        b.store(0, std::memory_order_relaxed);
    }
    m_records.reserve(256);
}

MemoryManager::~MemoryManager() {
    // Al apagar el motor, valida que no hayan fugas críticas
    AssertNoLeaks();
}

void* MemoryManager::Alloc(size_t size, MemoryTag tag, const char* label) {
    if (size == 0) return nullptr;

    void* ptr = std::malloc(size);
    if (!ptr) return nullptr;

    std::lock_guard<std::mutex> lock(m_mutex);

    MemoryRecord record;
    record.address = ptr;
    record.size = size;
    record.tag = tag;
    record.label = label ? label : "Unlabeled";
    record.isAligned = false;

    m_records.push_back(record);
    m_activeAllocationCount.store(m_records.size(), std::memory_order_relaxed);
    const size_t newTotal = m_totalAllocatedBytes.load(std::memory_order_relaxed) + size;
    m_totalAllocatedBytes.store(newTotal, std::memory_order_relaxed);
    if (newTotal > m_peakAllocatedBytes.load(std::memory_order_relaxed)) {
        m_peakAllocatedBytes.store(newTotal, std::memory_order_relaxed);
    }
    const size_t tagIdx = static_cast<size_t>(tag);
    if (tagIdx < static_cast<size_t>(MemoryTag::Count)) {
        m_bytesByTag[tagIdx].fetch_add(size, std::memory_order_relaxed);
    }

    return ptr;
}

void MemoryManager::Free(void*& ptr) {
    if (!ptr) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = std::find_if(m_records.begin(), m_records.end(),
        [ptr](const MemoryRecord& rec) { return rec.address == ptr; });

    if (it != m_records.end()) {
        const size_t sz = it->size;
        const size_t tagIdx = static_cast<size_t>(it->tag);
        const size_t curTotal = m_totalAllocatedBytes.load(std::memory_order_relaxed);
        m_totalAllocatedBytes.store((curTotal >= sz) ? (curTotal - sz) : 0, std::memory_order_relaxed);
        if (tagIdx < static_cast<size_t>(MemoryTag::Count)) {
            const size_t curTag = m_bytesByTag[tagIdx].load(std::memory_order_relaxed);
            m_bytesByTag[tagIdx].store((curTag >= sz) ? (curTag - sz) : 0, std::memory_order_relaxed);
        }
        *it = m_records.back();
        m_records.pop_back();
        m_activeAllocationCount.store(m_records.size(), std::memory_order_relaxed);
    }

    std::free(ptr);
    // Garantía Anti-Puntero Suelto: se anula el puntero original por referencia
    ptr = nullptr;
}

void* MemoryManager::AllocAligned(size_t alignment, size_t size, MemoryTag tag, const char* label) {
    if (size == 0) return nullptr;

    void* ptr = WIIU_MEMALIGN(alignment, size);
    if (!ptr) return nullptr;

    std::lock_guard<std::mutex> lock(m_mutex);

    MemoryRecord record;
    record.address = ptr;
    record.size = size;
    record.tag = tag;
    record.label = label ? label : "Aligned_Resource";
    record.isAligned = true;

    m_records.push_back(record);
    m_activeAllocationCount.store(m_records.size(), std::memory_order_relaxed);
    const size_t newTotal = m_totalAllocatedBytes.load(std::memory_order_relaxed) + size;
    m_totalAllocatedBytes.store(newTotal, std::memory_order_relaxed);
    if (newTotal > m_peakAllocatedBytes.load(std::memory_order_relaxed)) {
        m_peakAllocatedBytes.store(newTotal, std::memory_order_relaxed);
    }
    const size_t tagIdx = static_cast<size_t>(tag);
    if (tagIdx < static_cast<size_t>(MemoryTag::Count)) {
        m_bytesByTag[tagIdx].fetch_add(size, std::memory_order_relaxed);
    }

    return ptr;
}

void MemoryManager::FreeAligned(void*& ptr) {
    if (!ptr) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = std::find_if(m_records.begin(), m_records.end(),
        [ptr](const MemoryRecord& rec) { return rec.address == ptr; });

    if (it != m_records.end()) {
        const size_t sz = it->size;
        const size_t tagIdx = static_cast<size_t>(it->tag);
        const size_t curTotal = m_totalAllocatedBytes.load(std::memory_order_relaxed);
        m_totalAllocatedBytes.store((curTotal >= sz) ? (curTotal - sz) : 0, std::memory_order_relaxed);
        if (tagIdx < static_cast<size_t>(MemoryTag::Count)) {
            const size_t curTag = m_bytesByTag[tagIdx].load(std::memory_order_relaxed);
            m_bytesByTag[tagIdx].store((curTag >= sz) ? (curTag - sz) : 0, std::memory_order_relaxed);
        }
        *it = m_records.back();
        m_records.pop_back();
        m_activeAllocationCount.store(m_records.size(), std::memory_order_relaxed);
    }

    WIIU_FREEALIGNED(ptr);
    // Garantía Anti-Puntero Suelto: se anula el puntero original por referencia
    ptr = nullptr;
}

void MemoryManager::RegisterGX2Resource(const void* handle, size_t bytes, MemoryTag tag, const char* label) {
    if (!handle || bytes == 0) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    MemoryRecord record;
    record.address = const_cast<void*>(handle);
    record.size = bytes;
    record.tag = tag;
    record.label = label ? label : "GX2_Hardware_Resource";
    record.isAligned = true;

    m_records.push_back(record);
    m_activeAllocationCount.store(m_records.size(), std::memory_order_relaxed);
    const size_t newTotal = m_totalAllocatedBytes.load(std::memory_order_relaxed) + bytes;
    m_totalAllocatedBytes.store(newTotal, std::memory_order_relaxed);
    if (newTotal > m_peakAllocatedBytes.load(std::memory_order_relaxed)) {
        m_peakAllocatedBytes.store(newTotal, std::memory_order_relaxed);
    }
    const size_t tagIdx = static_cast<size_t>(tag);
    if (tagIdx < static_cast<size_t>(MemoryTag::Count)) {
        m_bytesByTag[tagIdx].fetch_add(bytes, std::memory_order_relaxed);
    }
}

void MemoryManager::UnregisterGX2Resource(const void* handle) {
    if (!handle) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = std::find_if(m_records.begin(), m_records.end(),
        [handle](const MemoryRecord& rec) { return rec.address == handle; });

    if (it != m_records.end()) {
        const size_t sz = it->size;
        const size_t tagIdx = static_cast<size_t>(it->tag);
        const size_t curTotal = m_totalAllocatedBytes.load(std::memory_order_relaxed);
        m_totalAllocatedBytes.store((curTotal >= sz) ? (curTotal - sz) : 0, std::memory_order_relaxed);
        if (tagIdx < static_cast<size_t>(MemoryTag::Count)) {
            const size_t curTag = m_bytesByTag[tagIdx].load(std::memory_order_relaxed);
            m_bytesByTag[tagIdx].store((curTag >= sz) ? (curTag - sz) : 0, std::memory_order_relaxed);
        }
        *it = m_records.back();
        m_records.pop_back();
        m_activeAllocationCount.store(m_records.size(), std::memory_order_relaxed);
    }
}

size_t MemoryManager::GetActiveAllocationCount() const noexcept {
    return m_activeAllocationCount.load(std::memory_order_relaxed);
}

size_t MemoryManager::GetTotalAllocatedBytes() const noexcept {
    return m_totalAllocatedBytes.load(std::memory_order_relaxed);
}

size_t MemoryManager::GetPeakAllocatedBytes() const noexcept {
    return m_peakAllocatedBytes.load(std::memory_order_relaxed);
}

size_t MemoryManager::GetAllocatedBytesByTag(MemoryTag tag) const noexcept {
    const size_t idx = static_cast<size_t>(tag);
    if (idx < static_cast<size_t>(MemoryTag::Count)) {
        return m_bytesByTag[idx].load(std::memory_order_relaxed);
    }
    return 0;
}

void MemoryManager::AuditSceneTeardown(const char* sceneName) {
    std::lock_guard<std::mutex> lock(m_mutex);

    size_t sceneLingeringBytes = 0;
    size_t sceneLingeringBlocks = 0;

    for (const auto& rec : m_records) {
        if (rec.tag == MemoryTag::Scene ||
            rec.tag == MemoryTag::ShaderProgram ||
            rec.tag == MemoryTag::VertexBuffer ||
            rec.tag == MemoryTag::IndexBuffer ||
            rec.tag == MemoryTag::ContextState ||
            rec.tag == MemoryTag::SurfaceDepth) {
            sceneLingeringBytes += rec.size;
            sceneLingeringBlocks++;
        }
    }

    if (sceneLingeringBlocks > 0) {
        // Se reporta en consola de depuración Cafe OS / log
        std::printf("[MemoryManager] ADVERTENCIA: Al descargar '%s' quedaron %zu bloques (%zu bytes) sin liberar.\n",
                    sceneName ? sceneName : "Escena", sceneLingeringBlocks, sceneLingeringBytes);
    }

    // Reset O(1) determinista de la arena de escena
    m_sceneArena.Reset();
}

void MemoryManager::AssertNoLeaks() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_records.empty()) {
        std::printf("[MemoryManager] AVISO: Al cerrar el motor existen %zu asignaciones activas (%zu bytes).\n",
                    m_records.size(), m_totalAllocatedBytes.load(std::memory_order_relaxed));
    }
}

bool MemoryManager::VerifyMemoryIntegrity() {
    // 1. Verificación de ResourcePool & ResourceHandle (OPT-213)
    struct DummyResource {
        uint32_t magic{0xCAFEBABE};
        int value{42};
    };

    ResourcePool<DummyResource, 8> pool;
    auto h1 = pool.Emplace();
    if (!h1.IsValid()) {
        std::printf("[MemoryManager] ERROR: Fallo al crear ResourceHandle inicial.\n");
        return false;
    }

    DummyResource* res1 = pool.Get(h1);
    if (!res1 || res1->magic != 0xCAFEBABE || res1->value != 42) {
        std::printf("[MemoryManager] ERROR: Fallo al resolver ResourceHandle h1.\n");
        return false;
    }

    // Copia de handle que debe quedar invalidada al destruir el recurso
    ResourceHandle<DummyResource> staleCopy = h1;
    pool.Destroy(h1);

    if (h1.IsValid()) {
        std::printf("[MemoryManager] ERROR: El handle original no fue anulado tras Destroy.\n");
        return false;
    }

    if (pool.Get(staleCopy) != nullptr) {
        std::printf("[MemoryManager] ERROR CRÍTICO: Fallo en Generational Check. Handle obsoleto retornó puntero vivo.\n");
        return false;
    }

    // Alocar de nuevo en la misma ranura: la nueva generación debe diferir de staleCopy
    auto h2 = pool.Emplace();
    if (h2.GetIndex() == staleCopy.GetIndex() && pool.Get(staleCopy) != nullptr) {
        std::printf("[MemoryManager] ERROR CRÍTICO: Use-after-free detectado en reutilización de ranura.\n");
        return false;
    }
    pool.Destroy(h2);

    // 2. Verificación de SceneArena (OPT-214)
    void* sceneAlloc = m_sceneArena.Alloc(128, 256);
    if (!sceneAlloc || (reinterpret_cast<uintptr_t>(sceneAlloc) & 255) != 0) {
        std::printf("[MemoryManager] ERROR: Fallo en alineación de 256 bytes en SceneArena.\n");
        return false;
    }
    SceneArena::Marker marker = m_sceneArena.GetMarker();
    m_sceneArena.Alloc(256, 32);
    m_sceneArena.FreeToMarker(marker);
    if (m_sceneArena.GetUsedBytes() != marker) {
        std::printf("[MemoryManager] ERROR: Fallo en Scoped Rollback de SceneArena.\n");
        return false;
    }
    m_sceneArena.Reset();
    if (m_sceneArena.GetUsedBytes() != 0) {
        std::printf("[MemoryManager] ERROR: Fallo en Reset O(1) de SceneArena.\n");
        return false;
    }

    // 3. Verificación de LinearFrameArena
    void* frameAlloc = m_frameArena.Alloc(64, 32);
    if (!frameAlloc || (reinterpret_cast<uintptr_t>(frameAlloc) & 31) != 0) {
        std::printf("[MemoryManager] ERROR: Fallo en alineación de 32 bytes en LinearFrameArena.\n");
        return false;
    }
    m_frameArena.Reset();

    std::printf("[MemoryManager] EXITO: Arquitectura de memoria determinista (ResourcePool, SceneArena, FrameArena) 100%% VERIFICADA.\n");
    return true;
}

} // namespace WiiU
