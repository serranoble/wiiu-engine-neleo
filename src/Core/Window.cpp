#include "Core/Window.hpp"
#include "Core/MemoryManager.hpp"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <utility>

#if defined(__WIIU__) || defined(__wiiu__)
#include <coreinit/foreground.h>
#include <coreinit/memexpheap.h>
#include <coreinit/memfrmheap.h>
#include <coreinit/memheap.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <gx2/clear.h>
#include <gx2/context.h>
#include <gx2/display.h>
#include <gx2/enum.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/state.h>
#include <gx2/surface.h>
#include <gx2/swap.h>
#include <gx2r/mem.h>
#include <gx2r/resource.h>
#include <malloc.h>
#include <proc_ui/procui.h>
#include <sysapp/launch.h>
#else
#include <SDL2/SDL.h>
#endif

namespace WiiU {

namespace {

#if defined(__WIIU__) || defined(__wiiu__)

constexpr uint32_t GFX_HEAP_TAG = 0x5755444Du; // 'WUDM'
constexpr uint32_t CMD_BUFFER_POOL_SIZE = 0x400000u; // 4 MB

void* s_cmdBufferPool = nullptr;
bool s_gx2Initialized = false;
std::atomic<bool> s_hasForeground{false};
std::atomic<bool> s_exitingProcUI{false};
bool s_tvDrcEnabled = false;
bool s_procUICallbacksRegistered = false;
uint32_t s_swapInterval = 1u;

GX2TVRenderMode s_tvRenderMode = GX2_TV_RENDER_MODE_WIDE_720P;
uint32_t s_tvWidth = 1280u;
uint32_t s_tvHeight = 720u;
uint32_t s_tvScanBufferSize = 0u;
void* s_tvScanBuffer = nullptr;

GX2DrcRenderMode s_drcRenderMode = GX2_DRC_RENDER_MODE_SINGLE;
uint32_t s_drcScanBufferSize = 0u;
void* s_drcScanBuffer = nullptr;

MEMHeapHandle s_fgExpHeap = nullptr;
MEMHeapHandle s_mem1ExpHeap = nullptr;

GX2ContextState* s_defaultContextState = nullptr;
GX2ColorBuffer s_mainColorBuffer{};

OSTime s_prevSubmittedTs = 0;
OSTime s_lastSubmittedTs = 0;

// Alocador GX2R residente en DDR3 (MEM2) para sobrevivir a DestroyForegroundHeaps en transiciones HOME Menu/ProcUI.
void* GX2RAllocCallback(GX2RResourceFlags /*flags*/, uint32_t size, uint32_t alignment) {
    if (size == 0u) return nullptr;
    const uint32_t safeAlign = (alignment < 64u) ? 64u : alignment;
    return memalign(safeAlign, size);
}

void GX2RFreeCallback(GX2RResourceFlags /*flags*/, void* block) {
    if (block) {
        std::free(block);
    }
}

bool InitForegroundHeaps() noexcept {
    if (!s_fgExpHeap) {
        MEMHeapHandle fgFrm = MEMGetBaseHeapHandle(MEM_BASE_HEAP_FG);
        if (fgFrm && MEMRecordStateForFrmHeap(fgFrm, GFX_HEAP_TAG)) {
            const uint32_t fgSize = MEMGetAllocatableSizeForFrmHeapEx(fgFrm, 4);
            if (fgSize > 0u) {
                void* fgMem = MEMAllocFromFrmHeapEx(fgFrm, fgSize, 4);
                if (fgMem) {
                    s_fgExpHeap = MEMCreateExpHeapEx(fgMem, fgSize, MEM_HEAP_FLAG_USE_LOCK);
                }
            }
        }
    }
    if (!s_mem1ExpHeap) {
        MEMHeapHandle mem1Frm = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1);
        if (mem1Frm && MEMRecordStateForFrmHeap(mem1Frm, GFX_HEAP_TAG)) {
            const uint32_t mem1Size = MEMGetAllocatableSizeForFrmHeapEx(mem1Frm, 4);
            if (mem1Size > 0u) {
                void* mem1Mem = MEMAllocFromFrmHeapEx(mem1Frm, mem1Size, 4);
                if (mem1Mem) {
                    s_mem1ExpHeap = MEMCreateExpHeapEx(mem1Mem, mem1Size, MEM_HEAP_FLAG_USE_LOCK);
                }
            }
        }
    }
    return (s_fgExpHeap != nullptr) && (s_mem1ExpHeap != nullptr);
}

void DestroyForegroundHeaps() noexcept {
    if (s_mem1ExpHeap) {
        MEMDestroyExpHeap(s_mem1ExpHeap);
        s_mem1ExpHeap = nullptr;
    }
    if (MEMHeapHandle mem1Frm = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1)) {
        MEMFreeByStateToFrmHeap(mem1Frm, GFX_HEAP_TAG);
    }

    if (s_fgExpHeap) {
        MEMDestroyExpHeap(s_fgExpHeap);
        s_fgExpHeap = nullptr;
    }
    if (MEMHeapHandle fgFrm = MEMGetBaseHeapHandle(MEM_BASE_HEAP_FG)) {
        MEMFreeByStateToFrmHeap(fgFrm, GFX_HEAP_TAG);
    }
}

uint32_t OnProcUIAcquire(void* /*arg*/) {
    Window::AcquireForeground();
    return 0u;
}

uint32_t OnProcUIRelease(void* /*arg*/) {
    Window::ReleaseForeground();
    return 0u;
}

uint32_t OnProcUISaveCallback(void* /*arg*/) {
    OSSavesDone_ReadyToRelease();
    return 0u;
}

#else

SDL_Renderer* s_activeRenderer = nullptr;

#endif

} // namespace

Window::Window(const std::string& title, int width, int height)
    : m_title(title), m_width(width), m_height(height) {}

Window::~Window() {
    Shutdown();
}

Window::Window(Window&& other) noexcept
    : m_title(std::move(other.m_title)),
      m_width(other.m_width),
      m_height(other.m_height)
#if !defined(__WIIU__) && !defined(__wiiu__)
      , m_window(other.m_window),
      m_renderer(other.m_renderer)
#endif
{
#if !defined(__WIIU__) && !defined(__wiiu__)
    other.m_window = nullptr;
    other.m_renderer = nullptr;
    s_activeRenderer = m_renderer;
#endif
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        Shutdown();
        m_title = std::move(other.m_title);
        m_width = other.m_width;
        m_height = other.m_height;
#if !defined(__WIIU__) && !defined(__wiiu__)
        m_window = other.m_window;
        m_renderer = other.m_renderer;
        other.m_window = nullptr;
        other.m_renderer = nullptr;
        s_activeRenderer = m_renderer;
#endif
    }
    return *this;
}

void* Window::GetActiveNativeColorBuffer() noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    return (s_hasForeground.load(std::memory_order_acquire) && s_mainColorBuffer.surface.image) ? static_cast<void*>(&s_mainColorBuffer) : nullptr;
#else
    return nullptr;
#endif
}

void* Window::GetActiveNativeContextState() noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    return s_hasForeground.load(std::memory_order_acquire) ? static_cast<void*>(s_defaultContextState) : nullptr;
#else
    return nullptr;
#endif
}

void* Window::GetActiveRendererHandle() noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    return nullptr;
#else
    return static_cast<void*>(s_activeRenderer);
#endif
}

bool Window::HasForeground() noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    return s_hasForeground.load(std::memory_order_acquire);
#else
    return true;
#endif
}

bool Window::IsExitingProcUI() noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    return s_exitingProcUI.load(std::memory_order_acquire);
#else
    return false;
#endif
}

void Window::NotifyExitingProcUI() noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    s_exitingProcUI.store(true, std::memory_order_release);
#endif
}

void Window::AcquireForeground() noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!s_gx2Initialized || s_hasForeground.load(std::memory_order_acquire)) {
        return;
    }

    if (!InitForegroundHeaps()) {
        DestroyForegroundHeaps();
        return;
    }

    // 1. Scanbuffer TV en Foreground Bucket
    if (s_tvScanBufferSize > 0u) {
        s_tvScanBuffer = MEMAllocFromExpHeapEx(s_fgExpHeap, s_tvScanBufferSize, GX2_SCAN_BUFFER_ALIGNMENT);
        if (!s_tvScanBuffer) {
            DestroyForegroundHeaps();
            return;
        }
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU, s_tvScanBuffer, s_tvScanBufferSize);
        GX2SetTVBuffer(s_tvScanBuffer, s_tvScanBufferSize, s_tvRenderMode,
                       GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, GX2_BUFFERING_MODE_DOUBLE);
    }

    // 2. Scanbuffer GamePad DRC en Foreground Bucket
    if (s_drcScanBufferSize > 0u) {
        s_drcScanBuffer = MEMAllocFromExpHeapEx(s_fgExpHeap, s_drcScanBufferSize, GX2_SCAN_BUFFER_ALIGNMENT);
        if (!s_drcScanBuffer) {
            if (s_tvScanBuffer && s_fgExpHeap) {
                MEMFreeToExpHeap(s_fgExpHeap, s_tvScanBuffer);
                s_tvScanBuffer = nullptr;
            }
            DestroyForegroundHeaps();
            return;
        }
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU, s_drcScanBuffer, s_drcScanBufferSize);
        GX2SetDRCBuffer(s_drcScanBuffer, s_drcScanBufferSize, s_drcRenderMode,
                        GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, GX2_BUFFERING_MODE_DOUBLE);
    }

    // 3. ColorBuffer principal 1280x720 en eDRAM/MEM1
    std::memset(&s_mainColorBuffer, 0, sizeof(GX2ColorBuffer));
    s_mainColorBuffer.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    s_mainColorBuffer.surface.width = 1280u;
    s_mainColorBuffer.surface.height = 720u;
    s_mainColorBuffer.surface.depth = 1u;
    s_mainColorBuffer.surface.mipLevels = 1u;
    s_mainColorBuffer.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    s_mainColorBuffer.surface.aa = GX2_AA_MODE1X;
    s_mainColorBuffer.surface.use = GX2_SURFACE_USE_TEXTURE_COLOR_BUFFER_TV;
    s_mainColorBuffer.surface.tileMode = GX2_TILE_MODE_DEFAULT;
    s_mainColorBuffer.viewNumSlices = 1u;
    GX2CalcSurfaceSizeAndAlignment(&s_mainColorBuffer.surface);

    const int cbAlign = (s_mainColorBuffer.surface.alignment < 256u)
                        ? 256 : static_cast<int>(s_mainColorBuffer.surface.alignment);
    s_mainColorBuffer.surface.image = MEMAllocFromExpHeapEx(
        s_mem1ExpHeap, s_mainColorBuffer.surface.imageSize, cbAlign);
    if (!s_mainColorBuffer.surface.image) {
        if (s_drcScanBuffer && s_fgExpHeap) { MEMFreeToExpHeap(s_fgExpHeap, s_drcScanBuffer); s_drcScanBuffer = nullptr; }
        if (s_tvScanBuffer && s_fgExpHeap)  { MEMFreeToExpHeap(s_fgExpHeap, s_tvScanBuffer);  s_tvScanBuffer = nullptr; }
        DestroyForegroundHeaps();
        return;
    }

    std::memset(s_mainColorBuffer.surface.image, 0, s_mainColorBuffer.surface.imageSize);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU, s_mainColorBuffer.surface.image, s_mainColorBuffer.surface.imageSize);
    GX2InitColorBufferRegs(&s_mainColorBuffer);

    if (s_defaultContextState) {
        GX2SetContextState(s_defaultContextState);
    }
    GX2SetColorBuffer(&s_mainColorBuffer, GX2_RENDER_TARGET_0);
    GX2SetViewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
    GX2SetScissor(0, 0, 1280, 720);
    GX2SetTVScale(s_tvWidth, s_tvHeight);
    GX2SetDRCScale(854u, 480u);
    GX2SetSwapInterval(s_swapInterval);

    s_prevSubmittedTs = 0;
    s_lastSubmittedTs = 0;
    s_tvDrcEnabled = false;
    s_hasForeground.store(true, std::memory_order_release);
#endif
}

void Window::ReleaseForeground() noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!s_hasForeground.load(std::memory_order_acquire)) {
        return;
    }
    if (s_gx2Initialized) {
        GX2DrawDone();
    }

    if (s_mainColorBuffer.surface.image && s_mem1ExpHeap) {
        MEMFreeToExpHeap(s_mem1ExpHeap, s_mainColorBuffer.surface.image);
    }
    std::memset(&s_mainColorBuffer, 0, sizeof(GX2ColorBuffer));

    if (s_tvScanBuffer && s_fgExpHeap) {
        MEMFreeToExpHeap(s_fgExpHeap, s_tvScanBuffer);
        s_tvScanBuffer = nullptr;
    }
    if (s_drcScanBuffer && s_fgExpHeap) {
        MEMFreeToExpHeap(s_fgExpHeap, s_drcScanBuffer);
        s_drcScanBuffer = nullptr;
    }

    DestroyForegroundHeaps();

    // Preservar señales TV/DRC encendidas como en WIIU_ForegroundReleased (SDL_wiiuvideo.o) para que el Menú HOME sea visible
    GX2SetTVEnable(TRUE);
    GX2SetDRCEnable(TRUE);

    s_prevSubmittedTs = 0;
    s_lastSubmittedTs = 0;
    s_tvDrcEnabled = false;
    s_hasForeground.store(false, std::memory_order_release);
#endif
}

bool Window::Init() {
    Shutdown();
#if defined(__WIIU__) || defined(__wiiu__)
    s_exitingProcUI.store(false, std::memory_order_release);
    if (!ProcUIIsRunning()) {
        s_procUICallbacksRegistered = false;
        ProcUIInitEx(&OnProcUISaveCallback, nullptr);
    }

    s_cmdBufferPool = MemoryManager::Instance().AllocAligned(
        GX2_COMMAND_BUFFER_ALIGNMENT, CMD_BUFFER_POOL_SIZE, MemoryTag::General, "GX2_CmdBufferPool");
    if (!s_cmdBufferPool) {
        return false;
    }

    uint32_t initAttribs[] = {
        GX2_INIT_CMD_BUF_BASE, reinterpret_cast<uintptr_t>(s_cmdBufferPool),
        GX2_INIT_CMD_BUF_POOL_SIZE, CMD_BUFFER_POOL_SIZE,
        GX2_INIT_ARGC, 0u,
        GX2_INIT_ARGV, 0u,
        GX2_INIT_END
    };
    GX2Init(initAttribs);
    s_gx2Initialized = true;

    switch (GX2GetSystemTVScanMode()) {
        case GX2_TV_SCAN_MODE_NONE:
        case GX2_TV_SCAN_MODE_576I:
        case GX2_TV_SCAN_MODE_480I:
        case GX2_TV_SCAN_MODE_480P:
            s_tvRenderMode = GX2_TV_RENDER_MODE_WIDE_480P;
            s_tvWidth = 854u;
            s_tvHeight = 480u;
            break;
        case GX2_TV_SCAN_MODE_720P:
            s_tvRenderMode = GX2_TV_RENDER_MODE_WIDE_720P;
            s_tvWidth = 1280u;
            s_tvHeight = 720u;
            break;
        case GX2_TV_SCAN_MODE_1080I:
        case GX2_TV_SCAN_MODE_1080P:
        default:
            s_tvRenderMode = GX2_TV_RENDER_MODE_WIDE_1080P;
            s_tvWidth = 1920u;
            s_tvHeight = 1080u;
            break;
    }

    uint32_t unkTv = 0u;
    GX2CalcTVSize(s_tvRenderMode, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,
                  GX2_BUFFERING_MODE_DOUBLE, &s_tvScanBufferSize, &unkTv);

    s_drcRenderMode = GX2GetSystemDRCScanMode();
    if (s_drcRenderMode == GX2_DRC_RENDER_MODE_DISABLED) {
        s_drcRenderMode = GX2_DRC_RENDER_MODE_SINGLE;
    }
    uint32_t unkDrc = 0u;
    GX2CalcDRCSize(s_drcRenderMode, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,
                   GX2_BUFFERING_MODE_DOUBLE, &s_drcScanBufferSize, &unkDrc);

    GX2RSetAllocator(&GX2RAllocCallback, &GX2RFreeCallback);

    s_defaultContextState = static_cast<GX2ContextState*>(
        MemoryManager::Instance().AllocAligned(
            GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState),
            MemoryTag::General, "Window_DefaultContextState"));
    if (!s_defaultContextState) {
        Shutdown();
        return false;
    }
    GX2SetupContextStateEx(s_defaultContextState, TRUE);

    if (!s_procUICallbacksRegistered) {
        ProcUIRegisterCallback(PROCUI_CALLBACK_ACQUIRE, &OnProcUIAcquire, nullptr, 100);
        ProcUIRegisterCallback(PROCUI_CALLBACK_RELEASE, &OnProcUIRelease, nullptr, 100);
        s_procUICallbacksRegistered = true;
    }

    AcquireForeground();
    if (!s_hasForeground.load(std::memory_order_acquire)) {
        Shutdown();
        return false;
    }

    GX2SetSwapInterval(s_swapInterval);
    s_prevSubmittedTs = 0;
    s_lastSubmittedTs = 0;
    return true;
#else
    m_window = SDL_CreateWindow(m_title.c_str(),
                                SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                m_width, m_height, 0);
    if (!m_window) return false;

    m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer) {
        m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED);
    }
    if (!m_renderer) {
        Shutdown();
        return false;
    }

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    s_activeRenderer = m_renderer;
    return true;
#endif
}

void Window::Clear(const ColorRGBA& color) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!s_hasForeground.load(std::memory_order_acquire) || !s_defaultContextState || !s_mainColorBuffer.surface.image) return;
    GX2SetContextState(s_defaultContextState);
    GX2ClearColor(&s_mainColorBuffer,
                  static_cast<float>(color.r) / 255.0f,
                  static_cast<float>(color.g) / 255.0f,
                  static_cast<float>(color.b) / 255.0f,
                  static_cast<float>(color.a) / 255.0f);
    // GX2ClearColor modifica el contexto de hardware; re-enlazar estado base limpio
    GX2SetContextState(s_defaultContextState);
    GX2SetColorBuffer(&s_mainColorBuffer, GX2_RENDER_TARGET_0);
    GX2SetViewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
    GX2SetScissor(0, 0, 1280, 720);
    GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
    GX2SetAlphaTest(FALSE, GX2_COMPARE_FUNC_ALWAYS, 0.0f);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0x01, FALSE, TRUE);
#else
    if (!m_renderer) return;
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderClear(m_renderer);
#endif
}

void Window::Present() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!s_hasForeground.load(std::memory_order_acquire) || !s_mainColorBuffer.surface.image) return;

    GX2CopyColorBufferToScanBuffer(&s_mainColorBuffer, GX2_SCAN_TARGET_TV);
    GX2CopyColorBufferToScanBuffer(&s_mainColorBuffer, GX2_SCAN_TARGET_DRC);
    GX2SwapScanBuffers();

    if (s_defaultContextState) {
        GX2SetContextState(s_defaultContextState);
    }
    GX2Flush();

    if (!s_tvDrcEnabled) {
        GX2SetTVEnable(TRUE);
        GX2SetDRCEnable(TRUE);
        s_tvDrcEnabled = true;
    }

    s_prevSubmittedTs = s_lastSubmittedTs;
    s_lastSubmittedTs = GX2GetLastSubmittedTimeStamp();
    if (s_prevSubmittedTs > GX2GetRetiredTimeStamp()) {
        GX2WaitTimeStamp(s_prevSubmittedTs);
    }

    const uint32_t maxPendingFlips = (s_swapInterval > 0u) ? 1u : 2u;
    while (s_hasForeground.load(std::memory_order_acquire) && !ProcUIInShutdown()) {
        uint32_t swapCount = 0u;
        uint32_t flipCount = 0u;
        OSTime lastFlip = 0;
        OSTime lastVsync = 0;
        GX2GetSwapStatus(&swapCount, &flipCount, &lastFlip, &lastVsync);
        if ((swapCount - flipCount) < maxPendingFlips) {
            break;
        }
        GX2WaitForVsync();
    }
#else
    if (m_renderer) {
        SDL_RenderPresent(m_renderer);
    }
#endif
}

void Window::SetSwapInterval(int interval) noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    s_swapInterval = (interval > 0) ? 1u : 0u;
    if (s_gx2Initialized && s_hasForeground.load(std::memory_order_acquire)) {
        GX2SetSwapInterval(s_swapInterval);
    }
#else
    if (m_renderer) {
        SDL_RenderSetVSync(m_renderer, (interval > 0) ? 1 : 0);
    }
#endif
}

void Window::Shutdown() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (s_gx2Initialized) {
        if (ProcUIIsRunning() && !s_exitingProcUI.load(std::memory_order_acquire)) {
            SYSLaunchMenu();
            while (ProcUIIsRunning() && !s_exitingProcUI.load(std::memory_order_acquire)) {
                const ProcUIStatus st = ProcUIProcessMessages(TRUE);
                if (st == PROCUI_STATUS_RELEASE_FOREGROUND) {
                    ReleaseForeground();
                    ProcUIDrawDoneRelease();
                } else if (st == PROCUI_STATUS_EXITING) {
                    s_exitingProcUI.store(true, std::memory_order_release);
                    break;
                } else {
                    OSSleepTicks(OSMillisecondsToTicks(16));
                }
            }
        }
        ReleaseForeground();
        GX2Shutdown();
        s_gx2Initialized = false;
    } else {
        ReleaseForeground();
    }

    if (s_defaultContextState) {
        void* rawCtx = static_cast<void*>(s_defaultContextState);
        MemoryManager::Instance().FreeAligned(rawCtx);
        s_defaultContextState = nullptr;
    }

    if (s_cmdBufferPool) {
        MemoryManager::Instance().FreeAligned(s_cmdBufferPool);
        s_cmdBufferPool = nullptr;
    }
    s_prevSubmittedTs = 0;
    s_lastSubmittedTs = 0;
#else
    if (m_renderer) {
        if (s_activeRenderer == m_renderer) {
            s_activeRenderer = nullptr;
        }
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
#endif
}

} // namespace WiiU
