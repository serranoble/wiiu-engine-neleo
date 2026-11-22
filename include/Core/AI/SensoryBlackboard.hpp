#pragma once

#include "Core/ECS/Entity.hpp"
#include "Graphics/Math3D.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace WiiU {

enum class StimulusType : uint8_t {
    None = 0,
    Sight,
    Hearing,
    Damage
};

enum class BlackboardValueType : uint8_t {
    Empty = 0,
    Bool,
    Int,
    Float,
    Vec3,
    EntityRef
};

struct BlackboardSlot {
    uint32_t keyHash{0};
    BlackboardValueType type{BlackboardValueType::Empty};
    union {
        bool b;
        int32_t i;
        float f;
        Point3D v;
        Entity e;
    } data{};

    constexpr BlackboardSlot() noexcept : keyHash(0), type(BlackboardValueType::Empty) {
        data.i = 0;
    }
};

/**
 * @brief [OPT-386] Pizarra Sensorial (Sensory Blackboard) Zero-Alloc para Agentes de IA.
 *
 * Almacena hechos tipados y memoria sensorial reciente del agente en un arreglo contiguo
 * compatible con la cache L1D de PowerPC Espresso (sin std::unordered_map ni heap).
 */
struct SensoryBlackboard {
    static constexpr size_t MAX_SLOTS = 16;

    // Campos rapidos de percepcion sensorial en caliente
    bool hasTarget{false};
    Entity targetEntity{Entity::Null};
    Point3D lastKnownTargetPos{0.0f, 0.0f, 0.0f};
    float timeSinceLastStimulus{999.0f};
    float alertLevel{0.0f}; // [0.0 = calmado, 1.0 = alerta maxima]
    StimulusType dominantStimulus{StimulusType::None};

    std::array<BlackboardSlot, MAX_SLOTS> slots{};

    [[nodiscard]] static constexpr uint32_t HashKey(std::string_view key) noexcept {
        uint32_t h = 2166136261u;
        for (char c : key) {
            h ^= static_cast<uint8_t>(c);
            h *= 16777619u;
        }
        return (h == 0u) ? 1u : h;
    }

    void Clear() noexcept {
        hasTarget = false;
        targetEntity = Entity::Null;
        lastKnownTargetPos = Point3D{0.0f, 0.0f, 0.0f};
        timeSinceLastStimulus = 999.0f;
        alertLevel = 0.0f;
        dominantStimulus = StimulusType::None;
        for (auto& s : slots) {
            s.keyHash = 0;
            s.type = BlackboardValueType::Empty;
        }
    }

    BlackboardSlot* FindOrAlloc(uint32_t h) noexcept {
        BlackboardSlot* freeSlot = nullptr;
        for (auto& s : slots) {
            if (s.keyHash == h) return &s;
            if (!freeSlot && s.type == BlackboardValueType::Empty) freeSlot = &s;
        }
        if (freeSlot) {
            freeSlot->keyHash = h;
        }
        return freeSlot;
    }

    [[nodiscard]] const BlackboardSlot* Find(uint32_t h) const noexcept {
        for (const auto& s : slots) {
            if (s.type != BlackboardValueType::Empty && s.keyHash == h) return &s;
        }
        return nullptr;
    }

    void SetBool(std::string_view key, bool v) noexcept {
        if (auto* s = FindOrAlloc(HashKey(key))) { s->type = BlackboardValueType::Bool; s->data.b = v; }
    }
    void SetInt(std::string_view key, int32_t v) noexcept {
        if (auto* s = FindOrAlloc(HashKey(key))) { s->type = BlackboardValueType::Int; s->data.i = v; }
    }
    void SetFloat(std::string_view key, float v) noexcept {
        if (auto* s = FindOrAlloc(HashKey(key))) { s->type = BlackboardValueType::Float; s->data.f = v; }
    }
    void SetPoint3D(std::string_view key, const Point3D& v) noexcept {
        if (auto* s = FindOrAlloc(HashKey(key))) { s->type = BlackboardValueType::Vec3; s->data.v = v; }
    }
    void SetEntity(std::string_view key, Entity e) noexcept {
        if (auto* s = FindOrAlloc(HashKey(key))) { s->type = BlackboardValueType::EntityRef; s->data.e = e; }
    }

    [[nodiscard]] bool GetBool(std::string_view key, bool fallback = false) const noexcept {
        const auto* s = Find(HashKey(key));
        return (s && s->type == BlackboardValueType::Bool) ? s->data.b : fallback;
    }
    [[nodiscard]] int32_t GetInt(std::string_view key, int32_t fallback = 0) const noexcept {
        const auto* s = Find(HashKey(key));
        return (s && s->type == BlackboardValueType::Int) ? s->data.i : fallback;
    }
    [[nodiscard]] float GetFloat(std::string_view key, float fallback = 0.0f) const noexcept {
        const auto* s = Find(HashKey(key));
        return (s && s->type == BlackboardValueType::Float) ? s->data.f : fallback;
    }
    [[nodiscard]] Point3D GetPoint3D(std::string_view key, const Point3D& fallback = Point3D{0.0f, 0.0f, 0.0f}) const noexcept {
        const auto* s = Find(HashKey(key));
        return (s && s->type == BlackboardValueType::Vec3) ? s->data.v : fallback;
    }
    [[nodiscard]] Entity GetEntity(std::string_view key, Entity fallback = Entity::Null) const noexcept {
        const auto* s = Find(HashKey(key));
        return (s && s->type == BlackboardValueType::EntityRef) ? s->data.e : fallback;
    }
};

} // namespace WiiU
