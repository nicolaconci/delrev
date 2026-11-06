#!/usr/bin/env bash
# One-command setup for StereoDelayHall on macOS.
# Run from the repository root: ./setup.sh
# Configures CMake, builds VST3 + AU + Standalone, runs the DSP unit tests,
# signs the plugin bundles (ad-hoc) and installs them into
# ~/Library/Audio/Plug-Ins/, overwriting any existing installation.
set -euo pipefail

cd "$(dirname "$0")"

BUILD_TYPE=Release
BUILD_DIR=build
ARTEFACTS="$BUILD_DIR/StereoDelayHall_artefacts/$BUILD_TYPE"
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"

echo "==> Checking prerequisites"
command -v cmake >/dev/null 2>&1 || { echo "ERROR: cmake not found (brew install cmake)"; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo "ERROR: Xcode Command Line Tools missing (xcode-select --install)"; exit 1; }

echo "==> Configuring CMake ($BUILD_TYPE)"
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=$BUILD_TYPE

echo "==> Building VST3 + AU + Standalone"
cmake --build "$BUILD_DIR" --config $BUILD_TYPE

echo "==> Running DSP unit tests"
cmake --build "$BUILD_DIR" --target dsp_test
"$BUILD_DIR/dsp_test" | tail -1

echo "==> Signing bundles (ad-hoc)"
codesign --force --deep --sign - "$ARTEFACTS/StereoDelayHall.vst3" \
    || echo "warning: could not sign the .vst3 (continuing anyway)"
codesign --force --deep --sign - "$ARTEFACTS/StereoDelayHall.component" \
    || echo "warning: could not sign the .component (continuing anyway)"

echo "==> Installing into ~/Library/Audio/Plug-Ins (overwriting existing)"
mkdir -p "$VST3_DIR" "$AU_DIR"
rm -rf "$VST3_DIR/StereoDelayHall.vst3" "$AU_DIR/StereoDelayHall.component"
cp -R "$ARTEFACTS/StereoDelayHall.vst3" "$VST3_DIR/"
cp -R "$ARTEFACTS/StereoDelayHall.component" "$AU_DIR/"

echo "==> Done"
echo "VST3:          $VST3_DIR/StereoDelayHall.vst3"
echo "Audio Unit:    $AU_DIR/StereoDelayHall.component"
echo "Standalone:    $ARTEFACTS/Standalone/StereoDelayHall.app"
echo "Restart your DAW (or: killall -9 AudioComponentRegistrar) to rescan plugins."
