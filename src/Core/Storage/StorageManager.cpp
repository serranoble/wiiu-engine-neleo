#include "Core/Storage/StorageManager.hpp"
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

namespace WiiU {

bool StorageManager::s_initialized = false;
char StorageManager::s_saveRoot[256] = "./";

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
    const char* candidatePaths[] = {
        "fs:/vol/external01/wiiu/apps/wiiu_demos/",
        "wiiu/apps/wiiu_demos/",
        "sd:/wiiu/apps/wiiu_demos/"
    };

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

void StorageManager::Shutdown() {
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

    // [OPT-396] Reemplazo atómico de Shadow Copying (Jason Gregory Cap. 5.3.3)
    std::remove(finalPath);
    if (std::rename(tempPath, finalPath) != 0) {
        std::remove(tempPath);
        return false;
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

} // namespace WiiU
