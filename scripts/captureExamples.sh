#!/usr/bin/env bash
# Sequentially launch each built example, let it warm up, capture (video or
# screenshots) for a fixed duration, then let the engine auto-quit.
#
# The capture behaviour is driven entirely by environment variables read by the
# engine in Application::ParseAutoCaptureEnv(); no per-example code is involved.
# Each example is launched from its own output directory so its relative
# `assets/` path resolves and captures land predictably.
#
# Usage:
#   scripts/captureExamples.sh [BUILD_DIR]        # BUILD_DIR defaults to ./build
#
# Environment overrides:
#   MODE=video|screenshot   capture mode               (default video)
#   WARMUP=<seconds>        settle time before capture (default 3)
#   DURATION=<seconds>      capture length             (default 10)
#                           screenshot mode: 1 shot/sec; set 0 for a single shot
#   TIMEOUT=<seconds>       hard kill safety net       (default WARMUP+DURATION+30)
#   CAPTURE_DIR=<path>      where to collect outputs   (default ./output/captures)
#   EXAMPLES="A B C"        whitespace-separated subset to run
#
# Headless machines (CI): wrap the call in xvfb, e.g.
#   xvfb-run -a -s "-screen 0 1280x720x24" scripts/captureExamples.sh

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BUILD_DIR="${1:-$REPO_ROOT/build}"
[[ "$BUILD_DIR" = /* ]] || BUILD_DIR="$REPO_ROOT/$BUILD_DIR"

MODE="${MODE:-video}"
WARMUP="${WARMUP:-3}"
DURATION="${DURATION:-10}"
TIMEOUT="${TIMEOUT:-$(awk "BEGIN{print $WARMUP + $DURATION + 30}")}"
CAPTURE_DIR="${CAPTURE_DIR:-$REPO_ROOT/output/captures}"
CONFIG="${CONFIG:-}"

if [[ "$MODE" == "screenshot" ]]; then EXT=png; else EXT=mp4; fi

# Keep in sync with Examples/CMakeLists.txt (MazeFPS is disabled there).
DEFAULT_EXAMPLES="HelloWorld SceneLoader Physics2D RPG CardBattle VoxelWorld Terrain MidnightSkyraiders MultiplayerSandbox VideoPlayer LuaScripting"
EXAMPLES="${EXAMPLES:-$DEFAULT_EXAMPLES}"

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

mkdir -p "$CAPTURE_DIR"
echo "Build dir : $BUILD_DIR"
echo "Mode      : $MODE  (warmup ${WARMUP}s, duration ${DURATION}s, timeout ${TIMEOUT}s)"
echo "Output    : $CAPTURE_DIR"
echo

ran=0
failed=0
for ex in $EXAMPLES; do
    bin="$(find_bin "$ex")"
    if [[ -z "$bin" ]]; then
        echo "skip  $ex  (not built under $BUILD_DIR/$ex)"
        continue
    fi

    out="$CAPTURE_DIR/$ex.$EXT"
    echo "rec   $ex  ->  $out"
    ran=$((ran + 1))

    (
        cd "$(dirname "$bin")" || exit 1
        AE_CAPTURE_MODE="$MODE" \
        AE_CAPTURE_WARMUP="$WARMUP" \
        AE_CAPTURE_DURATION="$DURATION" \
        AE_CAPTURE_OUTPUT="$out" \
        timeout --signal=TERM "${TIMEOUT}" "./$ex"
    )
    rc=$?

    if [[ $rc -eq 124 ]]; then
        echo "warn  $ex  timed out after ${TIMEOUT}s (killed)"
        failed=$((failed + 1))
    elif [[ $rc -ne 0 ]]; then
        echo "warn  $ex  exited with code $rc"
        failed=$((failed + 1))
    fi
done

echo
echo "Done. Ran $ran example(s), $failed warning(s). Captures in $CAPTURE_DIR"
[[ $failed -eq 0 ]]
