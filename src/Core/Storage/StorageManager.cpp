#include "Core/Storage/StorageManager.hpp"
#include "Core/Storage/CryptoHMAC.hpp"
#include "Core/JobSystem.hpp"
#include "Core/StackString.hpp"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <sys/stat.h>

#if defined(_WIN32) && !defined(__WIIU__) && !defined(__wiiu__)
#include <direct.h>
#include <io.h>
#define MKDIR(path) _mkdir(path)
#else
#include <sys/types.h>
#include <unistd.h>
#define MKDIR(path) mkdir(path, 0777)
#endif

namespace Neleo {

bool StorageManager::s_initialized = false;
char StorageManager::s_saveRoot[256] = "./";

namespace {
char g_appFolderName[64] = "wiiu_demos";
}

void StorageManager::SetAppFolderName(std::string_view folderName) noexcept {
    if (folderName.empty() || folderName.size() >= sizeof(g_appFolderName)) {
        return;
    }
    std::memcpy(g_appFolderName, folderName.data(), folderName.size());
    g_appFolderName[folderName.size()] = '\0';
}

static inline bool DirectoryExists(const char* path) noexcept {
    if (!path || path[0] == '\0') return false;
    struct stat st;
    if (stat(path, &st) == 0) {
        return (st.st_mode & S_IFDIR) != 0;
    }
    return false;
}

static inline bool TryWriteProbe(const char* dirPath) noexcept {
    char probePath[272];
    std::snprintf(probePath, sizeof(probePath), "%s.sal_probe", dirPath);
    FILE* f = std::fopen(probePath, "wb");
    if (!f) return false;
    std::fclose(f);
    std::remove(probePath);
    return true;
}

void StorageManager::Initialize() {
    if (s_initialized) return;

    // 1. Probar primero si el directorio de trabajo local ("./") es escribible.
    // Esto funciona al 100% en Cemu, Windows, Linux y ejecutable standalone .rpx en tarjeta SD.
    // TryWriteProbe utiliza std::fopen/fclose/remove y no invoca funciones de devoptab no montados.
    if (TryWriteProbe("./")) {
        std::strncpy(s_saveRoot, "./", sizeof(s_saveRoot) - 1);
        s_saveRoot[sizeof(s_saveRoot) - 1] = '\0';
        s_initialized = true;
        return;
    }

#if defined(__WIIU__) || defined(__wiiu__)
    // 2. Si "./" no es escribible (ej. paquete instalable .wuhb de Aroma en content:/ de solo lectura en Wii U real):
    // Probar primero la ruta nativa de Cafe OS para la tarjeta SD (/vol/external01):
    char cand0[160]{};
    char cand1[160]{};
    char cand2[160]{};
    std::snprintf(cand0, sizeof(cand0), "fs:/vol/external01/wiiu/apps/%s/", g_appFolderName);
    std::snprintf(cand1, sizeof(cand1), "wiiu/apps/%s/", g_appFolderName);
    std::snprintf(cand2, sizeof(cand2), "sd:/wiiu/apps/%s/", g_appFolderName);
    const char* candidatePaths[] = { cand0, cand1, cand2 };

    bool mounted = false;
    for (const char* path : candidatePaths) {
        if (TryWriteProbe(path)) {
            std::strncpy(s_saveRoot, path, sizeof(s_saveRoot) - 1);
            s_saveRoot[sizeof(s_saveRoot) - 1] = '\0';
            mounted = true;
            break;
        }
        if (EnsureDirectoryExists(path) && TryWriteProbe(path)) {
            std::strncpy(s_saveRoot, path, sizeof(s_saveRoot) - 1);
            s_saveRoot[sizeof(s_saveRoot) - 1] = '\0';
            mounted = true;
            break;
        }
    }

    if (!mounted) {
        std::strncpy(s_saveRoot, "./", sizeof(s_saveRoot) - 1);
        s_saveRoot[sizeof(s_saveRoot) - 1] = '\0';
    }
#else
    std::strncpy(s_saveRoot, "./", sizeof(s_saveRoot) - 1);
    s_saveRoot[sizeof(s_saveRoot) - 1] = '\0';
#endif

    s_initialized = true;
}

namespace {
void ReleaseAsyncSaveStagingBuffers() noexcept;
}

void StorageManager::Shutdown() {
    WaitForAsyncSaves();
    ReleaseAsyncSaveStagingBuffers();
    s_initialized = false;
}

bool StorageManager::IsInitialized() noexcept {
    return s_initialized;
}

const char* StorageManager::GetSaveRoot() noexcept {
    if (!s_initialized) {
        Initialize();
    }
    return s_saveRoot;
}

bool StorageManager::ResolveSavePath(std::string_view relativePath, char* outBuf, size_t maxLen) noexcept {
    if (!outBuf || maxLen == 0) return false;

    if (!s_initialized) {
        Initialize();
    }

    // Limpiar prefijo virtual "save:/" o "save:\" si está presente
    std::string_view cleanRel = relativePath;
    if (cleanRel.rfind(VFS_PREFIX_SAVE, 0) == 0) {
        cleanRel.remove_prefix(VFS_PREFIX_SAVE.size());
    } else if (cleanRel.rfind("save:\\", 0) == 0) {
        cleanRel.remove_prefix(6);
    }

    // Si la ruta ya es absoluta o contiene s_saveRoot, evitar duplicar el prefijo
    if (cleanRel.rfind(s_saveRoot, 0) == 0) {
        if (cleanRel.size() + 1 > maxLen) return false;
        std::memcpy(outBuf, cleanRel.data(), cleanRel.size());
        outBuf[cleanRel.size()] = '\0';
        return true;
    }

    // Detección de dispositivos absolutos directos ("fs:/", "sd:/", "C:/")
    if (cleanRel.find(":/") != std::string_view::npos || cleanRel.find(":\\") != std::string_view::npos) {
        if (cleanRel.size() + 1 > maxLen) return false;
        std::memcpy(outBuf, cleanRel.data(), cleanRel.size());
        outBuf[cleanRel.size()] = '\0';
        return true;
    }

    // Quitar barras iniciales redundantes
    while (!cleanRel.empty() && (cleanRel.front() == '/' || cleanRel.front() == '\\')) {
        cleanRel.remove_prefix(1);
    }

    size_t rootLen = std::strlen(s_saveRoot);
    if (rootLen + cleanRel.size() + 1 > maxLen) {
        return false;
    }

    std::memcpy(outBuf, s_saveRoot, rootLen);
    std::memcpy(outBuf + rootLen, cleanRel.data(), cleanRel.size());
    outBuf[rootLen + cleanRel.size()] = '\0';

    return true;
}

std::string StorageManager::ResolveSavePath(std::string_view relativePath) {
    char buf[256];
    if (ResolveSavePath(relativePath, buf, sizeof(buf))) {
        return std::string(buf);
    }
    return std::string(GetSaveRoot()) + std::string(relativePath);
}

bool StorageManager::EnsureDirectoryExists(std::string_view fullPath) {
    if (fullPath.empty()) return false;

    char temp[256];
    size_t len = std::min(fullPath.size(), sizeof(temp) - 1);
    std::memcpy(temp, fullPath.data(), len);
    temp[len] = '\0';

    // Determinar el offset a partir del cual se pueden crear subdirectorios,
    // saltando prefijos de dispositivos ("fs:/vol/external01/", "sd:/", "C:/")
    size_t startIdx = 1;
    if (std::strncmp(temp, "fs:/vol/external01/", 19) == 0) {
        startIdx = 19;
    } else if (std::strncmp(temp, "fs:/vol/content/", 16) == 0) {
        startIdx = 16;
    } else if (std::strncmp(temp, "sd:/", 4) == 0) {
        startIdx = 4;
    } else {
        auto colonPos = fullPath.find(':');
        if (colonPos != std::string_view::npos && colonPos + 2 < len) {
            startIdx = colonPos + 2; // saltar después de ":/" o ":\"
        }
    }

    // Normalizar y crear jerarquía
    for (size_t i = startIdx; i < len; ++i) {
        if (temp[i] == '/' || temp[i] == '\\') {
            char c = temp[i];
            temp[i] = '\0';
            if (!DirectoryExists(temp)) {
                MKDIR(temp);
            }
            temp[i] = c;
        }
    }

    // Si la ruta terminaba con barra, crear el directorio final quitando la barra
    if (len > startIdx && (temp[len - 1] == '/' || temp[len - 1] == '\\')) {
        temp[len - 1] = '\0';
        if (!DirectoryExists(temp)) {
            MKDIR(temp);
        }
    }

    return true;
}

bool StorageManager::SaveFileExists(std::string_view relativePath) {
    char finalPath[256];
    if (!ResolveSavePath(relativePath, finalPath, sizeof(finalPath))) {
        return false;
    }
    FILE* f = std::fopen(finalPath, "rb");
    if (f) {
        std::fclose(f);
        return true;
    }
    // [TRC/Lot Check Recovery]: Comprobar si existe la copia temporal no confirmada
    char tempPath[264];
    std::snprintf(tempPath, sizeof(tempPath), "%s.tmp", finalPath);
    f = std::fopen(tempPath, "rb");
    if (f) {
        std::fclose(f);
        return true;
    }
    return false;
}

bool StorageManager::SaveFileAtomic(std::string_view relativePath, const void* data, size_t size) {
    char finalPath[256];
    if (!ResolveSavePath(relativePath, finalPath, sizeof(finalPath))) {
        return false;
    }

    // Asegurar directorio contenedor
    EnsureDirectoryExists(finalPath);

    char tempPath[264];
    std::snprintf(tempPath, sizeof(tempPath), "%s.tmp", finalPath);

    FILE* f = std::fopen(tempPath, "wb");
    if (!f) return false;

    size_t written = 0;
    if (size > 0 && data != nullptr) {
        written = std::fwrite(data, 1, size, f);
    }

    std::fflush(f);

#if defined(_WIN32) && !defined(__WIIU__) && !defined(__wiiu__)
    int fd = _fileno(f);
    if (fd >= 0) _commit(fd);
#elif !defined(__WIIU__) && !defined(__wiiu__)
    int fd = fileno(f);
    if (fd >= 0) fsync(fd);
#endif

    std::fclose(f);

    if (written != size) {
        std::remove(tempPath);
        return false;
    }

    // [OPT-396] Reemplazo atómico de Shadow Copying (TRC-safe: intentar rename directo primero)
    if (std::rename(tempPath, finalPath) != 0) {
        std::remove(finalPath);
        if (std::rename(tempPath, finalPath) != 0) {
            // Preservar tempPath para permitir recuperacion TRC en LoadSaveFile()
            return false;
        }
    }

    return true;
}

bool StorageManager::SaveStringAtomic(std::string_view relativePath, std::string_view content) {
    return SaveFileAtomic(relativePath, content.data(), content.size());
}

bool StorageManager::LoadSaveFile(std::string_view relativePath, std::vector<uint8_t>& outBuffer) {
    char finalPath[256];
    if (!ResolveSavePath(relativePath, finalPath, sizeof(finalPath))) {
        return false;
    }

    FILE* f = std::fopen(finalPath, "rb");
    if (!f) {
        // [TRC/Lot Check Recovery]: Recuperar <finalPath>.tmp si ocurrió un corte de energía antes de rename
        char tempPath[264];
        std::snprintf(tempPath, sizeof(tempPath), "%s.tmp", finalPath);
        f = std::fopen(tempPath, "rb");
        if (!f) return false;
    }

    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    if (size < 0) {
        std::fclose(f);
        return false;
    }

    outBuffer.resize(static_cast<size_t>(size));
    size_t read = 0;
    if (size > 0) {
        read = std::fread(outBuffer.data(), 1, static_cast<size_t>(size), f);
    }
    std::fclose(f);

    return (read == static_cast<size_t>(size));
}

namespace {

constexpr uint8_t VERIFIED_MAGIC[4] = {'W', 'U', 'S', 'V'};
constexpr uint32_t VERIFIED_VERSION = 1u;
constexpr size_t VERIFIED_HEADER_SIZE = 4 + 4 + 4 + CryptoHMAC::SHA256_DIGEST_SIZE; // 44 bytes

inline void WriteU32BE(uint8_t* dst, uint32_t v) noexcept {
    dst[0] = static_cast<uint8_t>((v >> 24u) & 0xFFu);
    dst[1] = static_cast<uint8_t>((v >> 16u) & 0xFFu);
    dst[2] = static_cast<uint8_t>((v >>  8u) & 0xFFu);
    dst[3] = static_cast<uint8_t>((v       ) & 0xFFu);
}

inline uint32_t ReadU32BE(const uint8_t* src) noexcept {
    return (static_cast<uint32_t>(src[0]) << 24u) |
           (static_cast<uint32_t>(src[1]) << 16u) |
           (static_cast<uint32_t>(src[2]) <<  8u) |
           (static_cast<uint32_t>(src[3]));
}

struct alignas(32) AsyncSaveSlot {
    StackString<256> relativePath{};
    StackString<128> hmacKey{};
    std::vector<uint8_t> stagingData{};
    bool signWithHmac{false};
    std::atomic<bool> inUse{false};
    std::atomic<uint32_t> jobCounter{0};
};

std::array<AsyncSaveSlot, 2> g_asyncSaveSlots{};
std::atomic<bool> g_lastAsyncSaveSuccess{true};

void ReleaseAsyncSaveStagingBuffers() noexcept {
    for (auto& slot : g_asyncSaveSlots) {
        slot.stagingData.clear();
        slot.stagingData.shrink_to_fit();
        slot.inUse.store(false, std::memory_order_release);
    }
}

void AsyncSaveJobWorker(void* userData) {
    auto* slot = static_cast<AsyncSaveSlot*>(userData);
    if (!slot) return;

    bool ok = false;
    if (slot->signWithHmac) {
        ok = StorageManager::SaveFileVerifiedAtomic(
            slot->relativePath.string_view(),
            slot->stagingData.data(),
            slot->stagingData.size(),
            slot->hmacKey.string_view());
    } else {
        ok = StorageManager::SaveFileAtomic(
            slot->relativePath.string_view(),
            slot->stagingData.data(),
            slot->stagingData.size());
    }

    g_lastAsyncSaveSuccess.store(ok, std::memory_order_release);
    slot->inUse.store(false, std::memory_order_release);
}

} // namespace

bool StorageManager::SaveFileVerifiedAtomic(std::string_view relativePath, const void* data, size_t size,
                                            std::string_view hmacKey) {
    if (size > 0 && data == nullptr) return false;

    std::vector<uint8_t> packet(VERIFIED_HEADER_SIZE + size);
    std::memcpy(packet.data(), VERIFIED_MAGIC, 4);
    WriteU32BE(packet.data() + 4, VERIFIED_VERSION);
    WriteU32BE(packet.data() + 8, static_cast<uint32_t>(size));

    const CryptoHMAC::Digest mac = CryptoHMAC::ComputeHmacSha256(hmacKey, data, size);
    std::memcpy(packet.data() + 12, mac.data(), CryptoHMAC::SHA256_DIGEST_SIZE);

    if (size > 0) {
        std::memcpy(packet.data() + VERIFIED_HEADER_SIZE, data, size);
    }

    return SaveFileAtomic(relativePath, packet.data(), packet.size());
}

bool StorageManager::LoadSaveFileVerified(std::string_view relativePath, std::vector<uint8_t>& outPayload,
                                          std::string_view hmacKey) {
    outPayload.clear();
    std::vector<uint8_t> raw;
    if (!LoadSaveFile(relativePath, raw)) {
        return false;
    }

    if (raw.size() < VERIFIED_HEADER_SIZE) {
        return false;
    }
    if (std::memcmp(raw.data(), VERIFIED_MAGIC, 4) != 0) {
        return false;
    }

    const uint32_t version = ReadU32BE(raw.data() + 4);
    const uint32_t payloadSize = ReadU32BE(raw.data() + 8);
    if (version != VERIFIED_VERSION || raw.size() != VERIFIED_HEADER_SIZE + static_cast<size_t>(payloadSize)) {
        return false;
    }

    CryptoHMAC::Digest storedMac{};
    std::memcpy(storedMac.data(), raw.data() + 12, CryptoHMAC::SHA256_DIGEST_SIZE);

    const uint8_t* payloadPtr = raw.data() + VERIFIED_HEADER_SIZE;
    const CryptoHMAC::Digest computedMac = CryptoHMAC::ComputeHmacSha256(hmacKey, payloadPtr, payloadSize);

    if (!CryptoHMAC::ConstantTimeEqual(storedMac, computedMac)) {
        return false;
    }

    outPayload.assign(payloadPtr, payloadPtr + payloadSize);
    return true;
}

bool StorageManager::SaveFileAsync(std::string_view relativePath, const void* data, size_t size,
                                   bool signWithHmac, std::string_view hmacKey) {
    if (relativePath.empty() || (size > 0 && data == nullptr)) {
        return false;
    }
    if (!s_initialized) {
        Initialize();
    }

    for (auto& slot : g_asyncSaveSlots) {
        // Exigir jobCounter == 0 antes de reclamar el slot para evitar carreras con fetch_sub(1) en WorkerLoop.
        if (slot.jobCounter.load(std::memory_order_acquire) != 0u) {
            continue;
        }
        bool expected = false;
        if (slot.inUse.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            if (slot.jobCounter.load(std::memory_order_acquire) != 0u) {
                slot.inUse.store(false, std::memory_order_release);
                continue;
            }
            slot.relativePath.Format("%.*s", static_cast<int>(relativePath.size()), relativePath.data());
            slot.hmacKey.Format("%.*s", static_cast<int>(hmacKey.size()), hmacKey.data());
            slot.signWithHmac = signWithHmac;
            slot.stagingData.resize(size);
            if (size > 0) {
                std::memcpy(slot.stagingData.data(), data, size);
            }
            // Nota: JobSystem::Dispatch() ya realiza fetch_add(1) sobre jobCounter y WorkerLoop fetch_sub(1).
            // Nunca pre-incrementar a 1 para evitar deadlock en WaitForAsyncSaves().
            if (JobSystem::Instance().IsInitialized()) {
                JobSystem::Instance().Dispatch(&AsyncSaveJobWorker, &slot, &slot.jobCounter);
            } else {
                AsyncSaveJobWorker(&slot);
            }
            return true;
        }
    }

    return false;
}

bool StorageManager::IsAsyncSavePending() noexcept {
    for (const auto& slot : g_asyncSaveSlots) {
        if (slot.inUse.load(std::memory_order_acquire) ||
            slot.jobCounter.load(std::memory_order_acquire) > 0) {
            return true;
        }
    }
    return false;
}

void StorageManager::WaitForAsyncSaves() noexcept {
    for (auto& slot : g_asyncSaveSlots) {
        if (slot.jobCounter.load(std::memory_order_acquire) > 0 && JobSystem::Instance().IsInitialized()) {
            JobSystem::Instance().Wait(&slot.jobCounter);
        }
    }
}

bool StorageManager::GetLastAsyncSaveSuccess() noexcept {
    return g_lastAsyncSaveSuccess.load(std::memory_order_acquire);
}

} // namespace Neleo
