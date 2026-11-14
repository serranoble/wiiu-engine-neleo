#pragma once

#include "Scene/IScene.hpp"
#include "Graphics/Color.hpp"
#include "Graphics/Math3D.hpp"
#include <SDL2/SDL.h>
#include <gx2/draw.h>
#include <gx2/clear.h>
#include <gx2/texture.h>
#include <vector>
#include <cstdint>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace WiiU {

enum class RayQuality {
    // Modos Básicos de Baja Resolución (Desactivados para enfocarse en la gama alta HD)
    // LOW_160x90,
    // MED_240x135,
    // HIGH_320x180,
    // ULTRA_480x270,
    // NATIVE_360P_640x360,

    // Modos Avanzados de Alta Definición (HD)
    GAMEPAD_480P_854x480,
    QHD_540P_960x540,
    FULL_720P_1280x720,  // Buffer interno 648p (1152x648) escalado a 720p HD
    FULL_1080P_1920x1080 // Buffer interno 648p (1152x648) escalado a 1080p Full HD
};

enum class SmoothingMode {
    OFF_NEAREST,       // Sin filtrado (Punto a punto / Pixel art)
    BILINEAR_GX2,      // Filtrado Bilineal Hardware GX2 Sampler
    SUBPIXEL_AA,       // Anti-Aliasing Subpíxel en Bordes de Silueta
    FXAA_FULL,         // FXAA Completo por Shader (Fast Approximate Anti-Aliasing)
    BICUBIC_SHARPEN    // Interpolación Bicúbica con Enfocado Adaptativo
};

enum class OutputTargetHD {
    TARGET_720P_HD,     // Salida 1280x720 (720p HD)
    TARGET_1080P_FULLHD // Salida 1920x1080 (1080p Full HD)
};

enum class ScreenTargetMode {
    DUAL_TV_AND_GAMEPAD, // Renderizado Doble Pantalla Simultáneo
    GAMEPAD_NATIVE_854x480, // Target Nativo del GamePad (WVGA)
    TV_NATIVE_1280x720      // Target Nativo de la TV (720p HD)
};

struct SphereBoundingBox {
    int minX{0};
    int maxX{0};
    int minY{0};
    int maxY{0};
};

struct RaySphere {
    Point3D center;
    Point3D relCenter;
    float radius;
    float radiusSq;
    float invRadius;
    float cConstPrimary;
    ColorRGBA color;
    float reflectivity;
    SphereBoundingBox bbox;
};

struct RayBox {
    Point3D center;
    Point3D halfExtents;
    Point3D minB;
    Point3D maxB;
    ColorRGBA color;
    float reflectivity;
    SphereBoundingBox bbox;
};

class SceneRaytracing : public IScene {
public:
    SceneRaytracing();
    ~SceneRaytracing() override;

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(SDL_Renderer* renderer) override;

    [[nodiscard]] std::vector<std::string> GetRequiredAssets() const override {
        return {"click.wav"};
    }
    [[nodiscard]] std::string GetSceneName() const override {
        return "Demo 4: Ray Tracing CPU";
    }

    void SetQuality(RayQuality quality, SDL_Renderer* renderer);
    void SetSmoothingMode(SmoothingMode mode);

private:
    void InitRayGrid();
    void UpdateBoundingBoxes();
    void TraceScene(uint32_t* pixelBuffer, int width, int height, int pitchPixels);

    void InitThreadPool();
    void ShutdownThreadPool();
    void WorkerLoop(int threadId);
    void TraceRows(int threadId, int numThreads, uint32_t* pixelBuffer, int width, int height, int pitchPixels);

    RayQuality m_quality{RayQuality::FULL_1080P_1920x1080};
    ScreenTargetMode m_displayMode{ScreenTargetMode::TV_NATIVE_1280x720};
    OutputTargetHD m_outputTarget{OutputTargetHD::TARGET_1080P_FULLHD};
    SmoothingMode m_smoothingMode{SmoothingMode::OFF_NEAREST};
    int m_rayWidth{1152};
    int m_rayHeight{648};

    bool m_gpuAccel{true};
    int m_frameParity{0};

    // Camera Orbit Controls (Right Joystick / IJKL / Arrow Keys / Touch)
    Point3D m_camPivot{0.0f, 0.0f, 2.8f};
    float m_camYaw{0.0f};
    float m_camPitch{0.0f};
    float m_camDistance{4.2f};
    Point3D m_camPos{0.0f, 0.0f, -1.4f};
    Point3D m_camForward{0.0f, 0.0f, 1.0f};
    Point3D m_camRight{1.0f, 0.0f, 0.0f};
    Point3D m_camUp{0.0f, 1.0f, 0.0f};
    std::vector<Point2D> m_screenPlane;
    std::vector<Point3D> m_screenPlaneDirs;

    // Threading state (Lock-free Atomic Yield Thread Pool for CPU1 & CPU2)
    static constexpr int NUM_WORKERS = 2;
    bool m_multithreading{true};
    std::vector<std::thread> m_workerThreads;
    std::atomic<int> m_targetFrame{0};
    std::atomic<int> m_completedWorkers{0};
    std::atomic<bool> m_stopThreads{false};

    uint32_t* m_currentPixelBuffer{nullptr};
    int m_currentWidth{0};
    int m_currentHeight{0};
    int m_currentPitchPixels{0};

    SDL_Texture* m_rayTexture{nullptr};
    std::vector<uint32_t> m_pixelBuffer;
    std::vector<Point3D> m_rayGrid;
    std::vector<Point3D> m_invRayGrid; // Pre-calculated inverse directions for SoA Slab method

    std::vector<RaySphere> m_spheres;
    RayBox m_box;

    Point3D m_lightPos{-2.0f, 3.0f, -2.0f};
    float m_animTime{0.0f};

    Uint32 m_lastFrameTicks{0};
    float m_smoothedFps{0.0f};

    struct TouchClickEffect {
        float x{0.0f};
        float y{0.0f};
        float radius{0.0f};
        float alpha{0.0f};
        bool active{false};
    } m_clickEffect;

    SDL_Rect m_backBtnRect;
    SDL_Rect m_qualBtnRect;
    SDL_Rect m_targetBtnRect;
    SDL_Rect m_dispBtnRect;
    SDL_Rect m_gpuBtnRect;
    SDL_Rect m_cpuBtnRect;
    SDL_Rect m_smoothBtnRect;
    bool m_backHover{false};
    bool m_qualHover{false};
    bool m_targetHover{false};
    bool m_dispHover{false};
    bool m_gpuHover{false};
    bool m_cpuHover{false};
    bool m_smoothHover{false};
};

} // namespace WiiU
