#!/usr/bin/env bash
# Installs essentia from a release zip produced by ./installer.sh
# Usage: ./install-from-zip.sh [path/to/essentia-1.0.0.zip]
#        (default: build/essentia-1.0.0.zip in this repo)
set -euo pipefail

cd "$(dirname "$0")"
ZIP="${1:-build/essentia-1.0.0.zip}"
[ -f "$ZIP" ] || { echo "ERROR: $ZIP not found — run ./installer.sh first, or pass the zip path"; exit 1; }

TMP="$(mktemp -d)"
echo "==> Unzipping $ZIP"
unzip -q "$ZIP" -d "$TMP"
DIR="$(find "$TMP" -name install.sh -printf '%h' | head -1)"
[ -n "$DIR" ] || { echo "ERROR: install.sh not found inside the zip"; exit 1; }

echo "==> Installing"
bash "$DIR/install.sh"
