#!/usr/bin/env bash
# Smoke test for the bundled examples.
#
# Usage:
#   scripts/smoke [BUILD_DIR] [options]
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
    echo "  -w, --warmup <seconds>          Settle time before capture (default: 2)"
    echo "  -o, --artifact-dir <path>       Output directory for PNGs + logs (default: ./output/smoke)"
    echo "  -e, --examples <\"A B C\">      Space-separated subset of examples to run"
    echo "  -a, --allow-missing             Treat un-built examples as skip, not fail"
    echo "  -h, --help                      Show this help message"
    echo ""
    echo "Example:"
    echo "  $0"
    echo "  $0 build --warmup 3 --allow-missing"
}

# Default values
BUILD_DIR=""
WARMUP="${WARMUP:-2}"
ARTIFACT_DIR=""
EXAMPLES_SUBSET=""
ALLOW_MISSING="${ALLOW_MISSING:-0}"

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
        -w|--warmup)
            WARMUP="$2"
            shift 2
            ;;
        -o|--artifact-dir)
            ARTIFACT_DIR="$2"
            shift 2
            ;;
        -e|--examples)
            EXAMPLES_SUBSET="$2"
            shift 2
            ;;
        -a|--allow-missing)
            ALLOW_MISSING=1
            shift
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

# Default artifacts directory
if [[ -z "$ARTIFACT_DIR" ]]; then
    ARTIFACT_DIR="$REPO_ROOT/output/smoke"
else
    [[ "$ARTIFACT_DIR" = /* ]] || ARTIFACT_DIR="$REPO_ROOT/$ARTIFACT_DIR"
fi

DEFAULT_EXAMPLES="Animation MidnightSkyraiders CardBattle RPG Physics2D VideoPlayer 3DBasics VoxelWorld Terrain MultiplayerSandbox LuaScripting"
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

TIMEOUT_VAL="$(awk "BEGIN{print $WARMUP + 30}")"

mkdir -p "$ARTIFACT_DIR"
echo "Build dir : $BUILD_DIR"
echo "Artifacts : $ARTIFACT_DIR"
echo "Warmup ${WARMUP}s, timeout ${TIMEOUT_VAL}s"
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
        export AE_CAPTURE_MODE=screenshot
        export AE_CAPTURE_WARMUP="$WARMUP"
        export AE_CAPTURE_DURATION=0
        export AE_CAPTURE_OUTPUT="$out"
        
        if [[ -n "$TIMEOUT_CMD" ]]; then
            "$TIMEOUT_CMD" --signal=TERM "${TIMEOUT_VAL}" "./$ex"
        else
            "./$ex"
        fi
    ) >"$log" 2>&1
    rc=$?

    marker="$(grep -m1 '^\[Smoke\] result' "$log" || true)"
    detail="${marker#*] }"

    if [[ $rc -eq 124 ]]; then
        echo "FAIL  $ex  (timed out after ${TIMEOUT_VAL}s)"
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
