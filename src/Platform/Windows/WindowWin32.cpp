#include "Core/Window.hpp"
#include "Platform/Win32InputBridge.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <cstring>

namespace Neleo::Platform {

Win32InputBridge& GetWin32InputBridge() noexcept {
    static Win32InputBridge s_bridge;
    return s_bridge;
}

} // namespace Neleo::Platform

namespace Neleo {

namespace {

constexpr wchar_t kWindowClassName[] = L"WiiUEngineWin32Class";

using PFNWGLCREATECONTEXTATTRIBSARBPROC = HGLRC (WINAPI*)(HDC, HGLRC, const int*);
using PFNWGLSWAPINTERVALEXTPROC = BOOL (WINAPI*)(int);

constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB = 0x2091;
constexpr int WGL_CONTEXT_MINOR_VERSION_ARB = 0x2092;
constexpr int WGL_CONTEXT_PROFILE_MASK_ARB  = 0x9126;
constexpr int WGL_CONTEXT_CORE_PROFILE_BIT_ARB = 0x00000001;

HWND s_hwnd = nullptr;
HDC s_hdc = nullptr;
HGLRC s_hglrc = nullptr;
PFNWGLSWAPINTERVALEXTPROC s_wglSwapIntervalEXT = nullptr;
bool s_classRegistered = false;
bool s_isFullscreen = false;
DWORD s_windowedStyle = WS_OVERLAPPEDWINDOW;

using GLenumLocal = unsigned int;
using GLbitfieldLocal = unsigned int;
using GLintLocal = int;
using GLsizeiLocal = int;
using GLfloatLocal = float;

constexpr GLenumLocal kGLScissorTest = 0x0C11u;
constexpr GLbitfieldLocal kGLColorBufferBit = 0x4000u;
constexpr GLbitfieldLocal kGLDepthBufferBit = 0x0100u;

struct WindowGL11Procs {
    void (APIENTRY* Viewport)(GLintLocal, GLintLocal, GLsizeiLocal, GLsizeiLocal) = nullptr;
    void (APIENTRY* Scissor)(GLintLocal, GLintLocal, GLsizeiLocal, GLsizeiLocal) = nullptr;
    void (APIENTRY* ClearColor)(GLfloatLocal, GLfloatLocal, GLfloatLocal, GLfloatLocal) = nullptr;
    void (APIENTRY* Clear)(GLbitfieldLocal) = nullptr;
    void (APIENTRY* Enable)(GLenumLocal) = nullptr;
    void (APIENTRY* Disable)(GLenumLocal) = nullptr;
    bool loaded = false;
};

WindowGL11Procs s_gl11{};

bool LoadWindowGL11Procs() noexcept {
    HMODULE glMod = GetModuleHandleA("opengl32.dll");
    if (!glMod) return false;
    auto loadSym = [glMod](auto& dst, const char* name) noexcept {
        FARPROC sym = GetProcAddress(glMod, name);
        if (!sym) return false;
        std::memcpy(&dst, &sym, sizeof(dst));
        return true;
    };
    const bool ok = loadSym(s_gl11.Viewport, "glViewport") &&
                    loadSym(s_gl11.Scissor, "glScissor") &&
                    loadSym(s_gl11.ClearColor, "glClearColor") &&
                    loadSym(s_gl11.Clear, "glClear") &&
                    loadSym(s_gl11.Enable, "glEnable") &&
                    loadSym(s_gl11.Disable, "glDisable");
    s_gl11.loaded = ok;
    if (!ok) {
        s_gl11 = WindowGL11Procs{};
    }
    return ok;
}
WINDOWPLACEMENT s_windowedPlacement{ sizeof(WINDOWPLACEMENT) };
int s_clientWidth = 1280;
int s_clientHeight = 720;
int s_pendingSwapInterval = 1;

void UpdateViewportMetrics(int clientW, int clientH) noexcept {
    s_clientWidth = (clientW > 1) ? clientW : 1;
    s_clientHeight = (clientH > 1) ? clientH : 1;

    constexpr float kTargetAspect = 1280.0f / 720.0f;
    const float currentAspect = static_cast<float>(s_clientWidth) / static_cast<float>(s_clientHeight);

    int vpW = s_clientWidth;
    int vpH = s_clientHeight;
    int vpX = 0;
    int vpY = 0;

    if (currentAspect > kTargetAspect) {
        vpW = static_cast<int>(static_cast<float>(s_clientHeight) * kTargetAspect + 0.5f);
        vpX = (s_clientWidth - vpW) / 2;
    } else if (currentAspect < kTargetAspect) {
        vpH = static_cast<int>(static_cast<float>(s_clientWidth) / kTargetAspect + 0.5f);
        vpY = (s_clientHeight - vpH) / 2;
    }

    auto& bridge = Platform::GetWin32InputBridge();
    bridge.viewportX = vpX;
    bridge.viewportY = vpY;
    bridge.viewportW = (vpW > 1) ? vpW : 1;
    bridge.viewportH = (vpH > 1) ? vpH : 1;
}

void UpdatePointerCoords(int clientX, int clientY) noexcept {
    auto& bridge = Platform::GetWin32InputBridge();
    const float relX = static_cast<float>(clientX - bridge.viewportX) / static_cast<float>(bridge.viewportW);
    const float relY = static_cast<float>(clientY - bridge.viewportY) / static_cast<float>(bridge.viewportH);
    bridge.pointerX = std::clamp(relX * 1280.0f, 0.0f, 1280.0f);
    bridge.pointerY = std::clamp(relY * 720.0f, 0.0f, 720.0f);
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto& bridge = Platform::GetWin32InputBridge();

    switch (msg) {
        case WM_CLOSE:
            bridge.quitRequested = true;
            return 0;

        case WM_DESTROY:
            bridge.quitRequested = true;
            s_hdc = nullptr;
            s_hwnd = nullptr;
            PostQuitMessage(0);
            return 0;

        case WM_SIZE:
            UpdateViewportMetrics(LOWORD(lParam), HIWORD(lParam));
            return 0;

        case WM_KILLFOCUS:
            bridge.keysDown.fill(false);
            bridge.mouseLeftDown = false;
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            const uint32_t vk = static_cast<uint32_t>(wParam) & 0xFFu;
            const bool wasDown = (lParam & (1LL << 30)) != 0;
            if ((msg == WM_SYSKEYDOWN && vk == VK_RETURN && !wasDown) || (vk == VK_F11 && !wasDown)) {
                bridge.toggleFullscreenRequested = true;
            }
            bridge.keysDown[vk] = true;
            if (!wasDown) {
                bridge.keysPressed[vk] = true;
            }
            if (msg == WM_SYSKEYDOWN && vk == VK_RETURN) {
                return 0;
            }
            break;
        }

        case WM_KEYUP:
        case WM_SYSKEYUP: {
            const uint32_t vk = static_cast<uint32_t>(wParam) & 0xFFu;
            bridge.keysDown[vk] = false;
            break;
        }

        case WM_CHAR: {
            const uint32_t ch = static_cast<uint32_t>(wParam);
            if ((ch >= 0x20u && ch <= 0x7Eu) || ch == 0x08u || ch == 0x0Du || ch == 0x1Bu) {
                bridge.PushTypedChar(static_cast<char>(ch));
            }
            return 0;
        }

        case WM_LBUTTONDOWN:
            SetCapture(hwnd);
            UpdatePointerCoords(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            bridge.mouseLeftDown = true;
            bridge.mouseLeftPressed = true;
            return 0;

        case WM_LBUTTONUP:
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            UpdatePointerCoords(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            bridge.mouseLeftDown = false;
            return 0;

        case WM_MOUSEMOVE:
            UpdatePointerCoords(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void Utf8ToWide(const std::string& utf8, wchar_t* outWide, size_t outCap) noexcept {
    if (!outWide || outCap == 0u) return;
    const int written = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, outWide, static_cast<int>(outCap));
    if (written <= 0) {
        outWide[0] = L'\0';
    } else {
        outWide[outCap - 1u] = L'\0';
    }
}

void ApplyBorderlessFullscreen(HWND hwnd, bool fullscreen) noexcept {
    if (!hwnd) return;
    if (fullscreen == s_isFullscreen) return;

    if (fullscreen) {
        s_windowedStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
        s_windowedPlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(hwnd, &s_windowedPlacement);

        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(MONITORINFO) };
        if (GetMonitorInfoW(mon, &mi)) {
            SetWindowLongPtrW(hwnd, GWL_STYLE, static_cast<LONG_PTR>(s_windowedStyle & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
            SetWindowPos(hwnd, HWND_TOP,
                         mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            s_isFullscreen = true;
        }
    } else {
        SetWindowLongPtrW(hwnd, GWL_STYLE, static_cast<LONG_PTR>(s_windowedStyle));
        SetWindowPlacement(hwnd, &s_windowedPlacement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        s_isFullscreen = false;
    }

    RECT rc{};
    if (GetClientRect(hwnd, &rc)) {
        UpdateViewportMetrics(rc.right - rc.left, rc.bottom - rc.top);
    }
}

} // namespace

Window::Window(const std::string& title, int width, int height)
    : m_title(title), m_width(width), m_height(height) {}

Window::~Window() {
    Shutdown();
}

void* Window::GetActiveNativeColorBuffer() noexcept { return nullptr; }
void* Window::GetActiveNativeDepthBuffer() noexcept { return nullptr; }
void* Window::GetActiveNativeContextState() noexcept { return nullptr; }
void* Window::GetActiveNativeWindowHandle() noexcept { return static_cast<void*>(s_hwnd); }
bool Window::HasForeground() noexcept { return s_hwnd != nullptr; }
bool Window::IsExitingProcUI() noexcept { return Platform::GetWin32InputBridge().quitRequested; }
void Window::NotifyExitingProcUI() noexcept { Platform::GetWin32InputBridge().quitRequested = true; }
void Window::AcquireForeground() noexcept {}
void Window::ReleaseForeground() noexcept {}

void Window::PumpPlatformEvents() noexcept {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0u, 0u, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            Platform::GetWin32InputBridge().quitRequested = true;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (Platform::GetWin32InputBridge().toggleFullscreenRequested) {
        Platform::GetWin32InputBridge().toggleFullscreenRequested = false;
        ApplyBorderlessFullscreen(s_hwnd, !s_isFullscreen);
    }
}

void Window::ToggleFullscreen() noexcept {
    ApplyBorderlessFullscreen(s_hwnd, !s_isFullscreen);
}

bool Window::Init() {
    Shutdown();
    m_ownsWindow = true;

    Platform::GetWin32InputBridge() = Platform::Win32InputBridge{};
    const HINSTANCE hInst = GetModuleHandleW(nullptr);

    if (!s_classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = MainWndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
        wc.lpszClassName = kWindowClassName;
        if (!RegisterClassExW(&wc)) {
            return false;
        }
        s_classRegistered = true;
    }

    const int reqW = (m_width > 0) ? m_width : 1280;
    const int reqH = (m_height > 0) ? m_height : 720;
    RECT wr{ 0, 0, reqW, reqH };
    constexpr DWORD kStyle = WS_OVERLAPPEDWINDOW;
    AdjustWindowRectEx(&wr, kStyle, FALSE, 0u);

    const int winW = wr.right - wr.left;
    const int winH = wr.bottom - wr.top;
    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);
    const int posX = std::max(0, (screenW - winW) / 2);
    const int posY = std::max(0, (screenH - winH) / 2);

    wchar_t wideTitle[256]{};
    Utf8ToWide(m_title, wideTitle, sizeof(wideTitle) / sizeof(wchar_t));

    s_hwnd = CreateWindowExW(
        0u,
        kWindowClassName,
        wideTitle,
        kStyle,
        posX, posY,
        winW, winH,
        nullptr, nullptr, hInst, nullptr);
    if (!s_hwnd) {
        Shutdown();
        return false;
    }

    s_hdc = GetDC(s_hwnd);
    if (!s_hdc) {
        Shutdown();
        return false;
    }

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    const int pixelFmt = ChoosePixelFormat(s_hdc, &pfd);
    if (pixelFmt == 0 || !SetPixelFormat(s_hdc, pixelFmt, &pfd)) {
        Shutdown();
        return false;
    }

    HGLRC dummyCtx = wglCreateContext(s_hdc);
    if (!dummyCtx || !wglMakeCurrent(s_hdc, dummyCtx)) {
        if (dummyCtx) wglDeleteContext(dummyCtx);
        Shutdown();
        return false;
    }

    PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = nullptr;
    {
        PROC rawCreate = wglGetProcAddress("wglCreateContextAttribsARB");
        std::memcpy(&wglCreateContextAttribsARB, &rawCreate, sizeof(wglCreateContextAttribsARB));
        PROC rawSwap = wglGetProcAddress("wglSwapIntervalEXT");
        std::memcpy(&s_wglSwapIntervalEXT, &rawSwap, sizeof(s_wglSwapIntervalEXT));
    }

    if (wglCreateContextAttribsARB) {
        const int attribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
            WGL_CONTEXT_MINOR_VERSION_ARB, 3,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            0
        };
        HGLRC coreCtx = wglCreateContextAttribsARB(s_hdc, nullptr, attribs);
        if (coreCtx) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(dummyCtx);
            s_hglrc = coreCtx;
            if (!wglMakeCurrent(s_hdc, s_hglrc)) {
                Shutdown();
                return false;
            }
        } else {
            s_hglrc = dummyCtx;
        }
    } else {
        s_hglrc = dummyCtx;
    }

    if (!LoadWindowGL11Procs()) {
        Shutdown();
        return false;
    }

    if (s_wglSwapIntervalEXT) {
        s_wglSwapIntervalEXT(s_pendingSwapInterval);
    }

    ShowWindow(s_hwnd, SW_SHOW);
    UpdateWindow(s_hwnd);
    SetForegroundWindow(s_hwnd);

    RECT rc{};
    if (GetClientRect(s_hwnd, &rc)) {
        UpdateViewportMetrics(rc.right - rc.left, rc.bottom - rc.top);
    } else {
        UpdateViewportMetrics(reqW, reqH);
    }

    return true;
}

void Window::Clear(const ColorRGBA& color) {
    ClearActive(color);
}

void Window::ClearActive(const ColorRGBA& color) noexcept {
    if (!s_hwnd || !s_gl11.loaded) return;

    // Limpiar franjas negras de letterboxing y luego ajustar viewport 16:9 centrado con scissor
    s_gl11.Disable(kGLScissorTest);
    s_gl11.Viewport(0, 0, s_clientWidth, s_clientHeight);
    s_gl11.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    s_gl11.Clear(kGLColorBufferBit | kGLDepthBufferBit);

    const auto& bridge = Platform::GetWin32InputBridge();
    s_gl11.Viewport(bridge.viewportX, bridge.viewportY, bridge.viewportW, bridge.viewportH);
    if (color.r != 0u || color.g != 0u || color.b != 0u) {
        s_gl11.Enable(kGLScissorTest);
        s_gl11.Scissor(bridge.viewportX, bridge.viewportY, bridge.viewportW, bridge.viewportH);
        constexpr float kInv255 = 1.0f / 255.0f;
        s_gl11.ClearColor(static_cast<float>(color.r) * kInv255,
                          static_cast<float>(color.g) * kInv255,
                          static_cast<float>(color.b) * kInv255,
                          static_cast<float>(color.a) * kInv255);
        s_gl11.Clear(kGLColorBufferBit | kGLDepthBufferBit);
        s_gl11.Disable(kGLScissorTest);
    }
}

void Window::Present() {
    PresentActive();
}

void Window::PresentActive() noexcept {
    if (s_hdc) {
        SwapBuffers(s_hdc);
    }
}

void Window::SetSwapInterval(int interval) noexcept {
    s_pendingSwapInterval = (interval > 0) ? 1 : 0;
    if (s_hglrc && s_wglSwapIntervalEXT) {
        s_wglSwapIntervalEXT(s_pendingSwapInterval);
    }
}

void Window::Shutdown() {
    if (!m_ownsWindow) return;
    m_ownsWindow = false;
    s_gl11 = WindowGL11Procs{};

    if (s_hglrc) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(s_hglrc);
        s_hglrc = nullptr;
    }
    if (s_hdc && s_hwnd) {
        ReleaseDC(s_hwnd, s_hdc);
        s_hdc = nullptr;
    }
    if (s_hwnd) {
        HWND hwndToDestroy = s_hwnd;
        s_hwnd = nullptr;
        DestroyWindow(hwndToDestroy);
    }
    s_wglSwapIntervalEXT = nullptr;
    s_isFullscreen = false;
}

} // namespace Neleo
