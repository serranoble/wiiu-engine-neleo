---
name: milestone-handoff
description: Token-efficient workflow for updating docs/CURRENT_STATE.md, appending a milestone to HANDOFF_ANTIGRAVITY.md, and registering OPT-XXX entries in ANEXO_OPTIMIZACIONES_PENDIENTES.md without reading the full historical logs.
---

# Token-Efficient Milestone & Handoff Protocol

## Core Rule: Never Read Full Historical Logs
- `HANDOFF_ANTIGRAVITY.md` (>500 KB) and `ANEXO_OPTIMIZACIONES_PENDIENTES.md` (>700 KB) are **append-only archives**.
- **Never** call `read_file` across the whole file. Use:
  - `python scripts/agent_harness.py opt tail` to find the next `OPT-XXX` ID and line numbers.
  - `read_file` with targeted line ranges (last 30 lines or table tail only).

## Update Steps (or Delegate to `context_scribe` with `Model: "flash_lite"`)

1. **Update `docs/CURRENT_STATE.md`:**
   - Keep it under **3.5 KB** (~900 tokens).
   - Update date, latest milestone number, branch, key architectural changes, and active invariants.

2. **Append Milestone to `HANDOFF_ANTIGRAVITY.md`:**
   - Update only line 3 (`**Última Actualización:** ...`).
   - Append a concise milestone section (<= 25 lines) before `## ⚡ Comandos Útiles` containing:
     - Objective, closed `OPT-XXX` IDs, bullet list of key files changed, build sizes (`.rpx`/`.wuhb`), and Phase 1 / Phase 2 reviewer verdicts.

3. **Register `OPT-XXX` in `ANEXO_OPTIMIZACIONES_PENDIENTES.md`:**
   - Update header range (`OPT-01` a `OPT-XXX`) in lines 4–5.
   - Insert 1 row per new `OPT` at the end of the general status table.
   - Append a concise card (Área, Estado, Referencia, Problema, Mecanismo, Reusabilidad, PROS, CONTRAS, Recomendación Técnica) before `## 📌 PROTOCOLO DE MANTENIMIENTO`.

4. **Verify LF:**
   - Run `python scripts/agent_harness.py check --fix-lf` to guarantee `0 CRLF`.
