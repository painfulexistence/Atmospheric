#!/bin/bash
# Check that the Android NDK version pinned in HelloWorld's build.gradle.kts
# matches the version the vcpkg Android triplets are chainloaded against
# (triplets/arm64-android.cmake, triplets/x64-android.cmake).
# Usage:
#   ./scripts/checkNdkVersion.sh
#   ./scripts/checkNdkVersion.sh --print-primary-version

set -euo pipefail

REQUIRED_VERSIONS=("29.0.13113456") # <-- keep in sync with triplets/*-android.cmake

if [ "${1:-}" = "--print-primary-version" ]; then
  echo "${REQUIRED_VERSIONS[0]}"
  exit 0
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GRADLE_FILE="$SCRIPT_DIR/../Examples/HelloWorld/platforms/android/app/build.gradle.kts"

CURRENT_VERSION=$(grep -oE 'ndkVersion[[:space:]]*=[[:space:]]*"[0-9.]+"' "$GRADLE_FILE" | grep -oE '[0-9.]+')

if [ -z "$CURRENT_VERSION" ]; then
  echo "Error: failed to detect ndkVersion from $GRADLE_FILE." >&2
  exit 1
fi

for v in "${REQUIRED_VERSIONS[@]}"; do
  if [ "$CURRENT_VERSION" = "$v" ]; then
    echo "Android NDK version check passed ($CURRENT_VERSION)"
    exit 0
  fi
done

echo "Error: build.gradle.kts pins NDK $CURRENT_VERSION, but the vcpkg Android triplets require ${REQUIRED_VERSIONS[*]}." >&2
echo "  Update ndkVersion in Examples/HelloWorld/platforms/android/app/build.gradle.kts to ${REQUIRED_VERSIONS[0]}." >&2
exit 1
