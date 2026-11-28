#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Neleo {

/**
 * @brief Hash FNV-1a de 64 bits evaluable en tiempo de compilación para claves de localización.
 */
[[nodiscard]] constexpr uint64_t HashLocKey(std::string_view key) noexcept {
    uint64_t hash = 14695981039346656037ULL;
    for (const char c : key) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= 1099511628211ULL;
    }
    return (hash == 0ULL) ? 1ULL : hash;
}

/**
 * @brief Variante consteval (C++20) que garantiza cálculo en tiempo de compilación incluso en -O0.
 */
[[nodiscard]] consteval uint64_t HashLocKeyLiteral(std::string_view key) noexcept {
    return HashLocKey(key);
}

/**
 * @brief Descriptor de un idioma registrado en el manifiesto (assets/lang/languages.cfg).
 */
struct LanguageDescriptor {
    char code[16]{};         // Código ISO corto: "es", "en", "fr", "pt", ...
    char displayName[48]{};  // Nombre visible en UI (ASCII 5x7): "ESPANOL", "ENGLISH", ...
    char virtualPath[96]{};  // Ruta VFS: "content:/lang/es.lang"
};

/**
 * @brief Tabla de traducción autocontenida y Zero-Alloc en consulta (O(1) open addressing).
 *
 * Almacena todas las cadenas terminadas en '\0' dentro de un buffer plano contiguo,
 * garantizando 0 alocaciones dinámicas durante el renderizado a 60 FPS.
 */
class LocalizationTable {
public:
    static constexpr size_t HASH_TABLE_CAPACITY  = 1024u; // Potencia de 2
    static constexpr size_t TEXT_BUFFER_CAPACITY = 32768u;

    LocalizationTable() noexcept = default;
    LocalizationTable(const LocalizationTable&) = delete;
    LocalizationTable& operator=(const LocalizationTable&) = delete;
    LocalizationTable(LocalizationTable&&) = delete;
    LocalizationTable& operator=(LocalizationTable&&) = delete;

    void Clear() noexcept;

    /**
     * @brief Parsea un archivo de texto plano formato `clave=valor` (comentarios `#` o `;`).
     * @return Número de entradas válidas cargadas.
     */
    size_t ParseFromMemory(std::string_view content) noexcept;

    /**
     * @brief Inserta o actualiza una entrada individual (útil para pruebas o overrides).
     */
    bool Insert(std::string_view key, std::string_view value) noexcept;

    [[nodiscard]] const char* FindCStr(uint64_t keyHash, size_t* outLength = nullptr) const noexcept;
    [[nodiscard]] std::string_view Find(uint64_t keyHash) const noexcept;

    [[nodiscard]] size_t GetEntryCount() const noexcept { return m_entryCount; }
    [[nodiscard]] size_t GetTextBytesUsed() const noexcept { return m_textUsed; }
    [[nodiscard]] bool IsEmpty() const noexcept { return m_entryCount == 0u; }

private:
    struct EntrySlot {
        uint64_t hash{0ULL};
        uint32_t offset{0u};
        uint16_t length{0u};
        uint16_t reserved{0u};
    };

    std::array<EntrySlot, HASH_TABLE_CAPACITY> m_slots{};
    std::array<char, TEXT_BUFFER_CAPACITY> m_textBuffer{};
    size_t m_entryCount{0u};
    size_t m_textUsed{0u};
};

using LanguageChangeCallback = void(*)(std::string_view newLanguageCode, void* userData);

/**
 * @brief Gestor central de localización (i18n) del motor en engine_core.
 *
 * - Carga archivos de idioma vía VirtualFileSystem ("content:/lang/<code>.lang").
 * - Mantiene un catálogo fallback ("es") para que nunca exista una cadena vacía o nula.
 * - Soporta registro dinámico de nuevos idiomas desde "content:/lang/languages.cfg".
 * - Notifica cambios a escenas/modales activos mediante observadores pasivos Zero-Alloc.
 */
class LocalizationManager {
public:
    static constexpr size_t MAX_LANGUAGES = 16u;
    static constexpr size_t MAX_OBSERVERS = 16u;
    static constexpr std::string_view DEFAULT_FALLBACK_LANG = "es";

    static LocalizationManager& Instance() noexcept {
        static LocalizationManager instance;
        return instance;
    }

    LocalizationManager(const LocalizationManager&) = delete;
    LocalizationManager& operator=(const LocalizationManager&) = delete;
    LocalizationManager(LocalizationManager&&) = delete;
    LocalizationManager& operator=(LocalizationManager&&) = delete;

    /**
     * @brief Inicializa el subsistema, carga el manifiesto de idiomas y activa el idioma solicitado.
     */
    bool Initialize(std::string_view initialLanguage = DEFAULT_FALLBACK_LANG) noexcept;

    /**
     * @brief Libera tablas y observadores de forma determinista antes de Engine::Shutdown().
     */
    void Shutdown() noexcept;

    /**
     * @brief Carga el manifiesto de idiomas disponibles (formato: `codigo|NOMBRE|content:/lang/codigo.lang`).
     */
    bool LoadLanguageManifest(std::string_view manifestVirtualPath = "content:/lang/languages.cfg") noexcept;

    /**
     * @brief Registra un idioma manualmente (útil si el manifiesto no está presente o para tests).
     */
    bool RegisterLanguage(std::string_view code,
                          std::string_view displayName,
                          std::string_view virtualPath) noexcept;

    /**
     * @brief Cambia el idioma activo en caliente y notifica a los observadores registrados.
     */
    bool SetLanguage(std::string_view code) noexcept;

    /**
     * @brief Avanza cíclicamente al siguiente idioma registrado.
     */
    bool CycleLanguage() noexcept;

    [[nodiscard]] std::string_view GetCurrentLanguageCode() const noexcept;
    [[nodiscard]] std::string_view GetCurrentLanguageDisplayName() const noexcept;
    [[nodiscard]] bool IsCurrentLanguage(std::string_view code) const noexcept;
    [[nodiscard]] size_t GetLanguageCount() const noexcept { return m_languageCount; }
    [[nodiscard]] const LanguageDescriptor* GetLanguageAt(size_t index) const noexcept;
    [[nodiscard]] bool HasLanguage(std::string_view code) const noexcept;

    /**
     * @brief Consulta O(1) Zero-Alloc por hash precalculado en tiempo de compilación.
     */
    [[nodiscard]] std::string_view Get(uint64_t keyHash, std::string_view fallback) const noexcept;
    [[nodiscard]] std::string_view Get(std::string_view key) const noexcept;

    /**
     * @brief Consulta O(1) Zero-Alloc devolviendo puntero terminado en '\0' para snprintf/UI.
     */
    [[nodiscard]] const char* GetCStr(uint64_t keyHash, const char* fallback) const noexcept;
    [[nodiscard]] const char* GetCStr(const char* key) const noexcept;

    uint32_t RegisterChangeCallback(LanguageChangeCallback cb, void* userData) noexcept;
    void UnregisterChangeCallback(uint32_t observerId) noexcept;

    [[nodiscard]] size_t GetActiveEntryCount() const noexcept;
    [[nodiscard]] size_t GetFallbackEntryCount() const noexcept { return m_fallbackTable.GetEntryCount(); }
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

    /**
     * @brief Autodiagnóstico aislado (sin disparar observadores del singleton vivo).
     */
    [[nodiscard]] static bool VerifyIntegrity(const char** outFailureReason = nullptr) noexcept;

private:
    LocalizationManager() noexcept = default;
    ~LocalizationManager() noexcept = default;

    struct ObserverSlot {
        uint32_t id{0u};
        LanguageChangeCallback callback{nullptr};
        void* userData{nullptr};
    };

    const LanguageDescriptor* FindLanguageDescriptor(std::string_view code) const noexcept;
    bool LoadTableFromVirtualPath(std::string_view virtualPath, LocalizationTable& outTable) noexcept;
    void NotifyObservers() noexcept;

    std::array<LanguageDescriptor, MAX_LANGUAGES> m_languages{};
    size_t m_languageCount{0u};
    size_t m_activeLanguageIndex{0u};

    LocalizationTable m_fallbackTable{};
    LocalizationTable m_activeTable{};
    bool m_activeIsFallback{true};

    std::array<ObserverSlot, MAX_OBSERVERS> m_observers{};
    uint32_t m_nextObserverId{1u};
    bool m_initialized{false};
};

namespace Loc {

[[nodiscard]] inline std::string_view Tr(std::string_view key) noexcept {
    return LocalizationManager::Instance().Get(key);
}

[[nodiscard]] inline const char* TrCStr(const char* key) noexcept {
    return LocalizationManager::Instance().GetCStr(key);
}

} // namespace Loc

} // namespace Neleo

#define NELEO_LOC(keyLiteral) \
    (::Neleo::LocalizationManager::Instance().Get(::Neleo::HashLocKeyLiteral(keyLiteral), (keyLiteral)))

#define NELEO_LOC_CSTR(keyLiteral) \
    (::Neleo::LocalizationManager::Instance().GetCStr(::Neleo::HashLocKeyLiteral(keyLiteral), (keyLiteral)))
