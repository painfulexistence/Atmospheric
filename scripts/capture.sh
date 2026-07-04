#!/usr/bin/env bash
# Sequentially launch each built example, let it warm up, capture (video or
# screenshots) for a fixed duration, then let the engine auto-quit.
#
# The capture behaviour is driven entirely by environment variables read by the
# engine in Application::ParseAutoCaptureEnv().
#
# Usage:
#   scripts/capture [BUILD_DIR] [options]
#

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Helper to print usage
show_usage() {
    echo "Usage: $0 [BUILD_DIR] [options]"
    echo ""
    echo "BUILD_DIR defaults to ./build if not specified."
    echo ""
    echo "Options:"
    echo "  -m, --mode <video|screenshot>   Capture mode (default: video)"
    echo "  -w, --warmup <seconds>          Settle time before capture (default: 3)"
    echo "  -d, --duration <seconds>        Capture length in seconds (default: 10)"
    echo "  -o, --output-dir <path>         Output directory (default: ./output/captures)"
    echo "  -e, --examples <\"A B C\">      Space-separated subset of examples to run"
    echo "  -h, --help                      Show this help message"
    echo ""
    echo "Example:"
    echo "  $0"
    echo "  $0 build --mode screenshot --duration 2"
}

# Default values
BUILD_DIR=""
MODE="${MODE:-video}"
WARMUP="${WARMUP:-3}"
DURATION="${DURATION:-10}"
CAPTURE_DIR=""
EXAMPLES_SUBSET=""

# Parse first argument if it's a build dir
if [[ $# -gt 0 && ! "$1" =~ ^- ]]; then
    BUILD_DIR="$1"
    shift
fi

# Fallback default build dir
if [[ -z "$BUILD_DIR" ]]; then
    BUILD_DIR="$REPO_ROOT/build"
else
    [[ "$BUILD_DIR" = /* ]] || BUILD_DIR="$REPO_ROOT/$BUILD_DIR"
fi

# Parse options
while [[ $# -gt 0 ]]; do
    case "$1" in
        -m|--mode)
            MODE="$2"
            shift 2
            ;;
        -w|--warmup)
            WARMUP="$2"
            shift 2
            ;;
        -d|--duration)
            DURATION="$2"
            shift 2
            ;;
        -o|--output-dir)
            CAPTURE_DIR="$2"
            shift 2
            ;;
        -e|--examples)
            EXAMPLES_SUBSET="$2"
            shift 2
            ;;
        -h|--help)
            show_usage
            exit 0
            ;;
        *)
            echo "Error: Unknown option '$1'"
            show_usage
            exit 1
            ;;
    esac
done

if [[ "$MODE" == "screenshot" ]]; then EXT=png; else EXT=mp4; fi

# Default captures directory
if [[ -z "$CAPTURE_DIR" ]]; then
    CAPTURE_DIR="$REPO_ROOT/output/captures"
else
    [[ "$CAPTURE_DIR" = /* ]] || CAPTURE_DIR="$REPO_ROOT/$CAPTURE_DIR"
fi

DEFAULT_EXAMPLES="Animation CardBattle MidnightSkyraiders RPG Physics2D 3DBasics VoxelWorld Terrain VideoPlayer MultiplayerSandbox LuaScripting"
EXAMPLES="${EXAMPLES_SUBSET:-$DEFAULT_EXAMPLES}"

find_bin() {
    local ex="$1"
    local cands=(
        "$BUILD_DIR/$ex/$ex"
        "$BUILD_DIR/$ex/MinSizeRel/$ex"
        "$BUILD_DIR/$ex/Release/$ex"
        "$BUILD_DIR/$ex/RelWithDebInfo/$ex"
        "$BUILD_DIR/$ex/Debug/$ex"
    )
    for c in "${cands[@]}"; do
        [[ -f "$c" && -x "$c" ]] && { echo "$c"; return 0; }
    done
    
    if [[ -d "$BUILD_DIR/$ex" ]]; then
        local found
        found="$(find "$BUILD_DIR/$ex" -maxdepth 3 -type f -name "$ex" -perm -u+x 2>/dev/null | head -n1)"
        [[ -n "$found" ]] && { echo "$found"; return 0; }
    fi

    # Name-agnostic fallback: some examples stage a runtime whose binary name
    # differs from the folder (e.g. LuaScripting stages the shared AtmosLua
    # runtime). Run the unique executable in the folder, ignoring data files
    # under assets/.
    if [[ -d "$BUILD_DIR/$ex" ]]; then
        local found
        found="$(find "$BUILD_DIR/$ex" -maxdepth 2 -type f -perm -u+x -not -path '*/assets/*' 2>/dev/null | head -n1)"
        [[ -n "$found" ]] && { echo "$found"; return 0; }
    fi
    return 1
}

# Fallback check for timeout tool
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_CMD="timeout"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT_CMD="gtimeout"
else
    TIMEOUT_CMD=""
fi

TIMEOUT_VAL="$(awk "BEGIN{print $WARMUP + $DURATION + 30}")"

mkdir -p "$CAPTURE_DIR"
echo "Build dir : $BUILD_DIR"
echo "Mode      : $MODE  (warmup ${WARMUP}s, duration ${DURATION}s)"
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
        export AE_CAPTURE_MODE="$MODE"
        export AE_CAPTURE_WARMUP="$WARMUP"
        export AE_CAPTURE_DURATION="$DURATION"
        export AE_CAPTURE_OUTPUT="$out"
        
        if [[ -n "$TIMEOUT_CMD" ]]; then
            "$TIMEOUT_CMD" --signal=TERM "${TIMEOUT_VAL}" "./$ex"
        else
            "./$ex"
        fi
    )
    rc=$?

    if [[ $rc -eq 124 ]]; then
        echo "warn  $ex  timed out after ${TIMEOUT_VAL}s (killed)"
        failed=$((failed + 1))
    elif [[ $rc -ne 0 ]]; then
        echo "warn  $ex  exited with code $rc"
        failed=$((failed + 1))
    fi
done

echo
echo "Done. Ran $ran example(s), $failed warning(s). Captures in $CAPTURE_DIR"
[[ $failed -eq 0 ]]
