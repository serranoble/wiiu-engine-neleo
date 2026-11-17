#include "Scene/SceneCollisionTriggerDemoGX2.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/SceneMenuModels.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Core/AssetManager.hpp"
#include "Core/ResourceMonitor.hpp"
#include "Core/StackString.hpp"
#include "Core/Config/EngineConfig.hpp"
#include "Core/ECS/Systems/CollisionSystem.hpp"
#include "Core/ECS/Systems/TransformSystem.hpp"
#include "Core/ECS/Systems/AnimationSystem.hpp"
#include "Graphics/Animation/AnimationLoader.hpp"
#include "Graphics/Render2DGX2.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(__WIIU__) || defined(__wiiu__)
#include <vpad/input.h>
#include <padscore/wpad.h>
#include <coreinit/core.h>
#include <proc_ui/procui.h>
#endif

namespace WiiU {

namespace {

// Posiciones analíticas de los 4 pilares clásicos
constexpr Point3D kPillarPositions[SceneCollisionTriggerDemoGX2::NUM_PILLARS] = {
    Point3D{-4.2f, 0.0f, -3.2f},
    Point3D{-1.5f, 0.0f, -3.8f},
    Point3D{-4.2f, 0.0f,  2.8f},
    Point3D{-1.5f, 0.0f,  3.5f}
};

// Posiciones analíticas de las 5 balizas de energía
constexpr Point3D kBeaconPositions[SceneCollisionTriggerDemoGX2::NUM_BEACONS] = {
    Point3D{-4.2f, 0.2f, -1.0f},
    Point3D{-2.8f, 0.2f,  0.5f},
    Point3D{-1.5f, 0.2f, -1.5f},
    Point3D{ 0.0f, 0.2f,  2.2f},
    Point3D{ 2.2f, 0.2f,  2.8f}
};

// Layout de la Interfaz Virtual Canónica 1280x720
const UI::UIRect s_topBarPanel{16.0f, 14.0f, 1248.0f, 44.0f};
const UI::UIRect s_btnVolverTop{24.0f, 18.0f, 100.0f, 36.0f};

const UI::UIRect s_panelGame{16.0f, 66.0f, 388.0f, 246.0f};
const UI::UIRect s_panelTelem{930.0f, 66.0f, 334.0f, 190.0f};

const UI::UIRect s_btnBulletTime{1084.0f, 266.0f, 170.0f, 36.0f};
const UI::UIRect s_btnSplineCam{1084.0f, 310.0f, 170.0f, 36.0f};
const UI::UIRect s_btnSingleStep{1084.0f, 354.0f, 170.0f, 36.0f};

const UI::UIRect s_btnZoomIn{1180.0f, 410.0f, 74.0f, 38.0f};
const UI::UIRect s_btnZoomOut{1180.0f, 460.0f, 74.0f, 38.0f};

const UI::UIRect s_bottomBarPanel{16.0f, 574.0f, 1248.0f, 48.0f};
const UI::UIRect s_btnJump{24.0f, 580.0f, 150.0f, 36.0f};
const UI::UIRect s_btnSuperJump{184.0f, 580.0f, 160.0f, 36.0f};
const UI::UIRect s_btnResetLevel{354.0f, 580.0f, 180.0f, 36.0f};
const UI::UIRect s_btnDebugToggle{544.0f, 580.0f, 170.0f, 36.0f};
const UI::UIRect s_btnPause{724.0f, 580.0f, 140.0f, 36.0f};
const UI::UIRect s_btnResetCam{874.0f, 580.0f, 130.0f, 36.0f};
const UI::UIRect s_btnVolverBot{1134.0f, 580.0f, 120.0f, 36.0f};

} // namespace

SceneCollisionTriggerDemoGX2::SceneCollisionTriggerDemoGX2() = default;

SceneCollisionTriggerDemoGX2::~SceneCollisionTriggerDemoGX2() = default;

void SceneCollisionTriggerDemoGX2::LoadAsync() {
    AssetManager::LoadBinaryFile("models/skybox_day.wcube", m_stagedCubeMap);
    AssetManager::LoadBinaryFile("models/skydome.wmesh", m_stagedSkydomeMesh);
    AssetManager::LoadBinaryFile("models/ground_plane.wmesh", m_stagedGroundMesh);
    AssetManager::LoadBinaryFile("models/dutch_house.wmesh", m_stagedHouseMesh);
    AssetManager::LoadBinaryFile("models/dutch_house_props.wmesh", m_stagedPropsMesh);
    AssetManager::LoadBinaryFile("models/pillar.wmesh", m_stagedPillarMesh);
    AssetManager::LoadBinaryFile("models/beacon.wmesh", m_stagedBeaconMesh);
    AssetManager::LoadBinaryFile("models/obelisk.wmesh", m_stagedObeliskMesh);
    AssetManager::LoadBinaryFile("models/bird_orange.wmesh", m_stagedBirdMesh);
}

bool SceneCollisionTriggerDemoGX2::CreateTextures() {
    if (m_texturesCreated) return true;

    if (!m_stagedCubeMap.empty()) {
        LoadSceneCubeMapFromMemory(m_stagedCubeMap.data(), m_stagedCubeMap.size(), "Tex_Col_SkyboxCube");
    } else {
        LoadSceneCubeMap("models/skybox_day.wcube", "Tex_Col_SkyboxCube");
    }

    m_hSkydomeTexture = LoadSceneTexture("models/skydome.wtex", "Tex_Col_Skydome");
    m_hGrassTexture   = LoadSceneTexture("models/ground_grass.wtex", "Tex_Col_Grass");
    m_hHouseTexture   = LoadSceneTexture("models/dutch_house.wtex", "Tex_Col_House");
    m_hPropsTexture   = LoadSceneTexture("models/dutch_house_props.wtex", "Tex_Col_Props");
    m_hBirdTexture    = LoadSceneTexture("models/bird_orange.wtex", "Tex_Col_Bird");
    m_hWhiteTexture   = CreateSceneSolidTexture(0xFFFFFFFFu, "Tex_Col_White");

    m_texturesCreated = true;
    return true;
}

void SceneCollisionTriggerDemoGX2::ResetGameplayState() {
    AudioEngine3D::Instance().StopAllSounds();
    AudioEngine3D::Instance().SetReverbWetMix(0.05f);
    m_ecsWorld.Clear();

    m_gameClock.Reset();
    m_eventBus.Clear();
    m_collectedBeacons = 0;
    m_inGoalZone = false;
    m_levelCompleted = false;
    m_isPaused = false;
    m_bulletTimeActive = false;
    m_splineCameraActive = false;
    m_lastAcousticInZone = false;
    m_elapsedTime = 0.0f;
    m_droneAngle = 0.0f;
    m_lastBounceTime = -10.0f;
    m_prevPlayerPos = Point3D{0.0f, 0.0f, 1.2f};

    // 1. Skydome y Terreno
    m_skydomeEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_skydomeEntity, Point3D{0.0f, 0.0f, 0.0f});

    m_groundEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_groundEntity, Point3D{0.0f, 0.0f, 0.0f});

    // 2. Casa Holandesa + Props con AABB Collider estático
    m_houseEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_houseEntity, Point3D{4.8f, 0.0f, -1.8f});
    m_ecsWorld.EmplaceComponent<ColliderComponent>(
        m_houseEntity,
        ColliderComponent::MakeAABB(
            Point3D{1.65f, 3.2f, 2.35f},
            Point3D{-0.75f, 2.5f, -0.48f},
            true,
            CollisionLayer::Layer_StaticWorld,
            "CASA HOLANDESA (AABB)"
        )
    );

    m_propsEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_propsEntity, Point3D{4.8f, 0.0f, -1.8f});

    // 3. Cuatro Pilares Clásicos con Sphere Colliders estáticos (deslizamiento tangencial suave)
    for (uint32_t i = 0; i < NUM_PILLARS; ++i) {
        m_pillarEntities[i] = m_ecsWorld.CreateEntity();
        m_ecsWorld.EmplaceComponent<TransformComponent>(m_pillarEntities[i], kPillarPositions[i]);
        m_ecsWorld.EmplaceComponent<ColliderComponent>(
            m_pillarEntities[i],
            ColliderComponent::MakeSphere(
                0.68f,
                Point3D{0.0f, 1.0f, 0.0f},
                true,
                CollisionLayer::Layer_StaticWorld,
                "PILAR DE PIEDRA (ESFERA)"
            )
        );
    }

    // 4. Cinco Balizas de Energía recolectables (TriggerType::Collectible, oneShot = true)
    for (uint32_t i = 0; i < NUM_BEACONS; ++i) {
        m_beaconEntities[i] = m_ecsWorld.CreateEntity();
        m_ecsWorld.EmplaceComponent<TransformComponent>(m_beaconEntities[i], kBeaconPositions[i]);
        m_ecsWorld.EmplaceComponent<TriggerComponent>(
            m_beaconEntities[i],
            TriggerType::Collectible,
            1.15f,
            Point3D{0.0f, 0.9f, 0.0f},
            true,
            0.0f,
            "BALIZA DE ENERGIA"
        );
    }

    // 5. Plataforma Impulsora de Salto (TriggerType::BouncePad, oneShot = false)
    m_bouncePadEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(
        m_bouncePadEntity,
        Point3D{1.8f, 0.0f, -0.3f},
        Point3D{0.0f, 0.0f, 0.0f},
        Point3D{1.6f, 0.32f, 1.6f}
    );
    m_ecsWorld.EmplaceComponent<TriggerComponent>(
        m_bouncePadEntity,
        TriggerType::BouncePad,
        1.25f,
        Point3D{0.0f, 0.5f, 0.0f},
        false,
        12.5f,
        "PLATAFORMA SUPER-SALTO"
    );

    // 6. Obelisco Sagrado — Zona de Meta (TriggerType::Zone, oneShot = false)
    m_obeliskEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_obeliskEntity, Point3D{-6.2f, 0.0f, 0.2f});
    m_ecsWorld.EmplaceComponent<TriggerComponent>(
        m_obeliskEntity,
        TriggerType::Zone,
        1.95f,
        Point3D{0.0f, 1.5f, 0.0f},
        false,
        0.0f,
        "ALTAR META OBELISCO"
    );

    // 7. Entidad Jugador Controlable (Pájaro Naranja con 21 huesos LBS + KinematicCharacter + Sphere Collider)
    m_playerEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(
        m_playerEntity,
        Point3D{0.0f, 0.0f, 1.2f},
        Point3D{0.0f, 0.0f, 0.0f},
        Point3D{0.75f, 0.75f, 0.75f}
    );

    auto& kcc = m_ecsWorld.EmplaceComponent<KinematicCharacterComponent>(
        m_playerEntity,
        0.52f, // capsuleRadius
        1.30f, // capsuleHeight
        6.0f,  // walkSpeed
        8.5f,  // runSpeed
        7.8f,  // jumpSpeed
        18.0f  // gravity
    );
    kcc.isGrounded = true;

    m_ecsWorld.EmplaceComponent<ColliderComponent>(
        m_playerEntity,
        ColliderComponent::MakeSphere(
            0.55f,
            Point3D{0.0f, 0.65f, 0.0f},
            false,
            CollisionLayer::Layer_Player,
            "JUGADOR PAJARO"
        )
    );

    m_ecsWorld.EmplaceComponent<SkeletalMeshComponent>(m_playerEntity, &m_skeleton);
    m_currentAnimClip = (m_animationClips.size() > 2) ? 2 : 0;
    const AnimationClip* initClip = (!m_animationClips.empty()) ? &m_animationClips[m_currentAnimClip] : nullptr;
    m_ecsWorld.EmplaceComponent<AnimatorComponent>(m_playerEntity, initClip, true, 1.0f);

    // 7b. Dron Acústico Orbital (Cap. 15: Emisor 3D espacializado con Doppler físico y Ducking)
    m_dopplerDroneEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(
        m_dopplerDroneEntity,
        Point3D{4.2f, 2.2f, -0.2f},
        Point3D{0.0f, 0.0f, 0.0f},
        Point3D{0.55f, 0.55f, 0.55f}
    );
    auto& droneEmitter = m_ecsWorld.EmplaceComponent<AudioEmitterComponent>(
        m_dopplerDroneEntity,
        &AudioEngine3D::Instance().GetBeaconHumClip(),
        0.88f,
        2.2f,
        32.0f,
        true,
        90,
        AudioBus::Ambience
    );
    droneEmitter.Play();

    // 8. Inicializar cámara orbital con Spring-Arm Ray-AABB/Sphere + Riel Spline C1 (Cap. 17)
    static constexpr Point3D kSplineRailPoints[6] = {
        Point3D{ 0.0f, 4.2f,  8.5f},
        Point3D{ 7.5f, 3.6f,  3.5f},
        Point3D{ 8.2f, 4.8f, -5.0f},
        Point3D{ 0.0f, 5.2f, -8.5f},
        Point3D{-7.8f, 3.8f, -4.5f},
        Point3D{-8.5f, 4.0f,  4.5f}
    };
    m_orbitCamera.Reset();
    m_orbitCamera.SetCollisionEnabled(true);
    m_orbitCamera.SetSplineControlPoints(kSplineRailPoints, 6);
    m_orbitCamera.SetSplineRailActive(false);
    m_orbitCamera.SetLeftStickZoomEnabled(false);
    m_orbitCamera.SetAngles(0.0f, 20.0f);
    m_orbitCamera.SetDistance(6.2f);
    m_orbitCamera.SetTarget(Point3D{0.0f, 1.10f, 1.2f});
    m_orbitCamera.Update(m_ecsWorld, 0.001f);

    AnimationSystem::Update(m_ecsWorld, 0.001f);
    TransformSystem::Update(m_ecsWorld);

    m_eventBus.Publish(
        GameplayEvent{GameplayEventType::None, 0, 0, 1.0f, Point3D{0.0f, 0.0f, 1.2f}},
        "CAP. 7/15/17/18 LISTOS: CLOCK, DOPPLER, CAM & BUS",
        0x50F0FFFF,
        3.2f
    );
}

void SceneCollisionTriggerDemoGX2::OnGameInit() {
    AudioManager::Instance().PlayBGM();
    m_showDebugInfo = EngineConfig::Get().debug.showColliders;

    if (m_skeleton.jointCount == 0) {
        AnimationLoader::LoadSkeleton("models/bird_orange.wskel", m_skeleton);
        AnimationLoader::LoadClips("models/bird_orange.wanim", m_animationClips);
    }
    if (m_stagedBirdMesh.empty()) {
        AssetManager::LoadBinaryFile("models/bird_orange.wmesh", m_stagedBirdMesh);
    }

    auto loadMeshHelper = [](StaticMesh& mesh, std::vector<uint8_t>& staged, const char* path) {
        if (!staged.empty()) {
            mesh.LoadFromMemory(staged.data(), staged.size());
            staged.clear();
            staged.shrink_to_fit();
        } else {
            mesh.LoadFromFile(path);
        }
    };

    loadMeshHelper(m_skydomeMesh, m_stagedSkydomeMesh, "models/skydome.wmesh");
    loadMeshHelper(m_groundMesh,  m_stagedGroundMesh,  "models/ground_plane.wmesh");
    loadMeshHelper(m_houseMesh,   m_stagedHouseMesh,   "models/dutch_house.wmesh");
    loadMeshHelper(m_propsMesh,   m_stagedPropsMesh,   "models/dutch_house_props.wmesh");
    loadMeshHelper(m_pillarMesh,  m_stagedPillarMesh,  "models/pillar.wmesh");
    loadMeshHelper(m_beaconMesh,  m_stagedBeaconMesh,  "models/beacon.wmesh");
    loadMeshHelper(m_obeliskMesh, m_stagedObeliskMesh, "models/obelisk.wmesh");
    loadMeshHelper(m_birdMesh,    m_stagedBirdMesh,    "models/bird_orange.wmesh");

    CreateTextures();

    m_stagedCubeMap.clear(); m_stagedCubeMap.shrink_to_fit();

    SetCameraProjection(55.0f, 0.20f, 1000.0f);
    SetClearColor(ColorRGBA{132, 191, 232, 255});

    ResetGameplayState();
}

void SceneCollisionTriggerDemoGX2::OnGameExit() {
    AudioEngine3D::Instance().SetReverbWetMix(0.0f);
    AudioEngine3D::Instance().StopAllSounds();
    m_ecsWorld.Clear();
    m_skydomeMesh.Destroy();
    m_groundMesh.Destroy();
    m_houseMesh.Destroy();
    m_propsMesh.Destroy();
    m_pillarMesh.Destroy();
    m_beaconMesh.Destroy();
    m_obeliskMesh.Destroy();
    m_birdMesh.Destroy();

    m_hSkydomeTexture = {};
    m_hGrassTexture = {};
    m_hHouseTexture = {};
    m_hPropsTexture = {};
    m_hBirdTexture = {};
    m_hWhiteTexture = {};
    m_texturesCreated = false;
}

void SceneCollisionTriggerDemoGX2::OnGameUpdate(float dt) {
    auto& input = InputManager::Instance();
    const auto& touch = input.GetTouchState();

    auto* playerTrans = m_ecsWorld.GetComponent<TransformComponent>(m_playerEntity);
    auto* playerKcc   = m_ecsWorld.GetComponent<KinematicCharacterComponent>(m_playerEntity);

    auto toggleBulletTime = [this]() {
        m_bulletTimeActive = !m_bulletTimeActive;
        const float newScale = m_bulletTimeActive ? 0.25f : 1.0f;
        m_gameClock.SetTimeScale(newScale);
        m_eventBus.Publish(
            GameplayEvent{GameplayEventType::TimeScaleChanged, 0, 0, newScale, Point3D{}},
            m_bulletTimeActive ? "BULLET-TIME ACTIVADO (0.25X)" : "RELOJ DE GAMEPLAY NORMAL (1.0X)",
            m_bulletTimeActive ? 0x60D8FFFF : 0xA0FFA0FF,
            2.4f
        );
        AudioManager::Instance().PlayClick();
    };

    auto toggleSplineCamera = [this]() {
        m_splineCameraActive = !m_splineCameraActive;
        m_orbitCamera.SetSplineRailActive(m_splineCameraActive);
        m_eventBus.Publish(
            GameplayEvent{GameplayEventType::CameraModeChanged, 0, 0, m_splineCameraActive ? 1.0f : 0.0f, Point3D{}},
            m_splineCameraActive ? "CAMARA: RIEL SPLINE C1 DE CATMULL-ROM" : "CAMARA: ORBITA + SPRING-ARM AABB",
            0xFFD860FF,
            2.4f
        );
        AudioManager::Instance().PlayClick();
    };

    // 1. Controles Físicos de GamePad / Pro Controller
#if defined(__WIIU__) || defined(__wiiu__)
    if (input.IsButtonTriggered(VPAD_BUTTON_X) || input.IsButtonTriggered(WPAD_PRO_BUTTON_X)) {
        toggleBulletTime();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_L) || input.IsButtonTriggered(WPAD_PRO_BUTTON_L)) {
        toggleSplineCamera();
    }
    if (input.IsButtonTriggered(VPAD_BUTTON_MINUS) || input.IsButtonTriggered(WPAD_PRO_BUTTON_MINUS)) {
        m_isPaused = !m_isPaused;
        m_gameClock.SetPaused(m_isPaused);
        AudioManager::Instance().PlayClick();
    }
    if (m_isPaused && (input.IsButtonTriggered(VPAD_BUTTON_R) || input.IsButtonTriggered(WPAD_PRO_BUTTON_R))) {
        m_gameClock.RequestSingleStep();
        AudioManager::Instance().PlayClick();
    } else if (!m_isPaused) {
        if (input.IsButtonHeld(VPAD_BUTTON_R) || input.IsButtonHeld(WPAD_PRO_BUTTON_R)) {
            m_orbitCamera.AdjustZoom(-8.0f * dt);
        }
        if (input.IsButtonHeld(VPAD_BUTTON_ZR) || input.IsButtonHeld(WPAD_PRO_BUTTON_ZR)) {
            m_orbitCamera.AdjustZoom(8.0f * dt);
        }
    }
    if ((input.IsButtonTriggered(VPAD_BUTTON_A) || input.IsButtonTriggered(WPAD_PRO_BUTTON_A) ||
         input.IsButtonTriggered(VPAD_BUTTON_B) || input.IsButtonTriggered(WPAD_PRO_BUTTON_B)) && playerKcc) {
        if (playerKcc->isGrounded) {
            playerKcc->velocity.y = playerKcc->jumpSpeed;
            playerKcc->isGrounded = false;
            playerKcc->isJumping = true;
            AudioManager::Instance().PlayClick();
        }
    }
#endif

    // 2. Controles Táctiles de la Pantalla del GamePad
    if (touch.active) {
        float tx = touch.x;
        float ty = touch.y;

        if (touch.justPressed) {
            if (s_btnVolverTop.Contains(tx, ty) || s_btnVolverBot.Contains(tx, ty)) {
                AudioManager::Instance().PlayClick();
                SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuModels>());
                return;
            }
            if (s_btnJump.Contains(tx, ty) && playerKcc) {
                if (playerKcc->isGrounded) {
                    playerKcc->velocity.y = playerKcc->jumpSpeed;
                    playerKcc->isGrounded = false;
                    playerKcc->isJumping = true;
                    AudioManager::Instance().PlayClick();
                }
            }
            if (s_btnSuperJump.Contains(tx, ty) && playerKcc) {
                playerKcc->velocity.y = 12.5f;
                playerKcc->isGrounded = false;
                playerKcc->isJumping = true;
                m_lastBounceTime = m_elapsedTime;
                m_eventBus.Publish(
                    GameplayEvent{GameplayEventType::BouncePadActivated, m_playerEntity.id, 0, 12.5f,
                                  playerTrans ? playerTrans->position : Point3D{}},
                    "IMPULSO BOUNCE PAD + HIT-STOP!",
                    0x80FF80FF,
                    2.4f
                );
            }
            if (s_btnResetLevel.Contains(tx, ty)) {
                AudioManager::Instance().PlayClick();
                ResetGameplayState();
                return;
            }
            if (s_btnDebugToggle.Contains(tx, ty)) {
                m_showDebugInfo = !m_showDebugInfo;
                EngineConfig::Instance().debug.showColliders = m_showDebugInfo;
                EngineConfig::Instance().debug.showTriggers = m_showDebugInfo;
                AudioManager::Instance().PlayClick();
            }
            if (s_btnPause.Contains(tx, ty)) {
                m_isPaused = !m_isPaused;
                m_gameClock.SetPaused(m_isPaused);
                AudioManager::Instance().PlayClick();
            }
            if (s_btnBulletTime.Contains(tx, ty)) {
                toggleBulletTime();
            }
            if (s_btnSplineCam.Contains(tx, ty)) {
                toggleSplineCamera();
            }
            if (s_btnSingleStep.Contains(tx, ty)) {
                m_isPaused = true;
                m_gameClock.RequestSingleStep();
                AudioManager::Instance().PlayClick();
            }
            if (s_btnResetCam.Contains(tx, ty)) {
                m_splineCameraActive = false;
                m_orbitCamera.SetSplineRailActive(false);
                m_orbitCamera.SetAngles(0.0f, 20.0f);
                m_orbitCamera.SetDistance(6.2f);
                AudioManager::Instance().PlayClick();
            }
            if (s_btnZoomIn.Contains(tx, ty)) {
                m_orbitCamera.AdjustZoom(-1.2f);
                AudioManager::Instance().PlayClick();
            }
            if (s_btnZoomOut.Contains(tx, ty)) {
                m_orbitCamera.AdjustZoom(1.2f);
                AudioManager::Instance().PlayClick();
            }
            // Zona central libre para arrastre orbital de cámara
            if (ty >= 65.0f && ty <= 570.0f && !s_panelGame.Contains(tx, ty) && !s_panelTelem.Contains(tx, ty) &&
                !s_btnZoomIn.Contains(tx, ty) && !s_btnZoomOut.Contains(tx, ty) &&
                !s_btnBulletTime.Contains(tx, ty) && !s_btnSplineCam.Contains(tx, ty) && !s_btnSingleStep.Contains(tx, ty)) {
                m_isDraggingCamera = true;
                m_dragStartX = tx;
                m_dragStartY = ty;
                m_initialYaw = m_orbitCamera.GetYaw();
                m_initialPitch = m_orbitCamera.GetPitch();
            }
        } else if (m_isDraggingCamera) {
            float dx = tx - m_dragStartX;
            float dy = ty - m_dragStartY;
            float newYaw = m_initialYaw - dx * 0.25f;
            float newPitch = std::clamp(m_initialPitch + dy * 0.20f, -10.0f, 75.0f);
            m_orbitCamera.SetAngles(newYaw, newPitch);
        }
    } else {
        m_isDraggingCamera = false;
    }

    // 3. Reloj Jerárquico GameClock (RealTime vs GameplayTime con Bullet-Time, Hit-Stop & Single-Step)
    m_gameClock.Tick(dt);
    const float realDt = m_gameClock.GetRealDelta();
    const float simDt = m_gameClock.GetGameplayDelta();
    m_eventBus.UpdateToasts(realDt);

    if (simDt > 0.0f) {
        m_elapsedTime += simDt;

        // 3a. Locomoción Cinemática en 3ª Persona Relativa a la Dirección Real de Vista de la Cámara
        if (playerTrans && playerKcc) {
            const auto stickL = input.GetLeftStick();
            float inX = stickL.x;
            float inY = stickL.y;

#if defined(__WIIU__) || defined(__wiiu__)
            if (input.IsButtonHeld(VPAD_BUTTON_LEFT)  || input.IsButtonHeld(WPAD_PRO_BUTTON_LEFT))  inX = -1.0f;
            if (input.IsButtonHeld(VPAD_BUTTON_RIGHT) || input.IsButtonHeld(WPAD_PRO_BUTTON_RIGHT)) inX =  1.0f;
            if (input.IsButtonHeld(VPAD_BUTTON_UP)    || input.IsButtonHeld(WPAD_PRO_BUTTON_UP))    inY =  1.0f;
            if (input.IsButtonHeld(VPAD_BUTTON_DOWN)  || input.IsButtonHeld(WPAD_PRO_BUTTON_DOWN))  inY = -1.0f;
#endif

            const float yawDeg = m_orbitCamera.GetYaw();
            const float yawRad = yawDeg * (3.14159265358979323846f / 180.0f);
            const float camFwdX   = -std::sin(yawRad);
            const float camFwdZ   =  std::cos(yawRad);
            const float camRightX = -std::cos(yawRad);
            const float camRightZ = -std::sin(yawRad);

            const float desiredMoveX = (camRightX * inX + camFwdX * inY);
            const float desiredMoveZ = (camRightZ * inX + camFwdZ * inY);
            const float moveLen = std::sqrt(desiredMoveX * desiredMoveX + desiredMoveZ * desiredMoveZ);

            const bool isRunning = input.IsButtonHeld(VPAD_BUTTON_Y) || input.IsButtonHeld(WPAD_PRO_BUTTON_Y) ||
                                   input.IsButtonHeld(VPAD_BUTTON_ZL) || input.IsButtonHeld(WPAD_PRO_BUTTON_ZL);
            const float targetSpeed = isRunning ? playerKcc->runSpeed : playerKcc->walkSpeed;

            if (moveLen > 0.15f) {
                const float invLen = 1.0f / moveLen;
                const float normDirX = desiredMoveX * invLen;
                const float normDirZ = desiredMoveZ * invLen;

                const float factor = std::min(moveLen, 1.0f);
                playerKcc->velocity.x = normDirX * targetSpeed * factor;
                playerKcc->velocity.z = normDirZ * targetSpeed * factor;
                playerKcc->horizontalSpeed = targetSpeed * factor;

                const float targetRotY = std::atan2(normDirX, normDirZ);
                float diffRot = targetRotY - playerTrans->rotation.y;
                constexpr float PI_F = 3.14159265358979323846f;
                while (diffRot >  PI_F) diffRot -= 2.0f * PI_F;
                while (diffRot < -PI_F) diffRot += 2.0f * PI_F;
                playerTrans->rotation.y += diffRot * std::min(12.0f * simDt, 1.0f);
            } else {
                playerKcc->velocity.x = 0.0f;
                playerKcc->velocity.z = 0.0f;
                playerKcc->horizontalSpeed = 0.0f;
            }

            // Gravedad y suelo plano
            if (!playerKcc->isGrounded) {
                playerKcc->velocity.y -= playerKcc->gravity * simDt;
            }

            Point3D nextPos = playerTrans->position;
            nextPos.x += playerKcc->velocity.x * simDt;
            nextPos.y += playerKcc->velocity.y * simDt;
            nextPos.z += playerKcc->velocity.z * simDt;

            if (nextPos.y <= 0.0f) {
                nextPos.y = 0.0f;
                playerKcc->velocity.y = 0.0f;
                playerKcc->isGrounded = true;
                playerKcc->isJumping = false;
            } else {
                playerKcc->isGrounded = false;
            }

            playerTrans->position = nextPos;
            playerTrans->MarkDirty();
        }

        // 3b. Resolver Colisiones Sólidas y Triggers Espaciales
        CollisionSystem::Update(m_ecsWorld, simDt);

        // 3c. Órbita del Dron Acústico Doppler (r = 5.4m, omega = 3.6 rad/s -> ~19.4 m/s)
        m_droneAngle += 3.6f * simDt;
        if (auto* dTrans = m_ecsWorld.GetComponent<TransformComponent>(m_dopplerDroneEntity)) {
            constexpr float DRONE_RADIUS = 5.4f;
            dTrans->position = Point3D{
                -1.2f + DRONE_RADIUS * std::cos(m_droneAngle),
                2.2f + 0.35f * std::sin(m_droneAngle * 2.0f),
                -0.2f + DRONE_RADIUS * std::sin(m_droneAngle)
            };
            dTrans->rotation.y = -m_droneAngle;
            dTrans->MarkDirty();
        }

        // 3d. Lógica de Triggers y Publicación en GameplayEventBus (Cap. 18)
        uint32_t collectedCount = 0;
        for (uint32_t i = 0; i < NUM_BEACONS; ++i) {
            auto* bTrig  = m_ecsWorld.GetComponent<TriggerComponent>(m_beaconEntities[i]);
            auto* bTrans = m_ecsWorld.GetComponent<TransformComponent>(m_beaconEntities[i]);
            if (!bTrig || !bTrans) continue;

            if (bTrig->enteredThisFrame) {
                m_eventBus.Publish(
                    GameplayEvent{GameplayEventType::CollectiblePicked, m_playerEntity.id,
                                  m_beaconEntities[i].id, static_cast<float>(i + 1), bTrans->position},
                    "BALIZA RECOLECTADA! (+HIT-STOP & TRAUMA)",
                    0x60FFFFFF,
                    2.6f
                );
            }

            if (bTrig->isCollected) {
                ++collectedCount;
                bTrans->rotation.y += 5.5f * simDt;
                bTrans->position.y = 1.65f + 0.15f * std::sin(m_elapsedTime * 4.5f + static_cast<float>(i));
                bTrans->scale = Point3D{0.42f, 0.42f, 0.42f};
                bTrans->MarkDirty();
            } else {
                bTrans->rotation.y += 1.8f * simDt;
                bTrans->position.y = 0.18f + 0.12f * std::sin(m_elapsedTime * 2.8f + static_cast<float>(i));
                bTrans->scale = Point3D{1.0f, 1.0f, 1.0f};
                bTrans->MarkDirty();
            }
        }
        m_collectedBeacons = collectedCount;

        if (auto* padTrig = m_ecsWorld.GetComponent<TriggerComponent>(m_bouncePadEntity)) {
            if (padTrig->enteredThisFrame) {
                m_lastBounceTime = m_elapsedTime;
                m_eventBus.Publish(
                    GameplayEvent{GameplayEventType::BouncePadActivated, m_playerEntity.id,
                                  m_bouncePadEntity.id, 12.5f, Point3D{1.8f, 0.0f, -0.3f}},
                    "BOUNCE PAD ACTIVADO! (SHAKE + DUCKING)",
                    0x80FF80FF,
                    2.4f
                );
            }
            if (auto* padTrans = m_ecsWorld.GetComponent<TransformComponent>(m_bouncePadEntity)) {
                const float timeSinceBounce = m_elapsedTime - m_lastBounceTime;
                const float pulse = (timeSinceBounce < 0.45f) ? (1.0f + 0.45f * std::sin(timeSinceBounce * 20.0f)) : 1.0f;
                padTrans->scale = Point3D{1.6f * pulse, 0.32f * pulse, 1.6f * pulse};
                padTrans->rotation.y += 1.2f * simDt;
                padTrans->MarkDirty();
            }
        }

        if (auto* obTrig = m_ecsWorld.GetComponent<TriggerComponent>(m_obeliskEntity)) {
            m_inGoalZone = obTrig->isInside;
            if (m_inGoalZone != m_lastAcousticInZone) {
                m_lastAcousticInZone = m_inGoalZone;
                const float targetReverb = m_inGoalZone ? 0.45f : 0.05f;
                AudioEngine3D::Instance().SetReverbWetMix(targetReverb);
                m_eventBus.Publish(
                    GameplayEvent{GameplayEventType::AcousticZoneChanged, m_playerEntity.id,
                                  m_obeliskEntity.id, targetReverb, Point3D{-6.2f, 0.0f, 0.2f}},
                    m_inGoalZone ? "ALTAR OBELISCO: REVERB SCHROEDER 45%" : "CAMPO ABIERTO: REVERB 5%",
                    m_inGoalZone ? 0xD090FFFF : 0xB0D8FFFF,
                    2.6f
                );
            }
            if (m_inGoalZone && m_collectedBeacons >= NUM_BEACONS && !m_levelCompleted) {
                m_levelCompleted = true;
                m_eventBus.Publish(
                    GameplayEvent{GameplayEventType::LevelGoalReached, m_playerEntity.id,
                                  m_obeliskEntity.id, static_cast<float>(NUM_BEACONS), Point3D{-6.2f, 0.0f, 0.2f}},
                    "VICTORIA! CAMARA EN RIEL SPLINE C1 ACTIVA",
                    0xFFD700FF,
                    4.0f
                );
            }
        }

        // 3e. Despacho desacoplado de eventos del GameplayEventBus
        GameplayEvent queuedEv{};
        while (m_eventBus.Poll(queuedEv)) {
            if (queuedEv.type == GameplayEventType::CollectiblePicked) {
                m_gameClock.TriggerHitStop(0.065f);
                m_orbitCamera.AddTrauma(0.35f);
                AudioEngine3D::Instance().PlaySound2D(
                    AudioEngine3D::Instance().GetFootstepClip(), 0.85f, false, 120, AudioBus::SFX);
                AudioManager::Instance().PlayClick();
            } else if (queuedEv.type == GameplayEventType::BouncePadActivated) {
                m_gameClock.TriggerHitStop(0.075f);
                m_orbitCamera.AddTrauma(0.60f);
                AudioEngine3D::Instance().PlaySound2D(
                    AudioEngine3D::Instance().GetFootstepClip(), 1.0f, false, 140, AudioBus::SFX);
                AudioManager::Instance().PlayClick();
            } else if (queuedEv.type == GameplayEventType::LevelGoalReached) {
                m_orbitCamera.AddTrauma(0.45f);
                m_splineCameraActive = true;
                m_orbitCamera.SetSplineRailActive(true);
                AudioManager::Instance().PlayClick();
            }
        }

        // 3f. Selección Automática de Animación Esquelética y Velocity Look-Ahead
        if (!m_animationClips.empty() && playerKcc) {
            int desiredClip = 2; // 2 = Mirar (Idle)
            if (m_levelCompleted && m_inGoalZone) {
                desiredClip = 1; // 1 = Piar
            } else if (!playerKcc->isGrounded || playerKcc->horizontalSpeed > 0.35f) {
                desiredClip = 0; // 0 = Salto / Carrera
            }
            const int safeClip = desiredClip % static_cast<int>(m_animationClips.size());
            if (safeClip != m_currentAnimClip) {
                m_currentAnimClip = safeClip;
                if (auto* anim = m_ecsWorld.GetComponent<AnimatorComponent>(m_playerEntity)) {
                    anim->Play(&m_animationClips[m_currentAnimClip], true);
                }
            }
        }

        if (playerTrans) {
            const Point3D vel = (playerTrans->position - m_prevPlayerPos) * (1.0f / simDt);
            m_orbitCamera.SetTargetVelocity(vel);
            m_prevPlayerPos = playerTrans->position;
        }

        AnimationSystem::Update(m_ecsWorld, simDt);
        TransformSystem::Update(m_ecsWorld);
    }

    // 4. Actualizar Cámara Orbital / Spring-Arm / Trauma / Spline siempre con realDt
    if (playerTrans) {
        m_orbitCamera.SetTarget(Point3D{
            playerTrans->position.x,
            playerTrans->position.y + 1.10f,
            playerTrans->position.z
        });
    }
    m_orbitCamera.Update(m_ecsWorld, realDt);

    // 5. Sincronizar Motor de Audio Espacial 3D (Doppler, Sidechain Ducking & Reverb)
    AudioSpatialSystem::Update(m_ecsWorld, simDt);
    AudioListener3D listener{};
    listener.position = playerTrans ? (playerTrans->position + Point3D{0.0f, 1.10f, 0.0f}) : m_orbitCamera.GetTarget();
    const Point3D camToTarget = (m_orbitCamera.GetTarget() - m_orbitCamera.GetPosition());
    Point3D flatFwd{camToTarget.x, 0.0f, camToTarget.z};
    listener.forward = (flatFwd.Length() > 0.001f) ? flatFwd.Normalized() : Point3D{0.0f, 0.0f, 1.0f};
    listener.up = Point3D{0.0f, 1.0f, 0.0f};
    listener.velocity = (simDt > 0.0f && playerKcc) ? playerKcc->velocity : Point3D{0.0f, 0.0f, 0.0f};
    listener.isSubmerged = false;
    AudioEngine3D::Instance().SetListener(listener);
}

void SceneCollisionTriggerDemoGX2::OnGameRender3D(ForwardRendererGX2& renderer) {
    GX2Texture* skyTex   = GetSceneTexture(m_hSkydomeTexture);
    GX2Texture* grassTex = GetSceneTexture(m_hGrassTexture);
    GX2Texture* houseTex = GetSceneTexture(m_hHouseTexture);
    GX2Texture* propsTex = GetSceneTexture(m_hPropsTexture);
    GX2Texture* birdTex  = GetSceneTexture(m_hBirdTexture);
    GX2Texture* whiteTex = GetSceneTexture(m_hWhiteTexture);
    GX2Texture* envCube  = GetSceneCubeMap();

    // 1. Skydome Hemisférico
    renderer.RenderSkydome(m_skydomeMesh, skyTex);

    // 2. Plano de Césped
    if (const auto* gTrans = m_ecsWorld.GetComponent<TransformComponent>(m_groundEntity)) {
        renderer.RenderStaticMesh(m_groundMesh, gTrans->worldMatrix, grassTex, nullptr, 0.0f, true, 0.8f, 0.42f);
    }

    // 3. Casa Holandesa + Tejado y Chimeneas (Obstáculo AABB)
    if (const auto* hTrans = m_ecsWorld.GetComponent<TransformComponent>(m_houseEntity)) {
        renderer.RenderStaticMesh(m_houseMesh, hTrans->worldMatrix, houseTex, envCube, 0.20f, true, 2.0f, 0.38f);
    }
    if (const auto* pTrans = m_ecsWorld.GetComponent<TransformComponent>(m_propsEntity)) {
        renderer.RenderStaticMesh(m_propsMesh, pTrans->worldMatrix, propsTex, envCube, 0.0f, true, 2.0f, 0.38f);
    }

    // 4. Pilares Clásicos (Obstáculos Esféricos)
    for (uint32_t i = 0; i < NUM_PILLARS; ++i) {
        if (const auto* pilTrans = m_ecsWorld.GetComponent<TransformComponent>(m_pillarEntities[i])) {
            const auto* pilCol = m_ecsWorld.GetComponent<ColliderComponent>(m_pillarEntities[i]);
            float amb = (pilCol && pilCol->isColliding) ? 0.72f : 0.40f;
            renderer.RenderStaticMesh(m_pillarMesh, pilTrans->worldMatrix, whiteTex, envCube, 0.08f, true, 1.5f, amb);
        }
    }

    // 5. Balizas de Energía (Triggers Collectibles)
    for (uint32_t i = 0; i < NUM_BEACONS; ++i) {
        if (const auto* bTrans = m_ecsWorld.GetComponent<TransformComponent>(m_beaconEntities[i])) {
            const auto* bTrig = m_ecsWorld.GetComponent<TriggerComponent>(m_beaconEntities[i]);
            float amb = (bTrig && bTrig->isCollected) ? 0.82f : 0.55f;
            renderer.RenderStaticMesh(m_beaconMesh, bTrans->worldMatrix, whiteTex, envCube, 0.35f, true, 1.2f, amb);
        }
    }

    // 6. Plataforma de Super-Salto (Trigger BouncePad)
    if (const auto* padTrans = m_ecsWorld.GetComponent<TransformComponent>(m_bouncePadEntity)) {
        renderer.RenderStaticMesh(m_beaconMesh, padTrans->worldMatrix, whiteTex, envCube, 0.45f, true, 1.5f, 0.68f);
    }

    // 7. Obelisco Sagrado (Zona de Meta + Zona de Reverberación)
    if (const auto* obTrans = m_ecsWorld.GetComponent<TransformComponent>(m_obeliskEntity)) {
        float obAmbient = m_levelCompleted ? 0.88f : (m_inGoalZone ? 0.70f : 0.45f);
        renderer.RenderStaticMesh(m_obeliskMesh, obTrans->worldMatrix, whiteTex, envCube, 0.30f, true, 1.8f, obAmbient);
    }

    // 7b. Dron Acústico Orbital (Audio 3D Doppler + Sidechain Ducking)
    if (const auto* dTrans = m_ecsWorld.GetComponent<TransformComponent>(m_dopplerDroneEntity)) {
        renderer.RenderStaticMesh(m_beaconMesh, dTrans->worldMatrix, whiteTex, envCube, 0.65f, true, 2.2f, 0.85f);
    }

    // 8. Personaje Jugador (Pájaro Naranja con Skinning LBS de 21 huesos)
    if (m_birdMesh.IsLoaded()) {
        const auto* pTrans   = m_ecsWorld.GetComponent<TransformComponent>(m_playerEntity);
        const auto* skelMesh = m_ecsWorld.GetComponent<SkeletalMeshComponent>(m_playerEntity);
        if (pTrans && skelMesh && skelMesh->IsValid()) {
            renderer.RenderSkinnedMesh(
                m_birdMesh,
                pTrans->worldMatrix,
                skelMesh->skinningMatrices,
                skelMesh->jointCount,
                birdTex,
                envCube,
                0.08f,
                false
            );
        }
    }
}

void SceneCollisionTriggerDemoGX2::OnGameRenderDebug3D() {
    if (!m_showDebugInfo) return;

    // 1. Dibujar todos los Colisionadores Sólidos usando DebugDraw3D desacoplado
    m_ecsWorld.ForEach<TransformComponent, ColliderComponent>(
        [&](Entity /*ent*/, const TransformComponent& trans, const ColliderComponent& col) {
            Point3D center = trans.position + col.localOffset;

            if (col.shape == ColliderShape::AABB) {
                ColorRGBA boxCol = col.isColliding ? ColorRGBA{255, 50, 50, 255} : ColorRGBA{255, 165, 30, 230};
                Point3D bMin = center - col.halfExtents;
                Point3D bMax = center + col.halfExtents;
                DebugDraw3D::DrawAABB(bMin, bMax, col.isColliding ? 3.2f : 2.2f, boxCol);
            } else if (col.shape == ColliderShape::Sphere) {
                if (col.isStatic) {
                    ColorRGBA pilCol = col.isColliding ? ColorRGBA{255, 60, 60, 255} : ColorRGBA{255, 190, 40, 220};
                    float w = col.isColliding ? 3.0f : 2.0f;
                    DebugDraw3D::DrawCylinder(trans.position, col.radius, 3.0f, w, pilCol, 16);
                } else {
                    ColorRGBA plyCol = col.isColliding ? ColorRGBA{255, 40, 40, 255} : ColorRGBA{50, 255, 90, 245};
                    DebugDraw3D::DrawSphere(center, col.radius, 2.5f, plyCol, 20);

                    if (col.isColliding) {
                        DebugDraw3D::DrawArrow(center, col.contactNormal, 1.6f, 4.0f, ColorRGBA::Yellow());
                    }
                }
            }
        }
    );

    // 2. Dibujar todos los Volúmenes Trigger usando DebugDraw3D desacoplado
    m_ecsWorld.ForEach<TransformComponent, TriggerComponent>(
        [&](Entity /*ent*/, const TransformComponent& trans, const TriggerComponent& trig) {
            if (!trig.isActive && trig.isCollected) {
                return;
            }
            Point3D center = trans.position + trig.localOffset;
            ColorRGBA trigCol = ColorRGBA::Cyan();
            if (trig.type == TriggerType::BouncePad) {
                trigCol = trig.isInside ? ColorRGBA::Green() : ColorRGBA{255, 70, 255, 240};
            } else if (trig.type == TriggerType::Zone) {
                trigCol = trig.isInside ? ColorRGBA::Green() : ColorRGBA{255, 215, 0, 240};
            } else {
                trigCol = ColorRGBA{0, 245, 255, 225};
            }

            DebugDraw3D::DrawCircleXZ(trans.position, trig.radius, 0.06f, 2.4f, trigCol, 20);
            DebugDraw3D::DrawCircleXZ(center, trig.radius * 0.85f, 0.0f, 1.8f, trigCol, 16);
        }
    );

    // 3. Dron Acústico Doppler y Riel Spline C^1 de Cámara (Cap. 15 / 17)
    if (const auto* dTrans = m_ecsWorld.GetComponent<TransformComponent>(m_dopplerDroneEntity)) {
        DebugDraw3D::DrawSphere(dTrans->position, 0.48f, 2.4f, ColorRGBA::Magenta(), 12);
        if (const auto* dAud = m_ecsWorld.GetComponent<AudioEmitterComponent>(m_dopplerDroneEntity)) {
            if (dAud->emitter.velocity.Length() > 0.1f) {
                DebugDraw3D::DrawArrow(dTrans->position, dAud->emitter.velocity.Normalized(), 1.8f, 3.0f, ColorRGBA::Cyan());
            }
        }
    }
    constexpr uint32_t SPLINE_SAMPLES = 18;
    for (uint32_t s = 0; s < SPLINE_SAMPLES; ++s) {
        const float t0 = static_cast<float>(s) / static_cast<float>(SPLINE_SAMPLES);
        const Point3D pt = m_orbitCamera.EvaluateSplineAt(t0);
        DebugDraw3D::DrawSphere(pt, 0.16f, 1.8f, m_splineCameraActive ? ColorRGBA::Yellow() : ColorRGBA{90, 210, 255, 190}, 8);
    }
}

void SceneCollisionTriggerDemoGX2::OnGameRenderHUD() {
#if defined(__WIIU__) || defined(__wiiu__)
    // 1. Barra Superior
    UI::UIToolkit::DrawPanel(s_topBarPanel);
    UI::UIToolkit::DrawButton(s_btnVolverTop, "< VOLVER", false, UI::TextScale::Normal);
    UI::UIToolkit::DrawLabel(140.0f, 22.0f, "COLISIONES SOLIDA SLIDE Y TRIGGERS ESPACIALES ECS (JUEGO 3D)", UI::TextScale::Normal, ColorRGBA::Cyan());

    // 2. Panel Izquierdo: Estado de Misión, Colisiones y Triggers
    UI::UIToolkit::DrawPanel(s_panelGame);
    UI::UIToolkit::DrawLabel(30.0f, 74.0f, "MISION Y COLISIONES EN VIVO", UI::TextScale::Normal, ColorRGBA::Yellow());

    StackString<64> beaconStr;
    beaconStr.Format("BALIZAS ENERGIA: %u / %u", m_collectedBeacons, NUM_BEACONS);
    ColorRGBA beaconCol = (m_collectedBeacons >= NUM_BEACONS) ? ColorRGBA::Green() : ColorRGBA::Cyan();
    UI::UIToolkit::DrawLabel(30.0f, 104.0f, beaconStr.c_str(), UI::TextScale::Normal, beaconCol);

    StackString<64> zoneStr;
    if (m_levelCompleted) {
        zoneStr.Format("META OBELISCO: MISION COMPLETADA!");
    } else if (m_inGoalZone) {
        zoneStr.Format("META OBELISCO: DENTRO (FALTAN %u)", NUM_BEACONS - m_collectedBeacons);
    } else {
        zoneStr.Format("META OBELISCO: %s", (m_collectedBeacons >= NUM_BEACONS) ? "VE AL OBELISCO!" : "FUERA DE ZONA");
    }
    ColorRGBA zoneCol = m_levelCompleted ? ColorRGBA::Green() : (m_inGoalZone ? ColorRGBA::Yellow() : ColorRGBA::White());
    UI::UIToolkit::DrawLabel(30.0f, 134.0f, zoneStr.c_str(), UI::TextScale::Normal, zoneCol);

    const auto* playerCol   = m_ecsWorld.GetComponent<ColliderComponent>(m_playerEntity);
    const auto* playerTrans = m_ecsWorld.GetComponent<TransformComponent>(m_playerEntity);
    const auto* playerKcc   = m_ecsWorld.GetComponent<KinematicCharacterComponent>(m_playerEntity);

    if (playerCol && playerCol->isColliding) {
        const char* hitTag = "OBSTACULO";
        if (const auto* hitCol = m_ecsWorld.GetComponent<ColliderComponent>(playerCol->collidedEntity)) {
            if (hitCol->tag) hitTag = hitCol->tag;
        }
        StackString<64> colStr;
        colStr.Format("SLIDE: %s", hitTag);
        UI::UIToolkit::DrawLabel(30.0f, 164.0f, colStr.c_str(), UI::TextScale::Normal, ColorRGBA::Orange());
    } else if ((m_elapsedTime - m_lastBounceTime) < 1.0f) {
        UI::UIToolkit::DrawLabel(30.0f, 164.0f, "TRIGGER: IMPULSO BOUNCE PAD!", UI::TextScale::Normal, ColorRGBA::Green());
    } else {
        UI::UIToolkit::DrawLabel(30.0f, 164.0f, "COLISION: LIBRE (SIN CONTACTO)", UI::TextScale::Normal, ColorRGBA{180, 220, 180, 255});
    }

    if (m_showDebugInfo && playerTrans && playerKcc) {
        StackString<64> posStr;
        posStr.Format("POS:(%.1f, %.1f, %.1f) | VEL:%.1fM/S",
                      playerTrans->position.x, playerTrans->position.y, playerTrans->position.z,
                      playerKcc->horizontalSpeed);
        UI::UIToolkit::DrawLabel(30.0f, 198.0f, posStr.c_str(), UI::TextScale::Small, ColorRGBA::Yellow());

        if (playerCol && playerCol->isColliding) {
            StackString<64> nrmStr;
            nrmStr.Format("MTV NORMAL:(%.2f, %.2f) PEN:%.3fM",
                          playerCol->contactNormal.x, playerCol->contactNormal.z, playerCol->penetrationDepth);
            UI::UIToolkit::DrawLabel(30.0f, 222.0f, nrmStr.c_str(), UI::TextScale::Small, ColorRGBA::Orange());
        } else {
            UI::UIToolkit::DrawLabel(30.0f, 222.0f, "CAPAS: PLAYER vs STATIC_WORLD + TRIGGERS", UI::TextScale::Small, ColorRGBA::Cyan());
        }
    }

    StackString<80> clockCamStr;
    clockCamStr.Format("RELOJ:%.2fX | ARM:%.2fM (%s) | SHAKE:%.2f",
                       m_gameClock.GetTimeScale(),
                       m_orbitCamera.GetEffectiveDistance(),
                       m_orbitCamera.IsSpringArmCompressing() ? "COLL" : "LIB",
                       m_orbitCamera.GetTrauma());
    UI::UIToolkit::DrawLabel(30.0f, 250.0f, clockCamStr.c_str(), UI::TextScale::Small, ColorRGBA{170, 255, 220, 255});

    auto& audio3D = AudioEngine3D::Instance();
    StackString<80> audioStr;
    audioStr.Format("DOPPLER:%.2fX | DUCK:%.2f | REV:%u%% | BUS:%u EV",
                    audio3D.GetLastDopplerPitch(),
                    audio3D.GetActiveDuckingGain(),
                    static_cast<unsigned>(audio3D.GetReverbWetMix() * 100.0f + 0.5f),
                    m_eventBus.GetTotalPublished());
    UI::UIToolkit::DrawLabel(30.0f, 276.0f, audioStr.c_str(), UI::TextScale::Small, ColorRGBA{255, 210, 140, 255});

    // 2b. Banner Central de Notificaciones Toast del GameplayEventBus (Cap. 18)
    const auto& toasts = m_eventBus.GetToasts();
    float toastY = 72.0f;
    for (const auto& toast : toasts) {
        if (toast.timeRemaining > 0.0f && toast.message[0] != '\0') {
            const uint32_t c = toast.colorRGBA;
            ColorRGBA tCol{
                static_cast<uint8_t>((c >> 24) & 0xFF),
                static_cast<uint8_t>((c >> 16) & 0xFF),
                static_cast<uint8_t>((c >> 8)  & 0xFF),
                255
            };
            UI::UIToolkit::DrawLabel(422.0f, toastY, toast.message.data(), UI::TextScale::Small, tCol);
            toastY += 22.0f;
        }
    }

    // 3. Panel Derecho: Telemetría Hardware y ForwardRendererGX2
    UI::UIToolkit::DrawPanel(s_panelTelem);
    UI::UIToolkit::DrawLabel(944.0f, 74.0f, "TELEMETRIA SILICIO LATTE", UI::TextScale::Normal, ColorRGBA::Yellow());

    float currentFps = GetFPS();
    float currentFrameTime = GetFrameTimeMs();

    StackString<64> fpsLabel;
    fpsLabel.Format("FPS: %.1f (%.2f ms)", currentFps, currentFrameTime);
    UI::UIToolkit::DrawLabel(944.0f, 104.0f, fpsLabel.c_str(), UI::TextScale::Normal, (currentFps >= 58.0f) ? ColorRGBA::Green() : ColorRGBA::Red());

    auto& renderer = ForwardRendererGX2::Instance();
    StackString<64> drawLabel;
    drawLabel.Format("DRAWS: %u | TRIS: %u", renderer.GetDrawCallsCount(), renderer.GetTrianglesCount());
    UI::UIToolkit::DrawLabel(944.0f, 134.0f, drawLabel.c_str(), UI::TextScale::Normal, ColorRGBA::White());

    const auto& metrics = ResourceMonitor::Instance().GetMetrics();
    float mem1MB = static_cast<float>(metrics.mem1Bytes) / (1024.0f * 1024.0f);
    float mem2MB = static_cast<float>(metrics.mem2Bytes) / (1024.0f * 1024.0f);
    StackString<64> memLabel;
    memLabel.Format("MEM1: %.1fMB | MEM2: %.1fMB", mem1MB, mem2MB);
    UI::UIToolkit::DrawLabel(944.0f, 164.0f, memLabel.c_str(), UI::TextScale::Normal, ColorRGBA::Cyan());

    UI::UIToolkit::DrawLabel(944.0f, 198.0f, "RENDERER: FORWARD GX2 MODULAR", UI::TextScale::Small, ColorRGBA{180, 220, 255, 255});
    UI::UIToolkit::DrawLabel(944.0f, 222.0f, "FISICA: 0 HEAP ALLOCS (60Hz)", UI::TextScale::Small, ColorRGBA::Green());

    // Botones de Control Hito 133 (Cap. 7 / 17) y Zoom Rápido
    UI::UIToolkit::DrawToggleButton(s_btnBulletTime, m_bulletTimeActive ? "BULLET: 0.25X" : "TIEMPO: 1.0X", m_bulletTimeActive, false, UI::TextScale::Small);
    UI::UIToolkit::DrawToggleButton(s_btnSplineCam, m_splineCameraActive ? "CAM: RIEL C1" : "CAM: ORBITA", m_splineCameraActive, false, UI::TextScale::Small);
    UI::UIToolkit::DrawButton(s_btnSingleStep, "STEP +1 FRAME", false, UI::TextScale::Small);

    UI::UIToolkit::DrawButton(s_btnZoomIn, "ZOOM+", false, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnZoomOut, "ZOOM-", false, UI::TextScale::Normal);

    // 4. Barra Inferior de Botones Táctiles Interactivos
    UI::UIToolkit::DrawPanel(s_bottomBarPanel);
    UI::UIToolkit::DrawButton(s_btnJump, "SALTAR", false, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnSuperJump, "SUPER SALTO", (m_elapsedTime - m_lastBounceTime) < 0.5f, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnResetLevel, "REINICIAR NIVEL", false, UI::TextScale::Normal);
    UI::UIToolkit::DrawToggleButton(s_btnDebugToggle, m_showDebugInfo ? "DEBUG: SI" : "DEBUG: NO", m_showDebugInfo, false, UI::TextScale::Normal);
    UI::UIToolkit::DrawToggleButton(s_btnPause, m_isPaused ? "PAUSA: SI" : "PAUSA: NO", m_isPaused, false, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnResetCam, "RESET CAM", false, UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(s_btnVolverBot, "SALIR", false, UI::TextScale::Normal);

    // 5. Barras Inferiores de Guía y Telemetría
    UI::UIToolkit::DrawGuideBar("L-STICK: MOVER | A/B: SALTAR | X: BULLET-TIME | L: RIEL CAM | -: PAUSA | R(PAUSA): +1 STEP | R-STICK: CAM", 636.0f, UI::TextScale::Small);

    StackString<256> telemFooter;
    telemFooter.Format("FPS:%.1f | BALIZAS:%u/5 | CAP.7 CLOCK:%.2fX | CAP.15 DOPPLER:%.2fX REV:%u%% | CAP.17 ARM:%.1fM | CAP.18 EV:%u",
                       currentFps, m_collectedBeacons,
                       m_gameClock.GetTimeScale(),
                       audio3D.GetLastDopplerPitch(),
                       static_cast<unsigned>(audio3D.GetReverbWetMix() * 100.0f + 0.5f),
                       m_orbitCamera.GetEffectiveDistance(),
                       m_eventBus.GetTotalPublished());
    UI::UIToolkit::DrawTelemetryBar(telemFooter.c_str(), 672.0f, UI::TextScale::Small);
#endif
}

} // namespace WiiU
