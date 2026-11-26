#include "Input/InputSelfTest.hpp"
#include "Input/VirtualGamepad.hpp"
#include "Input/InputContextStack.hpp"
#include "Input/SoftwareKeyboard.hpp"
#include <cmath>

namespace Neleo::Input {

void InputSelfTest::RunAll(Diagnostics::EngineSelfTestReport& outReport) {
    // 1. ApplyRadialDeadzone: 0 dentro del radio muerto, remapeo suave y magnitud <= 1 en diagonal
    {
        const AnalogStick dead = ApplyRadialDeadzone(0.08f, -0.06f, 0.18f);
        const AnalogStick mid  = ApplyRadialDeadzone(0.59f, 0.0f, 0.18f);
        const AnalogStick diag = ApplyRadialDeadzone(1.0f, 1.0f, 0.18f);

        const bool ok = (dead.magnitude == 0.0f) && (dead.x == 0.0f) && (dead.y == 0.0f) &&
                        (mid.magnitude > 0.40f) && (mid.magnitude < 0.60f) &&
                        (diag.magnitude <= 1.0001f) && (diag.magnitude > 0.99f);
        outReport.Record("INPUT", "RADIAL_DEADZONE", ok,
                         ok ? "DEADZONE RADIAL OK" : "FALLO RADIAL DEADZONE");
    }

    // 2. InputContextStack: prioridad, bloqueo de capas inferiores, consumo y pop
    {
        InputContextStack stack;
        stack.Clear();
        const bool p1 = stack.PushContext(InputContext::Gameplay3D, 10u, false);
        const bool p2 = stack.PushContext(InputContext::UIModal, 50u, true);
        const bool modalAllowed = stack.IsActionAllowed(InputContext::UIModal, InputAction::Interact);
        const bool gameBlocked  = !stack.IsActionAllowed(InputContext::Gameplay3D, InputAction::Interact);

        stack.ConsumeAction(InputAction::Interact);
        const bool consumed = stack.IsActionConsumed(InputAction::Interact) &&
                              !stack.IsActionAllowed(InputContext::UIModal, InputAction::Interact);
        stack.ResetFrameConsumptions();
        const bool popped = stack.PopContext() && (stack.GetDepth() == 1u) &&
                            stack.IsActionAllowed(InputContext::Gameplay3D, InputAction::Interact);

        const bool ok = p1 && p2 && modalAllowed && gameBlocked && consumed && popped;
        outReport.Record("INPUT", "INPUT_CONTEXT_STACK", ok,
                         ok ? "CONTEXT STACK/BLOCK OK" : "FALLO INPUT CONTEXT STACK");
    }

    // 3. SoftwareKeyboard::IsCharAllowed (filtro IPv4 vs FullText multiplataforma)
    {
        const bool ipOk = SoftwareKeyboard::IsCharAllowed('0', KeyboardInputMode::IPv4) &&
                          SoftwareKeyboard::IsCharAllowed('9', KeyboardInputMode::IPv4) &&
                          SoftwareKeyboard::IsCharAllowed('.', KeyboardInputMode::IPv4) &&
                          !SoftwareKeyboard::IsCharAllowed('A', KeyboardInputMode::IPv4) &&
                          !SoftwareKeyboard::IsCharAllowed(' ', KeyboardInputMode::IPv4);
        const bool fullOk = SoftwareKeyboard::IsCharAllowed('A', KeyboardInputMode::FullText) &&
                            SoftwareKeyboard::IsCharAllowed(' ', KeyboardInputMode::FullText) &&
                            !SoftwareKeyboard::IsCharAllowed('\n', KeyboardInputMode::FullText);
        const bool ok = ipOk && fullOk;
        outReport.Record("INPUT", "SWKBD_CHAR_FILTER", ok,
                         ok ? "FILTRO IPV4+FULLTEXT OK" : "FALLO FILTRO TECLADO VIRTUAL");
    }
}

} // namespace Neleo::Input
