#!/usr/bin/env bash
# One-command setup for essentia on macOS.
# Run from the repository root: ./setup.sh
# Fetches JUCE automatically (first run, needs network + git), configures CMake,
# builds VST3 + AU + Standalone, runs the DSP unit tests, signs the bundles
# (ad-hoc) and installs them into ~/Library/Audio/Plug-Ins/, overwriting any
# previous install.
set -euo pipefail

cd "$(dirname "$0")"

BUILD_TYPE=Release
BUILD_DIR=build
ARTEFACTS="$BUILD_DIR/StereoDelayHall_artefacts/$BUILD_TYPE"
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"

echo "==> Checking prerequisites"
command -v cmake >/dev/null 2>&1 || { echo "ERROR: cmake not found (brew install cmake)"; exit 1; }
command -v git   >/dev/null 2>&1 || { echo "ERROR: git not found (install Xcode Command Line Tools)"; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo "ERROR: Xcode Command Line Tools missing (xcode-select --install)"; exit 1; }

echo "==> Configuring CMake ($BUILD_TYPE) — first run downloads JUCE via git (needs network)"
ok=""
for attempt in 1 2 3; do
    if cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=$BUILD_TYPE; then ok=1; break; fi
    echo "    configure attempt $attempt failed, retrying in 5 s..."
    sleep 5
done
[ -n "$ok" ] || { echo "ERROR: cmake configure failed (see output above). Check network/git access to github.com."; exit 1; }
[ -d "$BUILD_DIR/_deps/juce-src" ] || { echo "ERROR: JUCE was not fetched into $BUILD_DIR/_deps — check network access to github.com"; exit 1; }
echo "    JUCE fetched OK"

echo "==> Building VST3 + AU + Standalone"
cmake --build "$BUILD_DIR" --config $BUILD_TYPE

echo "==> Running DSP unit tests"
cmake --build "$BUILD_DIR" --target dsp_test
"$BUILD_DIR/dsp_test" | tail -1

# Bundles are directories; prefer the standard JUCE layout, fall back to a search.
find_bundle() {
    local name="$1" subdir="$2"
    local p="$ARTEFACTS/$subdir/$name"
    if [ -e "$p" ]; then printf '%s' "$p"; return; fi
    find "$BUILD_DIR" -type d -name "$name" 2>/dev/null | head -1
}
VST3_BUNDLE="$(find_bundle StereoDelayHall.vst3 VST3)"
AU_BUNDLE="$(find_bundle StereoDelayHall.component AU)"
APP_BUNDLE="$(find_bundle StereoDelayHall.app Standalone)"
[ -n "$VST3_BUNDLE" ] || { echo "ERROR: StereoDelayHall.vst3 not found under $BUILD_DIR — build failed?"; exit 1; }
[ -n "$AU_BUNDLE" ]   || { echo "ERROR: StereoDelayHall.component not found under $BUILD_DIR — build failed?"; exit 1; }

echo "==> Signing bundles (ad-hoc)"
codesign --force --deep --sign - "$VST3_BUNDLE" \
    || echo "warning: could not sign the .vst3 (continuing anyway)"
codesign --force --deep --sign - "$AU_BUNDLE" \
    || echo "warning: could not sign the .component (continuing anyway)"

echo "==> Installing into ~/Library/Audio/Plug-Ins (overwriting existing)"
mkdir -p "$VST3_DIR" "$AU_DIR"
rm -rf "$VST3_DIR/StereoDelayHall.vst3" "$AU_DIR/StereoDelayHall.component"
cp -R "$VST3_BUNDLE" "$VST3_DIR/"
cp -R "$AU_BUNDLE" "$AU_DIR/"

echo "==> Done"
echo "VST3:          $VST3_DIR/StereoDelayHall.vst3"
echo "Audio Unit:    $AU_DIR/StereoDelayHall.component"
echo "Standalone:    ${APP_BUNDLE:-$ARTEFACTS/Standalone/StereoDelayHall.app}"
echo "Restart your DAW (or: killall -9 AudioComponentRegistrar) to rescan plugins."
