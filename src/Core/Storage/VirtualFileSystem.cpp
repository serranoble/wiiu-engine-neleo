#include "Core/Storage/VirtualFileSystem.hpp"
#include "Core/AssetManager.hpp"
#include "Core/Storage/StorageManager.hpp"
#include "Core/JobSystem.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#if defined(__WIIU__) || defined(__wiiu__)
#include <coreinit/cache.h>
#include <coreinit/thread.h>
#endif

namespace WiiU {

namespace {
inline void SpinYield() noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    OSYieldThread();
#else
    std::this_thread::yield();
#endif
}
} // namespace

VirtualFileSystem& VirtualFileSystem::Instance() noexcept {
    static VirtualFileSystem instance;
    return instance;
}

VirtualFileSystem::~VirtualFileSystem() noexcept {
    Shutdown();
}

void VirtualFileSystem::Initialize() noexcept {
    if (m_initialized.load(std::memory_order_acquire)) return;
    while (m_allocLock.test_and_set(std::memory_order_acquire)) { SpinYield(); }
    if (!m_initialized.load(std::memory_order_relaxed)) {
        for (auto& slot : m_slots) {
            slot.virtualPath.Clear();
            slot.buffer = VFSAlignedBuffer{};
            slot.status.store(static_cast<uint8_t>(VFSRequestStatus::Free), std::memory_order_relaxed);
            slot.jobCounter.store(0, std::memory_order_relaxed);
        }
        m_initialized.store(true, std::memory_order_release);
    }
    m_allocLock.clear(std::memory_order_release);
}

void VirtualFileSystem::Shutdown() noexcept {
    if (!m_initialized.exchange(false, std::memory_order_acq_rel)) return;
    WaitAll();
    while (m_allocLock.test_and_set(std::memory_order_acquire)) { SpinYield(); }
    for (auto& slot : m_slots) {
        if (slot.buffer.IsValid()) {
            FreeAlignedBuffer(slot.buffer);
        }
        slot.status.store(static_cast<uint8_t>(VFSRequestStatus::Free), std::memory_order_release);
    }
    m_allocLock.clear(std::memory_order_release);
}

bool VirtualFileSystem::ResolveVirtualPath(std::string_view virtualPath, char* outPath, size_t maxLen,
                                           int64_t* outFileSize) noexcept {
    if (!outPath || maxLen == 0 || virtualPath.empty()) return false;

    if (virtualPath.rfind(StorageManager::VFS_PREFIX_SAVE, 0) == 0) {
        if (!StorageManager::ResolveSavePath(virtualPath, outPath, maxLen)) {
            return false;
        }
        if (outFileSize) {
            *outFileSize = -1;
            FILE* f = std::fopen(outPath, "rb");
            if (f) {
                std::fseek(f, 0, SEEK_END);
                long sz = std::ftell(f);
                std::fclose(f);
                if (sz >= 0) *outFileSize = static_cast<int64_t>(sz);
            }
        }
        return true;
    }

    std::string_view cleanPath = virtualPath;
    if (cleanPath.rfind(StorageManager::VFS_PREFIX_CONTENT, 0) == 0) {
        cleanPath.remove_prefix(StorageManager::VFS_PREFIX_CONTENT.size());
    }

    try {
        std::string resolved = AssetManager::ResolvePath(std::string(cleanPath), outFileSize);
        if (resolved.empty() || resolved.size() + 1 > maxLen) {
            return false;
        }

        std::memcpy(outPath, resolved.data(), resolved.size());
        outPath[resolved.size()] = '\0';
        return true;
    } catch (...) {
        return false;
    }
}

namespace {
size_t NormalizeAlignmentPow2(size_t alignment) noexcept {
    if (alignment < 32u) return 32u;
    if ((alignment & (alignment - 1u)) == 0u) return alignment;
    size_t pow2 = 32u;
    while (pow2 < alignment && pow2 <= (size_t(1) << 20)) {
        pow2 <<= 1u;
    }
    return (pow2 >= alignment) ? pow2 : VirtualFileSystem::DEFAULT_ALIGNMENT;
}
} // namespace

bool VirtualFileSystem::ReadFileAligned(std::string_view virtualPath, VFSAlignedBuffer& outBuffer,
                                        size_t alignment, MemoryTag tag, const char* label) noexcept {
    outBuffer = VFSAlignedBuffer{};
    const size_t safeAlign = NormalizeAlignmentPow2(alignment);

    char physicalPath[256]{};
    int64_t knownSize = -1;
    if (!ResolveVirtualPath(virtualPath, physicalPath, sizeof(physicalPath), &knownSize)) {
        return false;
    }

    FILE* f = std::fopen(physicalPath, "rb");
    if (!f) {
        return false;
    }

    size_t fileSize = 0;
    if (knownSize > 0) {
        fileSize = static_cast<size_t>(knownSize);
    } else {
        if (std::fseek(f, 0, SEEK_END) == 0) {
            long sz = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            if (sz > 0) {
                fileSize = static_cast<size_t>(sz);
            }
        }
    }

    if (fileSize == 0) {
        std::fclose(f);
        return false;
    }

    const size_t alignedSize = (fileSize + safeAlign - 1) & ~(safeAlign - 1);
    void* mem = MemoryManager::Instance().AllocAligned(safeAlign, alignedSize, tag,
                                                       label ? label : "VFS_Aligned");
    if (!mem) {
        std::fclose(f);
        return false;
    }

    uint8_t* dst = static_cast<uint8_t*>(mem);
    size_t totalRead = 0;
    while (totalRead < fileSize) {
        const size_t remaining = fileSize - totalRead;
        const size_t chunk = (remaining > STREAM_CHUNK_SIZE) ? STREAM_CHUNK_SIZE : remaining;
        const size_t n = std::fread(dst + totalRead, 1, chunk, f);
        if (n == 0) break;
        totalRead += n;
    }
    std::fclose(f);

    if (totalRead != fileSize) {
        MemoryManager::Instance().FreeAligned(mem);
        return false;
    }

    if (alignedSize > fileSize) {
        std::memset(dst + fileSize, 0, alignedSize - fileSize);
    }

#if defined(__WIIU__) || defined(__wiiu__)
    // Garantizar coherencia de cache L1D/L2 antes de consumo DMA / GPU Latte GX2
    DCFlushRange(mem, static_cast<uint32_t>(alignedSize));
#endif

    outBuffer.data = mem;
    outBuffer.size = fileSize;
    outBuffer.alignment = safeAlign;
    outBuffer.tag = tag;
    return true;
}

void VirtualFileSystem::FreeAlignedBuffer(VFSAlignedBuffer& buffer) noexcept {
    if (buffer.data) {
        MemoryManager::Instance().FreeAligned(buffer.data);
        buffer.data = nullptr;
    }
    buffer.size = 0;
}

void VirtualFileSystem::AsyncReadJobEntry(void* userData) {
    auto* slot = static_cast<AsyncSlot*>(userData);
    if (!slot) return;

    VFSAlignedBuffer loaded{};
    const bool ok = ReadFileAligned(slot->virtualPath.string_view(), loaded,
                                    slot->alignment, slot->tag, slot->label);
    if (ok) {
        slot->buffer = loaded;
        slot->status.store(static_cast<uint8_t>(VFSRequestStatus::Completed), std::memory_order_release);
    } else {
        slot->buffer = VFSAlignedBuffer{};
        slot->status.store(static_cast<uint8_t>(VFSRequestStatus::Failed), std::memory_order_release);
    }
}

VFSRequestHandle VirtualFileSystem::ReadFileAsync(std::string_view virtualPath, size_t alignment,
                                                  MemoryTag tag, const char* label) noexcept {
    if (!m_initialized.load(std::memory_order_acquire)) {
        Initialize();
    }
    if (virtualPath.empty() || virtualPath.size() >= 256u) {
        return VFSRequestHandle{};
    }

    while (m_allocLock.test_and_set(std::memory_order_acquire)) { SpinYield(); }

    uint16_t chosenSlot = 0xFFFFu;
    uint16_t chosenGen = 0;
    for (uint16_t i = 0; i < MAX_ASYNC_REQUESTS; ++i) {
        const auto st = static_cast<VFSRequestStatus>(
            m_slots[i].status.load(std::memory_order_acquire));
        // Exigir jobCounter == 0 para garantizar que JobSystem::WorkerLoop ya ejecuto fetch_sub(1).
        if (st == VFSRequestStatus::Free &&
            m_slots[i].jobCounter.load(std::memory_order_acquire) == 0u) {
            AsyncSlot& s = m_slots[i];
            s.virtualPath.Format("%.*s", static_cast<int>(virtualPath.size()), virtualPath.data());
            s.buffer = VFSAlignedBuffer{};
            s.alignment = NormalizeAlignmentPow2(alignment);
            s.tag = tag;
            s.label = label ? label : "VFS_Async";
            const uint16_t prevGen = s.generation.load(std::memory_order_relaxed);
            const uint16_t nextGen = static_cast<uint16_t>((prevGen + 1u) == 0u ? 1u : (prevGen + 1u));
            s.generation.store(nextGen, std::memory_order_release);
            // Nota: JobSystem::Dispatch() ya realiza fetch_add(1) sobre jobCounter y WorkerLoop fetch_sub(1).
            s.status.store(static_cast<uint8_t>(VFSRequestStatus::Pending), std::memory_order_release);
            chosenSlot = i;
            chosenGen = nextGen;
            break;
        }
    }

    m_allocLock.clear(std::memory_order_release);

    if (chosenSlot == 0xFFFFu) {
        return VFSRequestHandle{};
    }

    AsyncSlot& target = m_slots[chosenSlot];
    if (JobSystem::Instance().IsInitialized()) {
        JobSystem::Instance().Dispatch(&AsyncReadJobEntry, &target, &target.jobCounter);
    } else {
        AsyncReadJobEntry(&target);
    }

    return VFSRequestHandle{chosenSlot, chosenGen};
}

VFSRequestStatus VirtualFileSystem::GetRequestStatus(VFSRequestHandle handle) const noexcept {
    if (!handle.IsValid() || handle.slot >= MAX_ASYNC_REQUESTS) {
        return VFSRequestStatus::Failed;
    }
    const AsyncSlot& s = m_slots[handle.slot];
    if (s.generation.load(std::memory_order_acquire) != handle.generation) {
        return VFSRequestStatus::Free;
    }
    return static_cast<VFSRequestStatus>(s.status.load(std::memory_order_acquire));
}

bool VirtualFileSystem::TakeCompletedBuffer(VFSRequestHandle handle, VFSAlignedBuffer& outBuffer) noexcept {
    outBuffer = VFSAlignedBuffer{};
    if (!handle.IsValid() || handle.slot >= MAX_ASYNC_REQUESTS) return false;

    AsyncSlot& s = m_slots[handle.slot];
    while (m_allocLock.test_and_set(std::memory_order_acquire)) { SpinYield(); }
    if (s.generation.load(std::memory_order_acquire) != handle.generation ||
        static_cast<VFSRequestStatus>(s.status.load(std::memory_order_acquire)) != VFSRequestStatus::Completed) {
        m_allocLock.clear(std::memory_order_release);
        return false;
    }

    // Invalidar handle (anti-ABA / anti-ReleaseRequest concurrente) y extraer buffer bajo lock
    const uint16_t nextGen = static_cast<uint16_t>((handle.generation + 1u) == 0u ? 1u : (handle.generation + 1u));
    s.generation.store(nextGen, std::memory_order_release);
    outBuffer = s.buffer;
    s.buffer = VFSAlignedBuffer{};
    s.status.store(static_cast<uint8_t>(VFSRequestStatus::Pending), std::memory_order_release);
    m_allocLock.clear(std::memory_order_release);

    if (s.jobCounter.load(std::memory_order_acquire) > 0u && JobSystem::Instance().IsInitialized()) {
        JobSystem::Instance().Wait(&s.jobCounter);
    }
    s.status.store(static_cast<uint8_t>(VFSRequestStatus::Free), std::memory_order_release);
    return outBuffer.IsValid();
}

void VirtualFileSystem::ReleaseRequest(VFSRequestHandle handle) noexcept {
    if (!handle.IsValid() || handle.slot >= MAX_ASYNC_REQUESTS) return;

    AsyncSlot& s = m_slots[handle.slot];
    if (s.generation.load(std::memory_order_acquire) != handle.generation) return;

    if (s.jobCounter.load(std::memory_order_acquire) > 0 && JobSystem::Instance().IsInitialized()) {
        JobSystem::Instance().Wait(&s.jobCounter);
    }

    while (m_allocLock.test_and_set(std::memory_order_acquire)) { SpinYield(); }
    if (s.generation.load(std::memory_order_acquire) == handle.generation) {
        const auto st = static_cast<VFSRequestStatus>(s.status.load(std::memory_order_acquire));
        if (st != VFSRequestStatus::Free) {
            if (s.buffer.IsValid()) {
                FreeAlignedBuffer(s.buffer);
            }
            s.status.store(static_cast<uint8_t>(VFSRequestStatus::Free), std::memory_order_release);
        }
    }
    m_allocLock.clear(std::memory_order_release);
}

void VirtualFileSystem::WaitAll() noexcept {
    for (auto& s : m_slots) {
        if (s.jobCounter.load(std::memory_order_acquire) > 0 && JobSystem::Instance().IsInitialized()) {
            JobSystem::Instance().Wait(&s.jobCounter);
        }
    }
}

uint32_t VirtualFileSystem::GetActiveRequestCount() const noexcept {
    uint32_t count = 0;
    for (const auto& s : m_slots) {
        const auto st = static_cast<VFSRequestStatus>(s.status.load(std::memory_order_acquire));
        if (st == VFSRequestStatus::Pending) {
            ++count;
        }
    }
    return count;
}

} // namespace WiiU
