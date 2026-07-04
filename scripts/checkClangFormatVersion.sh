#!/bin/bash
# Check that the installed clang-format and clang-tidy match the version
# pinned for this repo. Different major versions of clang-tidy change
# check behavior (identifier-naming, fix rewrites) and clang-format
# reflows different constructs, so mismatched local vs. CI versions
# produce diff churn and reproducibility issues.
#
# Usage:
#   ./scripts/checkClangVersion.sh                          # check both tools
#   ./scripts/checkClangVersion.sh --tool clang-format      # check one tool
#   ./scripts/checkClangVersion.sh --print-primary-version  # emit version only

set -euo pipefail

REQUIRED_VERSION=21

TOOLS=(clang-format clang-tidy)

while [ $# -gt 0 ]; do
  case "$1" in
    --print-primary-version)
      echo "$REQUIRED_VERSION"
      exit 0
      ;;
    --tool)
      shift
      TOOLS=("$1")
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 2
      ;;
  esac
  shift
done

# Prefer versioned binaries (clang-format-21) so users can keep multiple
# LLVM installs side-by-side.
find_tool() {
  local name="$1"
  if command -v "${name}-${REQUIRED_VERSION}" >/dev/null 2>&1; then
    command -v "${name}-${REQUIRED_VERSION}"
    return
  fi
  if command -v "$name" >/dev/null 2>&1; then
    command -v "$name"
    return
  fi
  local brew_path="/opt/homebrew/opt/llvm/bin/$name"
  if [ -x "$brew_path" ]; then
    echo "$brew_path"
    return
  fi
  return 1
}

check_tool() {
  local name="$1"
  local exe
  if ! exe="$(find_tool "$name")"; then
    echo "Error: $name not found. Install LLVM $REQUIRED_VERSION (e.g. brew install llvm@$REQUIRED_VERSION)." >&2
    return 1
  fi

  # First line examples:
  #   Ubuntu clang-format version 21.1.0
  #   Homebrew clang-tidy version 21.1.0
  local version_line
  version_line="$("$exe" --version | head -n1)"
  local major
  major="$(echo "$version_line" | grep -oE 'version[[:space:]]+[0-9]+' | grep -oE '[0-9]+' | head -n1)"

  if [ -z "$major" ]; then
    echo "Error: failed to parse $name version from: $version_line" >&2
    return 1
  fi

  if [ "$major" != "$REQUIRED_VERSION" ]; then
    echo "Error: $name major version mismatch." >&2
    echo "  Expected: $REQUIRED_VERSION.x" >&2
    echo "  Found:    $major.x  ($exe)" >&2
    echo "  Install matching version, e.g. \`brew install llvm@$REQUIRED_VERSION\`." >&2
    return 1
  fi

  echo "$name $major check passed ($exe)"
}

status=0
for t in "${TOOLS[@]}"; do
  check_tool "$t" || status=1
done
exit "$status"
