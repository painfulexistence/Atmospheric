#!/usr/bin/env bash
# Smoke test for the bundled examples.
#
# For each built example it: launches the binary, lets it warm up, captures a
# single frame (screenshot mode, no FFmpeg needed), and lets the engine
# auto-quit. An example PASSES only if it
#   1. exits cleanly (rc 0) within the timeout, and
#   2. emits a "[Smoke] result ... blank=0" marker — i.e. it actually rendered
#      a non-blank frame (catches "context up but nothing drawn" regressions).
#
# The captured PNGs are kept under the artifact dir so they double as a
# baseline for later visual diffing.
#
# Usage:
#   scripts/smokeTest.sh [BUILD_DIR]        # BUILD_DIR defaults to ./build
#
# Environment:
#   CONFIG=Debug|Release|MinSizeRel   multi-config subdir to prefer (optional)
#   WARMUP=<seconds>                  settle time before the shot (default 2)
#   TIMEOUT=<seconds>                 per-example hard kill (default WARMUP+30)
#   ARTIFACT_DIR=<path>               PNGs + logs (default ./output/smoke)
#   EXAMPLES="A B C"                  subset to run
#   ALLOW_MISSING=1                   treat un-built examples as skip, not fail
#
# Headless CI (examples need a GL context):
#   xvfb-run -a -s "-screen 0 1280x720x24" scripts/smokeTest.sh build Release

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BUILD_DIR="${1:-$REPO_ROOT/build}"
[[ "$BUILD_DIR" = /* ]] || BUILD_DIR="$REPO_ROOT/$BUILD_DIR"

WARMUP="${WARMUP:-2}"
TIMEOUT="${TIMEOUT:-$(awk "BEGIN{print $WARMUP + 30}")}"
ARTIFACT_DIR="${ARTIFACT_DIR:-$REPO_ROOT/output/smoke}"
CONFIG="${CONFIG:-${2:-}}"
ALLOW_MISSING="${ALLOW_MISSING:-0}"

# Keep in sync with Examples/CMakeLists.txt (MazeFPS is disabled there).
DEFAULT_EXAMPLES="HelloWorld SceneLoader Physics2D RPG CardBattle VoxelWorld Terrain MidnightSkyraiders MultiplayerSandbox VideoPlayer LuaScripting"
EXAMPLES="${EXAMPLES:-$DEFAULT_EXAMPLES}"

mkdir -p "$ARTIFACT_DIR"

# Locate an example's executable across single-config (build/<Name>/<Name>) and
# multi-config (build/<Name>/<Config>/<Name>) layouts.
find_bin() {
    local ex="$1" c
    local cands=()
    [[ -n "$CONFIG" ]] && cands+=("$BUILD_DIR/$ex/$CONFIG/$ex")
    cands+=("$BUILD_DIR/$ex/$ex")
    for c in "${cands[@]}"; do
        [[ -f "$c" && -x "$c" ]] && { echo "$c"; return 0; }
    done
    c="$(find "$BUILD_DIR/$ex" -maxdepth 2 -type f -name "$ex" -perm -u+x 2>/dev/null | head -n1)"
    [[ -n "$c" ]] && { echo "$c"; return 0; }
    return 1
}

echo "Build dir : $BUILD_DIR"
echo "Config    : ${CONFIG:-<single-config>}"
echo "Artifacts : $ARTIFACT_DIR"
echo "Warmup ${WARMUP}s, timeout ${TIMEOUT}s"
echo

pass=0
fail=0
declare -a results

for ex in $EXAMPLES; do
    bin="$(find_bin "$ex")"
    if [[ -z "$bin" ]]; then
        if [[ "$ALLOW_MISSING" == "1" ]]; then
            echo "SKIP  $ex  (no binary)"
            results+=("SKIP  $ex")
            continue
        fi
        echo "FAIL  $ex  (no binary under $BUILD_DIR/$ex)"
        results+=("FAIL  $ex  no-binary")
        fail=$((fail + 1))
        continue
    fi

    out="$ARTIFACT_DIR/$ex.png"
    log="$ARTIFACT_DIR/$ex.log"
    bindir="$(dirname "$bin")"

    (
        cd "$bindir" || exit 1
        AE_CAPTURE_MODE=screenshot \
        AE_CAPTURE_WARMUP="$WARMUP" \
        AE_CAPTURE_DURATION=0 \
        AE_CAPTURE_OUTPUT="$out" \
        timeout --signal=TERM "${TIMEOUT}" "./$ex"
    ) >"$log" 2>&1
    rc=$?

    marker="$(grep -m1 '^\[Smoke\] result' "$log" || true)"
    detail="${marker#*] }"

    if [[ $rc -eq 124 ]]; then
        echo "FAIL  $ex  (timed out after ${TIMEOUT}s)"
        results+=("FAIL  $ex  timeout")
        fail=$((fail + 1))
    elif [[ $rc -ne 0 ]]; then
        echo "FAIL  $ex  (exit $rc)"
        results+=("FAIL  $ex  exit=$rc")
        fail=$((fail + 1))
    elif [[ -z "$marker" ]]; then
        echo "FAIL  $ex  (no capture marker — never rendered?)"
        results+=("FAIL  $ex  no-marker")
        fail=$((fail + 1))
    elif [[ "$marker" == *"blank=1"* ]]; then
        echo "FAIL  $ex  (blank frame)  $detail"
        results+=("FAIL  $ex  blank")
        fail=$((fail + 1))
    elif [[ ! -s "$out" ]]; then
        echo "FAIL  $ex  (no PNG written)"
        results+=("FAIL  $ex  no-png")
        fail=$((fail + 1))
    else
        echo "PASS  $ex  $detail"
        results+=("PASS  $ex")
        pass=$((pass + 1))
    fi
done

echo
echo "──────── Smoke summary ────────"
printf '%s\n' "${results[@]}"
echo "───────────────────────────────"
echo "PASS=$pass  FAIL=$fail"

[[ $fail -eq 0 ]]
