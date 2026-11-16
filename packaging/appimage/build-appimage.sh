#!/usr/bin/env bash
# AppImage builder. Run on Linux x86_64 with libsdl2-dev.
# linuxdeploy is not downloaded automatically: provide a vetted binary in PATH.
set -euo pipefail
cd "$(dirname "$0")/../.."
: "${LINUXDEPLOY:=linuxdeploy}"
command -v "$LINUXDEPLOY" >/dev/null || {
  echo 'linuxdeploy not found. Install it and set LINUXDEPLOY if necessary.' >&2
  exit 2
}
: "${CIPHERGBA_APPDIR:=$PWD/build-appimage/AppDir}"
cmake -S . -B build-appimage/cmake -DCMAKE_BUILD_TYPE=Release \
  -DCIPHERGBA_FRONTEND=SDL2 -DCIPHERGBA_NATIVE_TUNING=OFF \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-appimage/cmake --target CipherGBA CipherGBA_Core CipherGBA_Core_Static -j "${CIPHERGBA_BUILD_JOBS:-2}"
mkdir -p "$CIPHERGBA_APPDIR"
cmake --install build-appimage/cmake --prefix "$CIPHERGBA_APPDIR/usr"
ARCH=x86_64 "$LINUXDEPLOY" --appdir "$CIPHERGBA_APPDIR" --output appimage
echo "Review generated AppImage dependencies and test on a clean host before release."
