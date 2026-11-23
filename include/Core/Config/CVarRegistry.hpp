#pragma once

#include "Core/StackString.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace WiiU {

namespace Diagnostics {
class CoreSelfTest;
}

enum class CVarType : uint8_t {
    Bool = 0,
    Int,
    Float,
    String
};

using CVarChangeCallback = void (*)(std::string_view name, void* userData);

/**
 * @brief Entrada individual del registro de Console Variables (CVar).
 * Almacenamiento contiguo Zero-Alloc con respaldo de valor por defecto y limites.
 */
struct CVarEntry {
    StackString<48> name{};
    StackString<96> description{};
    uint32_t nameHash{0};
    CVarType type{CVarType::Float};

    union {
        bool b;
        int32_t i;
        float f;
    } value{};

    union {
        bool b;
        int32_t i;
        float f;
    } defaultValue{};

    StackString<64> strValue{};
    StackString<64> defaultStrValue{};

    float minVal{-1.0e9f};
    float maxVal{1.0e9f};
    int32_t minInt{-2147483647};
    int32_t maxInt{2147483647};
    bool hasRange{false};
    bool inUse{false};

    CVarChangeCallback onChanged{nullptr};
    void* userData{nullptr};
};

/**
 * @brief [OPT-395] Registro Dinamico de Console Variables (CVars) y Procesador de Comandos In-Game.
 *
 * - 0 alocaciones dinamicas en consulta, modificacion y ejecucion de comandos.
 * - Busqueda por hash FNV-1a de 32 bits sobre arreglo plano en memoria estatica.
 * - Soporta comandos textuales: "get <var>", "set <var> <val>", "toggle <var>", "reset <var>", "list".
 */
class CVarRegistry {
public:
    static constexpr size_t MAX_CVARS = 64;

    static CVarRegistry& Instance() noexcept;

    CVarRegistry(const CVarRegistry&) = delete;
    CVarRegistry& operator=(const CVarRegistry&) = delete;
    CVarRegistry(CVarRegistry&&) = delete;
    CVarRegistry& operator=(CVarRegistry&&) = delete;

    void Clear() noexcept;
    void RegisterDefaultEngineCVars() noexcept;

    CVarEntry* RegisterBool(std::string_view name, bool defaultVal,
                            std::string_view description = "",
                            CVarChangeCallback cb = nullptr, void* userData = nullptr) noexcept;

    CVarEntry* RegisterInt(std::string_view name, int32_t defaultVal,
                           int32_t minVal = -2147483647, int32_t maxVal = 2147483647,
                           std::string_view description = "",
                           CVarChangeCallback cb = nullptr, void* userData = nullptr) noexcept;

    CVarEntry* RegisterFloat(std::string_view name, float defaultVal,
                             float minVal = -1.0e9f, float maxVal = 1.0e9f,
                             std::string_view description = "",
                             CVarChangeCallback cb = nullptr, void* userData = nullptr) noexcept;

    CVarEntry* RegisterString(std::string_view name, std::string_view defaultVal,
                              std::string_view description = "",
                              CVarChangeCallback cb = nullptr, void* userData = nullptr) noexcept;

    [[nodiscard]] CVarEntry* Find(std::string_view name) noexcept;
    [[nodiscard]] const CVarEntry* Find(std::string_view name) const noexcept;

    [[nodiscard]] bool GetBool(std::string_view name, bool fallback = false) const noexcept;
    [[nodiscard]] int32_t GetInt(std::string_view name, int32_t fallback = 0) const noexcept;
    [[nodiscard]] float GetFloat(std::string_view name, float fallback = 0.0f) const noexcept;
    [[nodiscard]] std::string_view GetString(std::string_view name, std::string_view fallback = "") const noexcept;

    bool SetBool(std::string_view name, bool val) noexcept;
    bool SetInt(std::string_view name, int32_t val) noexcept;
    bool SetFloat(std::string_view name, float val) noexcept;
    bool SetString(std::string_view name, std::string_view val) noexcept;
    bool Reset(std::string_view name) noexcept;
    void ResetAll() noexcept;

    /**
     * @brief Ejecuta un comando de consola ("set r.shadows 1", "toggle r.earlyz", "get phys.gravity").
     * @param outResponse Bufer en pila donde se escribe la respuesta del comando.
     */
    bool ExecuteCommand(std::string_view commandLine, StackString<256>& outResponse) noexcept;

    [[nodiscard]] size_t GetCount() const noexcept { return m_count; }
    [[nodiscard]] const CVarEntry* GetByIndex(size_t index) const noexcept {
        return (index < m_count) ? &m_entries[index] : nullptr;
    }

    [[nodiscard]] static constexpr uint32_t HashName(std::string_view s) noexcept {
        uint32_t h = 2166136261u;
        for (char c : s) {
            h ^= static_cast<uint8_t>(c);
            h *= 16777619u;
        }
        return h;
    }

private:
    friend class Diagnostics::CoreSelfTest;

    CVarRegistry() noexcept = default;
    ~CVarRegistry() noexcept = default;

    CVarEntry* AllocateOrFind(std::string_view name, CVarType type, std::string_view description,
                              CVarChangeCallback cb, void* userData) noexcept;

    std::array<CVarEntry, MAX_CVARS> m_entries{};
    size_t m_count{0};
};

} // namespace WiiU
