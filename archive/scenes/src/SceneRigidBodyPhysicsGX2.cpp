#include "Scene/SceneRigidBodyPhysicsGX2.hpp"
#include "Scene/SceneMenuModels.hpp"
#include "Scene/SceneManager.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include "Core/ECS/Components/ColliderComponent.hpp"
#include "Core/ECS/Components/RigidBodyComponent.hpp"
#include "Core/ECS/Components/KinematicCharacterComponent.hpp"
#include "Core/ECS/Systems/TransformSystem.hpp"
#include "Core/StackString.hpp"
#include "Core/Config/EngineConfig.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/DebugDraw3D.hpp"
#include "Graphics/UI/UIToolkit.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include <algorithm>
#include <cmath>

namespace WiiU {

namespace {

enum class PhysicsVisualKind : uint8_t {
    Floor       = 0,
    CrateBox    = 1,
    SteelBox    = 2,
    SphereRed   = 3,
    SphereGold  = 4,
    Avatar      = 5
};

struct PhysicsVisualTag {
    PhysicsVisualKind kind{PhysicsVisualKind::CrateBox};
    float reflection{0.10f};
    float specPower{1.2f};
};

// Constantes de interfaz alineadas al lienzo virtual 1280x720 (UIToolkit)
const UI::UIRect s_topBarPanel{ 8.0f, 8.0f, 1264.0f, 48.0f };
const UI::UIRect s_btnVolver{ 16.0f, 14.0f, 112.0f, 36.0f };
const UI::UIRect s_telemetryPanel{ 16.0f, 64.0f, 430.0f, 152.0f };
const UI::UILayoutRow s_actionRow{ 24.0f, 596.0f, 1232.0f, 38.0f, 4, 12.0f };

inline void SetVertex(MeshVertex& v,
                      float px, float py, float pz,
                      float nx, float ny, float nz,
                      float u, float vv,
                      float r, float g, float b) noexcept {
    v.pos[0] = px; v.pos[1] = py; v.pos[2] = pz;
    v.normal[0] = nx; v.normal[1] = ny; v.normal[2] = nz;
    v.texCoord[0] = u; v.texCoord[1] = vv;
    v.color[0] = r; v.color[1] = g; v.color[2] = b; v.color[3] = 1.0f;
}

void BuildBoxMeshData(std::vector<MeshVertex>& outVerts,
                      std::vector<uint32_t>& outIdx,
                      float hx, float hy, float hz,
                      float r, float g, float b,
                      float trimR, float trimG, float trimB) {
    outVerts.clear();
    outIdx.clear();
    outVerts.reserve(24);
    outIdx.reserve(36);

    struct FaceDef {
        Point3D normal;
        Point3D corners[4];
        bool isTopOrBottom;
    };

    const FaceDef faces[6] = {
        { { 0,  0,  1}, { {-hx,-hy, hz}, { hx,-hy, hz}, { hx, hy, hz}, {-hx, hy, hz} }, false },
        { { 0,  0, -1}, { { hx,-hy,-hz}, {-hx,-hy,-hz}, {-hx, hy,-hz}, { hx, hy,-hz} }, false },
        { { 1,  0,  0}, { { hx,-hy, hz}, { hx,-hy,-hz}, { hx, hy,-hz}, { hx, hy, hz} }, false },
        { {-1,  0,  0}, { {-hx,-hy,-hz}, {-hx,-hy, hz}, {-hx, hy, hz}, {-hx, hy,-hz} }, false },
        { { 0,  1,  0}, { {-hx, hy, hz}, { hx, hy, hz}, { hx, hy,-hz}, {-hx, hy,-hz} }, true  },
        { { 0, -1,  0}, { {-hx,-hy,-hz}, { hx,-hy,-hz}, { hx,-hy, hz}, {-hx,-hy, hz} }, true  }
    };

    const float uvs[4][2] = { {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f} };

    for (const auto& f : faces) {
        const uint32_t base = static_cast<uint32_t>(outVerts.size());
        const float cr = f.isTopOrBottom ? trimR : r;
        const float cg = f.isTopOrBottom ? trimG : g;
        const float cb = f.isTopOrBottom ? trimB : b;
        for (int c = 0; c < 4; ++c) {
            MeshVertex v{};
            SetVertex(v, f.corners[c].x, f.corners[c].y, f.corners[c].z,
                      f.normal.x, f.normal.y, f.normal.z,
                      uvs[c][0], uvs[c][1], cr, cg, cb);
            outVerts.push_back(v);
        }
        outIdx.push_back(base + 0); outIdx.push_back(base + 1); outIdx.push_back(base + 2);
        outIdx.push_back(base + 0); outIdx.push_back(base + 2); outIdx.push_back(base + 3);
    }
}

void BuildSphereMeshData(std::vector<MeshVertex>& outVerts,
                         std::vector<uint32_t>& outIdx,
                         float radius,
                         int rings, int sectors,
                         float r1, float g1, float b1,
                         float r2, float g2, float b2) {
    outVerts.clear();
    outIdx.clear();

    constexpr float kPi = 3.14159265358979323846f;
    for (int i = 0; i <= rings; ++i) {
        const float vFrac = static_cast<float>(i) / static_cast<float>(rings);
        const float phi = vFrac * kPi;
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);

        for (int j = 0; j <= sectors; ++j) {
            const float uFrac = static_cast<float>(j) / static_cast<float>(sectors);
            const float theta = uFrac * 2.0f * kPi;
            const float nx = sinPhi * std::cos(theta);
            const float ny = cosPhi;
            const float nz = sinPhi * std::sin(theta);

            // Bandas hemisféricas sutiles para visualizar la rotación angular SO(3) al instante
            const bool altBand = (((i / 2) + (j / 3)) & 1) == 0;
            const float cr = altBand ? r1 : r2;
            const float cg = altBand ? g1 : g2;
            const float cb = altBand ? b1 : b2;

            MeshVertex v{};
            SetVertex(v, nx * radius, ny * radius, nz * radius, nx, ny, nz, uFrac, vFrac, cr, cg, cb);
            outVerts.push_back(v);
        }
    }

    const int stride = sectors + 1;
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < sectors; ++j) {
            const uint32_t a = static_cast<uint32_t>(i * stride + j);
            const uint32_t b = a + static_cast<uint32_t>(stride);
            const uint32_t c = b + 1U;
            const uint32_t d = a + 1U;
            outIdx.push_back(a); outIdx.push_back(b); outIdx.push_back(d);
            outIdx.push_back(d); outIdx.push_back(b); outIdx.push_back(c);
        }
    }
}

void BuildCheckerFloorMeshData(std::vector<MeshVertex>& outVerts,
                               std::vector<uint32_t>& outIdx,
                               float halfExtent,
                               int tilesPerSide) {
    outVerts.clear();
    outIdx.clear();
    outVerts.reserve(static_cast<size_t>(tilesPerSide * tilesPerSide * 4));
    outIdx.reserve(static_cast<size_t>(tilesPerSide * tilesPerSide * 6));

    const float tileSize = (2.0f * halfExtent) / static_cast<float>(tilesPerSide);

    for (int z = 0; z < tilesPerSide; ++z) {
        for (int x = 0; x < tilesPerSide; ++x) {
            const float x0 = -halfExtent + static_cast<float>(x) * tileSize;
            const float z0 = -halfExtent + static_cast<float>(z) * tileSize;
            const float x1 = x0 + tileSize;
            const float z1 = z0 + tileSize;

            const bool isBorder = (x == 0 || z == 0 || x == tilesPerSide - 1 || z == tilesPerSide - 1);
            const bool isLight = ((x + z) & 1) == 0;

            float r = isLight ? 0.24f : 0.14f;
            float g = isLight ? 0.30f : 0.18f;
            float b = isLight ? 0.38f : 0.24f;
            if (isBorder) {
                r = 0.12f; g = 0.42f; b = 0.56f;
            }

            const uint32_t base = static_cast<uint32_t>(outVerts.size());
            MeshVertex v0{}, v1{}, v2{}, v3{};
            SetVertex(v0, x0, 0.5f, z1, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, r, g, b);
            SetVertex(v1, x1, 0.5f, z1, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, r, g, b);
            SetVertex(v2, x1, 0.5f, z0, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, r, g, b);
            SetVertex(v3, x0, 0.5f, z0, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, r, g, b);
            outVerts.push_back(v0);
            outVerts.push_back(v1);
            outVerts.push_back(v2);
            outVerts.push_back(v3);

            outIdx.push_back(base + 0); outIdx.push_back(base + 1); outIdx.push_back(base + 2);
            outIdx.push_back(base + 0); outIdx.push_back(base + 2); outIdx.push_back(base + 3);
        }
    }
}

} // namespace

// =============================================================================
// Carga Asíncrona en Core 2 y Ciclo de Vida de Escena
// =============================================================================

void SceneRigidBodyPhysicsGX2::LoadAsync() {
    BuildCheckerFloorMeshData(m_stagedFloor.vertices, m_stagedFloor.indices, 14.0f, 14);
    BuildBoxMeshData(m_stagedCrate.vertices, m_stagedCrate.indices,
                     1.0f, 1.0f, 1.0f,
                     0.88f, 0.52f, 0.18f,
                     0.98f, 0.72f, 0.34f);
    BuildBoxMeshData(m_stagedSteel.vertices, m_stagedSteel.indices,
                     1.0f, 1.0f, 1.0f,
                     0.26f, 0.52f, 0.78f,
                     0.42f, 0.72f, 0.96f);
    BuildSphereMeshData(m_stagedSphereRed.vertices, m_stagedSphereRed.indices,
                        1.0f, 12, 18,
                        0.95f, 0.24f, 0.18f,
                        1.0f, 0.65f, 0.22f);
    BuildSphereMeshData(m_stagedSphereGold.vertices, m_stagedSphereGold.indices,
                        1.0f, 12, 18,
                        0.96f, 0.78f, 0.18f,
                        0.28f, 0.85f, 0.98f);
    BuildBoxMeshData(m_stagedAvatar.vertices, m_stagedAvatar.indices,
                     0.35f, 0.75f, 0.35f,
                     0.15f, 0.88f, 0.46f,
                     0.55f, 1.0f, 0.75f);
}

void SceneRigidBodyPhysicsGX2::OnGameInit() {
    AudioManager::Instance().PlayBGM();
    SetClearColor(ColorRGBA{14, 22, 38, 255});

    m_floorMesh.CreateFromVertices(m_stagedFloor.vertices, m_stagedFloor.indices);
    m_crateMesh.CreateFromVertices(m_stagedCrate.vertices, m_stagedCrate.indices);
    m_steelMesh.CreateFromVertices(m_stagedSteel.vertices, m_stagedSteel.indices);
    m_sphereRedMesh.CreateFromVertices(m_stagedSphereRed.vertices, m_stagedSphereRed.indices);
    m_sphereGoldMesh.CreateFromVertices(m_stagedSphereGold.vertices, m_stagedSphereGold.indices);
    m_avatarMesh.CreateFromVertices(m_stagedAvatar.vertices, m_stagedAvatar.indices);

    m_stagedFloor.Clear();
    m_stagedCrate.Clear();
    m_stagedSteel.Clear();
    m_stagedSphereRed.Clear();
    m_stagedSphereGold.Clear();
    m_stagedAvatar.Clear();

    m_physicsSystem.SetGravity(Point3D{0.0f, -9.81f, 0.0f});
    m_physicsSystem.SetSubsteps(2);
    m_physicsSystem.SetSolverIterations(8);

    ResetPhysicsPlayground();

    m_orbitCamera.SetTarget(Point3D{0.0f, 1.4f, 0.5f});
    m_orbitCamera.SetAngles(18.0f, 26.0f);
    m_orbitCamera.SetDistance(13.5f);
}

void SceneRigidBodyPhysicsGX2::OnGameExit() {
    m_physicsSystem.Clear();
    m_ecsWorld.Clear();

    m_floorMesh.Destroy();
    m_crateMesh.Destroy();
    m_steelMesh.Destroy();
    m_sphereRedMesh.Destroy();
    m_sphereGoldMesh.Destroy();
    m_avatarMesh.Destroy();
}

// =============================================================================
// Construcción / Reinicio del Laboratorio Cap. 14
// =============================================================================

void SceneRigidBodyPhysicsGX2::ResetPhysicsPlayground() {
    m_physicsSystem.Clear();
    m_ecsWorld.Clear();
    m_gameClock.Reset();
    m_projectiles.fill(Entity::Null);
    m_projectileCursor = 0;
    m_projectileCount = 0;
    m_simTime = 0.0f;
    m_explosionFlashTimer = 0.0f;

    // 1. Suelo estático (cara superior exacta en y = 0.0f)
    m_floorEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(
        m_floorEntity, Point3D{0.0f, -0.5f, 0.0f}, Point3D{0, 0, 0}, Point3D{1.0f, 1.0f, 1.0f});
    m_ecsWorld.EmplaceComponent<ColliderComponent>(
        m_floorEntity, ColliderComponent::MakeAABB(Point3D{14.0f, 0.5f, 14.0f}, Point3D{0, 0, 0}, true, Layer_StaticWorld, "Floor"));
    m_ecsWorld.EmplaceComponent<RigidBodyComponent>(
        m_floorEntity, RigidBodyComponent::MakeStatic(0.35f, 0.72f));
    m_ecsWorld.EmplaceComponent<PhysicsVisualTag>(
        m_floorEntity, PhysicsVisualTag{PhysicsVisualKind::Floor, 0.04f, 0.6f});

    // Bordillos perimetrales estáticos para mantener esferas dentro del laboratorio
    const struct WallDef { Point3D pos; Point3D halfExt; } walls[4] = {
        { {  0.0f, 0.35f, -12.0f }, { 12.4f, 0.45f, 0.4f } },
        { {  0.0f, 0.35f,  12.0f }, { 12.4f, 0.45f, 0.4f } },
        { { -12.0f, 0.35f,  0.0f }, { 0.4f, 0.45f, 12.0f } },
        { {  12.0f, 0.35f,  0.0f }, { 0.4f, 0.45f, 12.0f } }
    };
    for (const auto& w : walls) {
        Entity wallEnt = m_ecsWorld.CreateEntity();
        m_ecsWorld.EmplaceComponent<TransformComponent>(wallEnt, w.pos, Point3D{0, 0, 0}, w.halfExt);
        m_ecsWorld.EmplaceComponent<ColliderComponent>(
            wallEnt, ColliderComponent::MakeAABB(w.halfExt, Point3D{0, 0, 0}, true, Layer_StaticWorld, "CurbWall"));
        m_ecsWorld.EmplaceComponent<RigidBodyComponent>(
            wallEnt, RigidBodyComponent::MakeStatic(0.45f, 0.50f));
        m_ecsWorld.EmplaceComponent<PhysicsVisualTag>(
            wallEnt, PhysicsVisualTag{PhysicsVisualKind::SteelBox, 0.08f, 1.0f});
    }

    // 2. Pirámide de 10 cajas rígidas dinámicas (Sequential Impulses + Coulomb + Sleeping)
    const Point3D boxHalf{0.48f, 0.48f, 0.48f};
    const float baseX = -2.4f;
    const float baseZ = -1.6f;
    for (int row = 0; row < 4; ++row) {
        const int cols = 4 - row;
        const float y = 0.485f + static_cast<float>(row) * 0.97f;
        const float startX = baseX - 0.5f * static_cast<float>(cols - 1) * 1.02f;
        for (int c = 0; c < cols; ++c) {
            const float x = startX + static_cast<float>(c) * 1.02f;
            Entity boxEnt = m_ecsWorld.CreateEntity();
            m_ecsWorld.EmplaceComponent<TransformComponent>(
                boxEnt, Point3D{x, y, baseZ}, Point3D{0, 0, 0}, boxHalf);
            m_ecsWorld.EmplaceComponent<ColliderComponent>(
                boxEnt, ColliderComponent::MakeAABB(boxHalf, Point3D{0, 0, 0}, false, Layer_DynamicBody, "PyramidBox"));
            RigidBodyComponent rb = RigidBodyComponent::MakeDynamicBox(2.4f, boxHalf, 0.12f, 0.75f);
            m_ecsWorld.EmplaceComponent<RigidBodyComponent>(boxEnt, rb);
            m_ecsWorld.EmplaceComponent<PhysicsVisualTag>(
                boxEnt, PhysicsVisualTag{PhysicsVisualKind::CrateBox, 0.05f, 0.8f});
        }
    }

    // 3. Péndulo / Cadena de 3 eslabones XPBD (BallSocketPin + Distance)
    const Point3D chainPivot{2.6f, 5.2f, -1.4f};
    Entity pivotBeam = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(
        pivotBeam, chainPivot, Point3D{0, 0, 0}, Point3D{0.6f, 0.15f, 0.25f});
    m_ecsWorld.EmplaceComponent<ColliderComponent>(
        pivotBeam, ColliderComponent::MakeAABB(Point3D{0.6f, 0.15f, 0.25f}, Point3D{0, 0, 0}, true, Layer_StaticWorld, "ChainPivot"));
    m_ecsWorld.EmplaceComponent<RigidBodyComponent>(pivotBeam, RigidBodyComponent::MakeStatic());
    m_ecsWorld.EmplaceComponent<PhysicsVisualTag>(
        pivotBeam, PhysicsVisualTag{PhysicsVisualKind::SteelBox, 0.25f, 2.0f});

    Entity prevChain = Entity::Null;
    const float linkLen = 1.05f;
    for (int k = 0; k < 3; ++k) {
        const float offsetX = static_cast<float>(k + 1) * 0.72f;
        const float offsetY = -static_cast<float>(k + 1) * 0.76f;
        const float linkRadius = 0.30f;

        Entity linkEnt = m_ecsWorld.CreateEntity();
        m_ecsWorld.EmplaceComponent<TransformComponent>(
            linkEnt,
            Point3D{chainPivot.x + offsetX, chainPivot.y + offsetY, chainPivot.z},
            Point3D{0, 0, 0},
            Point3D{linkRadius, linkRadius, linkRadius});
        m_ecsWorld.EmplaceComponent<ColliderComponent>(
            linkEnt, ColliderComponent::MakeSphere(linkRadius, Point3D{0, 0, 0}, false, Layer_DynamicBody, "ChainLink"));
        RigidBodyComponent rb = RigidBodyComponent::MakeDynamicSphere(1.8f, linkRadius, 0.65f, 0.25f);
        rb.canSleep = false;
        rb.linearDamping = 0.02f;
        m_ecsWorld.EmplaceComponent<RigidBodyComponent>(linkEnt, rb);
        m_ecsWorld.EmplaceComponent<PhysicsVisualTag>(
            linkEnt, PhysicsVisualTag{PhysicsVisualKind::SphereGold, 0.45f, 2.5f});

        PhysicsConstraint c{};
        c.type = PhysicsConstraintType::Distance;
        c.entityA = linkEnt;
        c.entityB = prevChain;
        c.localAnchorA = Point3D{0.0f, 0.0f, 0.0f};
        c.localAnchorB = (!prevChain.IsValid()) ? chainPivot : Point3D{0.0f, 0.0f, 0.0f};
        c.restLength = linkLen;
        c.compliance = 0.0002f;
        m_physicsSystem.AddConstraint(c);

        prevChain = linkEnt;
    }

    // 4. Oscilador Elástico (Muelle de Hooke + Amortiguamiento Viscoso)
    const Point3D springAnchor{4.6f, 4.6f, 1.4f};
    Entity springTop = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(
        springTop, springAnchor, Point3D{0, 0, 0}, Point3D{0.28f, 0.12f, 0.28f});
    m_ecsWorld.EmplaceComponent<ColliderComponent>(
        springTop, ColliderComponent::MakeAABB(Point3D{0.28f, 0.12f, 0.28f}, Point3D{0, 0, 0}, true, Layer_StaticWorld, "SpringAnchor"));
    m_ecsWorld.EmplaceComponent<RigidBodyComponent>(springTop, RigidBodyComponent::MakeStatic());
    m_ecsWorld.EmplaceComponent<PhysicsVisualTag>(
        springTop, PhysicsVisualTag{PhysicsVisualKind::SteelBox, 0.2f, 1.6f});

    Entity springWeight = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(
        springWeight, Point3D{4.6f, 1.75f, 1.4f}, Point3D{0, 0, 0}, Point3D{0.42f, 0.42f, 0.42f});
    m_ecsWorld.EmplaceComponent<ColliderComponent>(
        springWeight, ColliderComponent::MakeSphere(0.42f, Point3D{0, 0, 0}, false, Layer_DynamicBody, "SpringMass"));
    RigidBodyComponent springRb = RigidBodyComponent::MakeDynamicSphere(2.2f, 0.42f, 0.4f, 0.35f);
    springRb.canSleep = false;
    springRb.linearDamping = 0.03f;
    m_ecsWorld.EmplaceComponent<RigidBodyComponent>(springWeight, springRb);
    m_ecsWorld.EmplaceComponent<PhysicsVisualTag>(
        springWeight, PhysicsVisualTag{PhysicsVisualKind::SphereRed, 0.35f, 2.0f});

    PhysicsConstraint springConstraint{};
    springConstraint.type = PhysicsConstraintType::Spring;
    springConstraint.entityA = springWeight;
    springConstraint.entityB = springTop;
    springConstraint.localAnchorA = Point3D{0.0f, 0.35f, 0.0f};
    springConstraint.localAnchorB = Point3D{0.0f, 0.0f, 0.0f};
    springConstraint.restLength = 1.65f;
    springConstraint.stiffness = 62.0f;
    springConstraint.damping = 1.35f;
    m_physicsSystem.AddConstraint(springConstraint);

    // 5. Muñeco Articulado (Ragdoll Simplificado de 5 cuerpos unido por Constraints XPBD)
    const Point3D ragCenter{0.2f, 1.85f, 2.0f};
    Entity plinth = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(
        plinth, Point3D{ragCenter.x, 0.35f, ragCenter.z}, Point3D{0, 0, 0}, Point3D{0.75f, 0.35f, 0.75f});
    m_ecsWorld.EmplaceComponent<ColliderComponent>(
        plinth, ColliderComponent::MakeAABB(Point3D{0.75f, 0.35f, 0.75f}, Point3D{0, 0, 0}, true, Layer_StaticWorld, "Plinth"));
    m_ecsWorld.EmplaceComponent<RigidBodyComponent>(plinth, RigidBodyComponent::MakeStatic(0.25f, 0.8f));
    m_ecsWorld.EmplaceComponent<PhysicsVisualTag>(
        plinth, PhysicsVisualTag{PhysicsVisualKind::SteelBox, 0.1f, 1.0f});

    auto spawnRagPart = [this](const Point3D& pos, const Point3D& halfExt, float mass,
                               PhysicsVisualKind vis, bool isSphere) -> Entity {
        Entity e = m_ecsWorld.CreateEntity();
        m_ecsWorld.EmplaceComponent<TransformComponent>(
            e, pos, Point3D{0, 0, 0}, isSphere ? Point3D{halfExt.x, halfExt.x, halfExt.x} : halfExt);
        if (isSphere) {
            m_ecsWorld.EmplaceComponent<ColliderComponent>(
                e, ColliderComponent::MakeSphere(halfExt.x, Point3D{0, 0, 0}, false, Layer_DynamicBody, "RagSphere"));
            m_ecsWorld.EmplaceComponent<RigidBodyComponent>(
                e, RigidBodyComponent::MakeDynamicSphere(mass, halfExt.x, 0.25f, 0.65f));
        } else {
            m_ecsWorld.EmplaceComponent<ColliderComponent>(
                e, ColliderComponent::MakeAABB(halfExt, Point3D{0, 0, 0}, false, Layer_DynamicBody, "RagBox"));
            m_ecsWorld.EmplaceComponent<RigidBodyComponent>(
                e, RigidBodyComponent::MakeDynamicBox(mass, halfExt, 0.20f, 0.65f));
        }
        m_ecsWorld.EmplaceComponent<PhysicsVisualTag>(e, PhysicsVisualTag{vis, 0.15f, 1.4f});
        return e;
    };

    Entity ragTorso = spawnRagPart(ragCenter, Point3D{0.28f, 0.42f, 0.20f}, 3.5f, PhysicsVisualKind::SteelBox, false);
    Entity ragHead  = spawnRagPart(ragCenter + Point3D{0.0f, 0.72f, 0.0f}, Point3D{0.24f, 0.24f, 0.24f}, 1.4f, PhysicsVisualKind::SphereGold, true);
    Entity ragArmL  = spawnRagPart(ragCenter + Point3D{-0.60f, 0.20f, 0.0f}, Point3D{0.22f, 0.15f, 0.15f}, 1.1f, PhysicsVisualKind::CrateBox, false);
    Entity ragArmR  = spawnRagPart(ragCenter + Point3D{ 0.60f, 0.20f, 0.0f}, Point3D{0.22f, 0.15f, 0.15f}, 1.1f, PhysicsVisualKind::CrateBox, false);
    Entity ragPelvis = spawnRagPart(ragCenter + Point3D{0.0f, -0.78f, 0.0f}, Point3D{0.24f, 0.28f, 0.18f}, 2.4f, PhysicsVisualKind::CrateBox, false);

    auto addJoint = [this](Entity a, const Point3D& la, Entity b, const Point3D& lb, float len) {
        PhysicsConstraint c{};
        c.type = (len <= 0.05f) ? PhysicsConstraintType::BallSocketPin : PhysicsConstraintType::Distance;
        c.entityA = a;
        c.entityB = b;
        c.localAnchorA = la;
        c.localAnchorB = lb;
        c.restLength = len;
        c.compliance = 0.0015f;
        m_physicsSystem.AddConstraint(c);
    };

    addJoint(ragHead,   Point3D{0.0f, -0.24f, 0.0f}, ragTorso, Point3D{0.0f,  0.42f, 0.0f}, 0.08f);
    addJoint(ragArmL,   Point3D{0.22f, 0.0f,  0.0f}, ragTorso, Point3D{-0.30f, 0.26f, 0.0f}, 0.12f);
    addJoint(ragArmR,   Point3D{-0.22f, 0.0f, 0.0f}, ragTorso, Point3D{ 0.30f, 0.26f, 0.0f}, 0.12f);
    addJoint(ragPelvis, Point3D{0.0f,  0.28f, 0.0f}, ragTorso, Point3D{0.0f, -0.44f, 0.0f}, 0.10f);

    // 6. Tres Esferas de Restitución Diferenciada (e = 0.15, 0.55, 0.90)
    const float restValues[3] = { 0.15f, 0.55f, 0.90f };
    for (int i = 0; i < 3; ++i) {
        Entity ball = m_ecsWorld.CreateEntity();
        const float r = 0.35f;
        m_ecsWorld.EmplaceComponent<TransformComponent>(
            ball,
            Point3D{-5.2f + static_cast<float>(i) * 1.15f, 3.6f, 2.8f},
            Point3D{0, 0, 0},
            Point3D{r, r, r});
        m_ecsWorld.EmplaceComponent<ColliderComponent>(
            ball, ColliderComponent::MakeSphere(r, Point3D{0, 0, 0}, false, Layer_DynamicBody, "RestitutionBall"));
        m_ecsWorld.EmplaceComponent<RigidBodyComponent>(
            ball, RigidBodyComponent::MakeDynamicSphere(1.5f, r, restValues[i], 0.35f));
        m_ecsWorld.EmplaceComponent<PhysicsVisualTag>(
            ball, PhysicsVisualTag{(i == 2) ? PhysicsVisualKind::SphereRed : PhysicsVisualKind::SphereGold, 0.35f, 2.0f});
    }

    // 7. Avatar Controlable con KinematicCharacterComponent (KCC bidireccional)
    m_playerEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(
        m_playerEntity, Point3D{0.0f, 0.75f, 5.8f}, Point3D{0, 0, 0}, Point3D{1.0f, 1.0f, 1.0f});
    m_ecsWorld.EmplaceComponent<ColliderComponent>(
        m_playerEntity, ColliderComponent::MakeSphere(0.45f, Point3D{0, 0, 0}, false, Layer_Player, "PlayerKCC"));
    m_ecsWorld.EmplaceComponent<KinematicCharacterComponent>(
        m_playerEntity, 0.42f, 1.50f, 5.2f, 8.2f, 7.2f, 19.6f);
    m_ecsWorld.EmplaceComponent<PhysicsVisualTag>(
        m_playerEntity, PhysicsVisualTag{PhysicsVisualKind::Avatar, 0.25f, 1.8f});
}

void SceneRigidBodyPhysicsGX2::FireProjectileFromCamera() {
    const Point3D camPos = m_orbitCamera.GetPosition();
    const Point3D target = m_orbitCamera.GetTarget();
    Point3D shootDir = (target - camPos).Normalized();
    shootDir.y += 0.10f;
    shootDir = shootDir.Normalized();

    const Point3D spawnPos = camPos + (shootDir * 2.4f);
    const float projRadius = 0.36f;

    Entity proj = m_projectiles[m_projectileCursor];
    if (!proj.IsValid() || !m_ecsWorld.IsAlive(proj)) {
        proj = m_ecsWorld.CreateEntity();
        m_projectiles[m_projectileCursor] = proj;
        m_ecsWorld.EmplaceComponent<TransformComponent>(
            proj, spawnPos, Point3D{0, 0, 0}, Point3D{projRadius, projRadius, projRadius});
        m_ecsWorld.EmplaceComponent<ColliderComponent>(
            proj, ColliderComponent::MakeSphere(projRadius, Point3D{0, 0, 0}, false, Layer_DynamicBody, "Projectile"));
        m_ecsWorld.EmplaceComponent<RigidBodyComponent>(
            proj, RigidBodyComponent::MakeDynamicSphere(3.4f, projRadius, 0.58f, 0.40f));
        m_ecsWorld.EmplaceComponent<PhysicsVisualTag>(
            proj, PhysicsVisualTag{PhysicsVisualKind::SphereRed, 0.45f, 2.4f});
        if (m_projectileCount < MAX_PROJECTILES) {
            ++m_projectileCount;
        }
    }

    if (auto* tf = m_ecsWorld.GetComponent<TransformComponent>(proj)) {
        tf->position = spawnPos;
        tf->MarkDirty();
    }
    if (auto* rb = m_ecsWorld.GetComponent<RigidBodyComponent>(proj)) {
        rb->WakeUp();
        rb->orientation = Quaternion::Identity();
        rb->linearVelocity = shootDir * 19.5f;
        rb->angularVelocity = Point3D{4.5f, 1.5f, -3.0f};
    }

    m_projectileCursor = (m_projectileCursor + 1U) % MAX_PROJECTILES;
    m_orbitCamera.AddTrauma(0.22f);
    AudioManager::Instance().PlayClick();
}

void SceneRigidBodyPhysicsGX2::TriggerExplosion() {
    RigidBodyPhysicsSystem::ApplyRadialExplosion(
        m_ecsWorld,
        Point3D{-0.6f, 0.45f, 0.2f},
        9.5f,
        m_explosionImpulse,
        0.52f);
    m_explosionFlashTimer = 0.65f;
    m_orbitCamera.AddTrauma(0.65f);
    AudioManager::Instance().PlayClick();
}

// =============================================================================
// Actualización de Entradas y Paso Físico
// =============================================================================

void SceneRigidBodyPhysicsGX2::UpdateCameraAndCharacterInput(float dt) {
    auto& input = InputManager::Instance();
    const auto& pad = input.GetGamepad(0);

    // 1. Cámara orbital con Stick Derecho y zoom con ZL/ZR
    const float rX = pad.rightStick.x;
    const float rY = pad.rightStick.y;
    const float camSpeed = 68.0f * dt;
    if (std::abs(rX) > 0.05f || std::abs(rY) > 0.05f) {
        const float newPitch = std::clamp(m_orbitCamera.GetPitch() - rY * camSpeed, 10.0f, 80.0f);
        m_orbitCamera.SetAngles(m_orbitCamera.GetYaw() - rX * camSpeed, newPitch);
    }
    if (pad.IsHeld(VirtualButton::ZL)) {
        m_orbitCamera.SetDistance(std::clamp(m_orbitCamera.GetDistance() - 9.0f * dt, 6.0f, 26.0f));
    }
    if (pad.IsHeld(VirtualButton::ZR)) {
        m_orbitCamera.SetDistance(std::clamp(m_orbitCamera.GetDistance() + 9.0f * dt, 6.0f, 26.0f));
    }

    // 2. Desplazamiento KCC relativo a la orientación horizontal de la cámara
    float moveX = pad.leftStick.x;
    float moveY = pad.leftStick.y;
    if (pad.IsHeld(VirtualButton::DPadLeft))  moveX = -1.0f;
    if (pad.IsHeld(VirtualButton::DPadRight)) moveX =  1.0f;
    if (pad.IsHeld(VirtualButton::DPadUp))    moveY =  1.0f;
    if (pad.IsHeld(VirtualButton::DPadDown))  moveY = -1.0f;

    if (auto* kcc = m_ecsWorld.GetComponent<KinematicCharacterComponent>(m_playerEntity)) {
        if (std::abs(moveX) > 0.05f || std::abs(moveY) > 0.05f) {
            const float yawRad = m_orbitCamera.GetYaw() * (3.14159265358979323846f / 180.0f);
            const float fwdX   = -std::sin(yawRad);
            const float fwdZ   =  std::cos(yawRad);
            const float rightX = -std::cos(yawRad);
            const float rightZ = -std::sin(yawRad);

            const float worldDirX = rightX * moveX + fwdX * moveY;
            const float worldDirZ = rightZ * moveX + fwdZ * moveY;
            kcc->SetMovementInput(worldDirX, worldDirZ, pad.IsHeld(VirtualButton::R));
        } else {
            kcc->SetMovementInput(0.0f, 0.0f, false);
        }

        if (pad.WasJustPressed(VirtualButton::B)) {
            kcc->RequestJump();
        }
    }
}

void SceneRigidBodyPhysicsGX2::OnGameUpdate(float dt) {
    auto& input = InputManager::Instance();
    const auto& pad = input.GetGamepad(0);
    const auto& touch = input.GetTouchState();

    // 1. Retorno al Menú Principal (+ o botón táctil "< VOLVER")
    if (input.IsBackRequested() || pad.WasJustPressed(VirtualButton::Plus) ||
        (touch.justPressed && s_btnVolver.Contains(touch.x, touch.y))) {
        AudioManager::Instance().PlayClick();
        SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuModels>());
        return;
    }

    // 2. Acciones interactivas por botón o barra táctil inferior
    const bool touchShoot   = touch.justPressed && s_actionRow.GetCell(0).Contains(touch.x, touch.y);
    const bool touchExplode = touch.justPressed && s_actionRow.GetCell(1).Contains(touch.x, touch.y);
    const bool touchReset   = touch.justPressed && s_actionRow.GetCell(2).Contains(touch.x, touch.y);
    const bool touchDebug   = touch.justPressed && s_actionRow.GetCell(3).Contains(touch.x, touch.y);

    if (pad.WasJustPressed(VirtualButton::A) || touchShoot) {
        FireProjectileFromCamera();
    }
    if (pad.WasJustPressed(VirtualButton::X) || touchExplode) {
        TriggerExplosion();
    }
    if (pad.WasJustPressed(VirtualButton::Y) || touchReset) {
        AudioManager::Instance().PlayClick();
        ResetPhysicsPlayground();
    }
    if (pad.WasJustPressed(VirtualButton::L) || touchDebug) {
        m_showDebugWireframe = !m_showDebugWireframe;
        AudioManager::Instance().PlayClick();
    }
    if (pad.WasJustPressed(VirtualButton::Minus)) {
        m_isPaused = !m_isPaused;
        m_gameClock.SetPaused(m_isPaused);
        AudioManager::Instance().PlayClick();
    }

    m_gameClock.Tick(dt);
    const float realDt = m_gameClock.GetRealDelta();
    const float simDt = m_gameClock.GetGameplayDelta();

    // 3. Controles de Cámara y KCC
    UpdateCameraAndCharacterInput(realDt);

    if (m_explosionFlashTimer > 0.0f) {
        m_explosionFlashTimer = std::max(0.0f, m_explosionFlashTimer - realDt);
    }

    // 4. Paso Determinista de Sistemas ECS (KCC -> Dinámica Rígida + Constraints -> Transform)
    if (simDt > 0.0f) {
        m_simTime += simDt;
        RigidBodyPhysicsSystem::UpdateKinematicCharactersFlat(m_ecsWorld, simDt, 0.0f);

        // Mantener al personaje dentro del perímetro interior del laboratorio
        if (auto* pTf = m_ecsWorld.GetComponent<TransformComponent>(m_playerEntity)) {
            pTf->position.x = std::clamp(pTf->position.x, -11.0f, 11.0f);
            pTf->position.z = std::clamp(pTf->position.z, -11.0f, 11.0f);
            if (pTf->position.y < 0.75f) {
                pTf->position.y = 0.75f;
            }
            pTf->MarkDirty();
        }

        m_physicsSystem.Update(m_ecsWorld, simDt);
    }

    m_orbitCamera.Update(m_ecsWorld, realDt);
    TransformSystem::Update(m_ecsWorld);
}

// =============================================================================
// Renderizado 3D, Debug Wireframe 3D y HUD
// =============================================================================

void SceneRigidBodyPhysicsGX2::OnGameRender3D(ForwardRendererGX2& renderer) {
    m_ecsWorld.ForEach<TransformComponent, PhysicsVisualTag>(
        [this, &renderer](Entity /*entity*/, const TransformComponent& tf, const PhysicsVisualTag& vis) {
            const StaticMesh* mesh = &m_crateMesh;
            switch (vis.kind) {
                case PhysicsVisualKind::Floor:      mesh = &m_floorMesh; break;
                case PhysicsVisualKind::CrateBox:   mesh = &m_crateMesh; break;
                case PhysicsVisualKind::SteelBox:   mesh = &m_steelMesh; break;
                case PhysicsVisualKind::SphereRed:  mesh = &m_sphereRedMesh; break;
                case PhysicsVisualKind::SphereGold: mesh = &m_sphereGoldMesh; break;
                case PhysicsVisualKind::Avatar:     mesh = &m_avatarMesh; break;
            }

            if (mesh->IsLoaded()) {
                renderer.RenderStaticMesh(*mesh, tf.worldMatrix, nullptr, nullptr,
                                          vis.reflection, true, vis.specPower, 0.35f);
            }
        }
    );
}

void SceneRigidBodyPhysicsGX2::OnGameRenderDebug3D() {
    if (!m_showDebugWireframe) {
        return;
    }

    // 1. Dibujar colisionadores rígidos (Verde = Activo, Cian = Sleeping, Gris = Estático)
    m_ecsWorld.ForEach<TransformComponent, ColliderComponent>(
        [this](Entity entity, const TransformComponent& tf, const ColliderComponent& col) {
            if (entity == m_floorEntity) {
                return;
            }

            ColorRGBA wireColor = ColorRGBA{120, 140, 165, 160};
            if (const auto* rb = m_ecsWorld.GetComponent<RigidBodyComponent>(entity)) {
                if (rb->motionType == RigidBodyMotionType::Dynamic) {
                    wireColor = rb->isSleeping
                        ? ColorRGBA{40, 210, 255, 220}   // Cian = Sleeping
                        : ColorRGBA{90, 255, 110, 235};  // Verde = Activo
                }
            } else if ((col.layer & Layer_Player) != 0U) {
                wireColor = ColorRGBA::Yellow();
            }

            const Point3D c = tf.position + col.localOffset;
            if (col.shape == ColliderShape::Sphere) {
                DebugDraw3D::DrawSphere(c, col.radius * 1.02f, 2.0f, wireColor, 14);
            } else {
                DebugDraw3D::DrawBox(c, col.halfExtents, 2.0f, wireColor);
            }
        }
    );

    // 2. Dibujar restricciones XPBD (Magenta) y muelles de Hooke (Dorado)
    const size_t constraintCount = m_physicsSystem.GetConstraintCount();
    const PhysicsConstraint* constraints = m_physicsSystem.GetConstraints();
    for (size_t i = 0; i < constraintCount; ++i) {
        const PhysicsConstraint& c = constraints[i];
        if (!c.active) continue;

        const auto* tfA = m_ecsWorld.GetComponent<TransformComponent>(c.entityA);
        if (!tfA) continue;
        const auto* rbA = m_ecsWorld.GetComponent<RigidBodyComponent>(c.entityA);
        const Point3D pA = tfA->position + (rbA ? rbA->orientation.Normalized().Rotate(c.localAnchorA) : c.localAnchorA);

        Point3D pB = c.localAnchorB;
        if (c.entityB.IsValid()) {
            if (const auto* tfB = m_ecsWorld.GetComponent<TransformComponent>(c.entityB)) {
                const auto* rbB = m_ecsWorld.GetComponent<RigidBodyComponent>(c.entityB);
                pB = tfB->position + (rbB ? rbB->orientation.Normalized().Rotate(c.localAnchorB) : c.localAnchorB);
            }
        }

        const ColorRGBA linkColor = (c.type == PhysicsConstraintType::Spring)
            ? ColorRGBA{255, 215, 0, 255}
            : ColorRGBA{255, 70, 220, 255};
        DebugDraw3D::DrawLine(pA, pB, 3.0f, linkColor);
    }

    // 3. Dibujar puntos de contacto y normales de impulso secuencial (Rojo/Amarillo)
    const size_t contactCount = m_physicsSystem.GetContactCount();
    const RigidBodyContact* contacts = m_physicsSystem.GetContacts();
    for (size_t i = 0; i < contactCount; ++i) {
        const RigidBodyContact& ct = contacts[i];
        DebugDraw3D::DrawSphere(ct.contactPoint, 0.08f, 2.0f, ColorRGBA::Red(), 8);
        DebugDraw3D::DrawArrow(ct.contactPoint, ct.normal * -1.0f, 0.45f, 2.5f, ColorRGBA::Yellow());
    }

    // 4. Anillo visual efímero tras onda expansiva radial
    if (m_explosionFlashTimer > 0.0f) {
        const float t = 1.0f - (m_explosionFlashTimer / 0.65f);
        const float ringRadius = 0.8f + t * 8.5f;
        DebugDraw3D::DrawCircleXZ(Point3D{-0.6f, 0.15f, 0.2f}, ringRadius, 0.0f, 3.5f, ColorRGBA{255, 130, 30, 255}, 24);
    }
}

void SceneRigidBodyPhysicsGX2::OnGameRenderHUD() {
    const auto& tel = m_physicsSystem.GetTelemetry();
    const auto& touch = InputManager::Instance().GetTouchState();

    // 1. Barra Superior
    UI::UIToolkit::DrawPanel(s_topBarPanel);
    UI::UIToolkit::DrawButton(s_btnVolver, "< VOLVER", s_btnVolver.Contains(touch.x, touch.y), UI::TextScale::Normal);
    UI::UIToolkit::DrawLabel(145.0f, 22.0f,
                             "DINAMICA DE CUERPOS RIGIDOS, CONSTRAINTS Y KCC (CAP. 14 JASON GREGORY)",
                             UI::TextScale::Normal, ColorRGBA::Cyan());

    // 2. Panel de Telemetría Física
    UI::UIToolkit::DrawPanel(s_telemetryPanel);
    UI::UIToolkit::DrawLabel(28.0f, 74.0f,
                             m_isPaused ? "SOLVER FISICO: [PAUSADO]" : "SOLVER FISICO: SEQUENTIAL IMPULSES + XPBD",
                             UI::TextScale::Small, m_isPaused ? ColorRGBA::Red() : ColorRGBA::Yellow());

    StackString<80> line1;
    line1.Format("CUERPOS ACTIVOS: %2u  |  SLEEPING (CIAN): %2u",
                 tel.activeBodies, tel.sleepingBodies);
    UI::UIToolkit::DrawLabel(28.0f, 96.0f, line1.c_str(), UI::TextScale::Normal, ColorRGBA{120, 255, 160, 255});

    StackString<80> line2;
    line2.Format("CONTACTOS: %3u  |  CONSTRAINTS/MUELLES: %2u",
                 tel.activeContacts, tel.activeConstraints);
    UI::UIToolkit::DrawLabel(28.0f, 122.0f, line2.c_str(), UI::TextScale::Normal, ColorRGBA::White());

    StackString<80> line3;
    line3.Format("SUB:%u | ALPHA:%.2f | TRAUMA:%.2f | PROY:%zu/%zu",
                 tel.substeps, m_gameClock.GetInterpolationAlpha(), m_orbitCamera.GetTrauma(),
                 m_projectileCount, MAX_PROJECTILES);
    UI::UIToolkit::DrawLabel(28.0f, 148.0f, line3.c_str(), UI::TextScale::Small, ColorRGBA{180, 215, 245, 240});

    UI::UIToolkit::DrawSeparator(28.0f, 170.0f, 404.0f);
    UI::UIToolkit::DrawLabel(28.0f, 180.0f,
                             "VERDE=ACTIVO  CIAN=SLEEP  MAGENTA=XPBD  ORO=HOOKE",
                             UI::TextScale::Small, ColorRGBA{255, 220, 90, 255});

    // 3. Barra de Acciones Táctiles (encima del pie de HUD)
    const UI::UIRect btnShoot   = s_actionRow.GetCell(0);
    const UI::UIRect btnExplode = s_actionRow.GetCell(1);
    const UI::UIRect btnReset   = s_actionRow.GetCell(2);
    const UI::UIRect btnWire    = s_actionRow.GetCell(3);

    UI::UIToolkit::DrawButton(btnShoot, "DISPARAR ESFERA (A)",
                              btnShoot.Contains(touch.x, touch.y), UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(btnExplode, "ONDA EXPANSIVA (X)",
                              btnExplode.Contains(touch.x, touch.y), UI::TextScale::Normal);
    UI::UIToolkit::DrawButton(btnReset, "REINICIAR ESCENA (Y)",
                              btnReset.Contains(touch.x, touch.y), UI::TextScale::Normal);
    UI::UIToolkit::DrawToggleButton(btnWire, "WIREFRAME (L)", m_showDebugWireframe,
                                    btnWire.Contains(touch.x, touch.y), UI::TextScale::Normal);

    // 4. Pie de HUD Unificado
    UI::UIToolkit::DrawHudFooter({
        { "L-STICK: MOVER KCC | B: SALTAR | R-STICK: CAMARA | ZL/ZR: ZOOM", ColorRGBA::Yellow() },
        { "A: DISPARAR | X: ONDA EXPANSIVA | Y: REINICIAR | L: WIREFRAME | -: PAUSA | +: MENU", ColorRGBA::Cyan() }
    });
}

} // namespace WiiU
