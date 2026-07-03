#!/usr/bin/env python3
"""Generate the Terrain example's procedural 16-bit test heightmap.

This regenerates ``Examples/Terrain/assets/textures/test_heightmap_16bit.r16``,
a 512x512 little-endian uint16 headerless heightmap (the .r16 RAW format that
WorldCreator / Gaea also export) used by the Terrain example to demonstrate the
16-bit height pipeline.

The data is 100% procedurally generated from a fixed random seed (value-noise
FBm with a ridged transform) — it is NOT derived from any third-party asset.
The output is deterministic: same seed -> identical bytes, so this script is a
reproducible provenance record for the checked-in file. As algorithmically
generated noise, the output carries no third-party rights; treat it as public
domain (CC0), no attribution required.

Usage:
    python3 scripts/generateTestHeightmap.py [output_path]

Requires: numpy
"""
import os
import sys

import numpy as np

SIZE = 512   # output is SIZE x SIZE
SEED = 42    # fixed -> deterministic output
OCTAVES = 7
BASE_RES = 4


def octave(rng, res):
    """One octave of value noise, smootherstep-interpolated up to SIZE x SIZE."""
    g = rng.random((res + 1, res + 1))
    u = np.linspace(0, res, SIZE, endpoint=False)
    i = u.astype(int)
    f = u - i
    t = f * f * f * (f * (f * 6 - 15) + 10)   # smootherstep
    gx0 = g[:, i]
    gx1 = g[:, i + 1]
    row = gx0 * (1 - t) + gx1 * t
    gy0 = row[i, :]
    gy1 = row[i + 1, :]
    return gy0 * (1 - t[:, None]) + gy1 * t[:, None]


def generate():
    rng = np.random.default_rng(SEED)
    h = np.zeros((SIZE, SIZE))
    amp, total, res = 1.0, 0.0, BASE_RES
    for _ in range(OCTAVES):
        h += amp * octave(rng, res)
        total += amp
        amp *= 0.5
        res *= 2
    h /= total
    h = 1.0 - np.abs(h * 2.0 - 1.0)           # ridged -> mountain ridges
    h = h ** 2.2                              # sharpen peaks
    h = (h - h.min()) / (h.max() - h.min())   # normalize to [0, 1]
    return (h * 65535.0).astype("<u2")        # 16-bit little-endian


def default_output_path():
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(
        repo_root, "Examples", "Terrain", "assets", "textures",
        "test_heightmap.r16",
    )


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else default_output_path()
    data = generate()
    data.tofile(out)
    print(f"wrote {out}: {SIZE}x{SIZE} uint16, "
          f"{len(np.unique(data))} distinct height levels")


if __name__ == "__main__":
    main()
