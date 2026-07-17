#!/usr/bin/env python3
"""Compile Slang shaders to WGSL and GLSL 410 / GLSL ES 300.

Mirrors cmake/CompileSlang.cmake as a standalone driver so the codegen can run
in CI, in a pre-commit hook, or by hand without configuring the whole engine
build. Pipeline (option 1 in docs/slang-migration.md):

    .slang --slangc--> .spv --spirv-cross--> <name>.<entry>.glsl     (GL 4.1)
           |                 \--spirv-cross--> <name>.<entry>.es.glsl (GLSL ES 300)
           \-------slangc--> <name>.wgsl                              (WebGPU)

Entry points and their stages are read from a `// @slang-entry vs:vertex`
comment line in each source, so the shader file is self-describing.

Usage:
    scripts/compile_slang.py SRC_DIR OUT_DIR
    scripts/compile_slang.py Engine/default_assets/shaders/slang \
        Engine/default_assets/shaders/generated
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
from pathlib import Path

ENTRY_RE = re.compile(r"//\s*@slang-entry\s+([A-Za-z_]\w*)\s*:\s*(\w+)")


def tool(name: str) -> str:
    path = shutil.which(name)
    if not path:
        sys.exit(
            f"error: '{name}' not found on PATH. Install the Slang SDK / vcpkg "
            f"atmospheric[slang], or add it to PATH."
        )
    return path


def entries(src: Path) -> list[tuple[str, str]]:
    found = ENTRY_RE.findall(src.read_text())
    if not found:
        sys.exit(
            f"error: {src} declares no entry points. Add lines like:\n"
            f"    // @slang-entry vs:vertex\n"
            f"    // @slang-entry fs:fragment"
        )
    return found


def run(cmd: list[str]) -> None:
    print("  " + " ".join(Path(c).name if "/" in c else c for c in cmd))
    subprocess.run(cmd, check=True)


def compile_one(src: Path, out_dir: Path, slangc: str, xcross: str) -> None:
    stem = src.stem
    print(f"[{stem}]")

    # WGSL: one module, all entry points.
    run([slangc, str(src), "-target", "wgsl", "-o", str(out_dir / f"{stem}.wgsl")])

    # GLSL / ESSL: per entry point via SPIR-V.
    for ep, stage in entries(src):
        spv = out_dir / f"{stem}.{ep}.spv"
        run([slangc, str(src), "-target", "spirv",
             "-entry", ep, "-stage", stage, "-o", str(spv)])
        run([xcross, str(spv), "--version", "410", "--no-es",
             "--output", str(out_dir / f"{stem}.{ep}.glsl")])
        run([xcross, str(spv), "--version", "300", "--es",
             "--output", str(out_dir / f"{stem}.{ep}.es.glsl")])
        spv.unlink()  # intermediate; not shipped


def main() -> int:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src_dir, out_dir = Path(sys.argv[1]), Path(sys.argv[2])
    if not src_dir.is_dir():
        sys.exit(f"error: source dir not found: {src_dir}")
    out_dir.mkdir(parents=True, exist_ok=True)

    slangc, xcross = tool("slangc"), tool("spirv-cross")
    sources = sorted(src_dir.glob("*.slang"))
    if not sources:
        print(f"no .slang files in {src_dir}")
        return 0
    for src in sources:
        compile_one(src, out_dir, slangc, xcross)
    print(f"done: {len(sources)} shader(s) → {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
