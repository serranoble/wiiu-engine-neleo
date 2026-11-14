#include "Core/AssetManager.hpp"
#include <SDL2/SDL.h>
#include <cstdio>
#include <algorithm>
#include <mutex>

namespace WiiU {

std::atomic<uint32_t> AssetManager::s_asyncBytesLoaded{0};
std::atomic<uint32_t> AssetManager::s_asyncTotalBytes{0};
std::atomic<uint32_t> AssetManager::s_asyncFilesLoaded{0};

namespace {
    std::mutex s_prefixCacheMutex;
    std::string s_cachedPrefix;
    bool s_hasCachedPrefix = false;

    bool ProbeFileSize(const std::string& path, int64_t& outSize) {
        // Intento 1: Vía fopen estándar (bajo overhead en newlib / devkitPPC)
        FILE* f = std::fopen(path.c_str(), "rb");
        if (f) {
            std::fseek(f, 0, SEEK_END);
            long sz = std::ftell(f);
            std::fclose(f);
            if (sz > 0) {
                outSize = static_cast<int64_t>(sz);
                return true;
            }
        }

        // Intento 2: Vía SDL_RWops (resuelve empaquetado WUHB y capas virtuales de SDL)
        SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
        if (rw) {
            Sint64 sz = SDL_RWsize(rw);
            if (sz <= 0) {
                SDL_RWseek(rw, 0, RW_SEEK_END);
                sz = SDL_RWtell(rw);
            }
            SDL_RWclose(rw);
            if (sz > 0) {
                outSize = static_cast<int64_t>(sz);
                return true;
            }
        }

        return false;
    }
} // namespace

void AssetManager::ResetAsyncProgress(uint32_t totalExpectedBytes) noexcept {
    s_asyncBytesLoaded.store(0, std::memory_order_relaxed);
    s_asyncTotalBytes.store(totalExpectedBytes, std::memory_order_relaxed);
    s_asyncFilesLoaded.store(0, std::memory_order_relaxed);
}

uint32_t AssetManager::GetAsyncBytesLoaded() noexcept {
    return s_asyncBytesLoaded.load(std::memory_order_relaxed);
}

uint32_t AssetManager::GetAsyncTotalBytes() noexcept {
    return s_asyncTotalBytes.load(std::memory_order_relaxed);
}

float AssetManager::GetAsyncProgressRatio() noexcept {
    uint32_t total = s_asyncTotalBytes.load(std::memory_order_relaxed);
    if (total == 0) {
        return 0.0f;
    }
    uint32_t loaded = s_asyncBytesLoaded.load(std::memory_order_relaxed);
    float ratio = static_cast<float>(loaded) / static_cast<float>(total);
    return std::clamp(ratio, 0.0f, 1.0f);
}

uint32_t AssetManager::GetAsyncFilesLoaded() noexcept {
    return s_asyncFilesLoaded.load(std::memory_order_relaxed);
}

const std::vector<std::string>& AssetManager::GetSearchPrefixes() {
    static const std::vector<std::string> prefixes = {
        "content/",
        "assets/",
        "fs:/vol/content/",
        "fs:/vol/content/assets/",
        "/vol/content/",
        "/vol/content/assets/",
        "../content/",
        "../assets/",
        "../../content/",
        "../../assets/",
        "shaders/",
        "content/shaders/",
        "assets/shaders/",
        "" // Ruta directa sin prefijo
    };
    return prefixes;
}

std::string AssetManager::ResolvePath(const std::string& filepath, int64_t* outFileSize) {
    if (outFileSize) {
        *outFileSize = 0;
    }

    if (filepath.empty()) {
        return "";
    }

    // 0. Fast-path O(1): Probar primero el prefijo de montaje cacheado si ya fue descubierto
    {
        std::string cachedPref;
        bool hasCache = false;
        {
            std::lock_guard<std::mutex> lock(s_prefixCacheMutex);
            hasCache = s_hasCachedPrefix;
            cachedPref = s_cachedPrefix;
        }
        if (hasCache) {
            std::string fastCandidate = cachedPref + filepath;
            int64_t sz = 0;
            if (ProbeFileSize(fastCandidate, sz)) {
                if (outFileSize) {
                    *outFileSize = sz;
                }
                return fastCandidate;
            }
        }
    }

    const auto& prefixes = GetSearchPrefixes();

    // 1. Probar cada prefijo con la ruta relativa tal como se especificó
    for (const auto& prefix : prefixes) {
        std::string candidate = prefix + filepath;
        int64_t sz = 0;
        if (ProbeFileSize(candidate, sz)) {
            {
                std::lock_guard<std::mutex> lock(s_prefixCacheMutex);
                s_cachedPrefix = prefix;
                s_hasCachedPrefix = true;
            }
            if (outFileSize) {
                *outFileSize = sz;
            }
            return candidate;
        }
    }

    // 2. Si contiene barras, probar también con el nombre base del archivo
    size_t lastSlash = filepath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        std::string filename = filepath.substr(lastSlash + 1);
        for (const auto& prefix : prefixes) {
            std::string candidate = prefix + filename;
            int64_t sz = 0;
            if (ProbeFileSize(candidate, sz)) {
                if (outFileSize) {
                    *outFileSize = sz;
                }
                return candidate;
            }
        }
    }

    return "";
}

bool AssetManager::FileExists(const std::string& filepath) {
    return !ResolvePath(filepath).empty();
}

bool AssetManager::ValidateAssets(const std::vector<std::string>& requiredAssets,
                                  std::vector<std::string>& outMissing,
                                  uint32_t* outTotalBytes) {
    outMissing.clear();
    uint32_t totalBytes = 0;
    for (const auto& asset : requiredAssets) {
        int64_t fileSize = 0;
        std::string resolved = ResolvePath(asset, &fileSize);
        if (resolved.empty()) {
            outMissing.push_back(asset);
        } else if (fileSize > 0) {
            totalBytes += static_cast<uint32_t>(fileSize);
        }
    }
    if (outTotalBytes) {
        *outTotalBytes = totalBytes;
    }
    return outMissing.empty();
}

bool AssetManager::LoadBinaryFile(const std::string& filepath, std::vector<uint8_t>& outBuffer) {
    outBuffer.clear();

    if (filepath.empty()) {
        return false;
    }

    int64_t resolvedSize = 0;
    std::string resolvedPath = ResolvePath(filepath, &resolvedSize);
    if (resolvedPath.empty()) {
        return false;
    }

    // Pilar 1 (Jason Gregory Cap. 7): Pre-alocación exacta y lectura por ráfagas de 256 KB
    // directamente sobre el búfer de destino, con CERO vectores temporales y CERO realocaciones.
    if (resolvedSize > 0) {
        FILE* f = std::fopen(resolvedPath.c_str(), "rb");
        if (f) {
            // Búfer de stream de stdio de 64 KB para minimizar llamadas IPC a Starbuck
            std::setvbuf(f, nullptr, _IOFBF, 65536);
            outBuffer.resize(static_cast<size_t>(resolvedSize));
            uint8_t* dst = outBuffer.data();
            size_t remaining = static_cast<size_t>(resolvedSize);
            size_t totalRead = 0;

            while (remaining > 0) {
                size_t toRead = std::min(remaining, BURST_CHUNK_SIZE);
                size_t n = std::fread(dst + totalRead, 1, toRead, f);
                if (n == 0) {
                    break;
                }
                totalRead += n;
                remaining -= n;
                s_asyncBytesLoaded.fetch_add(static_cast<uint32_t>(n), std::memory_order_relaxed);
            }
            std::fclose(f);

            if (totalRead > 0) {
                if (totalRead < outBuffer.size()) {
                    outBuffer.resize(totalRead);
                }
                s_asyncFilesLoaded.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            outBuffer.clear();
        }

        SDL_RWops* rw = SDL_RWFromFile(resolvedPath.c_str(), "rb");
        if (rw) {
            outBuffer.resize(static_cast<size_t>(resolvedSize));
            uint8_t* dst = outBuffer.data();
            size_t remaining = static_cast<size_t>(resolvedSize);
            size_t totalRead = 0;

            while (remaining > 0) {
                size_t toRead = std::min(remaining, BURST_CHUNK_SIZE);
                size_t n = SDL_RWread(rw, dst + totalRead, 1, toRead);
                if (n == 0) {
                    break;
                }
                totalRead += n;
                remaining -= n;
                s_asyncBytesLoaded.fetch_add(static_cast<uint32_t>(n), std::memory_order_relaxed);
            }
            SDL_RWclose(rw);

            if (totalRead > 0) {
                if (totalRead < outBuffer.size()) {
                    outBuffer.resize(totalRead);
                }
                s_asyncFilesLoaded.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            outBuffer.clear();
        }
    }

    // Fallback para streams virtuales sin tamaño conocido de antemano
    SDL_RWops* rw = SDL_RWFromFile(resolvedPath.c_str(), "rb");
    if (rw) {
        std::vector<uint8_t> chunkBuf(BURST_CHUNK_SIZE);
        size_t n = 0;
        while ((n = SDL_RWread(rw, chunkBuf.data(), 1, chunkBuf.size())) > 0) {
            outBuffer.insert(outBuffer.end(), chunkBuf.data(), chunkBuf.data() + n);
            s_asyncBytesLoaded.fetch_add(static_cast<uint32_t>(n), std::memory_order_relaxed);
        }
        SDL_RWclose(rw);
        if (!outBuffer.empty()) {
            s_asyncFilesLoaded.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    }

    return false;
}

} // namespace WiiU
