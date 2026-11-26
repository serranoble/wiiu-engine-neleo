---
name: opengl33-win32-pal
description: Hardware and platform invariants for Windows x64 / ASUS ROG Ally (Win32 WGL OpenGL 3.3 Core, XInput, waveOut, ws2_32, 0 SDL2, 0 external DLLs). Load before editing Win32 PAL or OpenGL 3.3 backend code.
---

# Windows x64 / OpenGL 3.3 Core & Win32 PAL Invariants

## 1. Zero SDL2 & Zero Third-Party DLLs
- Strictly **0** SDL2 includes or `SDL_` symbols anywhere in `include/` or `src/` (`ZERO_SDL_SOURCE_REFS: PASS (0)`).
- Static linking (`-static`, `-static-libgcc`, `-static-libstdc++`) targeting **only standard Windows OS DLLs**: `KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `OPENGL32.dll`, `WINMM.dll`, `WS2_32.dll`, `XINPUT*`, and UCRT (`api-ms-win-crt-*`).
- Verified via `WIN_DLL_CHECK` in `python scripts/agent_harness.py check`.

## 2. Link-Time PAL & Header Hygiene
- Isolate Win32/OpenGL code inside `src/Platform/Win32/` and `src/Graphics/GL/` via `CMakeLists.txt` (`if(WUT) ... else() ... endif()`).
- **Never** `#include <windows.h>`, `<winsock2.h>`, or `<GL/gl.h>` inside any public header under `include/`.
- Use `GLFunctions` (`Neleo::Graphics::GL::LoadFunctions()` via `wglGetProcAddress`) and scalar typedefs/constants in `include/Graphics/GL/GLFunctions.hpp`.

## 3. OpenGL 3.3 Core Pipeline & State Invariants
- **Isolated VAOs per layout:** Never share one VAO across incompatible vertex layouts. Keep distinct VAOs (`staticVao`, `instVao`, `skinnedVao`, `skyVao`, `depthVao`, `shadowVao`, `fullscreenVao`) and bind before configuring attributes or issuing draws.
- **16:9 Letterbox Viewport Preservation:** Every off-screen FBO pass (`RenderTargetGL`, `ShadowMapRendererGL`, `PostProcessHDRGL`, `RHIFullscreenShaderGL`) MUST query `GL_VIEWPORT_VAL` before binding its FBO and restore that exact saved viewport when rebinding `FBO 0` so borderless/aspect-ratio letterboxing on ROG Ally (e.g., 16:10 or desktop resolutions) is never overwritten.
- **Scissor Guard:** Always `glDisable(GL_SCISSOR_TEST_VAL)` before clearing or blitting (`glBlitFramebuffer`) off-screen render targets.
- **Texture Unit & State Cleanliness:** In `ForwardRendererGL::EndFrame()`, explicitly unbind `GL_TEXTURE2_VAL`, `GL_TEXTURE1_VAL`, `GL_TEXTURE0_VAL`, reset active unit to `GL_TEXTURE0_VAL`, unbind VAO/VBO/program, and reset blend/depth/cull state before `Render2DGL` executes.
- **Coordinate Parity & Cooked Assets:** FBO textures in OpenGL are bottom-up (`projCoords.xy * 0.5 + 0.5`). Big-Endian `.wmesh` (48 B static / 80 B skinned) and `.wtex` (`RGBA8`/`BC1`/`BC3`) are converted Big-Endian → Little-Endian once at load time (never in the 60 FPS hot loop).

## 4. Win32 Window, Input, Audio & Shutdown Order
- `WindowWin32`: WGL OpenGL 3.3 Core context, DPI awareness, F11 borderless toggle, 16:9 letterbox viewport calculation.
- `InputWin32`: XInput polling + Win32 touch/mouse normalization mapped to `VirtualButton` / `InputManager`.
- `AudioDeviceWin32`: 48 kHz lock-free `waveOut` streaming matching the Wii U `AX` mixer rate.
- `Engine::Shutdown()`: Always call `Shutdown()` on `ShadowMapRendererGX2`, `PostProcessHDRGX2`, `ForwardRendererGX2`, `TextureLoader`, and `Render2DGX2` **before** `Graphics::GL::ResetFunctions()` and destroying the WGL context in `Window::Shutdown()`.
