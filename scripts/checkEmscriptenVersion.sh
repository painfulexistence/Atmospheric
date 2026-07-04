#!/bin/bash
# Check that the active Emscripten version matches the required version.
# Usage:
#   ./scripts/checkEmscriptenVersion.sh
#   ./scripts/checkEmscriptenVersion.sh --print-primary-version

set -euo pipefail

REQUIRED_VERSIONS=("4.0.18") # <-- update when upgrading Emscripten

if [ "${1:-}" = "--print-primary-version" ]; then
  echo "${REQUIRED_VERSIONS[0]}"
  exit 0
fi

CURRENT_VERSION=$(emcc --version | awk 'NR==1 { for(i=1;i<=NF;++i) { if($i~/^[0-9]+\.[0-9]+\.[0-9]+$/) { print $i; exit } } }')

if [ -z "$CURRENT_VERSION" ]; then
  echo "Error: failed to detect Emscripten version from emcc --version." >&2
  exit 1
fi

for v in "${REQUIRED_VERSIONS[@]}"; do
  if [ "$CURRENT_VERSION" = "$v" ]; then
    echo "Emscripten version check passed ($CURRENT_VERSION)"
    exit 0
  fi
done

echo "Error: requires Emscripten ${REQUIRED_VERSIONS[*]}, got $CURRENT_VERSION." >&2
echo "  Run: emsdk install ${REQUIRED_VERSIONS[0]} && emsdk activate ${REQUIRED_VERSIONS[0]}" >&2
exit 1
