#include "Scene/SceneRaytracing.hpp"
#include "Scene/SceneMenu.hpp"
#include "Scene/SceneMenuSDL.hpp"
#include "Scene/SceneManager.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Graphics/Render2D.hpp"
#include "Core/StackString.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>
#ifdef __wiiu__
#include <coreinit/thread.h>
#endif

namespace WiiU {

SceneRaytracing::SceneRaytracing() {
    // Fila 1 (Y = 15): Volver, Buffer Interno, Target Salida HD y Modo Pantalla
    m_backBtnRect   = { 15, 15, 130, 44 };
    m_qualBtnRect   = { 160, 15, 350, 44 };
    m_targetBtnRect = { 525, 15, 350, 44 };
    m_dispBtnRect   = { 890, 15, 375, 44 };

    // Fila 2 (Y = 66): Aceleración GPU, Multihilo CPU y Suavizado AA
    m_gpuBtnRect    = { 15, 66, 380, 44 };
    m_cpuBtnRect    = { 410, 66, 380, 44 };
    m_smoothBtnRect = { 805, 66, 460, 44 };

    // 2 Esferas + 1 Cubo 3D
    m_spheres = {
        { {-0.9f, 0.0f, 3.0f}, {}, 0.75f, 0.75f * 0.75f, 1.0f / 0.75f, 0.0f, ColorRGBA{0, 220, 255, 255}, 0.65f, {} },  // Cyan Espejo
        { {0.9f, 0.2f, 3.2f},  {}, 0.8f,  0.8f * 0.8f,   1.0f / 0.8f,  0.0f, ColorRGBA{255, 215, 0, 255}, 0.55f, {} }   // Dorado Metálico
    };

    m_box = {
        {0.0f, -0.3f, 2.3f},
        {0.42f, 0.42f, 0.42f},
        {-0.42f, -0.72f, 1.88f},
        {0.42f, 0.12f, 2.72f},
        ColorRGBA{235, 50, 150, 255}, // Magenta
        0.40f,
        {}
    };

    m_camPivot = Point3D{0.0f, 0.0f, 2.8f};
    m_camDistance = 4.2f;
    m_camYaw = 0.0f;
    m_camPitch = 0.0f;

    m_quality = RayQuality::FULL_1080P_1920x1080;
    m_outputTarget = OutputTargetHD::TARGET_1080P_FULLHD;
    m_displayMode = ScreenTargetMode::TV_NATIVE_1280x720;
    m_gpuAccel = true;
    m_multithreading = true;
    m_smoothingMode = SmoothingMode::BILINEAR_GX2;

    SetQuality(RayQuality::FULL_1080P_1920x1080, nullptr);
    InitThreadPool();
}

SceneRaytracing::~SceneRaytracing() {
    OnExit();
}

void SceneRaytracing::SetQuality(RayQuality quality, SDL_Renderer* renderer) {
    m_quality = quality;
    switch (quality) {
        case RayQuality::GAMEPAD_480P_854x480:
            m_rayWidth = 854;
            m_rayHeight = 480;
            break;
        case RayQuality::QHD_540P_960x540:
            m_rayWidth = 960;
            m_rayHeight = 540;
            break;
        case RayQuality::FULL_720P_1280x720:
            m_rayWidth = 1152; // Buffer interno 648p (1152x648) escalado a 720p HD
            m_rayHeight = 648;
            m_outputTarget = OutputTargetHD::TARGET_720P_HD;
            break;
        case RayQuality::FULL_1080P_1920x1080:
            m_rayWidth = 1152; // Buffer interno 648p (1152x648) escalado a 1080p Full HD
            m_rayHeight = 648;
            m_outputTarget = OutputTargetHD::TARGET_1080P_FULLHD;
            break;
    }

    m_pixelBuffer.assign(m_rayWidth * m_rayHeight, 0x0A0F1EFF);
    m_screenPlane.resize(m_rayWidth * m_rayHeight);
    m_screenPlaneDirs.resize(m_rayWidth * m_rayHeight);

    float aspect = static_cast<float>(m_rayWidth) / static_cast<float>(m_rayHeight);
    for (int y = 0; y < m_rayHeight; ++y) {
        float vy = (1.0f - 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(m_rayHeight));
        for (int x = 0; x < m_rayWidth; ++x) {
            float vx = (2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(m_rayWidth) - 1.0f) * aspect;
            m_screenPlane[y * m_rayWidth + x] = Point2D{vx, vy};
            m_screenPlaneDirs[y * m_rayWidth + x] = Point3D{vx, vy, 1.0f}.normalized();
        }
    }

    InitRayGrid();

    if (m_rayTexture) {
        SDL_DestroyTexture(m_rayTexture);
        m_rayTexture = nullptr;
    }

    if (renderer) {
        m_rayTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, 
                                         SDL_TEXTUREACCESS_STREAMING, m_rayWidth, m_rayHeight);
        if (m_rayTexture) {
            SDL_ScaleMode scaleMode = (m_smoothingMode == SmoothingMode::OFF_NEAREST) ? SDL_ScaleModeNearest : SDL_ScaleModeLinear;
            SDL_SetTextureScaleMode(m_rayTexture, scaleMode);
        }
    }
}

void SceneRaytracing::SetSmoothingMode(SmoothingMode mode) {
    m_smoothingMode = mode;
    if (m_rayTexture) {
        SDL_ScaleMode scaleMode = (m_smoothingMode == SmoothingMode::OFF_NEAREST) ? SDL_ScaleModeNearest : SDL_ScaleModeLinear;
        SDL_SetTextureScaleMode(m_rayTexture, scaleMode);
    }
}

void SceneRaytracing::InitRayGrid() {
    float cosP = std::cos(m_camPitch);
    float sinP = std::sin(m_camPitch);
    float cosY = std::cos(m_camYaw);
    float sinY = std::sin(m_camYaw);

    Point3D offset{
        m_camDistance * sinY * cosP,
        m_camDistance * sinP,
        -m_camDistance * cosY * cosP
    };

    m_camPos = m_camPivot + offset;

    m_camForward = (m_camPivot - m_camPos).normalized();
    Point3D worldUp{0.0f, 1.0f, 0.0f};
    m_camRight = Point3D::cross(m_camForward, worldUp).normalized();
    m_camUp = Point3D::cross(m_camRight, m_camForward).normalized();
}

void SceneRaytracing::OnEnter() {
    m_animTime = 0.0f;
    m_lastFrameTicks = SDL_GetTicks();
    m_smoothedFps = 60.0f;

    // Si los búferes fueron desalojados al salir previamente, se reasignan limpiamente
    if (m_pixelBuffer.empty()) {
        SetQuality(m_quality, nullptr);
        InitThreadPool();
    }

    AudioManager::Instance().StopBGM();
}

void SceneRaytracing::OnExit() {
    ShutdownThreadPool();

    if (m_rayTexture) {
        SDL_DestroyTexture(m_rayTexture);
        m_rayTexture = nullptr;
    }

    // Desalojo proactivo de memoria en heap (OPT-45): libera ~18 MB de búferes
    m_pixelBuffer.clear();
    m_pixelBuffer.shrink_to_fit();
    m_screenPlane.clear();
    m_screenPlane.shrink_to_fit();
    m_screenPlaneDirs.clear();
    m_screenPlaneDirs.shrink_to_fit();
}

static inline int clampInt(int val, int minV, int maxV) {
    return (val < minV) ? minV : ((val > maxV) ? maxV : val);
}

void SceneRaytracing::UpdateBoundingBoxes() {
    float aspect = static_cast<float>(m_rayWidth) / static_cast<float>(m_rayHeight);

    for (auto& s : m_spheres) {
        float cx = Point3D::dot(s.relCenter, m_camRight);
        float cy = Point3D::dot(s.relCenter, m_camUp);
        float cz = Point3D::dot(s.relCenter, m_camForward);

        if (cz <= 0.6f) {
            s.bbox = {0, m_rayWidth - 1, 0, m_rayHeight - 1};
        } else {
            float r = s.radius * 1.25f;
            float invZ = 1.0f / cz;
            float vxMin = (cx - r) * invZ;
            float vxMax = (cx + r) * invZ;
            float vyMin = (cy - r) * invZ;
            float vyMax = (cy + r) * invZ;

            int xMin = clampInt(static_cast<int>(((vxMin / aspect + 1.0f) * 0.5f) * m_rayWidth), 0, m_rayWidth - 1);
            int xMax = clampInt(static_cast<int>(((vxMax / aspect + 1.0f) * 0.5f) * m_rayWidth) + 1, 0, m_rayWidth - 1);
            int yMin = clampInt(static_cast<int>(((1.0f - vyMax) * 0.5f) * m_rayHeight), 0, m_rayHeight - 1);
            int yMax = clampInt(static_cast<int>(((1.0f - vyMin) * 0.5f) * m_rayHeight) + 1, 0, m_rayHeight - 1);

            s.bbox = {xMin, xMax, yMin, yMax};
        }
    }

    // Box Bounding Box
    Point3D boxRel = m_box.center - m_camPos;
    float cx = Point3D::dot(boxRel, m_camRight);
    float cy = Point3D::dot(boxRel, m_camUp);
    float cz = Point3D::dot(boxRel, m_camForward);

    if (cz <= 0.85f) {
        m_box.bbox = {0, m_rayWidth - 1, 0, m_rayHeight - 1};
    } else {
        float r = 0.85f;
        float invZ = 1.0f / cz;
        float vxMin = (cx - r) * invZ;
        float vxMax = (cx + r) * invZ;
        float vyMin = (cy - r) * invZ;
        float vyMax = (cy + r) * invZ;

        int xMin = clampInt(static_cast<int>(((vxMin / aspect + 1.0f) * 0.5f) * m_rayWidth), 0, m_rayWidth - 1);
        int xMax = clampInt(static_cast<int>(((vxMax / aspect + 1.0f) * 0.5f) * m_rayWidth) + 1, 0, m_rayWidth - 1);
        int yMin = clampInt(static_cast<int>(((1.0f - vyMax) * 0.5f) * m_rayHeight), 0, m_rayHeight - 1);
        int yMax = clampInt(static_cast<int>(((1.0f - vyMin) * 0.5f) * m_rayHeight) + 1, 0, m_rayHeight - 1);

        m_box.bbox = {xMin, xMax, yMin, yMax};
    }
}

void SceneRaytracing::Update(float dt) {
    m_animTime += 1.5f * dt;

    m_spheres[0].center.y = std::sin(m_animTime * 1.5f) * 0.4f;
    m_spheres[1].center.y = std::cos(m_animTime * 1.5f) * 0.4f;
    m_box.center.x        = std::sin(m_animTime * 2.0f) * 0.6f;

    m_lightPos.x = std::sin(m_animTime) * 3.0f;
    m_lightPos.z = 1.0f + std::cos(m_animTime) * 2.0f;

    // Control de Cámara Orbital con Joystick Derecho / Flechas / IJKL
    auto& input = InputManager::Instance();
    const auto& rightStick = input.GetRightStick();

    if (std::abs(rightStick.x) > 0.05f || std::abs(rightStick.y) > 0.05f) {
        m_camYaw   += rightStick.x * 2.2f * dt;
        m_camPitch += rightStick.y * 2.2f * dt;

        constexpr float MAX_PITCH = 1.35f;
        m_camPitch = std::clamp(m_camPitch, -MAX_PITCH, MAX_PITCH);
    }

    InitRayGrid();

    for (auto& s : m_spheres) {
        s.relCenter = s.center - m_camPos;
        s.cConstPrimary = Point3D::dot(s.relCenter, s.relCenter) - s.radiusSq;
    }

    m_box.minB = (m_box.center - m_camPos) - m_box.halfExtents;
    m_box.maxB = (m_box.center - m_camPos) + m_box.halfExtents;

    UpdateBoundingBoxes();



    if (m_clickEffect.active) {
        m_clickEffect.radius += 180.0f * dt;
        m_clickEffect.alpha -= 2.2f * dt;
        if (m_clickEffect.alpha <= 0.0f) {
            m_clickEffect.active = false;
        }
    }

    const auto& touch = input.GetTouchState();
    m_backHover = false;
    m_qualHover = false;
    m_targetHover = false;
    m_dispHover = false;
    m_gpuHover  = false;
    m_cpuHover  = false;

    if (touch.active) {
        if (touch.x >= m_backBtnRect.x && touch.x <= (m_backBtnRect.x + m_backBtnRect.w) &&
            touch.y >= m_backBtnRect.y && touch.y <= (m_backBtnRect.y + m_backBtnRect.h)) {
            m_backHover = true;
            if (touch.justPressed) {
                input.TriggerAction("VOLVER MENU");
                SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuSDL>());
                return;
            }
        }

        if (touch.x >= m_qualBtnRect.x && touch.x <= (m_qualBtnRect.x + m_qualBtnRect.w) &&
            touch.y >= m_qualBtnRect.y && touch.y <= (m_qualBtnRect.y + m_qualBtnRect.h)) {
            m_qualHover = true;
            if (touch.justPressed) {
                if (m_quality == RayQuality::GAMEPAD_480P_854x480) {
                    SetQuality(RayQuality::QHD_540P_960x540, nullptr);
                } else if (m_quality == RayQuality::QHD_540P_960x540) {
                    SetQuality(RayQuality::FULL_720P_1280x720, nullptr);
                } else if (m_quality == RayQuality::FULL_720P_1280x720) {
                    SetQuality(RayQuality::FULL_1080P_1920x1080, nullptr);
                } else {
                    SetQuality(RayQuality::GAMEPAD_480P_854x480, nullptr);
                }
            }
        }

        if (touch.x >= m_targetBtnRect.x && touch.x <= (m_targetBtnRect.x + m_targetBtnRect.w) &&
            touch.y >= m_targetBtnRect.y && touch.y <= (m_targetBtnRect.y + m_targetBtnRect.h)) {
            m_targetHover = true;
            if (touch.justPressed) {
                if (m_outputTarget == OutputTargetHD::TARGET_720P_HD) {
                    m_outputTarget = OutputTargetHD::TARGET_1080P_FULLHD;
                } else {
                    m_outputTarget = OutputTargetHD::TARGET_720P_HD;
                }
            }
        }

        if (touch.x >= m_dispBtnRect.x && touch.x <= (m_dispBtnRect.x + m_dispBtnRect.w) &&
            touch.y >= m_dispBtnRect.y && touch.y <= (m_dispBtnRect.y + m_dispBtnRect.h)) {
            m_dispHover = true;
            if (touch.justPressed) {
                if (m_displayMode == ScreenTargetMode::DUAL_TV_AND_GAMEPAD) {
                    m_displayMode = ScreenTargetMode::GAMEPAD_NATIVE_854x480;
                } else if (m_displayMode == ScreenTargetMode::GAMEPAD_NATIVE_854x480) {
                    m_displayMode = ScreenTargetMode::TV_NATIVE_1280x720;
                } else {
                    m_displayMode = ScreenTargetMode::DUAL_TV_AND_GAMEPAD;
                }
            }
        }

        if (touch.x >= m_gpuBtnRect.x && touch.x <= (m_gpuBtnRect.x + m_gpuBtnRect.w) &&
            touch.y >= m_gpuBtnRect.y && touch.y <= (m_gpuBtnRect.y + m_gpuBtnRect.h)) {
            m_gpuHover = true;
            if (touch.justPressed) {
                m_gpuAccel = !m_gpuAccel;
            }
        }

        if (touch.x >= m_cpuBtnRect.x && touch.x <= (m_cpuBtnRect.x + m_cpuBtnRect.w) &&
            touch.y >= m_cpuBtnRect.y && touch.y <= (m_cpuBtnRect.y + m_cpuBtnRect.h)) {
            m_cpuHover = true;
            if (touch.justPressed) {
                m_multithreading = !m_multithreading;
            }
        }

        if (touch.x >= m_smoothBtnRect.x && touch.x <= (m_smoothBtnRect.x + m_smoothBtnRect.w) &&
            touch.y >= m_smoothBtnRect.y && touch.y <= (m_smoothBtnRect.y + m_smoothBtnRect.h)) {
            m_smoothHover = true;
            if (touch.justPressed) {
                if (m_smoothingMode == SmoothingMode::OFF_NEAREST) {
                    SetSmoothingMode(SmoothingMode::BILINEAR_GX2);
                } else if (m_smoothingMode == SmoothingMode::BILINEAR_GX2) {
                    SetSmoothingMode(SmoothingMode::SUBPIXEL_AA);
                } else if (m_smoothingMode == SmoothingMode::SUBPIXEL_AA) {
                    SetSmoothingMode(SmoothingMode::FXAA_FULL);
                } else if (m_smoothingMode == SmoothingMode::FXAA_FULL) {
                    SetSmoothingMode(SmoothingMode::BICUBIC_SHARPEN);
                } else {
                    SetSmoothingMode(SmoothingMode::OFF_NEAREST);
                }
            }
        }

        if (!m_backHover && !m_qualHover && !m_targetHover && !m_dispHover && !m_gpuHover && !m_cpuHover && !m_smoothHover) {
            if (touch.justPressed) {
                m_clickEffect.x = touch.x;
                m_clickEffect.y = touch.y;
                m_clickEffect.radius = 10.0f;
                m_clickEffect.alpha = 1.0f;
                m_clickEffect.active = true;
            }
        }
    }
}

void SceneRaytracing::TraceScene(uint32_t* pixelBuffer, int width, int height, int pitchPixels) {
    if (m_multithreading) {
        m_currentPixelBuffer = pixelBuffer;
        m_currentWidth = width;
        m_currentHeight = height;
        m_currentPitchPixels = pitchPixels;
        m_completedWorkers.store(0, std::memory_order_relaxed);

        m_targetFrame.fetch_add(1, std::memory_order_release);

        TraceRows(0, NUM_WORKERS + 1, pixelBuffer, width, height, pitchPixels);

        while (m_completedWorkers.load(std::memory_order_acquire) < NUM_WORKERS) {
#ifdef __wiiu__
            OSYieldThread();
#else
            std::this_thread::yield();
#endif
        }
    } else {
        TraceRows(0, 1, pixelBuffer, width, height, pitchPixels);
    }

    if (m_gpuAccel) {
        m_frameParity = 1 - m_frameParity;
    }
}

void SceneRaytracing::InitThreadPool() {
    m_stopThreads = false;
    m_targetFrame = 0;
    m_completedWorkers = 0;
    for (int i = 0; i < NUM_WORKERS; ++i) {
        m_workerThreads.emplace_back(&SceneRaytracing::WorkerLoop, this, i + 1);
    }
}

void SceneRaytracing::ShutdownThreadPool() {
    m_stopThreads = true;
    m_targetFrame.fetch_add(1, std::memory_order_release);
    for (auto& thread : m_workerThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    m_workerThreads.clear();
}

void SceneRaytracing::WorkerLoop(int threadId) {
#ifdef __wiiu__
    if (threadId == 1) {
        OSSetThreadAffinity(OSGetCurrentThread(), 1 << 1);
    } else if (threadId == 2) {
        OSSetThreadAffinity(OSGetCurrentThread(), 1 << 2);
    }
#endif

    int lastProcessedFrame = 0;
    while (!m_stopThreads) {
        int currentTarget = m_targetFrame.load(std::memory_order_acquire);
        if (currentTarget > lastProcessedFrame) {
            lastProcessedFrame = currentTarget;
            if (m_stopThreads) break;

            TraceRows(threadId, NUM_WORKERS + 1, m_currentPixelBuffer, m_currentWidth, m_currentHeight, m_currentPitchPixels);
            m_completedWorkers.fetch_add(1, std::memory_order_release);
        } else {
#ifdef __wiiu__
            OSYieldThread();
#else
            std::this_thread::yield();
#endif
        }
    }
}

inline bool intersectSpherePrimaryFast(const Point3D& rayDir, const RaySphere& s, float& outT) {
    float b = rayDir.x * s.relCenter.x + rayDir.y * s.relCenter.y + rayDir.z * s.relCenter.z;
    if (b <= 0.0f) return false;

    float discriminant = b * b - s.cConstPrimary;
    if (discriminant < 0.0f) return false;

    float t = b - std::sqrtf(discriminant);
    if (t <= 0.001f) return false;

    outT = t;
    return true;
}

inline bool intersectBoxPrimaryFastInv(const Point3D& invRayDir, const RayBox& box, float& outT) {
    float t1 = box.minB.x * invRayDir.x;
    float t2 = box.maxB.x * invRayDir.x;
    float t3 = box.minB.y * invRayDir.y;
    float t4 = box.maxB.y * invRayDir.y;
    float t5 = box.minB.z * invRayDir.z;
    float t6 = box.maxB.z * invRayDir.z;

    float tminX = (t1 < t2) ? t1 : t2;
    float tmaxX = (t1 > t2) ? t1 : t2;
    float tminY = (t3 < t4) ? t3 : t4;
    float tmaxY = (t3 > t4) ? t3 : t4;
    float tminZ = (t5 < t6) ? t5 : t6;
    float tmaxZ = (t5 > t6) ? t5 : t6;

    float tNear = (tminX > tminY) ? ((tminX > tminZ) ? tminX : tminZ) : ((tminY > tminZ) ? tminY : tminZ);
    float tFar  = (tmaxX < tmaxY) ? ((tmaxX < tmaxZ) ? tmaxX : tmaxZ) : ((tmaxY < tmaxZ) ? tmaxY : tmaxZ);

    if (tFar < tNear || tFar < 0.001f) return false;

    outT = (tNear > 0.001f) ? tNear : tFar;
    return true;
}

static inline Point3D getBoxNormal(const Point3D& hitPosRel, const RayBox& box) {
    Point3D localPos = hitPosRel - box.center;
    Point3D absPos{std::abs(localPos.x) / box.halfExtents.x,
                   std::abs(localPos.y) / box.halfExtents.y,
                   std::abs(localPos.z) / box.halfExtents.z};

    if (absPos.x >= absPos.y && absPos.x >= absPos.z) {
        return Point3D{(localPos.x > 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f};
    } else if (absPos.y >= absPos.x && absPos.y >= absPos.z) {
        return Point3D{0.0f, (localPos.y > 0.0f) ? 1.0f : -1.0f, 0.0f};
    } else {
        return Point3D{0.0f, 0.0f, (localPos.z > 0.0f) ? 1.0f : -1.0f};
    }
}

static inline bool intersectBoxSecondaryFast(const Point3D& origin, const Point3D& rayDir, const RayBox& box, float& tHit, Point3D& hitNormal) {
    Point3D minB = box.center - box.halfExtents;
    Point3D maxB = box.center + box.halfExtents;

    float dirX = (std::abs(rayDir.x) < 1e-6f) ? (rayDir.x < 0.0f ? -1e-6f : 1e-6f) : rayDir.x;
    float dirY = (std::abs(rayDir.y) < 1e-6f) ? (rayDir.y < 0.0f ? -1e-6f : 1e-6f) : rayDir.y;
    float dirZ = (std::abs(rayDir.z) < 1e-6f) ? (rayDir.z < 0.0f ? -1e-6f : 1e-6f) : rayDir.z;

    float invDx = 1.0f / dirX;
    float invDy = 1.0f / dirY;
    float invDz = 1.0f / dirZ;

    float t1 = (minB.x - origin.x) * invDx;
    float t2 = (maxB.x - origin.x) * invDx;
    float t3 = (minB.y - origin.y) * invDy;
    float t4 = (maxB.y - origin.y) * invDy;
    float t5 = (minB.z - origin.z) * invDz;
    float t6 = (maxB.z - origin.z) * invDz;

    float tminX = (t1 < t2) ? t1 : t2;
    float tmaxX = (t1 > t2) ? t1 : t2;
    float tminY = (t3 < t4) ? t3 : t4;
    float tmaxY = (t3 > t4) ? t3 : t4;
    float tminZ = (t5 < t6) ? t5 : t6;
    float tmaxZ = (t5 > t6) ? t5 : t6;

    float tNear = (tminX > tminY) ? ((tminX > tminZ) ? tminX : tminZ) : ((tminY > tminZ) ? tminY : tminZ);
    float tFar  = (tmaxX < tmaxY) ? ((tmaxX < tmaxZ) ? tmaxX : tmaxZ) : ((tmaxY < tmaxZ) ? tmaxY : tmaxZ);

    if (tFar < tNear || tFar < 0.001f) return false;

    tHit = (tNear > 0.001f) ? tNear : tFar;

    if (tHit == t1) hitNormal = Point3D{-1.0f, 0.0f, 0.0f};
    else if (tHit == t2) hitNormal = Point3D{1.0f, 0.0f, 0.0f};
    else if (tHit == t3) hitNormal = Point3D{0.0f, -1.0f, 0.0f};
    else if (tHit == t4) hitNormal = Point3D{0.0f, 1.0f, 0.0f};
    else if (tHit == t5) hitNormal = Point3D{0.0f, 0.0f, -1.0f};
    else hitNormal = Point3D{0.0f, 0.0f, 1.0f};

    return true;
}

static inline bool isShadowOccluded(const Point3D& shadowOrigin, const Point3D& lightDir, const RaySphere& sphere) {
    Point3D oc = shadowOrigin - sphere.center;
    float b = oc.x * lightDir.x + oc.y * lightDir.y + oc.z * lightDir.z;
    float c = (oc.x * oc.x + oc.y * oc.y + oc.z * oc.z) - sphere.radiusSq;
    float discriminant = b * b - c;
    if (discriminant < 0.0f) return false;

    float sqrtD = std::sqrtf(discriminant);
    float t0 = -b - sqrtD;
    return (t0 > 0.001f);
}

static inline bool isBoxShadowOccluded(const Point3D& shadowOrigin, const Point3D& lightDir, const RayBox& box) {
    float t = 0.0f;
    Point3D n;
    return intersectBoxSecondaryFast(shadowOrigin, lightDir, box, t, n);
}

static inline bool intersectSphereSecondaryFast(const Point3D& origin, const Point3D& rayDir, const RaySphere& sphere, float& tHit, Point3D& hitNormal) {
    Point3D oc = origin - sphere.center;
    float b = oc.x * rayDir.x + oc.y * rayDir.y + oc.z * rayDir.z;
    float c = (oc.x * oc.x + oc.y * oc.y + oc.z * oc.z) - sphere.radiusSq;
    float discriminant = b * b - c;
    if (discriminant < 0.0f) return false;

    float sqrtD = std::sqrtf(discriminant);
    float t0 = -b - sqrtD;
    if (t0 > 0.001f) {
        tHit = t0;
    } else {
        float t1 = -b + sqrtD;
        if (t1 > 0.001f) tHit = t1; else return false;
    }

    Point3D hitPos = origin + rayDir * tHit;
    hitNormal = (hitPos - sphere.center) * sphere.invRadius;
    return true;
}

void SceneRaytracing::TraceRows(int threadId, int numThreads, uint32_t* pixelBuffer, int width, int height, int pitchPixels) {
    const size_t numSpheres = m_spheres.size();

    const float rxX = m_camRight.x,   rxY = m_camRight.y,   rxZ = m_camRight.z;
    const float ryX = m_camUp.x,      ryY = m_camUp.y,      ryZ = m_camUp.z;
    const float rzX = m_camForward.x, rzY = m_camForward.y, rzZ = m_camForward.z;

    int stepY  = m_gpuAccel ? 2 : 1;
    int startY = (m_gpuAccel ? m_frameParity : 0) + threadId * stepY;
    int strideY = numThreads * stepY;

    const auto& s0 = m_spheres[0];
    const auto& s1 = m_spheres[1];
    const auto& bx = m_box;

    for (int y = startY; y < height; y += strideY) {
        int texRowOffset0 = y * pitchPixels;
        uint32_t* rowPtr0 = pixelBuffer + texRowOffset0;
        uint32_t* rowPtr1 = (m_gpuAccel && (y + 1 < height)) ? (pixelBuffer + (y + 1) * pitchPixels) : nullptr;

        int rowBase = y * width;
        bool yInS0  = (y >= s0.bbox.minY && y <= s0.bbox.maxY);
        bool yInS1  = (y >= s1.bbox.minY && y <= s1.bbox.maxY);
        bool yInBox = (y >= bx.bbox.minY && y <= bx.bbox.maxY);

        if (!yInS0 && !yInS1 && !yInBox) {
            std::fill_n(rowPtr0, width, 0x0A0F1EFF);
            if (rowPtr1) {
                std::fill_n(rowPtr1, width, 0x0A0F1EFF);
            }
            continue;
        }

        int minX = width;
        int maxX = -1;

        if (yInS0) {
            minX = std::min(minX, s0.bbox.minX);
            maxX = std::max(maxX, s0.bbox.maxX);
        }
        if (yInS1) {
            minX = std::min(minX, s1.bbox.minX);
            maxX = std::max(maxX, s1.bbox.maxX);
        }
        if (yInBox) {
            minX = std::min(minX, bx.bbox.minX);
            maxX = std::max(maxX, bx.bbox.maxX);
        }

        minX = clampInt(minX, 0, width - 1);
        maxX = clampInt(maxX, 0, width - 1);

        if (minX > 0) {
            std::fill_n(rowPtr0, minX, 0x0A0F1EFF);
            if (rowPtr1) {
                std::fill_n(rowPtr1, minX, 0x0A0F1EFF);
            }
        }

        if (maxX < width - 1) {
            std::fill_n(rowPtr0 + maxX + 1, width - 1 - maxX, 0x0A0F1EFF);
            if (rowPtr1) {
                std::fill_n(rowPtr1 + maxX + 1, width - 1 - maxX, 0x0A0F1EFF);
            }
        }

        for (int x = minX; x <= maxX; ++x) {
            bool testS0  = yInS0  && (x >= s0.bbox.minX && x <= s0.bbox.maxX);
            bool testS1  = yInS1  && (x >= s1.bbox.minX && x <= s1.bbox.maxX);
            bool testBox = yInBox && (x >= bx.bbox.minX && x <= bx.bbox.maxX);

            uint32_t packedColor = 0x0A0F1EFF;

            if (testS0 || testS1 || testBox) {
                const Point3D& dirLocal = m_screenPlaneDirs[rowBase + x];
                Point3D rayDir{
                    rxX * dirLocal.x + ryX * dirLocal.y + rzX * dirLocal.z,
                    rxY * dirLocal.x + ryY * dirLocal.y + rzY * dirLocal.z,
                    rxZ * dirLocal.x + ryZ * dirLocal.y + rzZ * dirLocal.z
                };

                float nearestT = 1e9f;
                int hitObjectType = -1;
                int hitSphereIdx = -1;

                if (testS0) {
                    float t = 0.0f;
                    if (intersectSpherePrimaryFast(rayDir, s0, t)) {
                        if (t < nearestT) {
                            nearestT = t;
                            hitObjectType = 0;
                            hitSphereIdx = 0;
                        }
                    }
                }

                if (testS1) {
                    float t = 0.0f;
                    if (intersectSpherePrimaryFast(rayDir, s1, t)) {
                        if (t < nearestT) {
                            nearestT = t;
                            hitObjectType = 0;
                            hitSphereIdx = 1;
                        }
                    }
                }

                if (testBox) {
                    float dx = (std::abs(rayDir.x) < 1e-6f) ? (rayDir.x < 0.0f ? -1e-6f : 1e-6f) : rayDir.x;
                    float dy = (std::abs(rayDir.y) < 1e-6f) ? (rayDir.y < 0.0f ? -1e-6f : 1e-6f) : rayDir.y;
                    float dz = (std::abs(rayDir.z) < 1e-6f) ? (rayDir.z < 0.0f ? -1e-6f : 1e-6f) : rayDir.z;
                    Point3D invRayDir{1.0f / dx, 1.0f / dy, 1.0f / dz};

                    float tBox = 0.0f;
                    if (intersectBoxPrimaryFastInv(invRayDir, bx, tBox)) {
                        if (tBox < nearestT) {
                            nearestT = tBox;
                            hitObjectType = 1;
                        }
                    }
                }

                if (hitObjectType >= 0) {
                    Point3D nearestNormal;
                    Point3D worldHitPos = m_camPos + rayDir * nearestT;

                    if (hitObjectType == 0) {
                        Point3D hitPosRel = rayDir * nearestT;
                        nearestNormal = (hitPosRel - m_spheres[hitSphereIdx].relCenter) * m_spheres[hitSphereIdx].invRadius;
                    } else {
                        nearestNormal = getBoxNormal(worldHitPos, bx);
                    }

                    ColorRGBA hitBaseColor = (hitObjectType == 0) ? m_spheres[hitSphereIdx].color : m_box.color;
                    float hitReflectivity = (hitObjectType == 0) ? m_spheres[hitSphereIdx].reflectivity : m_box.reflectivity;

                    Point3D lightDir = (m_lightPos - worldHitPos).normalized();

                    bool inShadow = false;
                    Point3D shadowOrigin = worldHitPos + nearestNormal * 0.005f;

                    for (size_t i = 0; i < numSpheres; ++i) {
                        if (hitObjectType == 0 && static_cast<int>(i) == hitSphereIdx) continue;
                        if (isShadowOccluded(shadowOrigin, lightDir, m_spheres[i])) {
                            inShadow = true;
                            break;
                        }
                    }

                    if (!inShadow && hitObjectType != 1) {
                        if (isBoxShadowOccluded(shadowOrigin, lightDir, m_box)) {
                            inShadow = true;
                        }
                    }

                    float diff = inShadow ? 0.15f : std::max(0.20f, Point3D::dot(nearestNormal, lightDir));
                    
                    ColorRGBA reflectColor{15, 25, 45, 255};
                    if (!inShadow && hitReflectivity > 0.05f) {
                        Point3D reflectDir = (rayDir - nearestNormal * (2.0f * Point3D::dot(rayDir, nearestNormal))).normalized();

                        float reflectT = 1e9f;
                        int refObjType = -1;
                        int reflectSphereIdx = -1;
                        Point3D refNormal;

                        for (size_t i = 0; i < numSpheres; ++i) {
                            if (hitObjectType == 0 && static_cast<int>(i) == hitSphereIdx) continue;
                            float t = 0.0f;
                            Point3D n;
                            if (intersectSphereSecondaryFast(shadowOrigin, reflectDir, m_spheres[i], t, n)) {
                                if (t < reflectT) {
                                    reflectT = t;
                                    refObjType = 0;
                                    reflectSphereIdx = static_cast<int>(i);
                                    refNormal = n;
                                }
                            }
                        }

                        if (hitObjectType != 1) {
                            float tBox = 0.0f;
                            Point3D nBox;
                            if (intersectBoxSecondaryFast(shadowOrigin, reflectDir, m_box, tBox, nBox)) {
                                if (tBox < reflectT) {
                                    reflectT = tBox;
                                    refObjType = 1;
                                    refNormal = nBox;
                                }
                            }
                        }

                        if (refObjType >= 0) {
                            Point3D refHitPos = shadowOrigin + reflectDir * reflectT;
                            Point3D refLightDir = (m_lightPos - refHitPos).normalized();
                            float refDiff = std::max(0.2f, Point3D::dot(refNormal, refLightDir));
                            
                            ColorRGBA refObjColor = (refObjType == 0) ? m_spheres[reflectSphereIdx].color : m_box.color;
                            reflectColor = ColorRGBA{
                                static_cast<Uint8>(refObjColor.r * refDiff),
                                static_cast<Uint8>(refObjColor.g * refDiff),
                                static_cast<Uint8>(refObjColor.b * refDiff),
                                255
                            };
                        }
                    }

                    float refl = hitReflectivity;
                    float invRefl = 1.0f - refl;
                    Uint8 r = static_cast<Uint8>(std::min(255.0f, hitBaseColor.r * diff * invRefl + reflectColor.r * refl));
                    Uint8 g = static_cast<Uint8>(std::min(255.0f, hitBaseColor.g * diff * invRefl + reflectColor.g * refl));
                    Uint8 b = static_cast<Uint8>(std::min(255.0f, hitBaseColor.b * diff * invRefl + reflectColor.b * refl));
                    packedColor = (r << 24) | (g << 16) | (b << 8) | 0xFF;
                }
            }

            rowPtr0[x] = packedColor;
            if (rowPtr1) {
                rowPtr1[x] = packedColor;
            }
        }
    }
}

static inline uint32_t fastBlend50(uint32_t c1, uint32_t c2) {
    return ((c1 & 0xFEFEFEFE) >> 1) + ((c2 & 0xFEFEFEFE) >> 1);
}

static inline uint32_t fastBlend75_25(uint32_t c1, uint32_t c2) {
    uint32_t b50 = ((c1 & 0xFEFEFEFE) >> 1) + ((c2 & 0xFEFEFEFE) >> 1);
    return ((c1 & 0xFEFEFEFE) >> 1) + ((b50 & 0xFEFEFEFE) >> 1);
}

static inline uint8_t getFastLumaInt(uint32_t c) {
    uint32_t r = (c >> 24) & 0xFF;
    uint32_t g = (c >> 16) & 0xFF;
    uint32_t b = (c >> 8) & 0xFF;
    return static_cast<uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
}

static void ApplyBufferSmoothing(uint32_t* buffer, int width, int height, int pitchPixels, SmoothingMode mode) {
    if (mode == SmoothingMode::OFF_NEAREST || mode == SmoothingMode::BILINEAR_GX2) {
        return;
    }

    static std::vector<uint32_t> tempBuffer;
    size_t requiredSize = static_cast<size_t>(height * pitchPixels);
    if (tempBuffer.size() < requiredSize) {
        tempBuffer.resize(requiredSize);
    }
    std::memcpy(tempBuffer.data(), buffer, requiredSize * sizeof(uint32_t));

    if (mode == SmoothingMode::FXAA_FULL) {
        for (int y = 1; y < height - 1; ++y) {
            int rowCurr = y * pitchPixels;
            int rowPrev = (y - 1) * pitchPixels;
            int rowNext = (y + 1) * pitchPixels;

            for (int x = 1; x < width - 1; ++x) {
                uint32_t m = tempBuffer[rowCurr + x];
                uint8_t lM = getFastLumaInt(m);
                uint8_t lN = getFastLumaInt(tempBuffer[rowPrev + x]);
                uint8_t lS = getFastLumaInt(tempBuffer[rowNext + x]);
                uint8_t lE = getFastLumaInt(tempBuffer[rowCurr + x + 1]);
                uint8_t lW = getFastLumaInt(tempBuffer[rowCurr + x - 1]);

                uint8_t minL = std::min({lM, lN, lS, lE, lW});
                uint8_t maxL = std::max({lM, lN, lS, lE, lW});
                if ((maxL - minL) < 10) continue;

                uint32_t avgV = fastBlend50(tempBuffer[rowPrev + x], tempBuffer[rowNext + x]);
                uint32_t avgH = fastBlend50(tempBuffer[rowCurr + x - 1], tempBuffer[rowCurr + x + 1]);
                uint32_t avg4 = fastBlend50(avgV, avgH);

                buffer[rowCurr + x] = fastBlend50(m, avg4);
            }
        }
    } else if (mode == SmoothingMode::SUBPIXEL_AA) {
        for (int y = 1; y < height - 1; ++y) {
            int rowCurr = y * pitchPixels;
            int rowPrev = (y - 1) * pitchPixels;
            int rowNext = (y + 1) * pitchPixels;

            for (int x = 1; x < width - 1; ++x) {
                uint32_t m = tempBuffer[rowCurr + x];
                uint8_t lM = getFastLumaInt(m);
                uint8_t lN = getFastLumaInt(tempBuffer[rowPrev + x]);
                uint8_t lS = getFastLumaInt(tempBuffer[rowNext + x]);
                uint8_t lE = getFastLumaInt(tempBuffer[rowCurr + x + 1]);
                uint8_t lW = getFastLumaInt(tempBuffer[rowCurr + x - 1]);

                if (std::abs(lM - lN) < 8 && std::abs(lM - lS) < 8 &&
                    std::abs(lM - lE) < 8 && std::abs(lM - lW) < 8) continue;

                uint32_t avgV = fastBlend50(tempBuffer[rowPrev + x], tempBuffer[rowNext + x]);
                uint32_t avgH = fastBlend50(tempBuffer[rowCurr + x - 1], tempBuffer[rowCurr + x + 1]);
                uint32_t avg4 = fastBlend50(avgV, avgH);

                buffer[rowCurr + x] = fastBlend75_25(m, avg4);
            }
        }
    } else if (mode == SmoothingMode::BICUBIC_SHARPEN) {
        for (int y = 1; y < height - 1; ++y) {
            int rowCurr = y * pitchPixels;
            int rowPrev = (y - 1) * pitchPixels;
            int rowNext = (y + 1) * pitchPixels;

            for (int x = 1; x < width - 1; ++x) {
                uint32_t m = tempBuffer[rowCurr + x];
                uint8_t lM = getFastLumaInt(m);
                uint8_t lN = getFastLumaInt(tempBuffer[rowPrev + x]);
                uint8_t lS = getFastLumaInt(tempBuffer[rowNext + x]);
                uint8_t lE = getFastLumaInt(tempBuffer[rowCurr + x + 1]);
                uint8_t lW = getFastLumaInt(tempBuffer[rowCurr + x - 1]);

                if (std::abs(lM - lN) < 8 && std::abs(lM - lS) < 8 &&
                    std::abs(lM - lE) < 8 && std::abs(lM - lW) < 8) continue;

                uint32_t rM = (m >> 24) & 0xFF, gM = (m >> 16) & 0xFF, bM = (m >> 8) & 0xFF;
                uint32_t rN = (tempBuffer[rowPrev + x] >> 24) & 0xFF, rS = (tempBuffer[rowNext + x] >> 24) & 0xFF;
                uint32_t rE = (tempBuffer[rowCurr + x + 1] >> 24) & 0xFF, rW = (tempBuffer[rowCurr + x - 1] >> 24) & 0xFF;

                uint32_t gN = (tempBuffer[rowPrev + x] >> 16) & 0xFF, gS = (tempBuffer[rowNext + x] >> 16) & 0xFF;
                uint32_t gE = (tempBuffer[rowCurr + x + 1] >> 16) & 0xFF, gW = (tempBuffer[rowCurr + x - 1] >> 16) & 0xFF;

                uint32_t bN = (tempBuffer[rowPrev + x] >> 8) & 0xFF,  bS = (tempBuffer[rowNext + x] >> 8) & 0xFF;
                uint32_t bE = (tempBuffer[rowCurr + x + 1] >> 8) & 0xFF,  bW = (tempBuffer[rowCurr + x - 1] >> 8) & 0xFF;

                int rAvg = (rN + rS + rE + rW) >> 2;
                int gAvg = (gN + gS + gE + gW) >> 2;
                int bAvg = (bN + bS + bE + bW) >> 2;

                int rNew = std::clamp((static_cast<int>(rM) * 9 - rAvg * 5) >> 2, 0, 255);
                int gNew = std::clamp((static_cast<int>(gM) * 9 - gAvg * 5) >> 2, 0, 255);
                int bNew = std::clamp((static_cast<int>(bM) * 9 - bAvg * 5) >> 2, 0, 255);

                buffer[rowCurr + x] = (rNew << 24) | (gNew << 16) | (bNew << 8) | 0xFF;
            }
        }
    }
}

void SceneRaytracing::Render(SDL_Renderer* renderer) {
    Uint32 nowTicks = SDL_GetTicks();
    if (m_lastFrameTicks > 0) {
        Uint32 deltaTicks = nowTicks - m_lastFrameTicks;
        if (deltaTicks > 0) {
            float instFps = 1000.0f / static_cast<float>(deltaTicks);
            m_smoothedFps = m_smoothedFps * 0.88f + instFps * 0.12f;
        }
    }
    m_lastFrameTicks = nowTicks;

    if (!m_rayTexture && renderer) {
        m_rayTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, 
                                         SDL_TEXTUREACCESS_STREAMING, m_rayWidth, m_rayHeight);
    }
    if (m_rayTexture) {
        SDL_ScaleMode scaleMode = (m_smoothingMode == SmoothingMode::OFF_NEAREST) ? SDL_ScaleModeNearest : SDL_ScaleModeLinear;
        SDL_SetTextureScaleMode(m_rayTexture, scaleMode);
    }

    if (m_rayTexture) {
        void* lockedPixels = nullptr;
        int pitch = 0;
        if (SDL_LockTexture(m_rayTexture, nullptr, &lockedPixels, &pitch) == 0 && lockedPixels != nullptr) {
            uint32_t* dstBuffer = static_cast<uint32_t*>(lockedPixels);
            int pitchPixels = pitch / static_cast<int>(sizeof(uint32_t));
            TraceScene(dstBuffer, m_rayWidth, m_rayHeight, pitchPixels);
            ApplyBufferSmoothing(dstBuffer, m_rayWidth, m_rayHeight, pitchPixels, m_smoothingMode);
            SDL_UnlockTexture(m_rayTexture);
        } else {
            // Staging fallback
            m_pixelBuffer.assign(m_rayWidth * m_rayHeight, 0x0A0F1EFF);
            TraceScene(m_pixelBuffer.data(), m_rayWidth, m_rayHeight, m_rayWidth);
            ApplyBufferSmoothing(m_pixelBuffer.data(), m_rayWidth, m_rayHeight, m_rayWidth, m_smoothingMode);
            SDL_UpdateTexture(m_rayTexture, nullptr, m_pixelBuffer.data(), m_rayWidth * static_cast<int>(sizeof(uint32_t)));
        }
    }

    if (m_rayTexture) {
        if (m_displayMode == ScreenTargetMode::DUAL_TV_AND_GAMEPAD) {
            SDL_Rect tvDst = { 0, 0, 1280, 720 };
            SDL_RenderCopy(renderer, m_rayTexture, nullptr, &tvDst);

            SDL_Rect gpDst = { 780, 100, 450, 253 };
            SDL_Rect gpBorder = { gpDst.x - 4, gpDst.y - 4, gpDst.w + 8, gpDst.h + 8 };
            Render2D::drawBox(renderer, gpBorder.x, gpBorder.y, gpBorder.w, gpBorder.h, ColorRGBA{10, 20, 35, 230}, ColorRGBA::Yellow());
            SDL_RenderCopy(renderer, m_rayTexture, nullptr, &gpDst);
            Render2D::drawPixelString(renderer, "GAMEPAD PANTALLA NATIVA 854X480", gpDst.x + 10, gpDst.y + 10, 1, ColorRGBA::Yellow());

        } else if (m_displayMode == ScreenTargetMode::GAMEPAD_NATIVE_854x480) {
            SDL_Rect gpDst = { (1280 - 854) / 2, (720 - 480) / 2, 854, 480 };
            SDL_Rect gpBorder = { gpDst.x - 4, gpDst.y - 4, gpDst.w + 8, gpDst.h + 8 };
            Render2D::drawBox(renderer, gpBorder.x, gpBorder.y, gpBorder.w, gpBorder.h, ColorRGBA{5, 10, 20, 255}, ColorRGBA::Green());
            SDL_RenderCopy(renderer, m_rayTexture, nullptr, &gpDst);
            Render2D::drawPixelString(renderer, "VISTA NATIVA GAMEPAD WII U (854X480 WVGA)", gpDst.x + 20, gpDst.y + 15, 2, ColorRGBA::Green());

        } else {
            SDL_Rect tvDst = { 0, 0, 1280, 720 };
            SDL_RenderCopy(renderer, m_rayTexture, nullptr, &tvDst);
        }
    }

    // 1. Botón Volver
    ColorRGBA btnFill = m_backHover ? ColorRGBA{200, 40, 40, 230} : ColorRGBA{20, 30, 50, 220};
    ColorRGBA btnBorder = m_backHover ? ColorRGBA::Yellow() : ColorRGBA::Cyan();
    Render2D::drawBox(renderer, m_backBtnRect.x, m_backBtnRect.y, m_backBtnRect.w, m_backBtnRect.h, btnFill, btnBorder);
    Render2D::drawPixelString(renderer, "< VOLVER", m_backBtnRect.x + 12, m_backBtnRect.y + 12, 2, ColorRGBA::White());

    // 2. Botón Cambiar Calidad/Resolución del Buffer Interno
    const char* qualStr = "BUFFER: 854X480 (480P)";
    if (m_quality == RayQuality::GAMEPAD_480P_854x480) qualStr = "BUFFER: 854X480 (480P)";
    if (m_quality == RayQuality::QHD_540P_960x540) qualStr = "BUFFER: 960X540 (540P)";
    if (m_quality == RayQuality::FULL_720P_1280x720) qualStr = "BUFFER: 1152X648 (720P ESC)";
    if (m_quality == RayQuality::FULL_1080P_1920x1080) qualStr = "BUFFER: 1152X648 (1080P ESC)";

    ColorRGBA qFill = m_qualHover ? ColorRGBA{0, 180, 240, 230} : ColorRGBA{15, 30, 50, 220};
    ColorRGBA qBorder = m_qualHover ? ColorRGBA::Yellow() : ColorRGBA::Cyan();
    Render2D::drawBox(renderer, m_qualBtnRect.x, m_qualBtnRect.y, m_qualBtnRect.w, m_qualBtnRect.h, qFill, qBorder);
    Render2D::drawPixelString(renderer, qualStr, m_qualBtnRect.x + 10, m_qualBtnRect.y + 12, 2, ColorRGBA::White());

    // 3. Botón Cambiar Target HD de Salida (720p vs 1080p)
    const char* targetStr = (m_outputTarget == OutputTargetHD::TARGET_720P_HD) ? "SALIDA: 720P HD" : "SALIDA: 1080P FULL HD";
    ColorRGBA tFill = m_targetHover ? ColorRGBA{0, 200, 120, 230} : ColorRGBA{15, 45, 35, 220};
    ColorRGBA tBorder = m_targetHover ? ColorRGBA::Yellow() : ColorRGBA::Green();
    Render2D::drawBox(renderer, m_targetBtnRect.x, m_targetBtnRect.y, m_targetBtnRect.w, m_targetBtnRect.h, tFill, tBorder);
    Render2D::drawPixelString(renderer, targetStr, m_targetBtnRect.x + 10, m_targetBtnRect.y + 12, 2, ColorRGBA::White());

    // 3. Botón Cambiar Modo de Pantalla (TV vs GamePad vs Dual)
    const char* dispStr = "PANTALLA: DOBLE (TV+GP)";
    if (m_displayMode == ScreenTargetMode::GAMEPAD_NATIVE_854x480) dispStr = "PANTALLA: GAMEPAD (854X480)";
    if (m_displayMode == ScreenTargetMode::TV_NATIVE_1280x720) dispStr = "PANTALLA: TV HD (1280X720)";

    ColorRGBA dFill = m_dispHover ? ColorRGBA{0, 220, 120, 230} : ColorRGBA{15, 50, 40, 220};
    ColorRGBA dBorder = m_dispHover ? ColorRGBA::Yellow() : ColorRGBA::Green();
    Render2D::drawBox(renderer, m_dispBtnRect.x, m_dispBtnRect.y, m_dispBtnRect.w, m_dispBtnRect.h, dFill, dBorder);
    Render2D::drawPixelString(renderer, dispStr, m_dispBtnRect.x + 10, m_dispBtnRect.y + 12, 2, ColorRGBA::White());

    // 4. Botón Alternar Aceleración GPU
    const char* gpuStr = m_gpuAccel ? "GPU ACCEL: ON" : "GPU ACCEL: OFF";
    ColorRGBA gFill = m_gpuHover ? ColorRGBA{240, 140, 0, 230} : ColorRGBA{60, 30, 10, 220};
    ColorRGBA gBorder = m_gpuHover ? ColorRGBA::Yellow() : ColorRGBA{255, 160, 0, 255};
    Render2D::drawBox(renderer, m_gpuBtnRect.x, m_gpuBtnRect.y, m_gpuBtnRect.w, m_gpuBtnRect.h, gFill, gBorder);
    Render2D::drawPixelString(renderer, gpuStr, m_gpuBtnRect.x + 12, m_gpuBtnRect.y + 12, 2, ColorRGBA::White());

    // 5. Botón Alternar Multihilo CPU
    const char* cpuStr = m_multithreading ? "CPU: 2 CORES" : "CPU: 1 CORE";
    ColorRGBA cFill = m_cpuHover ? ColorRGBA{180, 0, 220, 230} : ColorRGBA{40, 10, 50, 220};
    ColorRGBA cBorder = m_cpuHover ? ColorRGBA::Yellow() : ColorRGBA{220, 50, 255, 255};
    Render2D::drawBox(renderer, m_cpuBtnRect.x, m_cpuBtnRect.y, m_cpuBtnRect.w, m_cpuBtnRect.h, cFill, cBorder);
    Render2D::drawPixelString(renderer, cpuStr, m_cpuBtnRect.x + 10, m_cpuBtnRect.y + 12, 2, ColorRGBA::White());

    // 6. Botón Cambiar Modo de Suavizado
    const char* smoothStr = "SUAVIZADO: SUBPIXEL AA";
    if (m_smoothingMode == SmoothingMode::OFF_NEAREST) smoothStr = "SUAVIZADO: DESACTIVADO";
    if (m_smoothingMode == SmoothingMode::BILINEAR_GX2) smoothStr = "SUAVIZADO: BILINEAL HW";
    if (m_smoothingMode == SmoothingMode::SUBPIXEL_AA) smoothStr = "SUAVIZADO: SUBPIXEL AA";
    if (m_smoothingMode == SmoothingMode::FXAA_FULL) smoothStr = "SUAVIZADO: FXAA FULL";
    if (m_smoothingMode == SmoothingMode::BICUBIC_SHARPEN) smoothStr = "SUAVIZADO: BICUBICO";

    ColorRGBA sFill = m_smoothHover ? ColorRGBA{220, 120, 0, 230} : ColorRGBA{45, 30, 15, 220};
    ColorRGBA sBorder = m_smoothHover ? ColorRGBA::Yellow() : ColorRGBA{255, 165, 0, 255};
    Render2D::drawBox(renderer, m_smoothBtnRect.x, m_smoothBtnRect.y, m_smoothBtnRect.w, m_smoothBtnRect.h, sFill, sBorder);
    Render2D::drawPixelString(renderer, smoothStr, m_smoothBtnRect.x + 10, m_smoothBtnRect.y + 12, 2, ColorRGBA::White());

    // 6. HUD Informativo con contador FPS en Vivo
    SDL_Rect hudBox = { 20, 580, 1240, 100 };
    Render2D::drawBox(renderer, hudBox.x, hudBox.y, hudBox.w, hudBox.h, ColorRGBA{10, 20, 35, 230}, ColorRGBA::Cyan());

    bool fpsLimited = SceneManager::Instance().IsFPSLimitEnabled();
    StackString<160> fpsTitle;
    fpsTitle.Format("DEMO 4: RAY TRACING NATIVO ESPRESSO - FPS: %d FPS (%s)",
                    static_cast<int>(m_smoothedFps + 0.5f),
                    fpsLimited ? "LIMITADO A 60 FPS" : "DESBLOQUEADO / UNCAPPED");
    Render2D::drawPixelString(renderer, fpsTitle.string_view(), 40, 595, 2, ColorRGBA::Yellow());
    Render2D::drawPixelString(renderer, "CAMARA ORBITAL 3D: MOVER CON JOYSTICK DERECHO (O TECLAS FLECHA / IJKL)", 40, 622, 2, ColorRGBA::White());

    StackString<160> audioStatus;
    audioStatus.Format("AUDIO: BGM OK (48KHZ) | CLICK.WAV %s", AudioManager::Instance().GetStatusString().c_str());
    Render2D::drawPixelString(renderer, audioStatus.string_view(), 40, 650, 2, ColorRGBA{0, 240, 255, 255});

    // 7. Renderizar Efecto Visual de Onda al hacer Click / Táctil
    if (m_clickEffect.active && m_clickEffect.alpha > 0.0f) {
        Uint8 a = static_cast<Uint8>(m_clickEffect.alpha * 255.0f);
        Render2D::drawShrinkingRing(renderer, static_cast<int>(m_clickEffect.x), static_cast<int>(m_clickEffect.y), 
                                    m_clickEffect.radius, ColorRGBA{0, 240, 255, a});
        Render2D::drawShrinkingRing(renderer, static_cast<int>(m_clickEffect.x), static_cast<int>(m_clickEffect.y), 
                                    m_clickEffect.radius * 0.65f, ColorRGBA{255, 230, 0, a});
    }
}

} // namespace WiiU
