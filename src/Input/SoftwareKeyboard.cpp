#include "Input/SoftwareKeyboard.hpp"
#include "Core/Localization/LocalizationManager.hpp"
#include "Core/Window.hpp"
#include "Core/MemoryManager.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

#if defined(__WIIU__) || defined(__wiiu__)
#include <coreinit/filesystem.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <gx2/clear.h>
#include <gx2/context.h>
#include <gx2/display.h>
#include <gx2/event.h>
#include <gx2/registers.h>
#include <gx2/swap.h>
#include <nn/swkbd.h>
#include <padscore/kpad.h>
#include <proc_ui/procui.h>
#include <vpad/input.h>
#else
#include "Audio/AudioManager.hpp"
#include "Graphics/Color.hpp"
#include "Graphics/Render2D.hpp"
#include "Input/InputManager.hpp"
#include "Platform/PlatformClock.hpp"
#include "Platform/Win32InputBridge.hpp"
#endif

namespace Neleo {

#if defined(__WIIU__) || defined(__wiiu__)
namespace {

void AsciiToUtf16(const char* src, char16_t* dst, size_t dstCap) noexcept {
    if (!dst || dstCap == 0u) return;
    size_t i = 0u;
    if (src) {
        while (src[i] != '\0' && i + 1u < dstCap) {
            dst[i] = static_cast<char16_t>(static_cast<unsigned char>(src[i]));
            ++i;
        }
    }
    dst[i] = u'\0';
}

void Utf16ToAscii(const char16_t* src, char* dst, size_t dstCap) noexcept {
    if (!dst || dstCap == 0u) return;
    size_t i = 0u;
    if (src) {
        while (src[i] != u'\0' && i + 1u < dstCap) {
            const char16_t ch = src[i];
            dst[i] = (ch >= 0x20 && ch <= 0x7E) ? static_cast<char>(ch) : '?';
            ++i;
        }
    }
    dst[i] = '\0';
}

bool PumpProcUIStep() noexcept {
    if (!ProcUIIsRunning()) {
        return false;
    }
    const ProcUIStatus status = ProcUIProcessMessages(TRUE);
    if (status == PROCUI_STATUS_EXITING) {
        Window::NotifyExitingProcUI();
        return false;
    }
    if (status == PROCUI_STATUS_RELEASE_FOREGROUND) {
        if (Window::HasForeground()) {
            GX2DrawDone();
        }
        Window::ReleaseForeground();
        ProcUIDrawDoneRelease();
        return true;
    }
    if (status == PROCUI_STATUS_IN_FOREGROUND && !Window::HasForeground()) {
        Window::AcquireForeground();
    }
    return true;
}

} // namespace
#endif

bool SoftwareKeyboard::Show(const char* hintText,
                            const char* initialText,
                            char* outText,
                            size_t outCapacity,
                            KeyboardInputMode mode,
                            int32_t maxLength) noexcept {
    if (!outText || outCapacity < 2u) {
        return false;
    }

#if defined(__WIIU__) || defined(__wiiu__)
    if (!Window::HasForeground() || Window::IsExitingProcUI()) {
        return false;
    }

    auto* colorBuffer = static_cast<GX2ColorBuffer*>(Window::GetActiveNativeColorBuffer());
    auto* contextState = static_cast<GX2ContextState*>(Window::GetActiveNativeContextState());
    if (!colorBuffer || !contextState) {
        return false;
    }

    // Drenar cola GPU antes de inicializar nn::swkbd
    GX2DrawDone();

    const uint32_t workMemSize = nn::swkbd::GetWorkMemorySize(0);
    if (workMemSize == 0u) {
        return false;
    }

    void* workMemory = MemoryManager::Instance().AllocAligned(
        4096u, workMemSize, MemoryTag::General, "Swkbd_WorkMemory");
    if (!workMemory) {
        return false;
    }
    std::memset(workMemory, 0, workMemSize);

    FSClient* fsClient = static_cast<FSClient*>(
        MemoryManager::Instance().AllocAligned(
            64u, sizeof(FSClient), MemoryTag::General, "Swkbd_FSClient"));
    if (!fsClient) {
        MemoryManager::Instance().FreeAligned(workMemory);
        return false;
    }
    std::memset(fsClient, 0, sizeof(FSClient));
    FSAddClient(fsClient, FS_ERROR_FLAG_NONE);

    nn::swkbd::CreateArg createArg{};
    createArg.regionType = nn::swkbd::RegionType::USA;
    createArg.workMemory = workMemory;
    createArg.fsClient = fsClient;

    if (!nn::swkbd::Create(createArg)) {
        FSDelClient(fsClient, FS_ERROR_FLAG_NONE);
        void* rawFs = static_cast<void*>(fsClient);
        MemoryManager::Instance().FreeAligned(rawFs);
        MemoryManager::Instance().FreeAligned(workMemory);
        return false;
    }

    char16_t hint16[96]{};
    char16_t init16[64]{};
    AsciiToUtf16(hintText, hint16, 96u);
    AsciiToUtf16(initialText, init16, 64u);

    nn::swkbd::AppearArg appearArg{};
    appearArg.keyboardArg.configArg.languageType =
        LocalizationManager::Instance().IsCurrentLanguage("en")
            ? nn::swkbd::LanguageType::English
            : nn::swkbd::LanguageType::Spanish;
    appearArg.keyboardArg.configArg.controllerType = nn::swkbd::ControllerType::DrcGamepad;
    appearArg.keyboardArg.configArg.disableNewLine = true;
    appearArg.keyboardArg.configArg.showWordSuggestions = false;

    if (mode == KeyboardInputMode::IPv4) {
        appearArg.keyboardArg.configArg.keyboardMode = nn::swkbd::KeyboardMode::Numpad;
        appearArg.keyboardArg.configArg.numpadCharLeft = u'.';
        appearArg.keyboardArg.configArg.numpadCharRight = u'\0';
    } else {
        appearArg.keyboardArg.configArg.keyboardMode = nn::swkbd::KeyboardMode::Full;
    }

    appearArg.inputFormArg.type = nn::swkbd::InputFormType::Default;
    appearArg.inputFormArg.initialText = (init16[0] != u'\0') ? init16 : nullptr;
    appearArg.inputFormArg.hintText = (hint16[0] != u'\0') ? hint16 : nullptr;
    appearArg.inputFormArg.maxTextLength = std::clamp<int32_t>(maxLength, 1, static_cast<int32_t>(outCapacity - 1u));
    appearArg.inputFormArg.higlightInitialText = true;

    bool accepted = false;
    bool dismissing = false;

    if (nn::swkbd::AppearInputForm(appearArg)) {
        VPADStatus vpadStatus{};
        KPADStatus kpadStatus[4]{};

        while (PumpProcUIStep()) {
            if (!Window::HasForeground() || Window::IsExitingProcUI()) {
                OSSleepTicks(OSMillisecondsToTicks(16));
                continue;
            }

            // Re-obtener punteros activos por si se re-adquirió foreground tras Menú HOME
            colorBuffer = static_cast<GX2ColorBuffer*>(Window::GetActiveNativeColorBuffer());
            contextState = static_cast<GX2ContextState*>(Window::GetActiveNativeContextState());
            if (!colorBuffer || !contextState) {
                break;
            }

            // Lectura directa de VPAD / KPAD para nn::swkbd::Calc
            VPADReadError vpadErr = VPAD_READ_NO_SAMPLES;
            const int32_t vpadCount = VPADRead(VPAD_CHAN_0, &vpadStatus, 1, &vpadErr);

            nn::swkbd::ControllerInfo ctrlInfo{};
            if (vpadCount > 0 && vpadErr == VPAD_READ_SUCCESS) {
                // Calibrar coordenadas táctiles del GamePad (0..4095 -> 1280x720) para que nn::swkbd reconozca toques
                VPADGetTPCalibratedPoint(VPAD_CHAN_0, &vpadStatus.tpNormal, &vpadStatus.tpNormal);
                VPADGetTPCalibratedPoint(VPAD_CHAN_0, &vpadStatus.tpFiltered1, &vpadStatus.tpFiltered1);
                VPADGetTPCalibratedPoint(VPAD_CHAN_0, &vpadStatus.tpFiltered2, &vpadStatus.tpFiltered2);
                ctrlInfo.vpad = &vpadStatus;
            }
            for (int c = 0; c < 4; ++c) {
                KPADError kErr = KPAD_ERROR_NO_SAMPLES;
                const int32_t kCount = KPADReadEx(static_cast<KPADChan>(c), &kpadStatus[c], 1, &kErr);
                if (kCount > 0 && kErr == KPAD_ERROR_OK) {
                    ctrlInfo.kpad[c] = &kpadStatus[c];
                }
            }

            nn::swkbd::Calc(ctrlInfo);
            if (nn::swkbd::IsNeedCalcSubThreadFont()) {
                nn::swkbd::CalcSubThreadFont();
            }
            if (nn::swkbd::IsNeedCalcSubThreadPredict()) {
                nn::swkbd::CalcSubThreadPredict();
            }

            if (!dismissing) {
                if (nn::swkbd::IsDecideOkButton(nullptr)) {
                    const char16_t* result16 = nn::swkbd::GetInputFormString();
                    if (result16) {
                        Utf16ToAscii(result16, outText, outCapacity);
                        accepted = true;
                    }
                    nn::swkbd::DisappearInputForm();
                    dismissing = true;
                } else if (nn::swkbd::IsDecideCancelButton(nullptr)) {
                    nn::swkbd::DisappearInputForm();
                    dismissing = true;
                }
            } else if (nn::swkbd::GetStateInputForm() == nn::swkbd::State::Hidden) {
                break;
            }

            // Renderizar vista TV y vista DRC sobre el ColorBuffer nativo de Window
            const float cbW = static_cast<float>(colorBuffer->surface.width);
            const float cbH = static_cast<float>(colorBuffer->surface.height);

            GX2ClearColor(colorBuffer, 0.04f, 0.07f, 0.13f, 1.0f);
            GX2SetContextState(contextState);
            GX2SetColorBuffer(colorBuffer, GX2_RENDER_TARGET_0);
            GX2SetViewport(0.0f, 0.0f, cbW, cbH, 0.0f, 1.0f);
            GX2SetScissor(0u, 0u, colorBuffer->surface.width, colorBuffer->surface.height);
            nn::swkbd::DrawTV();
            GX2CopyColorBufferToScanBuffer(colorBuffer, GX2_SCAN_TARGET_TV);

            GX2ClearColor(colorBuffer, 0.04f, 0.07f, 0.13f, 1.0f);
            GX2SetContextState(contextState);
            GX2SetColorBuffer(colorBuffer, GX2_RENDER_TARGET_0);
            GX2SetViewport(0.0f, 0.0f, cbW, cbH, 0.0f, 1.0f);
            GX2SetScissor(0u, 0u, colorBuffer->surface.width, colorBuffer->surface.height);
            nn::swkbd::DrawDRC();
            GX2CopyColorBufferToScanBuffer(colorBuffer, GX2_SCAN_TARGET_DRC);

            GX2SwapScanBuffers();
            GX2Flush();
            GX2WaitForVsync();
        }
    }

    if (Window::HasForeground() && !Window::IsExitingProcUI()) {
        GX2DrawDone();
    }

    nn::swkbd::Destroy();
    FSDelClient(fsClient, FS_ERROR_FLAG_NONE);
    void* rawFs = static_cast<void*>(fsClient);
    MemoryManager::Instance().FreeAligned(rawFs);
    MemoryManager::Instance().FreeAligned(workMemory);

    // Restaurar estado GX2 del motor tras el cierre de nn::swkbd
    if (Window::HasForeground() && !Window::IsExitingProcUI()) {
        colorBuffer = static_cast<GX2ColorBuffer*>(Window::GetActiveNativeColorBuffer());
        contextState = static_cast<GX2ContextState*>(Window::GetActiveNativeContextState());
        if (colorBuffer && contextState) {
            GX2SetContextState(contextState);
            GX2SetColorBuffer(colorBuffer, GX2_RENDER_TARGET_0);
            GX2SetViewport(0.0f, 0.0f,
                           static_cast<float>(colorBuffer->surface.width),
                           static_cast<float>(colorBuffer->surface.height),
                           0.0f, 1.0f);
            GX2SetScissor(0u, 0u, colorBuffer->surface.width, colorBuffer->surface.height);
        }
        RHIPipelineCache::Instance().Invalidate();
    }

    return accepted;
#else
    // =========================================================================
    // TECLADO VIRTUAL MODAL IN-ENGINE ZERO-ALLOC (Windows x64 / ASUS ROG Ally)
    // Soporta simultáneamente: Teclado físico (WM_CHAR), Mando XInput y Ratón/Táctil
    // =========================================================================
    enum class KeyAction : uint8_t {
        Char = 0,
        Backspace,
        Clear,
        Loopback,
        Space,
        Confirm,
        Cancel
    };

    struct VirtualKeyDef {
        const char* label;
        KeyAction action;
        char ch;
    };

    static constexpr int kIpCols = 4;
    static constexpr int kIpRows = 4;
    static constexpr VirtualKeyDef kIpKeys[kIpRows][kIpCols] = {
        { {"1", KeyAction::Char, '1'}, {"2", KeyAction::Char, '2'}, {"3", KeyAction::Char, '3'}, {"BORRAR",    KeyAction::Backspace, '\0'} },
        { {"4", KeyAction::Char, '4'}, {"5", KeyAction::Char, '5'}, {"6", KeyAction::Char, '6'}, {"LIMPIAR",   KeyAction::Clear,     '\0'} },
        { {"7", KeyAction::Char, '7'}, {"8", KeyAction::Char, '8'}, {"9", KeyAction::Char, '9'}, {"127.0.0.1", KeyAction::Loopback,  '\0'} },
        { {".", KeyAction::Char, '.'}, {"0", KeyAction::Char, '0'}, {"OK", KeyAction::Confirm, '\0'}, {"CANCELAR", KeyAction::Cancel, '\0'} }
    };

    static constexpr int kFullCols = 10;
    static constexpr int kFullRows = 4;
    static constexpr VirtualKeyDef kFullKeys[kFullRows][kFullCols] = {
        { {"1", KeyAction::Char, '1'}, {"2", KeyAction::Char, '2'}, {"3", KeyAction::Char, '3'}, {"4", KeyAction::Char, '4'}, {"5", KeyAction::Char, '5'},
          {"6", KeyAction::Char, '6'}, {"7", KeyAction::Char, '7'}, {"8", KeyAction::Char, '8'}, {"9", KeyAction::Char, '9'}, {"0", KeyAction::Char, '0'} },
        { {"Q", KeyAction::Char, 'Q'}, {"W", KeyAction::Char, 'W'}, {"E", KeyAction::Char, 'E'}, {"R", KeyAction::Char, 'R'}, {"T", KeyAction::Char, 'T'},
          {"Y", KeyAction::Char, 'Y'}, {"U", KeyAction::Char, 'U'}, {"I", KeyAction::Char, 'I'}, {"O", KeyAction::Char, 'O'}, {"P", KeyAction::Char, 'P'} },
        { {"A", KeyAction::Char, 'A'}, {"S", KeyAction::Char, 'S'}, {"D", KeyAction::Char, 'D'}, {"F", KeyAction::Char, 'F'}, {"G", KeyAction::Char, 'G'},
          {"H", KeyAction::Char, 'H'}, {"J", KeyAction::Char, 'J'}, {"K", KeyAction::Char, 'K'}, {"L", KeyAction::Char, 'L'}, {"DEL", KeyAction::Backspace, '\0'} },
        { {"Z", KeyAction::Char, 'Z'}, {"X", KeyAction::Char, 'X'}, {"C", KeyAction::Char, 'C'}, {"V", KeyAction::Char, 'V'}, {"B", KeyAction::Char, 'B'},
          {"N", KeyAction::Char, 'N'}, {"M", KeyAction::Char, 'M'}, {"SPC", KeyAction::Space, ' '}, {"OK", KeyAction::Confirm, '\0'}, {"ESC", KeyAction::Cancel, '\0'} }
    };

    const size_t maxAllowed = static_cast<size_t>(
        std::clamp<int32_t>(maxLength, 1, static_cast<int32_t>(outCapacity - 1u)));
    const size_t bufCap = std::min<size_t>(maxAllowed, 63u);

    char buffer[64]{};
    size_t len = 0u;
    if (initialText) {
        while (initialText[len] != '\0' && len < bufCap) {
            if (IsCharAllowed(initialText[len], mode)) {
                buffer[len] = initialText[len];
                ++len;
            } else {
                break;
            }
        }
        buffer[len] = '\0';
    }

    const bool isIpMode = (mode == KeyboardInputMode::IPv4);
    const int numRows = isIpMode ? kIpRows : kFullRows;
    const int numCols = isIpMode ? kIpCols : kFullCols;
    int selRow = 0;
    int selCol = 0;

    auto& bridge = Platform::GetWin32InputBridge();
    struct ScopedTextCapture {
        Platform::Win32InputBridge& b;
        explicit ScopedTextCapture(Platform::Win32InputBridge& ref) noexcept : b(ref) { b.textInputCapture = true; }
        ~ScopedTextCapture() noexcept { b.textInputCapture = false; }
    } captureGuard(bridge);

    bridge.ConsumeFrameEdges();
    InputManager::Instance().ClearFrameTriggers();

    bool running = true;
    bool accepted = false;
    bool done = false;
    float navCooldown = 0.0f;
    float blinkTimer = 0.0f;

    auto appendChar = [&](char ch) noexcept {
        if (len < bufCap && IsCharAllowed(ch, mode)) {
            buffer[len++] = ch;
            buffer[len] = '\0';
        }
    };

    auto applyAction = [&](const VirtualKeyDef& key) noexcept {
        AudioManager::Instance().PlayClick();
        switch (key.action) {
            case KeyAction::Char:
                appendChar(key.ch);
                break;
            case KeyAction::Space:
                appendChar(' ');
                break;
            case KeyAction::Backspace:
                if (len > 0u) {
                    buffer[--len] = '\0';
                }
                break;
            case KeyAction::Clear:
                len = 0u;
                buffer[0] = '\0';
                break;
            case KeyAction::Loopback: {
                constexpr const char* kLoopback = "127.0.0.1";
                len = 0u;
                while (kLoopback[len] != '\0' && len < bufCap) {
                    buffer[len] = kLoopback[len];
                    ++len;
                }
                buffer[len] = '\0';
                break;
            }
            case KeyAction::Confirm:
                accepted = true;
                done = true;
                break;
            case KeyAction::Cancel:
                accepted = false;
                done = true;
                break;
        }
    };

    // Geometría de la grilla (coordenadas virtuales 1280x720)
    const int panelX = isIpMode ? 240 : 80;
    const int panelY = 90;
    const int panelW = isIpMode ? 800 : 1120;
    const int panelH = 540;
    const int gridX = panelX + 36;
    const int gridY = panelY + 175;
    const int gridW = panelW - 72;
    const int gridH = 290;
    const int gap = 10;
    const int keyW = (gridW - gap * (numCols - 1)) / numCols;
    const int keyH = (gridH - gap * (numRows - 1)) / numRows;

    while (!done && running) {
        constexpr float kDt = 1.0f / 60.0f;
        blinkTimer += kDt;
        if (navCooldown > 0.0f) {
            navCooldown = std::max(0.0f, navCooldown - kDt);
        }

        // 1. Bombear eventos Win32 y capturar WM_CHAR antes de que InputManager limpie edges
        Window::PumpPlatformEvents();
        char localChars[Platform::Win32InputBridge::MAX_TYPED_CHARS]{};
        const uint8_t localCount = std::min<uint8_t>(
            bridge.typedCharCount,
            static_cast<uint8_t>(Platform::Win32InputBridge::MAX_TYPED_CHARS));
        for (uint8_t i = 0u; i < localCount; ++i) {
            localChars[i] = bridge.typedChars[i];
        }
        bridge.ClearTypedChars();

        if (bridge.quitRequested) {
            running = false;
            accepted = false;
            break;
        }

        // 2. Actualizar entrada unificada (XInput + ratón/táctil + teclas)
        InputManager::Instance().Update(running, kDt);
        if (!running) {
            accepted = false;
            break;
        }

        // 3. Procesar entrada de teclado físico (WM_CHAR)
        for (uint8_t i = 0u; i < localCount && !done; ++i) {
            const char ch = localChars[i];
            if (ch == 0x08) { // Backspace
                if (len > 0u) {
                    buffer[--len] = '\0';
                    AudioManager::Instance().PlayClick();
                }
            } else if (ch == 0x0D) { // Enter
                AudioManager::Instance().PlayClick();
                accepted = true;
                done = true;
            } else if (ch == 0x1B) { // Escape
                AudioManager::Instance().PlayClick();
                accepted = false;
                done = true;
            } else if (IsCharAllowed(ch, mode)) {
                appendChar(ch);
                AudioManager::Instance().PlayClick();
            }
        }

        // 4. Procesar mando XInput / D-Pad / Stick / Ratón / Táctil
        if (!done) {
            const auto& pad = InputManager::Instance().GetGamepad(0);

            int dRow = 0;
            int dCol = 0;
            if (pad.WasJustPressed(VirtualButton::DPadUp))    dRow = -1;
            if (pad.WasJustPressed(VirtualButton::DPadDown))  dRow = 1;
            if (pad.WasJustPressed(VirtualButton::DPadLeft))  dCol = -1;
            if (pad.WasJustPressed(VirtualButton::DPadRight)) dCol = 1;

            if (dRow == 0 && dCol == 0 && navCooldown <= 0.0f) {
                if (pad.leftStick.y > 0.55f)       dRow = -1;
                else if (pad.leftStick.y < -0.55f) dRow = 1;
                if (pad.leftStick.x < -0.55f)      dCol = -1;
                else if (pad.leftStick.x > 0.55f)  dCol = 1;
                if (dRow != 0 || dCol != 0) {
                    navCooldown = 0.18f;
                }
            }

            if (dRow != 0 || dCol != 0) {
                selRow = (selRow + dRow + numRows) % numRows;
                selCol = (selCol + dCol + numCols) % numCols;
                AudioManager::Instance().PlayClick();
            }

            if (pad.WasJustPressed(VirtualButton::A)) {
                const VirtualKeyDef& k = isIpMode ? kIpKeys[selRow][selCol] : kFullKeys[selRow][selCol];
                applyAction(k);
            } else if (pad.WasJustPressed(VirtualButton::X)) {
                if (len > 0u) {
                    buffer[--len] = '\0';
                    AudioManager::Instance().PlayClick();
                }
            } else if (pad.WasJustPressed(VirtualButton::Plus)) {
                AudioManager::Instance().PlayClick();
                accepted = true;
                done = true;
            } else if (pad.WasJustPressed(VirtualButton::B)) {
                AudioManager::Instance().PlayClick();
                accepted = false;
                done = true;
            } else if (pad.touch.justPressed) {
                const int tx = static_cast<int>(pad.touch.x);
                const int ty = static_cast<int>(pad.touch.y);
                for (int r = 0; r < numRows && !done; ++r) {
                    for (int c = 0; c < numCols && !done; ++c) {
                        const int kx = gridX + c * (keyW + gap);
                        const int ky = gridY + r * (keyH + gap);
                        if (tx >= kx && tx <= kx + keyW && ty >= ky && ty <= ky + keyH) {
                            selRow = r;
                            selCol = c;
                            const VirtualKeyDef& k = isIpMode ? kIpKeys[r][c] : kFullKeys[r][c];
                            applyAction(k);
                        }
                    }
                }
            }
        }

        // 5. Renderizado 2D Zero-Alloc sobre OpenGL 3.3 Core respetando viewport 16:9
        Window::ClearActive(ColorRGBA{8, 14, 26, 255});
        Render2D::Begin();

        Render2D::DrawBox(panelX, panelY, panelW, panelH,
                          ColorRGBA{14, 24, 42, 245}, ColorRGBA{85, 190, 255, 255});

        const char* title = (hintText && hintText[0] != '\0') ? hintText : NELEO_LOC_CSTR("swkbd.title");
        Render2D::DrawPixelString(title, panelX + 36, panelY + 28, 2, ColorRGBA::Cyan());

        // Campo de texto editable con cursor parpadeante
        Render2D::DrawBox(panelX + 36, panelY + 78, panelW - 72, 66,
                          ColorRGBA{6, 12, 22, 255}, ColorRGBA::Yellow());
        const bool showCursor = (static_cast<int>(blinkTimer * 2.5f) & 1) == 0;
        char displayBuf[80]{};
        std::snprintf(displayBuf, sizeof(displayBuf), "%s%s", buffer, showCursor ? "_" : "");
        Render2D::DrawPixelString(displayBuf, panelX + 54, panelY + 98, 3, ColorRGBA::White());

        char counterBuf[32]{};
        std::snprintf(counterBuf, sizeof(counterBuf), "%zu/%zu", len, bufCap);
        Render2D::DrawPixelString(counterBuf, panelX + panelW - 120, panelY + 102, 2, ColorRGBA{170, 210, 240, 255});

        // Grilla de teclas
        for (int r = 0; r < numRows; ++r) {
            for (int c = 0; c < numCols; ++c) {
                const VirtualKeyDef& k = isIpMode ? kIpKeys[r][c] : kFullKeys[r][c];
                const int kx = gridX + c * (keyW + gap);
                const int ky = gridY + r * (keyH + gap);
                const bool focused = (r == selRow && c == selCol);

                ColorRGBA fill{24, 42, 72, 240};
                ColorRGBA border{95, 160, 220, 255};
                if (k.action == KeyAction::Confirm) {
                    fill = ColorRGBA{18, 72, 42, 245};
                    border = ColorRGBA::Green();
                } else if (k.action == KeyAction::Cancel) {
                    fill = ColorRGBA{78, 24, 30, 245};
                    border = ColorRGBA::Red();
                } else if (k.action == KeyAction::Backspace || k.action == KeyAction::Clear) {
                    fill = ColorRGBA{64, 46, 20, 245};
                    border = ColorRGBA::Orange();
                }
                if (focused) {
                    fill = ColorRGBA{46, 108, 178, 255};
                    border = ColorRGBA::Yellow();
                }

                Render2D::DrawBox(kx, ky, keyW, keyH, fill, border);
                constexpr int kTextScale = 2;
                const int labelPxW = static_cast<int>(std::strlen(k.label)) * 6 * kTextScale;
                const int labelPxH = 7 * kTextScale;
                const int tx = kx + (keyW - labelPxW) / 2;
                const int ty = ky + (keyH - labelPxH) / 2;
                Render2D::DrawPixelString(k.label, tx, ty, kTextScale, ColorRGBA::White());
            }
        }

        Render2D::DrawPixelString(
            NELEO_LOC("swkbd.hint"),
            panelX + 36, panelY + panelH - 36, 1, ColorRGBA{180, 215, 245, 255});

        Render2D::End();
        Window::PresentActive();
        PlatformClock::SleepMs(1u);
    }

    bridge.ConsumeFrameEdges();
    InputManager::Instance().ClearFrameTriggers();

    if (accepted) {
        std::strncpy(outText, buffer, outCapacity - 1u);
        outText[outCapacity - 1u] = '\0';
        return true;
    }
    return false;
#endif
}

} // namespace Neleo
