#!/usr/bin/env python3
"""
Deterministic Agent Harness for Wii U 3D Engine C++20.
Reduces token overhead by replacing verbose shell/build/doc reads with compact,
structured summaries.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEXT_EXTS = {".cpp", ".hpp", ".h", ".c", ".py", ".sh", ".md", ".txt", ".cmake", ".vert", ".frag", ".json"}
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
    pattern = re.compile(r'#\s*include\s*[<"](?:gx2|gx2r|SDL2|GL)/|#\s*include\s*[<"](?:windows|winsock2)\.h[>"]')
    if not core_dir.exists():
        return leaks
    for p in core_dir.rglob("*.hpp"):
        text = p.read_text(encoding="utf-8", errors="replace")
        for idx, line in enumerate(text.splitlines(), start=1):
            if pattern.search(line):
                leaks.append(f"{p.relative_to(ROOT)}:{idx}: {line.strip()}")
    return leaks


def check_gl_invariants() -> list[str]:
    issues: list[str] = []
    inc_dir = ROOT / "include"
    hdr_leak_pat = re.compile(r'#\s*include\s*[<"](?:windows\.h|winsock2\.h|GL/)')
    if inc_dir.exists():
        for p in inc_dir.rglob("*"):
            if not p.is_file() or p.suffix.lower() not in {".hpp", ".h"}:
                continue
            text = p.read_text(encoding="utf-8", errors="replace")
            for idx, line in enumerate(text.splitlines(), start=1):
                if hdr_leak_pat.search(line):
                    issues.append(f"{p.relative_to(ROOT)}:{idx}: public header leaks OS/GL include '{line.strip()}'")

    fwd_gl = ROOT / "src" / "Graphics" / "GL" / "ForwardRendererGL.cpp"
    if fwd_gl.exists():
        ftext = fwd_gl.read_text(encoding="utf-8", errors="replace")
        for sym in ("GL_TEXTURE2_VAL", "GL_TEXTURE1_VAL", "GL_TEXTURE0_VAL"):
            if sym not in ftext:
                issues.append(f"src/Graphics/GL/ForwardRendererGL.cpp: missing texture unit cleanup '{sym}'")

    for fbo_file in (
        "src/Graphics/GL/RenderTargetGL.cpp",
        "src/Graphics/GL/ShadowMapRendererGL.cpp",
        "src/Graphics/GL/RHIFullscreenShaderGL.cpp",
    ):
        p = ROOT / fbo_file
        if p.exists():
            text = p.read_text(encoding="utf-8", errors="replace")
            if "GL_VIEWPORT_VAL" not in text:
                issues.append(f"{fbo_file}: missing GL_VIEWPORT_VAL save/restore guard for 16:9 letterbox")

    post_gl = ROOT / "src" / "Graphics" / "GL" / "PostProcessHDRGL.cpp"
    if post_gl.exists():
        ptext = post_gl.read_text(encoding="utf-8", errors="replace")
        if "GL_VIEWPORT_VAL" not in ptext and "m_hdrRT.EndPass(" not in ptext:
            issues.append("src/Graphics/GL/PostProcessHDRGL.cpp: missing viewport restore (GL_VIEWPORT_VAL or m_hdrRT.EndPass)")

    if fwd_gl.exists() and "(shadingMode == 6u) ? false : mat.backfaceCull" not in ftext:
        issues.append("src/Graphics/GL/ForwardRendererGL.cpp: skybox mode 6u must disable backface culling")

    water_qa_cpp = ROOT / "src" / "Scene" / "SceneQA_EnvironmentWater.cpp"
    if water_qa_cpp.exists():
        wtext = water_qa_cpp.read_text(encoding="utf-8", errors="replace")
        if "skyMat.backfaceCull = false;" not in wtext:
            issues.append("src/Scene/SceneQA_EnvironmentWater.cpp: skyMat must set backfaceCull = false for OpenGL 3.3")
    return issues


def check_win_dlls() -> tuple[str, list[str]]:
    exe = ROOT / "build-win" / "wiiu_demos.exe"
    if not exe.exists():
        return "SKIP (no build-win/wiiu_demos.exe)", []

    objdump_bin: str | None = None
    win_bin = find_win_toolchain_bin()
    if win_bin and (win_bin / "objdump.exe").exists():
        objdump_bin = str(win_bin / "objdump.exe")
    else:
        found = shutil.which("objdump")
        if found:
            objdump_bin = found

    if not objdump_bin:
        return "SKIP (objdump not found)", []

    code, out, _ = run_cmd([objdump_bin, "-p", str(exe)])
    if code != 0:
        return "ERROR (objdump failed)", ["objdump failed"]

    allowed_exact = {
        "kernel32.dll",
        "user32.dll",
        "gdi32.dll",
        "opengl32.dll",
        "winmm.dll",
        "ws2_32.dll",
        "advapi32.dll",
        "shell32.dll",
        "ntdll.dll",
        "ucrtbase.dll",
    }
    allowed_prefixes = ("api-ms-win-crt-", "xinput")

    unexpected: list[str] = []
    for line in out.splitlines():
        m = re.search(r"DLL Name:\s*(\S+)", line, re.IGNORECASE)
        if not m:
            continue
        dll = m.group(1).strip()
        low = dll.lower()
        if low in allowed_exact or low.startswith(allowed_prefixes):
            continue
        unexpected.append(dll)

    return ("PASS" if not unexpected else "FAIL"), unexpected


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
    for rel in ("build/wiiu_demos.rpx", "build/wiiu_demos.wuhb", "build-win/wiiu_demos.exe"):
        p = ROOT / rel
        if p.exists():
            size = p.stat().st_size
            print(f"BINARY {rel}: {size:,} B ({size / (1024 * 1024):.2f} MB)")
        else:
            print(f"BINARY {rel}: MISSING")


def check_zero_sdl_source_refs() -> list[str]:
    hits: list[str] = []
    pattern = re.compile(r"#\s*include\s*[<\"][^>\"]*SDL|\bSDL_[A-Za-z0-9_]+")
    for scan_dir in (ROOT / "include", ROOT / "src"):
        if not scan_dir.exists():
            continue
        for p in scan_dir.rglob("*"):
            if not p.is_file() or p.suffix.lower() not in {".hpp", ".h", ".cpp", ".c"}:
                continue
            text = p.read_text(encoding="utf-8", errors="replace")
            for idx, line in enumerate(text.splitlines(), start=1):
                if pattern.search(line):
                    hits.append(f"{p.relative_to(ROOT)}:{idx}: {line.strip()}")
    return hits


def check_gx2_invariants() -> list[str]:
    issues: list[str] = []
    win_cpp = ROOT / "src" / "Platform" / "WiiU" / "WindowWiiU.cpp"
    if not win_cpp.exists():
        win_cpp = ROOT / "src" / "Core" / "Window.cpp"
    if win_cpp.exists():
        text = win_cpp.read_text(encoding="utf-8", errors="replace")
        for sym in ("GX2SetColorBuffer", "GX2SetDepthBuffer", "GX2ClearDepthStencilEx", "GX2InitDepthBufferRegs"):
            if sym not in text:
                issues.append(f"{win_cpp.relative_to(ROOT)}: missing required GX2 invariant '{sym}'")
        idx_regs = text.find("GX2InitDepthBufferRegs")
        idx_hiz = text.find("GX2InitDepthBufferHiZEnable")
        if idx_regs != -1 and idx_hiz != -1 and idx_hiz < idx_regs:
            issues.append(f"{win_cpp.relative_to(ROOT)}: GX2InitDepthBufferHiZEnable must appear AFTER GX2InitDepthBufferRegs")

    shadow_cpp = ROOT / "src" / "Graphics" / "ShadowMapRendererGX2.cpp"
    if shadow_cpp.exists():
        stext = shadow_cpp.read_text(encoding="utf-8", errors="replace")
        if "GX2SetPixelShader(nullptr)" in stext:
            issues.append("src/Graphics/ShadowMapRendererGX2.cpp: GX2SetPixelShader(nullptr) crashes Cemu 2.6 HLE (0xc0000005)")

    fwd_cpp = ROOT / "src" / "Graphics" / "ForwardRendererGX2.cpp"
    if fwd_cpp.exists():
        ftext = fwd_cpp.read_text(encoding="utf-8", errors="replace")
        for sym in ("m_meshNoMipSampler", "BindMeshTextureAndSampler"):
            if sym not in ftext:
                issues.append(f"src/Graphics/ForwardRendererGX2.cpp: missing single-mip sampler guard '{sym}'")

    for scan_dir in (ROOT / "src" / "Scene", ROOT / "src" / "Graphics"):
        if scan_dir.exists():
            for p in scan_dir.rglob("*.cpp"):
                text = p.read_text(encoding="utf-8", errors="replace")
                if ("GX2DrawDone()" in text and
                    "HasForeground" not in text and
                    "ProcUIInForeground" not in text and
                    "WaitForGpuIdleSafe" not in text):
                    issues.append(f"{p.relative_to(ROOT)}: unguarded GX2DrawDone() without foreground check")

    # CafeGLSL VS<->PS varying linkage contract: if pbr_mesh_gx2.vert omits layout(location) on out,
    # paired PBR/water fragment shaders must also omit layout(location) on in so SPI_PS_INPUT_CNTL links by name.
    pbr_vs = ROOT / "assets" / "shaders" / "pbr_mesh_gx2.vert"
    if pbr_vs.exists():
        vs_text = pbr_vs.read_text(encoding="utf-8", errors="replace")
        vs_has_loc_out = bool(re.search(r"layout\s*\(\s*location\s*=\s*\d+\s*\)\s*out\b", vs_text))
        loc_in_pat = re.compile(r"layout\s*\(\s*location\s*=\s*\d+\s*\)\s*in\b")
        for frag_name in (
            "pbr_template_gx2.frag",
            "pbr_mesh_gx2.frag",
            "pbr_lite_gx2.frag",
            "pbr_shadow_gx2.frag",
            "water_glass_gx2.frag",
            "hdr_fog_gx2.frag",
        ):
            fp = ROOT / "assets" / "shaders" / frag_name
            if not fp.exists():
                continue
            ftext = fp.read_text(encoding="utf-8", errors="replace")
            fs_has_loc_in = bool(loc_in_pat.search(ftext))
            if fs_has_loc_in != vs_has_loc_out:
                issues.append(
                    f"assets/shaders/{frag_name}: varying layout(location) mismatch with pbr_mesh_gx2.vert "
                    f"(VS={vs_has_loc_out}, PS={fs_has_loc_in}) — corrupts SPI_PS_INPUT_CNTL in CafeGLSL"
                )
    return issues


def check_cooked_wmesh() -> tuple[int, list[str]]:
    issues: list[str] = []
    models_dir = ROOT / "assets" / "models"
    if not models_dir.exists():
        return 0, issues

    wmesh_files = sorted(models_dir.glob("*.wmesh"))
    for p in wmesh_files:
        rel = p.relative_to(ROOT)
        data = p.read_bytes()
        if len(data) < 96:
            issues.append(f"{rel}: file smaller than 96B MeshHeader ({len(data)} B)")
            continue
        magic, ver, _flags, vcnt, icnt, stride, ifmt, voff, vsz, ioff, isz = struct.unpack(">IHHIIIIIIII", data[:40])
        if magic != 0x574D5348:
            issues.append(f"{rel}: invalid magic 0x{magic:08X} (expected Big-Endian 0x574D5348)")
            continue
        if ver != 1:
            issues.append(f"{rel}: unsupported version {ver} (expected 1)")
            continue
        if stride not in (48, 80):
            issues.append(f"{rel}: unexpected vertexStride {stride} (expected 48 or 80)")
            continue
        if ifmt not in (0, 1):
            issues.append(f"{rel}: invalid indexFormat {ifmt} (expected 0=U16 or 1=U32)")
            continue
        if voff < 96 or ioff < 96 or (voff % 256) != 0 or (ioff % 32) != 0:
            issues.append(f"{rel}: misaligned offsets voff={voff} (256B), ioff={ioff} (32B)")
            continue
        if voff + vsz > len(data) or ioff + isz > len(data):
            issues.append(f"{rel}: buffer bounds overflow (vbo/ibo exceeds {len(data)} B)")
            continue
        if vsz != vcnt * stride:
            issues.append(f"{rel}: vboSize {vsz} != vertexCount*stride ({vcnt * stride})")
            continue
        idx_elem = 4 if ifmt == 1 else 2
        if isz < icnt * idx_elem:
            issues.append(f"{rel}: iboSize {isz} < indexCount*{idx_elem} ({icnt * idx_elem})")
            continue

        if icnt > 0 and vcnt > 0:
            idx_fmt_char = "I" if ifmt == 1 else "H"
            idxs = struct.unpack(">" + idx_fmt_char * icnt, data[ioff:ioff + icnt * idx_elem])
            if max(idxs) >= vcnt:
                issues.append(f"{rel}: IBO index {max(idxs)} >= vertexCount ({vcnt})")
                continue

        if stride == 80:
            for vi in range(vcnt):
                base = voff + vi * 80
                bi = struct.unpack("4B", data[base + 48:base + 52])
                bw = struct.unpack(">4f", data[base + 52:base + 68])
                if any(b >= 24 for b in bi):
                    issues.append(f"{rel}: vertex {vi} boneIndex >= 24 ({bi})")
                    break
                if any(math.isnan(w) or math.isinf(w) for w in bw):
                    issues.append(f"{rel}: vertex {vi} NaN/Inf boneWeight ({bw})")
                    break

    return len(wmesh_files), issues


def parse_gsh_metrics(path: Path) -> tuple[int, int | None, int | None, int]:
    """Parse GFD/GX2 header blocks to extract (file_size, vs_gprs, ps_gprs, ps_prog_bytes)."""
    data = path.read_bytes()
    if len(data) < 32 or data[:4] != b"Gfx2":
        return len(data), None, None, 0
    off = struct.unpack(">I", data[4:8])[0]
    vs_gprs: int | None = None
    ps_gprs: int | None = None
    ps_prog = 0
    while off + 32 <= len(data):
        magic, hsz, _maj, _minv, btype, bsz, _bidx, _resv = struct.unpack(">4sIIIIIII", data[off:off + 32])
        if magic != b"BLK{" or hsz < 32:
            break
        body_start = off + hsz
        body_end = body_start + bsz
        if body_end > len(data):
            break
        if btype == 3 and bsz >= 4:  # GX2_BLOCK_TYPE_VERTEX_SHADER_HEADER
            vs_gprs = struct.unpack(">I", data[body_start:body_start + 4])[0] & 0xFF
        elif btype == 6 and bsz >= 4:  # GX2_BLOCK_TYPE_PIXEL_SHADER_HEADER
            ps_gprs = struct.unpack(">I", data[body_start:body_start + 4])[0] & 0xFF
        elif btype == 7:  # GX2_BLOCK_TYPE_PIXEL_SHADER_PROGRAM
            ps_prog = bsz
        off = body_end
    return len(data), vs_gprs, ps_gprs, ps_prog


def check_shader_budgets() -> tuple[int, list[str]]:
    shader_dir = ROOT / "assets" / "shaders"
    manifest_path = shader_dir / "shader_budgets.json"
    if not manifest_path.exists():
        return 0, ["assets/shaders/shader_budgets.json: missing shader budget manifest"]

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except Exception as exc:
        return 0, [f"assets/shaders/shader_budgets.json: invalid JSON ({exc})"]

    default_max_bytes = int(manifest.get("default_max_bytes", 16384))
    default_max_ps_gprs = int(manifest.get("default_max_ps_gprs", 24))
    overrides = manifest.get("shaders", {})

    gsh_files = sorted(shader_dir.glob("*.gsh"))
    seen_names: set[str] = set()
    issues: list[str] = []

    for gsh in gsh_files:
        name = gsh.name
        seen_names.add(name)
        size, _vs_gprs, ps_gprs, _ps_prog = parse_gsh_metrics(gsh)
        rule = overrides.get(name, {})
        max_bytes = int(rule.get("max_bytes", default_max_bytes))
        max_ps_gprs = int(rule.get("max_ps_gprs", default_max_ps_gprs))

        if size > max_bytes:
            issues.append(f"{name}: size {size} B > budget {max_bytes} B")
        if ps_gprs is not None and ps_gprs > max_ps_gprs:
            issues.append(f"{name}: PS_GPR {ps_gprs} > budget {max_ps_gprs}")

    for listed_name in sorted(overrides.keys()):
        if listed_name not in seen_names:
            issues.append(f"{listed_name}: listed in shader_budgets.json but missing on disk")

    return len(gsh_files), issues


def cmd_check(args: argparse.Namespace) -> int:
    crlf = check_crlf(fix=args.fix_lf)
    leaks = check_core_leaks()
    zero_sdl_src = check_zero_sdl_source_refs()
    gx2_issues = check_gx2_invariants()
    gl_issues = check_gl_invariants()
    wmesh_count, wmesh_issues = check_cooked_wmesh()
    shader_count, shader_budget_issues = check_shader_budgets()
    sdl_status, sdl_count = check_sdl_symbols()
    dll_status, dll_unexpected = check_win_dlls()

    print(f"CRLF_CHECK: {'PASS (0)' if not crlf else f'FAIL ({len(crlf)})'}")
    for f in crlf[:15]:
        print(f"  CRLF: {f}")

    print(f"CORE_HEADER_LEAKS: {'PASS (0)' if not leaks else f'FAIL ({len(leaks)})'}")
    for l in leaks[:15]:
        print(f"  LEAK: {l}")

    print(f"ZERO_SDL_SOURCE_REFS: {'PASS (0)' if not zero_sdl_src else f'FAIL ({len(zero_sdl_src)})'}")
    for zs in zero_sdl_src[:15]:
        print(f"  SDL: {zs}")

    print(f"GX2_INVARIANTS: {'PASS (0)' if not gx2_issues else f'FAIL ({len(gx2_issues)})'}")
    for gi in gx2_issues[:15]:
        print(f"  GX2: {gi}")

    print(f"GL_INVARIANTS: {'PASS (0)' if not gl_issues else f'FAIL ({len(gl_issues)})'}")
    for gl_i in gl_issues[:15]:
        print(f"  GL:  {gl_i}")

    print(f"COOKED_WMESH_CHECK: {'PASS' if not wmesh_issues else 'FAIL'} ({wmesh_count} files, {len(wmesh_issues)} issues)")
    for wi in wmesh_issues[:15]:
        print(f"  WMESH: {wi}")

    print(f"SHADER_BUDGET_CHECK: {'PASS' if not shader_budget_issues else 'FAIL'} ({shader_count} shaders, {len(shader_budget_issues)} issues)")
    for sbi in shader_budget_issues[:15]:
        print(f"  SHADER: {sbi}")

    print(f"ELF_SDL_SYMBOLS: {sdl_status} ({sdl_count})")
    print(f"WIN_DLL_CHECK: {dll_status} ({len(dll_unexpected)})")
    for du in dll_unexpected[:15]:
        print(f"  DLL: {du}")

    report_binaries()

    failed = (
        bool(crlf)
        or bool(leaks)
        or bool(zero_sdl_src)
        or bool(gx2_issues)
        or bool(gl_issues)
        or bool(wmesh_issues)
        or bool(shader_budget_issues)
        or (sdl_count > 0)
        or bool(dll_unexpected)
        or dll_status.startswith("ERROR")
    )
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


def find_win_toolchain_bin() -> Path | None:
    winget_root = Path(os.environ.get("LOCALAPPDATA", "")) / "Microsoft" / "WinGet" / "Packages"
    if winget_root.exists():
        for match in sorted(winget_root.glob("BrechtSanders.WinLibs*/mingw64/bin/g++.exe")):
            return match.parent
    for cand in (Path(r"C:\devkitPro\msys2\ucrt64\bin"), Path(r"C:\msys64\ucrt64\bin")):
        if (cand / "g++.exe").exists():
            return cand
    gpp = shutil.which("g++")
    if gpp:
        return Path(gpp).parent
    return None


def _build_win(target: str | None) -> int:
    win_bin = find_win_toolchain_bin()
    if not win_bin:
        print("BUILD build-win: ERROR (Windows MinGW-w64 UCRT64 g++.exe not found)")
        return 1
    cmake_bin = str(win_bin / "cmake.exe") if (win_bin / "cmake.exe").exists() else (shutil.which("cmake") or "cmake")
    build_win = ROOT / "build-win"
    if not (build_win / "CMakeCache.txt").exists():
        cfg_cmd = [
            cmake_bin,
            "-S", str(ROOT),
            "-B", str(build_win),
            "-DCMAKE_C_COMPILER=" + str(win_bin / "gcc.exe"),
            "-DCMAKE_CXX_COMPILER=" + str(win_bin / "g++.exe"),
        ]
        if (win_bin / "ninja.exe").exists():
            cfg_cmd.extend(["-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=" + str(win_bin / "ninja.exe")])
        else:
            cfg_cmd.extend(["-G", "MinGW Makefiles", "-DCMAKE_MAKE_PROGRAM=" + str(win_bin / "mingw32-make.exe")])
        rc_cfg, out_cfg, err_cfg = run_cmd(cfg_cmd)
        if rc_cfg != 0:
            print("CMAKE CONFIGURE build-win FAILED:")
            print(out_cfg)
            print(err_cfg)
            return rc_cfg
    win_target = None if target == "wiiu_demos_wuhb" else target
    old_path = os.environ.get("PATH", "")
    os.environ["PATH"] = str(win_bin) + os.pathsep + old_path
    try:
        return run_filtered_build("build-win", win_target)
    finally:
        os.environ["PATH"] = old_path


def cmd_build(args: argparse.Namespace) -> int:
    if args.all:
        rc_wiiu = run_filtered_build("build", args.target)
        if rc_wiiu == 0 and args.lib:
            if (ROOT / "build_lib").exists():
                rc_wiiu = run_filtered_build("build_lib", None)
            else:
                print("BUILD build_lib: SKIP (directory not configured)")
        rc_win = _build_win(args.target) if rc_wiiu == 0 else rc_wiiu
        if rc_wiiu == 0 and rc_win == 0:
            report_binaries()
        return rc_wiiu if rc_wiiu != 0 else rc_win

    if args.win:
        rc = _build_win(args.target)
        if rc == 0:
            report_binaries()
        return rc

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
        numbered: list[tuple[int, int, str]] = []
        protocol_line = -1
        for idx, l in enumerate(lines, start=1):
            m = opt_re.match(l)
            if m:
                numbered.append((int(m.group(1)), idx, l))
            if protocol_line == -1 and l.startswith("## ") and "PROTOCOLO DE MANTENIMIENTO" in l:
                protocol_line = idx
        last_table_line = max((line_no for _, line_no, _ in numbered), default=-1)
        numbered.sort(key=lambda item: item[0])
        print(f"TOTAL_OPT_ROWS: {len(numbered)}")
        print(f"LAST_TABLE_LINE: {last_table_line}")
        print(f"PROTOCOL_HEADER_LINE: {protocol_line}")
        print("LAST_5_OPT_ROWS:")
        for _, _, row in numbered[-5:]:
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
    p_bld.add_argument("--win", action="store_true", help="Configure and build Windows native target in build-win/")
    p_bld.add_argument("--all", action="store_true", help="Build both Wii U (build/) and Windows native (build-win/)")
    p_bld.set_defaults(func=cmd_build)

    p_opt = sub.add_parser("opt", help="Query OPT-XXX or 'tail' in ANEXO without reading full file")
    p_opt.add_argument("query", help="OPT-442, 442, or tail")
    p_opt.set_defaults(func=cmd_opt)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
