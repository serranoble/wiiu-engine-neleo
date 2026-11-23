#!/usr/bin/env python3
"""
Deterministic Agent Harness for Wii U 3D Engine C++20.
Reduces token overhead by replacing verbose shell/build/doc reads with compact,
structured summaries.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEXT_EXTS = {".cpp", ".hpp", ".h", ".c", ".py", ".sh", ".md", ".txt", ".cmake", ".vert", ".frag"}
TEXT_NAMES = {"CMakeLists.txt"}


def run_cmd(cmd: list[str], cwd: Path = ROOT) -> tuple[int, str, str]:
    proc = subprocess.run(
        cmd,
        cwd=str(cwd),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    return proc.returncode, proc.stdout, proc.stderr


def cmd_context(_: argparse.Namespace) -> int:
    _, branch, _ = run_cmd(["git", "rev-parse", "--abbrev-ref", "HEAD"])
    _, log_out, _ = run_cmd(["git", "log", "-n", "5", "--oneline"])
    _, status_out, _ = run_cmd(["git", "status", "--porcelain"])

    modified = 0
    added = 0
    deleted = 0
    untracked = 0
    for line in status_out.splitlines():
        if not line:
            continue
        code = line[:2]
        if code == "??":
            untracked += 1
        elif "D" in code:
            deleted += 1
        elif "A" in code:
            added += 1
        else:
            modified += 1

    print(f"BRANCH: {branch.strip()}")
    print(f"GIT_STATUS: modified={modified} added={added} deleted={deleted} untracked={untracked}")
    print("RECENT_COMMITS:")
    for line in log_out.splitlines():
        print(f"  {line}")

    state_file = ROOT / "docs" / "CURRENT_STATE.md"
    if state_file.exists():
        print("--- CURRENT_STATE.md ---")
        print(state_file.read_text(encoding="utf-8", errors="replace").strip())
    else:
        print("CURRENT_STATE.md: MISSING")
    return 0


def is_text_source(path: Path) -> bool:
    return path.suffix.lower() in TEXT_EXTS or path.name in TEXT_NAMES


def check_crlf(fix: bool) -> list[str]:
    _, diff_out, _ = run_cmd(["git", "diff", "--name-only"])
    _, cached_out, _ = run_cmd(["git", "diff", "--cached", "--name-only"])
    _, untracked_out, _ = run_cmd(["git", "ls-files", "--others", "--exclude-standard"])

    candidates = set()
    for block in (diff_out, cached_out, untracked_out):
        for rel in block.splitlines():
            rel = rel.strip()
            if rel:
                candidates.add(rel)

    # Also always audit .agents/ and scripts/
    for extra_dir in (ROOT / ".agents", ROOT / "scripts", ROOT / "docs"):
        if extra_dir.exists():
            for p in extra_dir.rglob("*"):
                if p.is_file() and is_text_source(p):
                    candidates.add(str(p.relative_to(ROOT)).replace("\\", "/"))

    crlf_files: list[str] = []
    for rel in sorted(candidates):
        p = ROOT / rel
        if not p.is_file() or not is_text_source(p):
            continue
        data = p.read_bytes()
        if b"\r\n" in data:
            if fix:
                p.write_bytes(data.replace(b"\r\n", b"\n"))
            else:
                crlf_files.append(rel)
    return crlf_files


def check_core_leaks() -> list[str]:
    core_dir = ROOT / "include" / "Core"
    leaks: list[str] = []
    pattern = re.compile(r'#\s*include\s*[<"](gx2|gx2r|SDL2)/')
    if not core_dir.exists():
        return leaks
    for p in core_dir.rglob("*.hpp"):
        text = p.read_text(encoding="utf-8", errors="replace")
        for idx, line in enumerate(text.splitlines(), start=1):
            if pattern.search(line):
                leaks.append(f"{p.relative_to(ROOT)}:{idx}: {line.strip()}")
    return leaks


def check_sdl_symbols() -> tuple[str, int]:
    elf = ROOT / "build" / "wiiu_demos.elf"
    if not elf.exists():
        return "SKIP (no build/wiiu_demos.elf)", 0

    nm_candidates = [
        shutil.which("powerpc-eabi-nm"),
        r"C:\devkitPro\devkitPPC\bin\powerpc-eabi-nm.exe",
    ]
    nm_bin = None
    for cand in nm_candidates:
        if cand and Path(cand).exists():
            nm_bin = cand
            break

    if not nm_bin:
        return "SKIP (powerpc-eabi-nm not found)", 0

    code, out, _ = run_cmd([nm_bin, str(elf)])
    if code != 0:
        return "ERROR (nm failed)", -1

    sdl_syms = [line for line in out.splitlines() if " SDL_" in line or "\tSDL_" in line]
    return ("PASS" if not sdl_syms else "FAIL"), len(sdl_syms)


def report_binaries() -> None:
    for rel in ("build/wiiu_demos.rpx", "build/wiiu_demos.wuhb"):
        p = ROOT / rel
        if p.exists():
            size = p.stat().st_size
            print(f"BINARY {rel}: {size:,} B ({size / (1024 * 1024):.2f} MB)")
        else:
            print(f"BINARY {rel}: MISSING")


def check_gx2_invariants() -> list[str]:
    issues: list[str] = []
    win_cpp = ROOT / "src" / "Core" / "Window.cpp"
    if win_cpp.exists():
        text = win_cpp.read_text(encoding="utf-8", errors="replace")
        for sym in ("GX2SetColorBuffer", "GX2SetDepthBuffer", "GX2ClearDepthStencilEx", "GX2InitDepthBufferRegs"):
            if sym not in text:
                issues.append(f"src/Core/Window.cpp: missing required GX2 invariant '{sym}'")
        idx_regs = text.find("GX2InitDepthBufferRegs")
        idx_hiz = text.find("GX2InitDepthBufferHiZEnable")
        if idx_regs != -1 and idx_hiz != -1 and idx_hiz < idx_regs:
            issues.append("src/Core/Window.cpp: GX2InitDepthBufferHiZEnable must appear AFTER GX2InitDepthBufferRegs")

    shadow_cpp = ROOT / "src" / "Graphics" / "ShadowMapRendererGX2.cpp"
    if shadow_cpp.exists():
        stext = shadow_cpp.read_text(encoding="utf-8", errors="replace")
        if "GX2SetPixelShader(nullptr)" in stext:
            issues.append("src/Graphics/ShadowMapRendererGX2.cpp: GX2SetPixelShader(nullptr) crashes Cemu 2.6 HLE (0xc0000005)")

    for scan_dir in (ROOT / "src" / "Scene", ROOT / "src" / "Graphics"):
        if scan_dir.exists():
            for p in scan_dir.rglob("*.cpp"):
                text = p.read_text(encoding="utf-8", errors="replace")
                if ("GX2DrawDone()" in text and
                    "HasForeground" not in text and
                    "ProcUIInForeground" not in text and
                    "WaitForGpuIdleSafe" not in text):
                    issues.append(f"{p.relative_to(ROOT)}: unguarded GX2DrawDone() without foreground check")
    return issues


def cmd_check(args: argparse.Namespace) -> int:
    crlf = check_crlf(fix=args.fix_lf)
    leaks = check_core_leaks()
    gx2_issues = check_gx2_invariants()
    sdl_status, sdl_count = check_sdl_symbols()

    print(f"CRLF_CHECK: {'PASS (0)' if not crlf else f'FAIL ({len(crlf)})'}")
    for f in crlf[:15]:
        print(f"  CRLF: {f}")

    print(f"CORE_HEADER_LEAKS: {'PASS (0)' if not leaks else f'FAIL ({len(leaks)})'}")
    for l in leaks[:15]:
        print(f"  LEAK: {l}")

    print(f"GX2_INVARIANTS: {'PASS (0)' if not gx2_issues else f'FAIL ({len(gx2_issues)})'}")
    for gi in gx2_issues[:15]:
        print(f"  GX2: {gi}")

    print(f"ELF_SDL_SYMBOLS: {sdl_status} ({sdl_count})")
    report_binaries()

    failed = bool(crlf) or bool(leaks) or bool(gx2_issues) or (sdl_count > 0)
    return 1 if failed else 0


def run_filtered_build(build_dir: str, target: str | None) -> int:
    cmd = ["cmake", "--build", build_dir, "-j4"]
    if target:
        cmd.extend(["--target", target])

    code, out, err = run_cmd(cmd)
    combined = (out + "\n" + err).splitlines()

    noise_prefixes = (
        "Writing /",
        "Calculating metadata",
        "Updating sibling",
        "Populating data",
        "Writing header",
        "Writing dir_",
        "Writing file_",
    )
    warnings: list[str] = []
    errors: list[str] = []
    milestones: list[str] = []

    for line in combined:
        s = line.strip()
        if not s or s.startswith(noise_prefixes):
            continue
        low = s.lower()
        if "error:" in low or "fatal error:" in low or "undefined reference" in low:
            errors.append(s)
        elif "warning:" in low:
            warnings.append(s)
        elif s.startswith("[100%]") or "Built target wiiu_demos" in s or "Built target engine_" in s:
            milestones.append(s)

    print(f"BUILD {build_dir} ({target or 'all'}): {'OK' if code == 0 else f'FAIL (exit {code})'}")
    print(f"  WARNINGS: {len(warnings)} | ERRORS: {len(errors)}")
    for w in warnings[:20]:
        print(f"  WARN: {w}")
    for e in errors[:25]:
        print(f"  ERR:  {e}")
    if code == 0 and milestones:
        print(f"  LAST: {milestones[-1]}")
    return code


def cmd_build(args: argparse.Namespace) -> int:
    rc = run_filtered_build("build", args.target)
    if rc == 0 and args.lib:
        if (ROOT / "build_lib").exists():
            rc = run_filtered_build("build_lib", None)
        else:
            print("BUILD build_lib: SKIP (directory not configured)")
    if rc == 0:
        report_binaries()
    return rc


def cmd_opt(args: argparse.Namespace) -> int:
    anexo = ROOT / "ANEXO_OPTIMIZACIONES_PENDIENTES.md"
    if not anexo.exists():
        print("ANEXO_OPTIMIZACIONES_PENDIENTES.md not found")
        return 1

    lines = anexo.read_text(encoding="utf-8", errors="replace").splitlines()
    query = args.query.strip().upper()

    if query == "TAIL":
        opt_re = re.compile(r"^\|\s*\*\*OPT-(\d+)\*\*\s*\|")
        numbered: list[tuple[int, str]] = []
        for l in lines:
            m = opt_re.match(l)
            if m:
                numbered.append((int(m.group(1)), l))
        numbered.sort(key=lambda pair: pair[0])
        print(f"TOTAL_OPT_ROWS: {len(numbered)}")
        print("LAST_5_OPT_ROWS:")
        for _, row in numbered[-5:]:
            print(f"  {row}")
        return 0

    if not query.startswith("OPT-"):
        query = f"OPT-{query}"

    # Print table row
    for idx, line in enumerate(lines, start=1):
        if f"| **{query}** |" in line:
            print(f"TABLE L{idx}: {line}")
            break

    # Print detailed section
    header_token = f"[{query}]"
    start = -1
    for idx, line in enumerate(lines):
        if line.startswith("### ") and header_token in line:
            start = idx
            break
    if start == -1:
        print(f"DETAIL: {query} not found")
        return 1

    end = len(lines)
    for idx in range(start + 1, len(lines)):
        if lines[idx].startswith("### ") or lines[idx].startswith("## "):
            end = idx
            break

    print(f"DETAIL L{start + 1}-L{end}:")
    for line in lines[start:end]:
        print(line)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Wii U Engine Agent Harness")
    sub = parser.add_subparsers(dest="command", required=True)

    p_ctx = sub.add_parser("context", help="Compact repo status + CURRENT_STATE.md")
    p_ctx.set_defaults(func=cmd_context)

    p_chk = sub.add_parser("check", help="Static hygiene checks (LF, Core leaks, ELF SDL symbols)")
    p_chk.add_argument("--fix-lf", action="store_true", help="Automatically normalize CRLF to LF")
    p_chk.set_defaults(func=cmd_check)

    p_bld = sub.add_parser("build", help="Filtered CMake build output")
    p_bld.add_argument("--target", default="wiiu_demos_wuhb", help="CMake target for build/")
    p_bld.add_argument("--lib", action="store_true", help="Also build build_lib/")
    p_bld.set_defaults(func=cmd_build)

    p_opt = sub.add_parser("opt", help="Query OPT-XXX or 'tail' in ANEXO without reading full file")
    p_opt.add_argument("query", help="OPT-442, 442, or tail")
    p_opt.set_defaults(func=cmd_opt)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
