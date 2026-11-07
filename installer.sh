#!/usr/bin/env bash
# Builds a .pkg installer for essentia (VST3 + AU) into /Library/Audio/Plug-Ins.
# Local/personal use: ad-hoc signing is enough, NO Apple verification needed.
# For public distribution over the internet you would need an Apple Developer ID
# (sign + notarize, hardened runtime) or Gatekeeper will block the download.
set -euo pipefail

cd "$(dirname "$0")"

BUILD_DIR=build
ARTEFACTS="$BUILD_DIR/StereoDelayHall_artefacts/Release"
VERSION="1.0.0"
PKG_OUT="$BUILD_DIR/essentia-${VERSION}.pkg"

[ -d "$ARTEFACTS/VST3" ] || { echo "ERROR: build first (./setup.sh) - $ARTEFACTS not found"; exit 1; }

echo "==> Staging bundles"
STAGE="$(mktemp -d)/pkgroot"
mkdir -p "$STAGE/Library/Audio/Plug-Ins/VST3" "$STAGE/Library/Audio/Plug-Ins/Components" "$STAGE/scripts"
cp -R "$ARTEFACTS/VST3/StereoDelayHall.vst3" "$STAGE/Library/Audio/Plug-Ins/VST3/"
cp -R "$ARTEFACTS/AU/StereoDelayHall.component" "$STAGE/Library/Audio/Plug-Ins/Components/"

echo "==> Ad-hoc signing"
codesign --force --deep --sign - "$STAGE/Library/Audio/Plug-Ins/VST3/StereoDelayHall.vst3" \
    || echo "warning: could not sign vst3"
codesign --force --deep --sign - "$STAGE/Library/Audio/Plug-Ins/Components/StereoDelayHall.component" \
    || echo "warning: could not sign component"

echo "==> Postinstall script (rescan Audio Units)"
cat > "$STAGE/scripts/postinstall" <<'EOS'
#!/bin/sh
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
EOS
chmod +x "$STAGE/scripts/postinstall"

echo "==> Building $PKG_OUT"
pkgbuild --root "$STAGE" \
         --scripts "$STAGE/scripts" \
         --identifier com.vibeaudio.essentia \
         --version "$VERSION" \
         --install-location / \
         "$PKG_OUT"

echo "==> Done: $PKG_OUT"
echo "Install with: open $PKG_OUT   (asks for admin password, installs to /Library/Audio/Plug-Ins)"
echo "Then restart your DAW."
