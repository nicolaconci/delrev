#!/usr/bin/env bash
# Builds a distributable ZIP for essentia: the plugin bundles + an install.sh
# script. On each Mac: unzip, then ./install.sh
#
# Why a zip and not a .pkg: on macOS 10.15+ the Installer rejects unsigned
# packages that install to the system volume ("package not compatible ...
# installing content on the system volume"). A zip installing into the USER
# library (~/Library/Audio/Plug-Ins) always works, needs no admin password and
# no Apple verification — perfect for personal multi-Mac use.
set -euo pipefail

cd "$(dirname "$0")"

BUILD_DIR=build
ARTEFACTS="$BUILD_DIR/StereoDelayHall_artefacts/Release"
VERSION="1.0.0"
ZIP_OUT="$BUILD_DIR/essentia-${VERSION}.zip"

[ -d "$ARTEFACTS/VST3" ] || { echo "ERROR: build first (./setup.sh) - $ARTEFACTS not found"; exit 1; }

echo "==> Staging bundles"
STAGE="$(mktemp -d)/essentia-${VERSION}"
mkdir -p "$STAGE"
cp -R "$ARTEFACTS/VST3/StereoDelayHall.vst3" "$STAGE/"
cp -R "$ARTEFACTS/AU/StereoDelayHall.component" "$STAGE/"

echo "==> Ad-hoc signing (avoids Gatekeeper issues)"
codesign --force --deep --sign - "$STAGE/StereoDelayHall.vst3" \
    || echo "warning: could not sign vst3"
codesign --force --deep --sign - "$STAGE/StereoDelayHall.component" \
    || echo "warning: could not sign component"

echo "==> Writing install.sh"
cat > "$STAGE/install.sh" <<'EOS'
#!/usr/bin/env bash
# Installs essentia (VST3 + AU) into the USER library: ~/Library/Audio/Plug-Ins
# Works on macOS 10.15+, no admin password needed.
set -euo pipefail
cd "$(dirname "$0")"
DEST="$HOME/Library/Audio/Plug-Ins"
mkdir -p "$DEST/VST3" "$DEST/Components"
codesign --force --deep --sign - StereoDelayHall.vst3 2>/dev/null || true
codesign --force --deep --sign - StereoDelayHall.component 2>/dev/null || true
rm -rf "$DEST/VST3/StereoDelayHall.vst3" "$DEST/Components/StereoDelayHall.component"
cp -R StereoDelayHall.vst3 "$DEST/VST3/"
cp -R StereoDelayHall.component "$DEST/Components/"
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "==> essentia installed to $DEST - restart your DAW"
EOS
chmod +x "$STAGE/install.sh"

echo "==> Zipping $ZIP_OUT"
rm -f "$ZIP_OUT"
(cd "$(dirname "$STAGE")" && zip -qr "$OLDPWD/$ZIP_OUT" "$(basename "$STAGE")")

echo "==> Done: $ZIP_OUT"
echo "On each Mac:  unzip $ZIP_OUT  &&  cd essentia-${VERSION}  &&  ./install.sh"
