#!/bin/bash
# Check that the Android NDK version pinned in HelloWorld's build.gradle.kts
# matches the version the vcpkg Android triplets are chainloaded against.
#
# triplets/*-android.cmake (the files vcpkg actually reads) are the single
# source of truth for the required NDK version — this script derives from
# them directly instead of hardcoding a second copy that could drift.
#
# Usage:
#   ./scripts/checkNdkVersion.sh
#   ./scripts/checkNdkVersion.sh --print-primary-version

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TRIPLETS_DIR="$SCRIPT_DIR/../triplets"

# Extract the NDK version from every *-android.cmake triplet's
# VCPKG_CHAINLOAD_TOOLCHAIN_FILE path, and make sure they all agree.
mapfile -t TRIPLET_VERSIONS < <(
  grep -ohE '/ndk/[0-9]+\.[0-9]+\.[0-9]+/' "$TRIPLETS_DIR"/*-android.cmake \
    | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' \
    | sort -u
)

if [ "${#TRIPLET_VERSIONS[@]}" -eq 0 ]; then
  echo "Error: failed to detect an NDK version from any triplets/*-android.cmake file." >&2
  exit 1
fi

if [ "${#TRIPLET_VERSIONS[@]}" -gt 1 ]; then
  echo "Error: Android triplets disagree on NDK version: ${TRIPLET_VERSIONS[*]}" >&2
  exit 1
fi

REQUIRED_VERSION="${TRIPLET_VERSIONS[0]}"

if [ "${1:-}" = "--print-primary-version" ]; then
  echo "$REQUIRED_VERSION"
  exit 0
fi

GRADLE_FILE="$SCRIPT_DIR/../Examples/HelloWorld/platforms/android/app/build.gradle.kts"

CURRENT_VERSION=$(grep -oE 'ndkVersion[[:space:]]*=[[:space:]]*"[0-9.]+"' "$GRADLE_FILE" | grep -oE '[0-9.]+')

if [ -z "$CURRENT_VERSION" ]; then
  echo "Error: failed to detect ndkVersion from $GRADLE_FILE." >&2
  exit 1
fi

if [ "$CURRENT_VERSION" = "$REQUIRED_VERSION" ]; then
  echo "Android NDK version check passed ($CURRENT_VERSION)"
  exit 0
fi

echo "Error: build.gradle.kts pins NDK $CURRENT_VERSION, but the vcpkg Android triplets require $REQUIRED_VERSION." >&2
echo "  Update ndkVersion in Examples/HelloWorld/platforms/android/app/build.gradle.kts to $REQUIRED_VERSION." >&2
exit 1
