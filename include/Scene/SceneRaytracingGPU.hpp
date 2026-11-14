#pragma once

#include "Scene/SceneRaytracing.hpp"
#include "Scene/IScene.hpp"
#include "Graphics/Color.hpp"
#include "Graphics/Math3D.hpp"
#include <SDL2/SDL.h>
#include <gx2/draw.h>
#include <gx2/clear.h>
#include <gx2/texture.h>
#ifdef __wiiu__
#include <whb/gfx.h>
#include <gx2/state.h>
#include <gx2/registers.h>
#include <gx2/context.h>
#include <gx2/enum.h>
#include <gx2r/buffer.h>
#include <gx2r/draw.h>
#endif
#include <vector>
#include <cstdint>
#include <string>
#include <thread>
#include <atomic>

namespace WiiU {

enum class RayQualityGPU {
    // Modos Básicos de Baja Resolución (Desactivados para enfocarse en la gama alta HD)
    // LOW_160x90,
    // MED_240x135,
    // HIGH_320x180,
    // ULTRA_480x270,
    // NATIVE_360P_640x360,

    // Modos Avanzados de Alta Definición (HD)
    GAMEPAD_480P_854x480,
    QHD_540P_960x540,
    HD_576P_1024x576,
    HD_648P_1152x648,
    FULL_720P_1280x720,  // Buffer interno 648p (1152x648) escalado a 720p HD
    FULL_1080P_1920x1080 // Buffer interno 648p (1152x648) escalado a 1080p Full HD
};

struct GPUBoundingBox {
    int minX{0};
    int maxX{0};
    int minY{0};
    int maxY{0};
};

// Estructuras de Uniforms de GPU (Alineadas a 16 bytes para GX2 / AMD Latte)
struct alignas(16) GPUSphereUniform {
    Point3D center;
    float radius{0.0f};
    float radiusSq{0.0f};
    float reflectivity{0.0f};
    float padding[2]{0.0f, 0.0f};
    ColorRGBA color;
    GPUBoundingBox bbox;
};

struct alignas(16) GPUBoxUniform {
    Point3D center;
    float reflectivity{0.0f};
    Point3D halfExtents;
    float padding{0.0f};
    ColorRGBA color;
    GPUBoundingBox bbox;
};

struct alignas(16) GPUCameraUniform {
    Point3D pos;
    float aspect{1.7777778f}; // 16:9 (1280x720)
    Point3D forward;
    float fov{60.0f};
    Point3D right;
    float padding1{0.0f};
    Point3D up;
    float padding2{0.0f};
};

struct alignas(16) GPUSceneUniforms {
    GPUCameraUniform camera;
    GPUSphereUniform spheres[2];
    GPUBoxUniform box;
    Point3D lightPos;
    float animTime{0.0f};
    ColorRGBA lightColor{255, 255, 255, 255};
};

class SceneRaytracingGPU : public IScene {
public:
    SceneRaytracingGPU();
    ~SceneRaytracingGPU() override;

    SceneRaytracingGPU(const SceneRaytracingGPU&) = delete;
    SceneRaytracingGPU& operator=(const SceneRaytracingGPU&) = delete;
    SceneRaytracingGPU(SceneRaytracingGPU&&) = delete;
    SceneRaytracingGPU& operator=(SceneRaytracingGPU&&) = delete;

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(SDL_Renderer* renderer) override;

    [[nodiscard]] std::vector<std::string> GetRequiredAssets() const override {
        return {"click.wav", "shaders/raytracing.gsh", "shaders/native_gx2.gsh"};
    }
    [[nodiscard]] std::string GetSceneName() const override {
        return "Demo 6: Ray Tracing GPU Shaders";
    }

    [[nodiscard]] bool IsPureGX2() const noexcept override {
#if defined(__WIIU__) || defined(__wiiu__)
        return true;
#else
        return false;
#endif
    }

    void SetQuality(RayQualityGPU quality, SDL_Renderer* renderer);
    void SetSmoothingMode(SmoothingMode mode);

private:
    void InitRayGrid();
    void UpdateGPUUniforms();
    void UpdateBoundingBoxes();

    void InitThreadPool();
    void ShutdownThreadPool();
    void WorkerLoop(int threadId);
    void TraceSceneGPU(uint32_t* pixelBuffer, int width, int height, int pitchPixels);
    void TraceRowsGPU(int threadId, int numThreads, uint32_t* pixelBuffer, int width, int height, int pitchPixels);
    bool InitGX2Pipeline();
    void BindColorBuffer(SDL_Renderer* renderer);
    void RenderGX2();
    void RenderHUD_GX2();
    void DestroyGX2Pipeline();

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

    // Uniform Buffer Block transferible a la GPU AMD Latte (GX2)
    GPUSceneUniforms m_sceneUniforms;

    // Recursos de Sombreador CafeGLSL (GFD / GSH)
    bool m_gfdShaderLoaded{false};
    bool m_renderedWithGPU{false};
#if defined(__WIIU__) || defined(__wiiu__)
    WHBGfxShaderGroup m_gfdShaderGroup{};
    bool m_useDirectGFD{false};
    GX2FetchShader m_directFetchShader{};
    GX2VertexShader* m_directVs{nullptr};
    void* m_directVsProgram{nullptr};
    GX2PixelShader* m_directPs{nullptr};
    void* m_directPsProgram{nullptr};
    void* m_directFsProgram{nullptr};

    GX2RBuffer m_quadBuffer{};
    bool m_quadBufferCreated{false};
    GX2ContextState* m_nativeContextState{nullptr};
    GX2ColorBuffer* m_pColorBuffer{nullptr};
    bool m_colorBufferBound{false};
#endif

    // Recursos de Textura de Renderizado
    RayQualityGPU m_quality{RayQualityGPU::FULL_1080P_1920x1080};
    OutputTargetHD m_outputTarget{OutputTargetHD::TARGET_1080P_FULLHD};
    SmoothingMode m_smoothingMode{SmoothingMode::OFF_NEAREST};

    SDL_Texture* m_gpuTexture{nullptr};
    SDL_Surface* m_gpuSurface{nullptr};
    std::vector<uint32_t> m_pixelBuffer;
    std::vector<Point3D> m_screenPlaneDirs;
    int m_gpuWidth{1152};
    int m_gpuHeight{648};
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

    Point3D m_lightPos{-2.0f, 3.0f, -2.0f};
    float m_animTime{0.0f};

    Uint32 m_lastFrameTicks{0};
    float m_smoothedFps{60.0f};

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
    SDL_Rect m_gpuBtnRect;
    SDL_Rect m_cpuBtnRect;
    SDL_Rect m_smoothBtnRect;
    bool m_backHover{false};
    bool m_qualHover{false};
    bool m_targetHover{false};
    bool m_gpuHover{false};
    bool m_cpuHover{false};
    bool m_smoothHover{false};
};

} // namespace WiiU
