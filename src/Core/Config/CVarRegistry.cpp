#include "Core/Config/CVarRegistry.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace WiiU {

namespace {

std::string_view Trim(std::string_view sv) noexcept {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
        sv.remove_suffix(1);
    }
    return sv;
}

std::string_view NextToken(std::string_view& rest) noexcept {
    rest = Trim(rest);
    if (rest.empty()) return {};
    size_t pos = 0;
    while (pos < rest.size() && rest[pos] != ' ' && rest[pos] != '\t') {
        ++pos;
    }
    std::string_view tok = rest.substr(0, pos);
    rest.remove_prefix(pos);
    return tok;
}

bool ParseBoolToken(std::string_view tok, bool& outVal) noexcept {
    if (tok == "1" || tok == "true" || tok == "TRUE" || tok == "on" || tok == "ON") {
        outVal = true;
        return true;
    }
    if (tok == "0" || tok == "false" || tok == "FALSE" || tok == "off" || tok == "OFF") {
        outVal = false;
        return true;
    }
    return false;
}

} // namespace

CVarRegistry& CVarRegistry::Instance() noexcept {
    static CVarRegistry instance;
    return instance;
}

void CVarRegistry::Clear() noexcept {
    for (auto& e : m_entries) {
        e = CVarEntry{};
    }
    m_count = 0;
}

void CVarRegistry::RegisterDefaultEngineCVars() noexcept {
    RegisterBool("r.earlyz", true, "Habilita Early-Z Depth Pre-Pass y Hi-Z Fast-Clear en eDRAM");
    RegisterBool("r.shadows", true, "Habilita Shadow Mapping hardware PCF 2x2");
    RegisterBool("r.instancing", true, "Habilita Hardware Geometry Instancing GX2");
    RegisterBool("r.render_queue_sort", true, "Habilita ordenamiento 64-bit Stateless RenderQueue");
    RegisterFloat("r.shadow_bias", 0.0025f, 0.0f, 0.05f, "Depth bias para Shadow Map hardware");
    RegisterFloat("phys.gravity", 16.0f, 0.0f, 50.0f, "Gravedad vertical global del motor (m/s^2)");
    RegisterBool("phys.spatial_hash", true, "Activa Broadphase SpatialHashGrid en colisiones");
    RegisterBool("audio.bgm_streaming", true, "Habilita streaming ping-pong BGM <= 256 KB");
    RegisterInt("audio.virtual_voices", 128, 32, 128, "Limite de voces virtuales en AudioEngine3D");
    RegisterBool("input.autopause_disconnect", true, "Pausa automatica ante desconexion HID");
    RegisterBool("debug.show_telemetry", true, "Muestra telemetria de rendimiento en HUD");
}

CVarEntry* CVarRegistry::AllocateOrFind(std::string_view name, CVarType type, std::string_view description,
                                        CVarChangeCallback cb, void* userData) noexcept {
    if (name.empty() || name.size() >= m_entries[0].name.capacity()) return nullptr;
    if (CVarEntry* existing = Find(name)) {
        if (existing->type != type) {
            return nullptr;
        }
        existing->onChanged = cb;
        existing->userData = userData;
        return existing;
    }
    if (m_count >= MAX_CVARS) return nullptr;

    CVarEntry& e = m_entries[m_count++];
    e = CVarEntry{};
    e.name.Format("%.*s", static_cast<int>(name.size()), name.data());
    e.description.Format("%.*s", static_cast<int>(description.size()), description.data());
    e.nameHash = HashName(e.name.string_view());
    e.type = type;
    e.onChanged = cb;
    e.userData = userData;
    e.inUse = true;
    return &e;
}

CVarEntry* CVarRegistry::RegisterBool(std::string_view name, bool defaultVal, std::string_view description,
                                      CVarChangeCallback cb, void* userData) noexcept {
    CVarEntry* e = AllocateOrFind(name, CVarType::Bool, description, cb, userData);
    if (!e) return nullptr;
    e->value.b = defaultVal;
    e->defaultValue.b = defaultVal;
    e->hasRange = false;
    return e;
}

CVarEntry* CVarRegistry::RegisterInt(std::string_view name, int32_t defaultVal,
                                     int32_t minVal, int32_t maxVal, std::string_view description,
                                     CVarChangeCallback cb, void* userData) noexcept {
    if (minVal > maxVal) std::swap(minVal, maxVal);
    CVarEntry* e = AllocateOrFind(name, CVarType::Int, description, cb, userData);
    if (!e) return nullptr;
    const int32_t clamped = std::clamp(defaultVal, minVal, maxVal);
    e->value.i = clamped;
    e->defaultValue.i = clamped;
    e->minInt = minVal;
    e->maxInt = maxVal;
    e->hasRange = true;
    return e;
}

CVarEntry* CVarRegistry::RegisterFloat(std::string_view name, float defaultVal,
                                       float minVal, float maxVal, std::string_view description,
                                       CVarChangeCallback cb, void* userData) noexcept {
    if (!std::isfinite(defaultVal) || !std::isfinite(minVal) || !std::isfinite(maxVal)) return nullptr;
    if (minVal > maxVal) std::swap(minVal, maxVal);
    CVarEntry* e = AllocateOrFind(name, CVarType::Float, description, cb, userData);
    if (!e) return nullptr;
    const float clamped = std::clamp(defaultVal, minVal, maxVal);
    e->value.f = clamped;
    e->defaultValue.f = clamped;
    e->minVal = minVal;
    e->maxVal = maxVal;
    e->hasRange = true;
    return e;
}

CVarEntry* CVarRegistry::RegisterString(std::string_view name, std::string_view defaultVal,
                                        std::string_view description,
                                        CVarChangeCallback cb, void* userData) noexcept {
    CVarEntry* e = AllocateOrFind(name, CVarType::String, description, cb, userData);
    if (!e) return nullptr;
    e->strValue.Format("%.*s", static_cast<int>(defaultVal.size()), defaultVal.data());
    e->defaultStrValue = e->strValue;
    e->hasRange = false;
    return e;
}

CVarEntry* CVarRegistry::Find(std::string_view name) noexcept {
    const uint32_t h = HashName(name);
    for (size_t i = 0; i < m_count; ++i) {
        if (m_entries[i].inUse && m_entries[i].nameHash == h && m_entries[i].name.string_view() == name) {
            return &m_entries[i];
        }
    }
    return nullptr;
}

const CVarEntry* CVarRegistry::Find(std::string_view name) const noexcept {
    const uint32_t h = HashName(name);
    for (size_t i = 0; i < m_count; ++i) {
        if (m_entries[i].inUse && m_entries[i].nameHash == h && m_entries[i].name.string_view() == name) {
            return &m_entries[i];
        }
    }
    return nullptr;
}

bool CVarRegistry::GetBool(std::string_view name, bool fallback) const noexcept {
    const CVarEntry* e = Find(name);
    if (!e || e->type != CVarType::Bool) return fallback;
    return e->value.b;
}

int32_t CVarRegistry::GetInt(std::string_view name, int32_t fallback) const noexcept {
    const CVarEntry* e = Find(name);
    if (!e || e->type != CVarType::Int) return fallback;
    return e->value.i;
}

float CVarRegistry::GetFloat(std::string_view name, float fallback) const noexcept {
    const CVarEntry* e = Find(name);
    if (!e || e->type != CVarType::Float) return fallback;
    return e->value.f;
}

std::string_view CVarRegistry::GetString(std::string_view name, std::string_view fallback) const noexcept {
    const CVarEntry* e = Find(name);
    if (!e || e->type != CVarType::String) return fallback;
    return e->strValue.string_view();
}

bool CVarRegistry::SetBool(std::string_view name, bool val) noexcept {
    CVarEntry* e = Find(name);
    if (!e || e->type != CVarType::Bool) return false;
    e->value.b = val;
    if (e->onChanged) e->onChanged(e->name.string_view(), e->userData);
    return true;
}

bool CVarRegistry::SetInt(std::string_view name, int32_t val) noexcept {
    CVarEntry* e = Find(name);
    if (!e || e->type != CVarType::Int) return false;
    if (e->hasRange) {
        val = std::clamp(val, e->minInt, e->maxInt);
    }
    e->value.i = val;
    if (e->onChanged) e->onChanged(e->name.string_view(), e->userData);
    return true;
}

bool CVarRegistry::SetFloat(std::string_view name, float val) noexcept {
    if (!std::isfinite(val)) return false;
    CVarEntry* e = Find(name);
    if (!e || e->type != CVarType::Float) return false;
    if (e->hasRange) {
        val = std::clamp(val, e->minVal, e->maxVal);
    }
    e->value.f = val;
    if (e->onChanged) e->onChanged(e->name.string_view(), e->userData);
    return true;
}

bool CVarRegistry::SetString(std::string_view name, std::string_view val) noexcept {
    CVarEntry* e = Find(name);
    if (!e || e->type != CVarType::String) return false;
    e->strValue.Format("%.*s", static_cast<int>(val.size()), val.data());
    if (e->onChanged) e->onChanged(e->name.string_view(), e->userData);
    return true;
}

bool CVarRegistry::Reset(std::string_view name) noexcept {
    CVarEntry* e = Find(name);
    if (!e) return false;
    switch (e->type) {
        case CVarType::Bool:   e->value.b = e->defaultValue.b; break;
        case CVarType::Int:    e->value.i = e->defaultValue.i; break;
        case CVarType::Float:  e->value.f = e->defaultValue.f; break;
        case CVarType::String: e->strValue = e->defaultStrValue; break;
    }
    if (e->onChanged) e->onChanged(e->name.string_view(), e->userData);
    return true;
}

void CVarRegistry::ResetAll() noexcept {
    for (size_t i = 0; i < m_count; ++i) {
        if (m_entries[i].inUse) {
            Reset(m_entries[i].name.string_view());
        }
    }
}

bool CVarRegistry::ExecuteCommand(std::string_view commandLine, StackString<256>& outResponse) noexcept {
    outResponse.Clear();
    std::string_view rest = Trim(commandLine);
    if (rest.empty()) {
        outResponse.Set("ERR: comando vacio");
        return false;
    }

    const std::string_view verb = NextToken(rest);
    if (verb == "list") {
        outResponse.Format("CVars registradas: %zu / %zu", m_count, MAX_CVARS);
        return true;
    }

    const std::string_view name = NextToken(rest);
    if (name.empty()) {
        outResponse.Set("ERR: falta nombre de CVar");
        return false;
    }

    CVarEntry* e = Find(name);
    if (!e) {
        outResponse.Format("ERR: CVar no encontrada '%.*s'", static_cast<int>(name.size()), name.data());
        return false;
    }

    if (verb == "get") {
        switch (e->type) {
            case CVarType::Bool:
                outResponse.Format("%s = %s", e->name.c_str(), e->value.b ? "true" : "false");
                return true;
            case CVarType::Int:
                outResponse.Format("%s = %d", e->name.c_str(), e->value.i);
                return true;
            case CVarType::Float:
                outResponse.Format("%s = %.4f", e->name.c_str(), static_cast<double>(e->value.f));
                return true;
            case CVarType::String:
                outResponse.Format("%s = \"%s\"", e->name.c_str(), e->strValue.c_str());
                return true;
        }
    }

    if (verb == "toggle") {
        if (e->type != CVarType::Bool) {
            outResponse.Format("ERR: '%s' no es booleana", e->name.c_str());
            return false;
        }
        SetBool(name, !e->value.b);
        outResponse.Format("%s = %s", e->name.c_str(), e->value.b ? "true" : "false");
        return true;
    }

    if (verb == "reset") {
        Reset(name);
        outResponse.Format("OK: '%s' restaurada al valor por defecto", e->name.c_str());
        return true;
    }

    if (verb == "set") {
        const std::string_view valTok = Trim(rest);
        if (valTok.empty()) {
            outResponse.Set("ERR: falta valor para 'set'");
            return false;
        }
        char numBuf[64]{};
        const size_t copyLen = std::min(valTok.size(), sizeof(numBuf) - 1);
        std::memcpy(numBuf, valTok.data(), copyLen);
        numBuf[copyLen] = '\0';

        switch (e->type) {
            case CVarType::Bool: {
                bool parsed = false;
                if (!ParseBoolToken(valTok, parsed)) {
                    outResponse.Set("ERR: valor booleano invalido (usa 0/1/true/false)");
                    return false;
                }
                SetBool(name, parsed);
                outResponse.Format("OK: %s = %s", e->name.c_str(), e->value.b ? "true" : "false");
                return true;
            }
            case CVarType::Int: {
                char* endPtr = nullptr;
                long parsed = std::strtol(numBuf, &endPtr, 10);
                if (endPtr == numBuf || *endPtr != '\0') {
                    outResponse.Set("ERR: valor entero invalido");
                    return false;
                }
                SetInt(name, static_cast<int32_t>(parsed));
                outResponse.Format("OK: %s = %d", e->name.c_str(), e->value.i);
                return true;
            }
            case CVarType::Float: {
                char* endPtr = nullptr;
                float parsed = std::strtof(numBuf, &endPtr);
                if (endPtr == numBuf || *endPtr != '\0' || !std::isfinite(parsed)) {
                    outResponse.Set("ERR: valor flotante invalido");
                    return false;
                }
                SetFloat(name, parsed);
                outResponse.Format("OK: %s = %.4f", e->name.c_str(), static_cast<double>(e->value.f));
                return true;
            }
            case CVarType::String: {
                SetString(name, valTok);
                outResponse.Format("OK: %s = \"%s\"", e->name.c_str(), e->strValue.c_str());
                return true;
            }
        }
    }

    outResponse.Format("ERR: comando desconocido '%.*s'", static_cast<int>(verb.size()), verb.data());
    return false;
}

} // namespace WiiU
