---
name: wiiu-gx2-silicon
description: Hardware invariants for Nintendo Wii U (IBM Espresso PPC 750CL, AMD Latte GX2, Cafe OS ProcUI, sndcore2/AX). Load before editing graphics, window/swapchain, audio DMA, shaders, or threading code.
---

# Wii U Silicon & Cafe OS Invariants (Hito 144 — 100% Nativo)

## 1. Zero SDL2 on Wii U
- Wii U builds (`#if defined(__WIIU__) || defined(__wiiu__)`) must have **0** `SDL_` symbols in `wiiu_demos.elf`.
- All PC/SDL2 code must stay isolated under `#if !defined(__WIIU__) && !defined(__wiiu__)`.
- Use `OSGetSystemTime()` / `OSTimerClockSpeed` for timing and `std::FILE` for I/O.

## 2. ProcUI & Foreground Guards (Menú HOME)
- **Never** call `GX2DrawDone()` or teardown GPU buffers without checking:
  ```cpp
  if (WiiU::Window::HasForeground() && !WiiU::Window::IsExitingProcUI()) {
      GX2DrawDone();
  }
  ```
- In `ReleaseForeground()`, keep `GX2SetTVEnable(TRUE); GX2SetDRCEnable(TRUE);` so Cafe OS can render the HOME Menu overlay without a black screen.
- When not in foreground, yield passively with `OSSleepTicks(OSMillisecondsToTicks(16))` (never busy-spin Core 1).
- Never use `WHBGfxLoadGFDShaderGroup` or `WHBProc*`; use Direct GFD (`GFDGetVertexShader`/`PixelShader`) and `ProcUIInitEx`.

## 3. AMD Latte GX2 Pipeline & Memory
- **Alignment:** Shaders, uniform blocks, textures and VBOs/IBOs require **256-byte alignment** (`MemoryManager::AllocAligned(size, 256)`).
- **Cache coherency:** Always call `GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER / CPU_TEXTURE / CPU_ATTRIBUTE_BUFFER, ptr, size)` after CPU writes before GPU consumption.
- **Shader mode:** Call `GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER)` at frame start (`BeginFrame`).
- **Samplers & CubeMaps:** Always bind fallback `&m_defaultNeutralTex` / `&m_defaultNeutralCubeMap` when a texture is null; unmapped TMU slots stall Latte and trigger watchdog reboots.
- **Skinning fetch shader:** `boneIndices` (`GX2_ATTRIB_FORMAT_UINT_8_8_8_8`) must use `GX2_ENDIAN_SWAP_NONE`.
- **Render2DGX2:** Uses triple-buffering (`BUFFER_SLOTS = 3`) with sub-range `GX2DrawEx`; call `Render2DGX2::Instance().Shutdown()` and `ForwardRendererGX2::Instance().Shutdown()` before `Window::Shutdown()`.

## 4. Audio DMA (`sndcore2` / AX) & Espresso SMP
- Ping-pong DMA buffer in MEM2 (`16,384` B, `AX_BUFFER_SAMPLES = 2048`) must not cross a 512 MB physical boundary (`OSEffectiveToPhysical`). Flush with `DCStoreRange` before DMA reads.
- Dedicated mix thread runs at `prio - 1`; `AXFrameCallbackStatic` must remain lock-free.
- Espresso L1D cache lines are **32 bytes** (`alignas(32)` between producer/consumer atomics). Use explicit `acquire`/`release` memory orders on weakly-ordered PowerPC.
- Always use bounded waits (`OSWaitEventWithTimeout`) on Core 2 workers to prevent shutdown deadlocks.
