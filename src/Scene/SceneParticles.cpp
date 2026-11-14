#include "Scene/SceneParticles.hpp"
#include "Scene/SceneMenuSDL.hpp"
#include "Scene/SceneManager.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"
#include "Core/StackString.hpp"
#include <algorithm>

namespace WiiU {

constexpr uint64_t SEED_PRESETS[] = {
    0x1337BEEFCAFEULL,
    0xCAFE2026DEADULL,
    0x424242424242ULL,
    0x9876543210ABULL
};
constexpr size_t NUM_SEEDS = sizeof(SEED_PRESETS) / sizeof(SEED_PRESETS[0]);

SceneParticles::SceneParticles()
    : m_starfield(250, SEED_PRESETS[0]),
      m_prng(SEED_PRESETS[0]),
      m_backButton([]() {
          AudioManager::Instance().PlayClick();
          SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuSDL>());
      }),
      m_btnReseed(710, 18, 165, 42, "[A] RESEED", 2),
      m_btnMode(888, 18, 185, 42, "[X] MODO DIST", 2),
      m_btnPause(1085, 18, 155, 42, "[Y] PAUSAR", 2) {

    m_btnReseed.SetColors(ColorRGBA{20, 45, 75, 230}, ColorRGBA{40, 140, 255, 240},
                          ColorRGBA::Cyan(), ColorRGBA::Cyan(), ColorRGBA::White());
    m_btnMode.SetColors(ColorRGBA{20, 45, 75, 230}, ColorRGBA{40, 140, 255, 240},
                        ColorRGBA::Yellow(), ColorRGBA::Yellow(), ColorRGBA::White());
    m_btnPause.SetColors(ColorRGBA{20, 45, 75, 230}, ColorRGBA{40, 140, 255, 240},
                         ColorRGBA::Orange(), ColorRGBA::Orange(), ColorRGBA::White());

    m_particles.resize(NUM_DYNAMIC_PARTICLES);
    m_vertexBatch.resize(NUM_DYNAMIC_PARTICLES * 4);
    m_indexBatch.resize(NUM_DYNAMIC_PARTICLES * 6);

    ResetEmitter();
}

void SceneParticles::ResetEmitter() {
    m_prng.SetSeed(m_activeSeed);
    m_starfield.ResetSeed(m_activeSeed);

    for (auto& p : m_particles) {
        InitParticle(p, true);
    }
}

void SceneParticles::CycleSeed() {
    m_seedIndex = (m_seedIndex + 1) % NUM_SEEDS;
    m_activeSeed = SEED_PRESETS[m_seedIndex];
    ResetEmitter();
}

void SceneParticles::CycleMode() {
    const uint8_t nextMode = (static_cast<uint8_t>(m_mode) + 1) % 4;
    m_mode = static_cast<EmitterMode>(nextMode);

    if (m_mode == EmitterMode::FountainCone) {
        m_emitterOrigin = Point3D{0.0f, -2.5f, 6.0f};
    } else if (m_mode == EmitterMode::SphericalNova) {
        m_emitterOrigin = Point3D{0.0f, 0.0f, 6.0f};
    } else if (m_mode == EmitterMode::GalacticDisc) {
        m_emitterOrigin = Point3D{0.0f, 0.0f, 6.5f};
    } else {
        m_emitterOrigin = Point3D{0.0f, 0.0f, 8.0f};
    }

    ResetEmitter();
}

void SceneParticles::InitParticle(DynamicParticle& p, bool initialBurst) {
    switch (m_mode) {
        case EmitterMode::HyperspaceBox: {
            // Muestreo AABB volumétrico uniforme en 3D
            p.pos = m_prng.NextPointInAABB(Point3D{-7.0f, -4.5f, 2.0f}, Point3D{7.0f, 4.5f, 14.0f});
            p.vel = Point3D{0.0f, 0.0f, -m_prng.NextFloatRange(3.5f, 9.0f)};
            p.maxLife = m_prng.NextFloatRange(2.0f, 4.0f);
            p.life = initialBurst ? m_prng.NextFloatRange(0.0f, p.maxLife) : p.maxLife;
            p.size = m_prng.NextFloatRange(1.8f, 3.8f);
            p.color = m_prng.NextColor(ColorRGBA{180, 220, 255, 255}, 0.25f);
            break;
        }

        case EmitterMode::FountainCone: {
            // Muestreo dentro de un cono 3D de apertura angular con velocidad Gaussiana (Box-Muller)
            const Point2D jitter = m_prng.NextPointInCircle(0.35f);
            p.pos = Point3D{m_emitterOrigin.x + jitter.x, m_emitterOrigin.y, m_emitterOrigin.z + jitter.y};

            const Point3D coneDir = m_prng.NextDirectionInCone(Point3D{0.0f, 1.0f, 0.0f}, 0.52f);
            const float speed = std::max(1.5f, m_prng.NextGaussian(5.8f, 1.0f));
            p.vel = Point3D{coneDir.x * speed, coneDir.y * speed, coneDir.z * speed};

            p.maxLife = m_prng.NextFloatRange(1.8f, 2.7f);
            p.life = initialBurst ? m_prng.NextFloatRange(0.0f, p.maxLife) : p.maxLife;
            p.size = m_prng.NextFloatRange(2.0f, 4.2f);

            // Gradiente térmico (Amarillo / Naranja / Oro)
            const float t = m_prng.NextFloat01();
            if (t > 0.6f) {
                p.color = ColorRGBA{255, static_cast<Uint8>(m_prng.NextIntRange(200, 255)), 40, 255};
            } else if (t > 0.25f) {
                p.color = ColorRGBA{255, static_cast<Uint8>(m_prng.NextIntRange(100, 180)), 20, 255};
            } else {
                p.color = ColorRGBA{255, static_cast<Uint8>(m_prng.NextIntRange(40, 90)), 20, 255};
            }
            break;
        }

        case EmitterMode::SphericalNova: {
            // Muestreo analítico de Arquímedes sobre superficie esférica S^2
            p.pos = m_emitterOrigin;
            const Point3D sphereDir = m_prng.NextPointOnSphere(1.0f);
            const float speed = std::max(1.0f, m_prng.NextGaussian(4.2f, 0.8f));
            p.vel = Point3D{sphereDir.x * speed, sphereDir.y * speed, sphereDir.z * speed};

            p.maxLife = m_prng.NextFloatRange(1.3f, 2.1f);
            p.life = initialBurst ? m_prng.NextFloatRange(0.0f, p.maxLife) : p.maxLife;
            p.size = m_prng.NextFloatRange(2.2f, 5.0f);

            // Colores energéticos púrpuras, cian y magenta
            const float t = m_prng.NextFloat01();
            if (t > 0.5f) {
                p.color = ColorRGBA{static_cast<Uint8>(m_prng.NextIntRange(160, 255)), 40, 255, 255};
            } else {
                p.color = ColorRGBA{40, static_cast<Uint8>(m_prng.NextIntRange(180, 255)), 255, 255};
            }
            break;
        }

        case EmitterMode::GalacticDisc: {
            // Muestreo en disco 2D con compensación r = R*sqrt(u) para densidad homogénea
            const Point2D discPt = m_prng.NextPointInCircle(4.2f);
            p.pos = Point3D{
                m_emitterOrigin.x + discPt.x,
                m_emitterOrigin.y + m_prng.NextFloatRange(-0.35f, 0.35f),
                m_emitterOrigin.z + discPt.y
            };

            // Velocidad angular tangencial perpendicular al radio
            const float distFromCenter = std::sqrt(discPt.x * discPt.x + discPt.y * discPt.y);
            const float orbitalSpeed = 1.6f + distFromCenter * 0.4f;
            p.vel = Point3D{-discPt.y * orbitalSpeed, m_prng.NextFloatSigned() * 0.2f, discPt.x * orbitalSpeed};

            p.maxLife = m_prng.NextFloatRange(3.0f, 5.0f);
            p.life = initialBurst ? m_prng.NextFloatRange(0.0f, p.maxLife) : p.maxLife;
            p.size = m_prng.NextFloatRange(1.8f, 3.6f);

            // Colores esmeralda, aguamarina y blanco estelar
            p.color = m_prng.NextColor(ColorRGBA{40, 240, 180, 255}, 0.2f);
            break;
        }
    }
}

void SceneParticles::Update(float dt) {
    m_simulationTime += dt;

    if (dt > 0.0001f) {
        const float instFps = 1.0f / dt;
        m_smoothedFps = m_smoothedFps * 0.90f + instFps * 0.10f;
    }

    auto& input = InputManager::Instance();
    const auto& touch = input.GetTouchState();

    // 1. Manejo de Botón Volver
    if (m_backButton.UpdateAndHandle(touch)) {
        return;
    }
    if (input.IsBackRequested()) {
        AudioManager::Instance().PlayClick();
        SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuSDL>());
        return;
    }

    // 2. Manejo de Botones del HUD
    if (m_btnReseed.Update(touch)) {
        CycleSeed();
        AudioManager::Instance().PlayClick();
    }
    if (m_btnMode.Update(touch)) {
        CycleMode();
        AudioManager::Instance().PlayClick();
    }
    if (m_btnPause.Update(touch)) {
        m_isPaused = !m_isPaused;
        AudioManager::Instance().PlayClick();
    }

#if defined(__WIIU__) || defined(__wiiu__)
    // Botón A: Reseed
    if (input.IsButtonTriggered(VPAD_BUTTON_A)) {
        CycleSeed();
        AudioManager::Instance().PlayClick();
    }
    // Botón X: Modo
    if (input.IsButtonTriggered(VPAD_BUTTON_X)) {
        CycleMode();
        AudioManager::Instance().PlayClick();
    }
    // Botón Y: Pausa
    if (input.IsButtonTriggered(VPAD_BUTTON_Y)) {
        m_isPaused = !m_isPaused;
        AudioManager::Instance().PlayClick();
    }
#endif

    // 3. Control Interactivo con Touch / Stylus
    if (touch.active) {
        if (touch.x >= 100 && touch.x <= 1180 && touch.y >= 80 && touch.y <= 550) {
            const float normX = (static_cast<float>(touch.x) - 640.0f) / 640.0f;
            const float normY = (360.0f - static_cast<float>(touch.y)) / 360.0f;

            if (m_mode == EmitterMode::FountainCone) {
                m_emitterOrigin.x = normX * 4.5f;
                m_emitterOrigin.y = normY * 3.0f;
            } else if (m_mode == EmitterMode::SphericalNova || m_mode == EmitterMode::GalacticDisc) {
                m_emitterOrigin.x = normX * 3.5f;
                m_emitterOrigin.y = normY * 2.5f;
            }

            if (touch.justPressed) {
                m_ring.Trigger();
            }
        }
    }

    // 4. Actualización de Partículas si no está pausado
    if (!m_isPaused) {
        if (m_mode == EmitterMode::HyperspaceBox) {
            const float speedMultiplier = touch.active ? 3.0f : 1.0f;
            m_starfield.Update(dt * speedMultiplier);
        }

        for (auto& p : m_particles) {
            if (m_mode == EmitterMode::FountainCone) {
                // Gravedad hacia abajo en modo fuente
                p.vel.y -= 3.8f * dt;
            }

            p.pos.x += p.vel.x * dt;
            p.pos.y += p.vel.y * dt;
            p.pos.z += p.vel.z * dt;

            p.life -= dt;
            if (p.life <= 0.0f) {
                InitParticle(p, false);
            }
        }
    }

    m_ring.Update(dt);
}

void SceneParticles::Render(SDL_Renderer* renderer) {
    constexpr float CENTER_X = 640.0f;
    constexpr float CENTER_Y = 360.0f;

    // 1. En modo Hiperespacio, dibujar también el fondo estelar masivo
    if (m_mode == EmitterMode::HyperspaceBox) {
        m_starfield.Render(renderer, 1280, 720);
    }

    // 2. Renderizado en Lote de Partículas Dinámicas (Single Draw Call en GPU GX2)
    int visibleCount = 0;
    for (const auto& p : m_particles) {
        if (p.pos.z <= 0.2f) continue;

        const float invZ = 1.0f / p.pos.z;
        const float screenX = CENTER_X + (p.pos.x * invZ) * 450.0f;
        const float screenY = CENTER_Y - (p.pos.y * invZ) * 450.0f;

        if (screenX >= -20.0f && screenX <= 1300.0f && screenY >= -20.0f && screenY <= 740.0f) {
            const float lifeRatio = std::clamp(p.life / p.maxLife, 0.0f, 1.0f);
            const Uint8 alpha = static_cast<Uint8>(p.color.a * lifeRatio);

            const float sz = std::clamp(p.size * (1.8f * invZ), 1.0f, 12.0f);
            const SDL_Color ptCol = { p.color.r, p.color.g, p.color.b, alpha };

            const int vertOffset = visibleCount * 4;
            m_vertexBatch[vertOffset + 0] = { { screenX - sz, screenY - sz }, ptCol, { 0.0f, 0.0f } };
            m_vertexBatch[vertOffset + 1] = { { screenX + sz, screenY - sz }, ptCol, { 0.0f, 0.0f } };
            m_vertexBatch[vertOffset + 2] = { { screenX + sz, screenY + sz }, ptCol, { 0.0f, 0.0f } };
            m_vertexBatch[vertOffset + 3] = { { screenX - sz, screenY + sz }, ptCol, { 0.0f, 0.0f } };

            const int idxOffset = visibleCount * 6;
            m_indexBatch[idxOffset + 0] = vertOffset + 0;
            m_indexBatch[idxOffset + 1] = vertOffset + 1;
            m_indexBatch[idxOffset + 2] = vertOffset + 2;
            m_indexBatch[idxOffset + 3] = vertOffset + 0;
            m_indexBatch[idxOffset + 4] = vertOffset + 2;
            m_indexBatch[idxOffset + 5] = vertOffset + 3;

            ++visibleCount;
        }
    }

    if (visibleCount > 0) {
        SDL_RenderGeometry(renderer, nullptr,
                           m_vertexBatch.data(), visibleCount * 4,
                           m_indexBatch.data(), visibleCount * 6);
    }

    // 3. Efecto de Anillo Ondulatorio Táctil
    m_ring.Render(renderer, 640, 360);

    // 4. Barra Superior e Interactivos
    constexpr SDL_Rect topBar = { 20, 10, 1240, 56 };
    Render2D::drawBox(renderer, topBar.x, topBar.y, topBar.w, topBar.h,
                      ColorRGBA{10, 20, 35, 230}, ColorRGBA::Cyan());

    m_backButton.Render(renderer);
    m_btnReseed.Render(renderer);
    m_btnMode.Render(renderer);
    m_btnPause.Render(renderer);

    Render2D::drawPixelString(renderer, "CAP. 4.6 JASON GREGORY: PRNG DETERMINISTA Y DISTRIBUCIONES 3D",
                              145, 28, 2, ColorRGBA::Yellow());

    // 5. Panel Izquierdo: Telemetría Exhaustiva de PRNG
    constexpr SDL_Rect telemetryBox = { 30, 80, 520, 195 };
    Render2D::drawBox(renderer, telemetryBox.x, telemetryBox.y, telemetryBox.w, telemetryBox.h,
                      ColorRGBA{10, 22, 40, 235}, ColorRGBA::Cyan());

    Render2D::drawPixelString(renderer, "TELEMETRIA SUBSISTEMA PRNG (PCG32 64/32)",
                              45, 95, 2, ColorRGBA::Yellow());

    StackString<64> seedStr;
    seedStr.Format("SEMILLA: 0x%012llX (PRESET %zu/%zu)", m_activeSeed, m_seedIndex + 1, NUM_SEEDS);
    Render2D::drawPixelString(renderer, seedStr.string_view(), 45, 122, 2, ColorRGBA::White());

    const char* modeName = "1. CAJA AABB (HIPERESPACIO 3D)";
    if (m_mode == EmitterMode::FountainCone) modeName = "2. CONO 3D (FUENTE + GAUSS)";
    else if (m_mode == EmitterMode::SphericalNova) modeName = "3. ESFERA S^2 (SUPERNOVA)";
    else if (m_mode == EmitterMode::GalacticDisc) modeName = "4. DISCO 2D (VORTICE R*SQRT(U))";

    StackString<64> modeStr;
    modeStr.Format("MODO: %s", modeName);
    Render2D::drawPixelString(renderer, modeStr.string_view(), 45, 148, 2, ColorRGBA::Cyan());

    StackString<64> callsStr;
    callsStr.Format("MUESTRAS PRNG GENERADAS: %llu", m_prng.GetCallCount());
    Render2D::drawPixelString(renderer, callsStr.string_view(), 45, 174, 2, ColorRGBA{160, 255, 160, 255});

    StackString<64> countStr;
    countStr.Format("PARTICULAS: %d ACTIVAS | 0 HEAP ALLOCS", visibleCount);
    Render2D::drawPixelString(renderer, countStr.string_view(), 45, 200, 2, ColorRGBA::Green());

    StackString<64> statusStr;
    statusStr.Format("ESTADO: %s | %d FPS (LATTE GX2)",
                     m_isPaused ? "PAUSA ACTIVA" : "EJECUTANDO 60 FPS",
                     static_cast<int>(m_smoothedFps + 0.5f));
    Render2D::drawPixelString(renderer, statusStr.string_view(), 45, 226, 2,
                              m_isPaused ? ColorRGBA::Orange() : ColorRGBA::White());

    // 6. Barra Inferior con Guía de Controles
    constexpr SDL_Rect bottomBar = { 30, 645, 1220, 58 };
    Render2D::drawBox(renderer, bottomBar.x, bottomBar.y, bottomBar.w, bottomBar.h,
                      ColorRGBA{10, 20, 35, 230}, ColorRGBA::Cyan());

    Render2D::drawPixelString(renderer, "[A] RESEED DETERMINISTA | [X] CAMBIAR MODO DISTRIBUCION | [Y] PAUSA",
                              50, 658, 2, ColorRGBA{255, 220, 140, 255});
    Render2D::drawPixelString(renderer, "[TOUCH / LAPIZ] DESPLAZAR ORIGEN EMISOR | [VOLVER / B] MENU PRINCIPAL",
                              50, 680, 2, ColorRGBA::White());
}

} // namespace WiiU
