#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
# Local builds use host-native tuning. Set CIPHERGBA_NATIVE_TUNING=OFF
# for generic builds intended to run on other CPU models.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPIXELGBA_ENABLE_LTO=ON \
  -DCIPHERGBA_NATIVE_TUNING="${CIPHERGBA_NATIVE_TUNING:-ON}"
# Build only the application and its required core dependencies.
# The optional static SDK is available as a separate explicit target.
cmake --build build --target CipherGBA -j "${CIPHERGBA_BUILD_JOBS:-2}"
printf 'CipherGBA built: %s/bin/CipherGBA\n' "$PWD"
