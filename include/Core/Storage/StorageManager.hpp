#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace WiiU {

/**
 * @brief [Hito 125 - OPT-396 - Jason Gregory Cap. 5.2 / Cap. 5.3]
 * Capa de Abstracción de Almacenamiento (SAL) y Sistema de Archivos Virtual (VFS).
 * 
 * Centraliza la resolución de rutas de guardado, desacopla el motor del silicio específico
 * (Wii U SD/MLC, Switch nn::fs, PC Windows/Linux) e implementa el patrón "Shadow Copying"
 * de salvaguarda atómica para prevenir corrupción de datos bajo normativas Nintendo Lot Check / TRC.
 */
class StorageManager {
public:
    StorageManager() = delete;

    static void SetAppFolderName(std::string_view folderName) noexcept;
    static void Initialize();
    static void Shutdown();
    [[nodiscard]] static bool IsInitialized() noexcept;

    // Prefijos de montaje lógicos canónicos
    static constexpr std::string_view VFS_PREFIX_SAVE = "save:/";
    static constexpr std::string_view VFS_PREFIX_CONTENT = "content:/";

    /**
     * @brief Resuelve una ruta lógica ("save:/config.ini") o relativa a la ruta física de guardado.
     * Retorna std::string (para uso fuera del bucle caliente a 60 FPS).
     */
    [[nodiscard]] static std::string ResolveSavePath(std::string_view relativePath);

    /**
     * @brief Versión de resolución de ruta en búfer de pila (0 alocaciones dinámicas).
     * @return true si la ruta cupo en el búfer; false si se truncó.
     */
    static bool ResolveSavePath(std::string_view relativePath, char* outBuf, size_t maxLen) noexcept;

    /**
     * @brief Retorna la raíz física del directorio de guardado para la plataforma activa.
     */
    [[nodiscard]] static const char* GetSaveRoot() noexcept;

    /**
     * @brief Asegura que la jerarquía de directorios de una ruta exista en disco.
     */
    static bool EnsureDirectoryExists(std::string_view fullPath);

    /**
     * @brief Comprueba si un archivo existe en el espacio de guardado.
     */
    [[nodiscard]] static bool SaveFileExists(std::string_view relativePath);

    /**
     * @brief Patrón de Salvaguarda Atómica (Shadow Copying) de Jason Gregory (Cap. 5.3.3).
     * Escribe a <path>.tmp, sincroniza buffers a disco (fflush/fsync) y renombra atómicamente.
     */
    static bool SaveFileAtomic(std::string_view relativePath, const void* data, size_t size);

    /**
     * @brief Versión de conveniencia para cadenas de texto (ej. INI, texto estructurado).
     */
    static bool SaveStringAtomic(std::string_view relativePath, std::string_view content);

    /**
     * @brief Carga completa de un archivo de guardado a un búfer en memoria.
     */
    static bool LoadSaveFile(std::string_view relativePath, std::vector<uint8_t>& outBuffer);

    /**
     * @brief [OPT-398] Guarda datos de forma atomica con envoltura criptografica HMAC-SHA256.
     */
    static bool SaveFileVerifiedAtomic(std::string_view relativePath, const void* data, size_t size,
                                       std::string_view hmacKey = DEFAULT_SAVE_HMAC_KEY);

    /**
     * @brief [OPT-398] Carga y valida integridad HMAC-SHA256 en tiempo constante.
     */
    static bool LoadSaveFileVerified(std::string_view relativePath, std::vector<uint8_t>& outPayload,
                                     std::string_view hmacKey = DEFAULT_SAVE_HMAC_KEY);

    /**
     * @brief [OPT-397] Despacha un guardado no bloqueante a Core 2 usando doble bufer ping-pong.
     * @return true si el trabajo fue aceptado en una ranura libre; false si ambos buferes estan ocupados.
     */
    static bool SaveFileAsync(std::string_view relativePath, const void* data, size_t size,
                              bool signWithHmac = false,
                              std::string_view hmacKey = DEFAULT_SAVE_HMAC_KEY);

    [[nodiscard]] static bool IsAsyncSavePending() noexcept;
    static void WaitForAsyncSaves() noexcept;
    [[nodiscard]] static bool GetLastAsyncSaveSuccess() noexcept;

    static constexpr std::string_view DEFAULT_SAVE_HMAC_KEY = "WiiU_Engine_C++20_SaveIntegrity_v1";

private:
    static bool s_initialized;
    static char s_saveRoot[256];
};

} // namespace WiiU
