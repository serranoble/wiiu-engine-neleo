# Estado Vivo del Proyecto — Wii U 3D Engine C++20

**Última actualización:** 21 de Noviembre de 2026 (Hito 145: Subsistema de Redes `engine_net` + Servidor Dual TCP/UDP `OPT-443`)
**Rama activa:** `refactor/engine-stability-and-scene-archive`
**Plataforma base:** Nintendo Wii U (Espresso PPC 750CL Tri-Core @ 1.24 GHz, AMD Latte GX2 @ 550 MHz, Cafe OS) — diseño multi-consola (Wii U / Switch / PC).

---

## Arquitectura Vigente (Hito 145 — 8 Librerías Estáticas + Redes)

- **8 librerías estáticas (DAG acíclico en `CMakeLists.txt`):**
  1. `engine_core`, 2. `engine_physics`, 3. `engine_audio`, 4. `engine_input`, 5. `engine_graphics`, 6. `engine_ai`, 7. `engine_framework`, 8. `engine_net` (`NetSocket` HAL no bloqueante `nsysnet`/`Winsock2`, `NetBitStream` endian-safe, `NetSession` UDP `WUN1` + TCP framing, `NetReplication` snapshots 50B + LERP/SLERP).
- **Herramienta de Diagnóstico:** `tools/net_test_server.py` (servidor local dual TCP 7777 + UDP 7778 con simulación de latencia/pérdida).
- **Umbrella header público:** `include/WiiUEngine.hpp`.

---

## Invariantes Críticos del Motor

1. **Cero SDL2 en Wii U:** `0` símbolos `SDL_` en `build/wiiu_demos.elf`.
2. **Guardas de primer plano GX2:** Toda llamada a `GX2DrawDone()` debe protegerse con `Window::HasForeground() && !Window::IsExitingProcUI()`.
3. **Zero Hot Allocations:** Cero alocaciones en el heap a 60 FPS en bucles críticos.
4. **Terminaciones de línea:** Estrictamente LF (`0 CRLF`).
5. **Prohibición FTP:** Nunca subir binarios a la consola sin orden explícita.
