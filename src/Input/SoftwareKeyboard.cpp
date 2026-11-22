#include "Input/SoftwareKeyboard.hpp"
#include "Core/Window.hpp"
#include "Core/MemoryManager.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include <algorithm>
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
#endif

namespace WiiU {

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
    appearArg.keyboardArg.configArg.languageType = nn::swkbd::LanguageType::Spanish;
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
    (void)hintText;
    (void)initialText;
    (void)mode;
    (void)maxLength;
    return false;
#endif
}

} // namespace WiiU
