#include "Core/Diagnostics/EngineSelfTestSuite.hpp"
#include "Core/Diagnostics/CoreSelfTest.hpp"
#include "Core/MemoryManager.hpp"
#include "Core/ECS/ECSWorld.hpp"
#include "Core/JobSystem.hpp"
#include "Core/ResourceMonitor.hpp"
#include "Core/Physics/PhysicsSelfTest.hpp"
#include "Audio/AudioSelfTest.hpp"
#include "Input/InputSelfTest.hpp"
#include "Graphics/GraphicsSelfTest.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include "Graphics/UI/UIToolkit.hpp"
#include "Core/AI/AISelfTest.hpp"
#include "Net/NetSocket.hpp"
#include "Scene/ModalStack.hpp"
#include <cmath>
#include <memory>

namespace WiiU::Diagnostics {

namespace {

class SelfClosingTestOverlay final : public ISceneOverlay {
public:
    explicit SelfClosingTestOverlay(ModalStack& owner) noexcept : m_owner(owner) {}
    void Update(float /*dt*/) override { m_owner.Pop(); }
    void Render() override {}
    [[nodiscard]] const char* GetOverlayName() const noexcept override { return "SelfClosingOverlay"; }
private:
    ModalStack& m_owner;
};

} // namespace

EngineSelfTestReport EngineSelfTestSuite::RunAll() {
    EngineSelfTestReport report{};
    report.Reset();

    // 1. engine_core (3 verificadores existentes + 4 CoreSelfTest = 7 pruebas)
    {
        const bool ok = MemoryManager::Instance().VerifyMemoryIntegrity();
        report.Record("CORE", "MEMORY_MANAGER", ok, ok ? "MEM2/ARENAS OK" : "FALLO MEMORY MANAGER");
    }
    {
        const bool ok = ECSWorld::VerifyECSIntegrity();
        report.Record("CORE", "ECS_WORLD", ok, ok ? "ENTITIES/COMPONENTS OK" : "FALLO ECS WORLD");
    }
    {
        const bool ok = JobSystem::VerifyJobSystemIntegrity();
        report.Record("CORE", "JOB_SYSTEM", ok, ok ? "SMP WORKERS OK" : "FALLO JOB SYSTEM");
    }
    CoreSelfTest::RunAll(report);

    // 2. engine_physics (2 pruebas)
    Physics::PhysicsSelfTest::RunAll(report);

    // 3. engine_audio (2 pruebas)
    Audio::AudioSelfTest::RunAll(report);

    // 4. engine_input (2 pruebas)
    Input::InputSelfTest::RunAll(report);

    // 5. engine_graphics (8 GraphicsSelfTest + 2 verificadores existentes = 10 pruebas)
    Graphics::GraphicsSelfTest::RunAll(report);
    {
        const bool ok = StaticMesh::VerifyMeshIntegrity();
        report.Record("GFX", "STATIC_MESH_INT", ok, ok ? "WMESH HEADER/STRIDE OK" : "FALLO STATIC MESH");
    }
    {
        const bool ok = UI::UIToolkit::VerifyUIToolkitIntegrity();
        report.Record("GFX", "UI_TOOLKIT_INT", ok, ok ? "LAYOUT/WIDGETS OK" : "FALLO UI TOOLKIT");
    }

    // 6. engine_ai (2 pruebas)
    AI::AISelfTest::RunAll(report);

    // 7. engine_net (1 suite compuesta: HAL + BitStream + Endianness + Sequence + SnapshotInterpolator)
    {
        const bool ok = NetPlatform::VerifyNetIntegrity();
        report.Record("NET", "NET_PLATFORM_INT", ok, ok ? "HAL/BITSTREAM/REPL OK" : "FALLO ENGINE_NET");
    }

    // 8. engine_framework (2 pruebas: ModalStack anti bleed-through + ResourceMonitor)
    {
        ModalStack stack;
        const bool pushed = stack.Push(std::make_unique<SelfClosingTestOverlay>(stack));
        const bool blockedBefore = stack.IsBlockingSceneUpdate() && stack.IsBlockingSceneInput() && (stack.GetDepth() == 1u);
        stack.Update(0.016f);
        // Tras auto-cerrarse en Update(), la pila queda vacía pero blockedBefore impide bleed-through en el mismo frame
        const bool closedCleanly = stack.IsEmpty() && !stack.IsBlockingSceneUpdate();
        const bool ok = pushed && blockedBefore && closedCleanly;
        report.Record("FW", "MODAL_STACK_LIFECYCLE", ok, ok ? "PUSH/SELF-POP/NO-BLEED OK" : "FALLO MODAL STACK");
    }
    {
        const auto& m = ResourceMonitor::Instance().GetMetrics();
        const bool ok = std::isfinite(m.fps) &&
                        std::isfinite(m.mem1Percent) && (m.mem1Percent >= 0.0f) && (m.mem1Percent <= 100.0f) &&
                        std::isfinite(m.mem2Percent) && (m.mem2Percent >= 0.0f) && (m.mem2Percent <= 100.0f);
        report.Record("FW", "RESOURCE_MONITOR_FINITE", ok, ok ? "TELEMETRIA FINITA OK" : "FALLO RESOURCE MONITOR");
    }

    return report;
}

} // namespace WiiU::Diagnostics
