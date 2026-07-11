#!/usr/bin/env bash
# Download Pixar's Kitchen_set USD sample into Examples/USDViewer/assets/kitchen.
# Kept out of the repo (~30MB). Official source: openusd.org downloads page.
set -euo pipefail
cd "$(dirname "$0")/.."
DEST="Examples/USDViewer/assets/kitchen"
mkdir -p "$DEST"
URL="https://graphics.pixar.com/usd/files/Kitchen_set.zip"
echo "Fetching Kitchen_set from $URL ..."
curl -L -o "$DEST/Kitchen_set.zip" "$URL"
unzip -o -q "$DEST/Kitchen_set.zip" -d "$DEST"
# The zip contains a Kitchen_set/ folder; flatten so the .usd sits at $DEST.
if [ -d "$DEST/Kitchen_set" ]; then
    mv "$DEST"/Kitchen_set/* "$DEST"/
    rmdir "$DEST/Kitchen_set"
fi
rm -f "$DEST/Kitchen_set.zip"
echo "Done: $DEST/Kitchen_set.usd"
