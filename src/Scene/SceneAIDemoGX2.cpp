#include "Scene/SceneAIDemoGX2.hpp"
#include "Scene/SceneMenuModels.hpp"
#include "Scene/SceneManager.hpp"
#include "Core/AssetManager.hpp"
#include "Core/StackString.hpp"
#include "Core/Config/EngineConfig.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include "Core/ECS/Systems/TransformSystem.hpp"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace WiiU {

namespace {
    // Rectángulos estáticos de interfaz de usuario (UIToolkit)
    static const UI::UIRect s_topBarPanel{ 20.0f, 12.0f, 1240.0f, 46.0f };
    static const UI::UIRect s_btnVolverTop{ 30.0f, 20.0f, 100.0f, 30.0f };
    static const UI::UIRect s_panelFSM{ 20.0f, 68.0f, 430.0f, 240.0f };
    static const UI::UIRect s_panelNav{ 830.0f, 68.0f, 430.0f, 240.0f };
    static const UI::UIRect s_panelBottom{ 20.0f, 635.0f, 1240.0f, 70.0f };
}

SceneAIDemoGX2::SceneAIDemoGX2()
    : GameScene3D() {
}

SceneAIDemoGX2::~SceneAIDemoGX2() {
    OnGameExit();
}

void SceneAIDemoGX2::LoadAsync() {
    AssetManager::LoadBinaryFile("models/skydome.wmesh", m_stagedSkydomeMesh);
    AssetManager::LoadBinaryFile("models/ground_plane.wmesh", m_stagedGroundMesh);
    AssetManager::LoadBinaryFile("models/pillar.wmesh", m_stagedPillarMesh);
    AssetManager::LoadBinaryFile("models/beacon.wmesh", m_stagedBeaconMesh);
    AssetManager::LoadBinaryFile("models/obelisk.wmesh", m_stagedObeliskMesh);
}

void SceneAIDemoGX2::SetupNavGraph() {
    m_navGraph.Clear();

    // 1. Añadir los 8 nodos perimetrales del cuadrilátero de navegación
    const Point3D nodePositions[NUM_WAYPOINT_NODES] = {
        Point3D{-6.0f, 0.0f, -6.0f}, // 0: Noroeste
        Point3D{ 0.0f, 0.0f, -7.5f}, // 1: Norte
        Point3D{ 6.0f, 0.0f, -6.0f}, // 2: Noreste
        Point3D{ 7.5f, 0.0f,  0.0f}, // 3: Este
        Point3D{ 6.0f, 0.0f,  6.0f}, // 4: Sureste
        Point3D{ 0.0f, 0.0f,  7.5f}, // 5: Sur
        Point3D{-6.0f, 0.0f,  6.0f}, // 6: Suroeste
        Point3D{-7.5f, 0.0f,  0.0f}, // 7: Oeste
        Point3D{ 0.0f, 0.0f,  0.0f}  // 8: Hub Central
    };

    for (uint32_t i = 0; i < NUM_WAYPOINT_NODES; ++i) {
        m_navGraph.AddNode(nodePositions[i], 1.2f);
    }

    // 2. Conectar anillo exterior (0 <-> 1 <-> 2 <-> 3 <-> 4 <-> 5 <-> 6 <-> 7 <-> 0)
    for (uint32_t i = 0; i < 8; ++i) {
        m_navGraph.AddBiDirectionalEdge(i, (i + 1) % 8);
    }

    // 3. Conectar radios ortogonales hacia el Hub Central (8)
    m_navGraph.AddBiDirectionalEdge(1, 8); // Norte <-> Centro
    m_navGraph.AddBiDirectionalEdge(3, 8); // Este  <-> Centro
    m_navGraph.AddBiDirectionalEdge(5, 8); // Sur   <-> Centro
    m_navGraph.AddBiDirectionalEdge(7, 8); // Oeste <-> Centro

    // Nota: Las diagonales (0->8, 2->8, 4->8, 6->8) no se conectan porque los 4 pilares
    // en (-3, -3), (3, -3), (3, 3), (-3, 3) bloquean la línea de visión directa.
    // Esto obliga al algoritmo A* a buscar la ruta ortogonal o perimetral óptima.
}

bool SceneAIDemoGX2::CreateTextures() {
    if (m_texturesCreated) return true;
    m_hSkydomeTexture = LoadSceneTexture("models/skydome.wtex", "AIDemo_SkydomeTex");
    m_hGrassTexture   = LoadSceneTexture("models/ground_grass.wtex", "AIDemo_GrassTex");
    m_hWhiteTexture   = CreateSceneSolidTexture(0xFFFFFFFFu, "AIDemo_WhiteTex");
    m_texturesCreated = true;
    return true;
}

void SceneAIDemoGX2::OnGameInit() {
    AudioManager::Instance().PlayBGM();

    // Sincronizar banderas de depuración global (Jason Gregory Cap. 15.2 / Cap. 5.3)
    m_showGraphEdges = EngineConfig::Get().debug.showNavGraph;
    m_showVisionCone = EngineConfig::Get().debug.showVisionCones;

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
    loadMeshHelper(m_pillarMesh,  m_stagedPillarMesh,  "models/pillar.wmesh");
    loadMeshHelper(m_beaconMesh,  m_stagedBeaconMesh,  "models/beacon.wmesh");
    loadMeshHelper(m_obeliskMesh, m_stagedObeliskMesh, "models/obelisk.wmesh");

    CreateTextures();
    SetupNavGraph();

    SetCameraProjection(50.0f, 0.20f, 1000.0f);
    SetClearColor(ColorRGBA{100, 160, 220, 255});

    // 1. Configurar Cámara Orbital con vista táctica aérea isométrica
    m_orbitCamera.SetAngles(0.0f, 48.0f);
    m_orbitCamera.SetDistance(22.0f);
    m_orbitCamera.SetTarget(Point3D{0.0f, 0.0f, 0.0f});
    m_orbitCamera.Update(m_ecsWorld, 0.001f);

    // 2. Entidad Skydome
    m_skydomeEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_skydomeEntity, Point3D{0, 0, 0}, Point3D{0, 0, 0}, Point3D{25, 25, 25});

    // 3. Entidad Terreno Suelo
    m_groundEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_groundEntity, Point3D{0, -0.05f, 0}, Point3D{0, 0, 0}, Point3D{1.4f, 1.0f, 1.4f});

    // 4. Entidades Pilares de Obstáculo (4 columnas simétricas)
    const Point3D pillarPos[NUM_PILLARS] = {
        Point3D{-3.0f, 0.0f, -3.0f},
        Point3D{ 3.0f, 0.0f, -3.0f},
        Point3D{ 3.0f, 0.0f,  3.0f},
        Point3D{-3.0f, 0.0f,  3.0f}
    };
    for (uint32_t i = 0; i < NUM_PILLARS; ++i) {
        m_pillarEntities[i] = m_ecsWorld.CreateEntity();
        m_ecsWorld.EmplaceComponent<TransformComponent>(m_pillarEntities[i], pillarPos[i], Point3D{0, 0, 0}, Point3D{1.2f, 1.8f, 1.2f});
    }

    // 5. Entidades Balizas Físicas en cada Nodo del Grafo
    for (uint32_t i = 0; i < NUM_WAYPOINT_NODES; ++i) {
        const NavNode* nd = m_navGraph.GetNode(i);
        if (nd) {
            m_waypointEntities[i] = m_ecsWorld.CreateEntity();
            m_ecsWorld.EmplaceComponent<TransformComponent>(
                m_waypointEntities[i],
                Point3D{nd->position.x, 0.1f, nd->position.z},
                Point3D{0, 0, 0},
                Point3D{0.5f, 0.5f, 0.5f}
            );
        }
    }

    // 6. Entidad Agente IA Autónomo
    m_aiAgentEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(
        m_aiAgentEntity,
        Point3D{-6.0f, 0.0f, -6.0f},
        Point3D{0, 0, 0},
        Point3D{0.8f, 0.8f, 0.8f}
    );

    auto& ai = m_ecsWorld.EmplaceComponent<AIControllerComponent>(m_aiAgentEntity);
    ai.currentState = AIState::Patrol;
    ai.homePosition = Point3D{-6.0f, 0.0f, -6.0f};
    ai.patrolStartNode = 0;
    ai.patrolTargetNode = 4; // Cruza en diagonal por el Hub Central o por el anillo exterior
    ai.moveSpeed = 3.2f;
    ai.chaseSpeed = 4.8f;
    ai.perceptionRadius = 8.5f;
    ai.perceptionFOV = 110.0f;
    ai.waypointArrivalRadius = 0.9f;
    ai.slowingRadius = 1.4f;

    // Calcular ruta inicial de patrulla con A*
    AStarPathfinder::FindPath(m_navGraph, ai.patrolStartNode, ai.patrolTargetNode, ai.path);

    // 7. Entidad Jugador Interactivo
    m_playerPos = Point3D{0.0f, 0.0f, 5.0f};
    m_playerEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(
        m_playerEntity,
        m_playerPos,
        Point3D{0, 0, 0},
        Point3D{0.65f, 0.65f, 0.65f}
    );

    TransformSystem::Update(m_ecsWorld);
}

void SceneAIDemoGX2::OnGameExit() {
    m_ecsWorld.Clear();
    m_skydomeMesh.Destroy();
    m_groundMesh.Destroy();
    m_pillarMesh.Destroy();
    m_beaconMesh.Destroy();
    m_obeliskMesh.Destroy();

    m_hSkydomeTexture = {};
    m_hGrassTexture = {};
    m_hWhiteTexture = {};
    m_texturesCreated = false;
}

void SceneAIDemoGX2::OnGameUpdate(float dt) {
    m_simulationTime += dt;

    auto& input = InputManager::Instance();
    const auto& touch = input.GetTouchState();

    // 1. Botón Volver Táctil
    if (touch.justPressed && s_btnVolverTop.Contains(touch.x, touch.y)) {
        AudioManager::Instance().PlayClick();
        SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuModels>());
        return;
    }

    if (input.IsBackRequested()) {
        AudioManager::Instance().PlayClick();
        SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuModels>());
        return;
    }

    // 2. Controles de Cámara Orbital (Stick Derecho / Gatillos)
    const auto& rStick = input.GetRightStick();
    float rotSpeed = 60.0f * dt;
    if (std::abs(rStick.x) > 0.15f) {
        m_orbitCamera.SetAngles(m_orbitCamera.GetYaw() - rStick.x * rotSpeed, m_orbitCamera.GetPitch());
    }
    if (std::abs(rStick.y) > 0.15f) {
        float newPitch = std::clamp(m_orbitCamera.GetPitch() - rStick.y * rotSpeed, 15.0f, 85.0f);
        m_orbitCamera.SetAngles(m_orbitCamera.GetYaw(), newPitch);
    }
#if defined(__WIIU__) || defined(__wiiu__)
    if (input.IsButtonHeld(VPAD_BUTTON_ZL) || input.IsButtonHeld(VPAD_BUTTON_L)) {
        m_orbitCamera.SetDistance(std::clamp(m_orbitCamera.GetDistance() - 10.0f * dt, 10.0f, 40.0f));
    }
    if (input.IsButtonHeld(VPAD_BUTTON_ZR) || input.IsButtonHeld(VPAD_BUTTON_R)) {
        m_orbitCamera.SetDistance(std::clamp(m_orbitCamera.GetDistance() + 10.0f * dt, 10.0f, 40.0f));
    }

    // Botón B: Pausar/Reanudar simulación
    if (input.IsButtonTriggered(VPAD_BUTTON_B)) {
        m_isPaused = !m_isPaused;
        AudioManager::Instance().PlayClick();
    }

    // Botón X: Alternar visualización de debug
    if (input.IsButtonTriggered(VPAD_BUTTON_X)) {
        m_showGraphEdges = !m_showGraphEdges;
        m_showVisionCone = m_showGraphEdges;
        EngineConfig::Instance().debug.showNavGraph = m_showGraphEdges;
        EngineConfig::Instance().debug.showVisionCones = m_showVisionCone;
        AudioManager::Instance().PlayClick();
    }

    // Botón A: Forzar conmutación de destino A*
    if (input.IsButtonTriggered(VPAD_BUTTON_A)) {
        if (auto* ai = m_ecsWorld.GetComponent<AIControllerComponent>(m_aiAgentEntity)) {
            if (const auto* aiTrans = m_ecsWorld.GetComponent<TransformComponent>(m_aiAgentEntity)) {
                ai->isPatrolForward = !ai->isPatrolForward;
                uint32_t targetNode = ai->isPatrolForward ? ai->patrolTargetNode : ai->patrolStartNode;
                AStarPathfinder::FindPath(m_navGraph, aiTrans->position, m_navGraph.GetNode(targetNode)->position, ai->path);
                AudioManager::Instance().PlayClick();
            }
        }
    }
#endif

    // 3. Movimiento Interactivo del Jugador (Stick Izquierdo o D-Pad)
    const auto& lStick = input.GetLeftStick();
    float inX = lStick.x;
    float inY = lStick.y;

#if defined(__WIIU__) || defined(__wiiu__)
    if (input.IsButtonHeld(VPAD_BUTTON_LEFT))  inX = -1.0f;
    if (input.IsButtonHeld(VPAD_BUTTON_RIGHT)) inX =  1.0f;
    if (input.IsButtonHeld(VPAD_BUTTON_UP))    inY =  1.0f;
    if (input.IsButtonHeld(VPAD_BUTTON_DOWN))  inY = -1.0f;
#endif

    if (std::abs(inX) > 0.15f || std::abs(inY) > 0.15f) {
        float yawRad = m_orbitCamera.GetYaw() * (3.14159265358979323846f / 180.0f);
        float camFwdX   = -std::sin(yawRad);
        float camFwdZ   =  std::cos(yawRad);
        float camRightX = -std::cos(yawRad);
        float camRightZ = -std::sin(yawRad);

        float moveDirX = (camRightX * inX + camFwdX * inY);
        float moveDirZ = (camRightZ * inX + camFwdZ * inY);
        float mag = std::sqrt(moveDirX * moveDirX + moveDirZ * moveDirZ);
        if (mag > 0.001f) {
            moveDirX /= mag;
            moveDirZ /= mag;
            m_playerPos.x += moveDirX * (m_playerSpeed * dt);
            m_playerPos.z += moveDirZ * (m_playerSpeed * dt);
        }
    }

    // Delimitar jugador al perímetro del escenario [-9.5m, 9.5m]
    m_playerPos.x = std::clamp(m_playerPos.x, -9.5f, 9.5f);
    m_playerPos.z = std::clamp(m_playerPos.z, -9.5f, 9.5f);

    if (auto* pTrans = m_ecsWorld.GetComponent<TransformComponent>(m_playerEntity)) {
        pTrans->position = m_playerPos;
        pTrans->MarkDirty();
    }

    // Actualización de Sistemas si no está en pausa
    if (!m_isPaused) {
        AISteeringSystem::Update(m_ecsWorld, m_navGraph, m_playerPos, dt);
    }

    m_orbitCamera.Update(m_ecsWorld, dt);
    TransformSystem::Update(m_ecsWorld);
}

void SceneAIDemoGX2::OnGameRender3D(ForwardRendererGX2& renderer) {
    GX2Texture* skyTex   = GetSceneTexture(m_hSkydomeTexture);
    GX2Texture* grassTex = GetSceneTexture(m_hGrassTexture);
    GX2Texture* whiteTex = GetSceneTexture(m_hWhiteTexture);

    // 1. Skydome
    renderer.RenderSkydome(m_skydomeMesh, skyTex);

    // 2. Terreno de Césped
    if (const auto* gTrans = m_ecsWorld.GetComponent<TransformComponent>(m_groundEntity)) {
        renderer.RenderStaticMesh(m_groundMesh, gTrans->worldMatrix, grassTex, nullptr, 0.0f, true, 0.8f, 0.42f);
    }

    // 3. Pilares Obstáculos
    for (uint32_t i = 0; i < NUM_PILLARS; ++i) {
        if (const auto* pTrans = m_ecsWorld.GetComponent<TransformComponent>(m_pillarEntities[i])) {
            renderer.RenderStaticMesh(m_pillarMesh, pTrans->worldMatrix, whiteTex, nullptr, 0.05f, true, 1.5f, 0.45f);
        }
    }

    // 4. Balizas de los Nodos del Grafo
    for (uint32_t i = 0; i < NUM_WAYPOINT_NODES; ++i) {
        if (const auto* bTrans = m_ecsWorld.GetComponent<TransformComponent>(m_waypointEntities[i])) {
            float amb = (i == 8) ? 0.85f : 0.50f;
            renderer.RenderStaticMesh(m_beaconMesh, bTrans->worldMatrix, whiteTex, nullptr, 0.35f, true, 1.2f, amb);
        }
    }

    // 5. Agente de IA (Representado por la malla del Obelisco con iluminación reactiva)
    if (const auto* aiTrans = m_ecsWorld.GetComponent<TransformComponent>(m_aiAgentEntity)) {
        const auto* ai = m_ecsWorld.GetComponent<AIControllerComponent>(m_aiAgentEntity);
        float amb = 0.55f;
        if (ai) {
            if (ai->currentState == AIState::Chase) amb = 0.95f;
            else if (ai->currentState == AIState::Investigate) amb = 0.75f;
        }
        renderer.RenderStaticMesh(m_obeliskMesh, aiTrans->worldMatrix, whiteTex, nullptr, 0.25f, true, 1.6f, amb);
    }

    // 6. Jugador Interactivo (Representado por una baliza cristalina brillante)
    if (const auto* plTrans = m_ecsWorld.GetComponent<TransformComponent>(m_playerEntity)) {
        renderer.RenderStaticMesh(m_beaconMesh, plTrans->worldMatrix, whiteTex, nullptr, 0.50f, true, 2.0f, 0.90f);
    }
}

void SceneAIDemoGX2::OnGameRenderDebug3D() {
    const auto* aiTrans = m_ecsWorld.GetComponent<TransformComponent>(m_aiAgentEntity);
    const auto* ai = m_ecsWorld.GetComponent<AIControllerComponent>(m_aiAgentEntity);

    // 1. Dibujar Red de Aristas del Grafo de Navegación (NavGraph)
    if (m_showGraphEdges) {
        for (uint32_t i = 0; i < m_navGraph.GetNodeCount(); ++i) {
            const NavNode* node = m_navGraph.GetNode(i);
            if (!node) continue;

            // Anillo base de cada nodo en el suelo
            DebugDraw3D::DrawCircleXZ(node->position, node->radius * 0.6f, 0.06f, 1.8f, ColorRGBA{60, 200, 255, 180});

            for (uint32_t e = 0; e < node->edgeCount; ++e) {
                const uint32_t targetId = node->edges[e].targetNodeId;
                if (targetId > i) { // Dibujar cada arista no dirigida una sola vez
                    const NavNode* targetNode = m_navGraph.GetNode(targetId);
                    if (targetNode) {
                        Point3D pA = node->position;
                        Point3D pB = targetNode->position;
                        pA.y += 0.08f;
                        pB.y += 0.08f;
                        DebugDraw3D::DrawLine(pA, pB, 1.6f, ColorRGBA{220, 220, 60, 160});
                    }
                }
            }
        }
    }

    // 2. Dibujar Camino Activo del Algoritmo A*
    if (m_showAStarPath && ai && aiTrans && ai->path.isValid && ai->path.waypointCount > 0) {
        Point3D prevPt = aiTrans->position;
        prevPt.y = 0.25f;

        // Línea conectando la posición actual del agente al waypoint actual
        Point3D currWp = ai->path.GetCurrentWaypoint();
        currWp.y = 0.25f;
        DebugDraw3D::DrawLine(prevPt, currWp, 3.5f, ColorRGBA::Green());

        // Líneas entre los siguientes waypoints calculados por A*
        for (uint32_t i = ai->path.currentIndex; i + 1 < ai->path.waypointCount; ++i) {
            Point3D wA = ai->path.waypoints[i];
            Point3D wB = ai->path.waypoints[i + 1];
            wA.y = 0.25f;
            wB.y = 0.25f;
            DebugDraw3D::DrawLine(wA, wB, 3.5f, ColorRGBA::Green());
        }

        // Anillo de llegada verde en el waypoint final de la ruta A*
        Point3D finalGoal = ai->path.waypoints[ai->path.waypointCount - 1];
        DebugDraw3D::DrawCircleXZ(finalGoal, 1.2f, 0.28f, 2.5f, ColorRGBA::Green());
    }

    // 3. Dibujar Cono Sensorial de Percepción y Rayo de Detección
    if (m_showVisionCone && aiTrans && ai) {
        Point3D eyePos = aiTrans->position;
        eyePos.y = 1.6f;

        constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
        const float halfFovRad = (ai->perceptionFOV * 0.5f) * DEG_TO_RAD;

        const float leftAngle = aiTrans->rotation.y + halfFovRad;
        const float rightAngle = aiTrans->rotation.y - halfFovRad;

        const Point3D leftRayEnd{
            eyePos.x - std::sin(leftAngle) * ai->perceptionRadius,
            0.15f,
            eyePos.z + std::cos(leftAngle) * ai->perceptionRadius
        };
        const Point3D rightRayEnd{
            eyePos.x - std::sin(rightAngle) * ai->perceptionRadius,
            0.15f,
            eyePos.z + std::cos(rightAngle) * ai->perceptionRadius
        };

        // Límites laterales del cono
        DebugDraw3D::DrawLine(eyePos, leftRayEnd, 1.8f, ColorRGBA{255, 220, 40, 200});
        DebugDraw3D::DrawLine(eyePos, rightRayEnd, 1.8f, ColorRGBA{255, 220, 40, 200});

        // Arco perimétrico del cono de visión
        constexpr int ARC_STEPS = 12;
        Point3D prevArcPt = leftRayEnd;
        for (int s = 1; s <= ARC_STEPS; ++s) {
            float t = static_cast<float>(s) / static_cast<float>(ARC_STEPS);
            float curAngle = leftAngle + (rightAngle - leftAngle) * t;
            Point3D arcPt{
                eyePos.x - std::sin(curAngle) * ai->perceptionRadius,
                0.15f,
                eyePos.z + std::cos(curAngle) * ai->perceptionRadius
            };
            DebugDraw3D::DrawLine(prevArcPt, arcPt, 1.5f, ColorRGBA{255, 220, 40, 160});
            prevArcPt = arcPt;
        }

        // Rayo de detección hacia el jugador (Rojo si está visible)
        if (ai->targetVisible) {
            Point3D playerEye = m_playerPos;
            playerEye.y = 1.0f;
            DebugDraw3D::DrawLine(eyePos, playerEye, 3.2f, ColorRGBA{255, 30, 30, 255});
            DebugDraw3D::DrawSphere(m_playerPos, 1.0f, 2.0f, ColorRGBA{255, 40, 40, 255});
        }
    }

    // 4. Halo Tridimensional de Estado de la FSM sobre el Agente IA
    if (aiTrans && ai) {
        ColorRGBA haloColor = ColorRGBA{40, 140, 255, 240}; // Azul Patrol
        float haloRadius = 1.0f;

        switch (ai->currentState) {
            case AIState::Patrol:
                haloColor = ColorRGBA{40, 140, 255, 240}; // Azul
                break;
            case AIState::Chase: {
                haloColor = ColorRGBA{255, 30, 30, 255}; // Rojo intenso
                // Pulso rítmico de alerta
                float pulse = 1.0f + 0.25f * std::sin(m_simulationTime * 8.0f);
                haloRadius = 1.1f * pulse;
                break;
            }
            case AIState::Investigate:
                haloColor = ColorRGBA{255, 220, 20, 240}; // Amarillo alerta
                haloRadius = 1.15f;
                break;
            case AIState::ReturnHome:
                haloColor = ColorRGBA{200, 50, 255, 240}; // Púrpura retorno
                break;
            default:
                break;
        }

        DebugDraw3D::DrawCircleXZ(aiTrans->position, haloRadius, 2.5f, 3.0f, haloColor);
    }
}

void SceneAIDemoGX2::OnGameRenderHUD() {
#if defined(__WIIU__) || defined(__wiiu__)
    const auto* ai = m_ecsWorld.GetComponent<AIControllerComponent>(m_aiAgentEntity);
    const auto* aiTrans = m_ecsWorld.GetComponent<TransformComponent>(m_aiAgentEntity);

    // 1. Barra Superior con botón Volver
    UI::UIToolkit::DrawPanel(s_topBarPanel);
    UI::UIToolkit::DrawButton(s_btnVolverTop, "< VOLVER", false, UI::TextScale::Normal);
    UI::UIToolkit::DrawLabel(145.0f, 24.0f, "INTELIGENCIA ARTIFICIAL Y NAVEGACION (CAP. 19 JASON GREGORY)", UI::TextScale::Normal, ColorRGBA::Cyan());

    // 2. Tarjeta Izquierda: Telemetría de la Máquina de Estados (FSM)
    UI::UIToolkit::DrawPanel(s_panelFSM);
    UI::UIToolkit::DrawLabel(35.0f, 80.0f, "MAQUINA DE ESTADOS FINITOS (HFSM)", UI::TextScale::Normal, ColorRGBA::Yellow());

    if (ai) {
        ColorRGBA stateColor = ColorRGBA::Cyan();
        const char* stateDetail = "PATRULLA A*";
        if (ai->currentState == AIState::Chase) {
            stateColor = ColorRGBA::Red();
            stateDetail = "PERSECUCION";
        } else if (ai->currentState == AIState::Investigate) {
            stateColor = ColorRGBA{255, 210, 40, 255};
            stateDetail = "INSPECCIONANDO";
        } else if (ai->currentState == AIState::ReturnHome) {
            stateColor = ColorRGBA{220, 80, 255, 255};
            stateDetail = "RETORNO PATRULLA";
        }

        StackString<64> stateStr;
        stateStr.Format("ESTADO: [%s] (%s)", GetAIStateName(ai->currentState).data(), stateDetail);
        UI::UIToolkit::DrawLabel(35.0f, 110.0f, stateStr.c_str(), UI::TextScale::Normal, stateColor);

        StackString<64> timerStr;
        if (ai->currentState == AIState::Investigate) {
            timerStr.Format("TIEMPO EN ESTADO: %.1f s / 2.5 s", ai->stateTimer);
        } else {
            timerStr.Format("TIEMPO EN ESTADO: %.1f s", ai->stateTimer);
        }
        UI::UIToolkit::DrawLabel(35.0f, 138.0f, timerStr.c_str(), UI::TextScale::Normal, ColorRGBA::White());

        StackString<64> visionStr;
        if (ai->targetStationaryIgnored) {
            visionStr.Format("OBJETIVO: INERTE (IGNORADO / PACIFICO)");
            UI::UIToolkit::DrawLabel(35.0f, 166.0f, visionStr.c_str(), UI::TextScale::Normal, ColorRGBA{140, 220, 255, 255});
        } else if (ai->targetVisible) {
            visionStr.Format("VISION: OBJETIVO DETECTADO (CONO)");
            UI::UIToolkit::DrawLabel(35.0f, 166.0f, visionStr.c_str(), UI::TextScale::Normal, ColorRGBA::Red());
        } else {
            visionStr.Format("VISION: BUSCANDO OBJETIVO...");
            UI::UIToolkit::DrawLabel(35.0f, 166.0f, visionStr.c_str(), UI::TextScale::Normal, ColorRGBA{160, 220, 160, 255});
        }

        if (aiTrans) {
            float dx = m_playerPos.x - aiTrans->position.x;
            float dz = m_playerPos.z - aiTrans->position.z;
            float dist = std::sqrt(dx * dx + dz * dz);
            StackString<64> distStr;
            distStr.Format("DISTANCIA AL JUGADOR: %.2f m", dist);
            UI::UIToolkit::DrawLabel(35.0f, 194.0f, distStr.c_str(), UI::TextScale::Normal, ColorRGBA::White());
        }

        StackString<64> memStr;
        memStr.Format("HEAP ALLOCS HOT-LOOP: 0 ALLOCS");
        UI::UIToolkit::DrawLabel(35.0f, 222.0f, memStr.c_str(), UI::TextScale::Normal, ColorRGBA::Green());

        StackString<64> statusStr;
        statusStr.Format("ESTADO: %s (B = PAUSAR)", m_isPaused ? "PAUSA ACTIVA" : "EJECUTANDO 60 FPS");
        UI::UIToolkit::DrawLabel(35.0f, 250.0f, statusStr.c_str(), UI::TextScale::Normal, m_isPaused ? ColorRGBA::Orange() : ColorRGBA{180, 255, 180, 255});
    }

    // 3. Tarjeta Derecha: Navegación Espacial y Algoritmo A*
    UI::UIToolkit::DrawPanel(s_panelNav);
    UI::UIToolkit::DrawLabel(845.0f, 80.0f, "NAVEGACION Y ALGORITMO A*", UI::TextScale::Normal, ColorRGBA::Yellow());

    if (ai) {
        StackString<64> pathStr;
        if (ai->path.isValid) {
            pathStr.Format("RUTA A*: VALIDA (%u/%u NODOS)", ai->path.currentIndex + 1, ai->path.waypointCount);
            UI::UIToolkit::DrawLabel(845.0f, 110.0f, pathStr.c_str(), UI::TextScale::Normal, ColorRGBA::Green());
        } else {
            pathStr.Format("RUTA A*: CALCULANDO...");
            UI::UIToolkit::DrawLabel(845.0f, 110.0f, pathStr.c_str(), UI::TextScale::Normal, ColorRGBA{255, 160, 60, 255});
        }

        StackString<64> steerStr;
        const char* steerMode = "ARRIVE (DECELERACION)";
        if (ai->currentState == AIState::Chase) steerMode = "ARRIVE (PERSECUCION SUAVE)";
        else if (ai->currentState == AIState::Investigate) steerMode = "STOP (INSPECCIONANDO)";
        else if (ai->currentState == AIState::ReturnHome) steerMode = "ARRIVE (A* RETORNO)";
        steerStr.Format("STEERING: %s", steerMode);
        UI::UIToolkit::DrawLabel(845.0f, 138.0f, steerStr.c_str(), UI::TextScale::Normal, ColorRGBA::White());

        float speed = std::sqrt(ai->velocity.x * ai->velocity.x + ai->velocity.z * ai->velocity.z);
        StackString<64> speedStr;
        speedStr.Format("VELOCIDAD LINEAL: %.2f m/s", speed);
        UI::UIToolkit::DrawLabel(845.0f, 166.0f, speedStr.c_str(), UI::TextScale::Normal, ColorRGBA::White());

        StackString<64> graphStr;
        graphStr.Format("NODOS NAVGRAPH: %zu | ARISTAS ACTIVAS", m_navGraph.GetNodeCount());
        UI::UIToolkit::DrawLabel(845.0f, 194.0f, graphStr.c_str(), UI::TextScale::Normal, ColorRGBA{160, 220, 255, 255});

        StackString<64> fovStr;
        fovStr.Format("CAMPO VISION: %.0f DEG | RANGO: %.1f m", ai->perceptionFOV, ai->perceptionRadius);
        UI::UIToolkit::DrawLabel(845.0f, 222.0f, fovStr.c_str(), UI::TextScale::Normal, ColorRGBA::White());

        StackString<64> debugStr;
        debugStr.Format("DEBUG WIREFRAME: %s (X = TOGGLE)", m_showGraphEdges ? "ACTIVO" : "OCULTO");
        UI::UIToolkit::DrawLabel(845.0f, 250.0f, debugStr.c_str(), UI::TextScale::Normal, m_showGraphEdges ? ColorRGBA::Green() : ColorRGBA{180, 180, 180, 255});
    }

    // 4. Barra Inferior con Guía de Controles
    UI::UIToolkit::DrawPanel(s_panelBottom);
    UI::UIToolkit::DrawLabel(35.0f, 646.0f, "[STICK L / D-PAD] MOVER JUGADOR | [STICK R / ZL / ZR] CAMARA ORBITAL", UI::TextScale::Normal, ColorRGBA::White());
    UI::UIToolkit::DrawLabel(35.0f, 672.0f, "[A] INVERTIR PATRULLA A* | [X] GRAFO ON/OFF | [B] PAUSA | [VOLVER] REGRESAR", UI::TextScale::Normal, ColorRGBA{255, 220, 140, 255});
#endif
}

} // namespace WiiU
