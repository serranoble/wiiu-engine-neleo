#pragma once

#include "Scene/IScene.hpp"
#include "Scene/Starfield3D.hpp"
#include "Scene/RingEffect.hpp"
#include "Graphics/UIComponents.hpp"
#include "Core/Math/PRNG.hpp"
#include <SDL2/SDL.h>
#include <vector>

namespace WiiU {

enum class EmitterMode : uint8_t {
    HyperspaceBox = 0, // Muestreo en caja AABB volumétrica 3D
    FountainCone  = 1, // Dirección en cono 3D (NextDirectionInCone + Gaussiana)
    SphericalNova = 2, // Superficie y volumen esférico 3D (NextPointOnSphere S^2)
    GalacticDisc  = 3  // Disco plano uniforme 2D/3D (NextPointInCircle r=R*sqrt(u))
};

struct DynamicParticle {
    Point3D pos;
    Point3D vel;
    ColorRGBA color;
    float life{0.0f};
    float maxLife{1.0f};
    float size{2.0f};
};

class SceneParticles : public IScene {
public:
    SceneParticles();

    void Update(float dt) override;
    void Render(SDL_Renderer* renderer) override;

    [[nodiscard]] std::vector<std::string> GetRequiredAssets() const override {
        return {"click.wav"};
    }
    [[nodiscard]] std::string GetSceneName() const override {
        return "Demo 2: Particulas 3D & PRNG";
    }

    void CycleSeed();
    void CycleMode();
    void ResetEmitter();

private:
    void InitParticle(DynamicParticle& p, bool initialBurst = false);

    Starfield3D m_starfield;
    RingEffect  m_ring;
    PRNG        m_prng;

    static constexpr size_t NUM_DYNAMIC_PARTICLES = 400;
    std::vector<DynamicParticle> m_particles;
    std::vector<SDL_Vertex>      m_vertexBatch;
    std::vector<int>             m_indexBatch;

    EmitterMode m_mode{EmitterMode::FountainCone};
    size_t      m_seedIndex{0};
    uint64_t    m_activeSeed{0x1337BEEFCAFEULL};
    bool        m_isPaused{false};
    float       m_smoothedFps{60.0f};
    float       m_simulationTime{0.0f};
    Point3D     m_emitterOrigin{0.0f, -2.5f, 6.0f};

    // Botones interactivos
    UIBackButton m_backButton;
    UIButton     m_btnReseed;
    UIButton     m_btnMode;
    UIButton     m_btnPause;
};

} // namespace WiiU
