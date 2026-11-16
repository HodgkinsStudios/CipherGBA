#!/usr/bin/env bash
# Created by Jacob Hodgkins.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
if [[ ! -x ./bin/CipherGBA || ! -f ./bin/libCipherGBA_Core.so.1 ]]; then
    echo "CipherGBA not built yet. Building now..."
    ./build.sh
fi
exec ./bin/CipherGBA "$@"
