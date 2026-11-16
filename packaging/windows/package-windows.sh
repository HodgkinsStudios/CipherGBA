#!/usr/bin/env bash
# Package a testable Windows x64 development build, including MSYS2 UCRT DLLs.
# Executed in GitHub Actions' MSYS2 UCRT64 shell, not on users' machines.
set -euo pipefail
cd "$(dirname "$0")/../.."
command -v ldd >/dev/null || { echo 'ldd is required' >&2;exit 2; }
name="CipherGBA-v0.3-Windows-x64-CI"
dest="dist/$name"
mkdir -p "$dest"
cp bin/CipherGBA.exe "$dest/"
cp README.md RELEASE_NOTES.md THIRD_PARTY_NOTICES.md "$dest/"
cat > "$dest/START_HERE.txt" <<'EOF'
CipherGBA v0.3 — Windows x64 CI build
Start CipherGBA.exe. Press Ctrl+O to open a ROM, or drag and drop a GBA
ROM (.gba or .zip) onto the emulator window. Ctrl+B imports an original
16KiB GBA BIOS. You may also pass --rom /path/to/game.gba on the command line.

This package is an CI package; test interactively before treating it as a final
release. Some Linux X11-only features are not available in SDL2.
No BIOS, ROM or game files are included. User saves live under the
OS-specific application data directory, not alongside CipherGBA.exe.
EOF
# ldd enumerates transitively resolved MinGW/UCRT DLLs. Include all non-system
# dependencies to avoid requiring an MSYS2 installation on the recipient's PC.
mapfile -t dlls < <(ldd bin/CipherGBA.exe |
    sed -nE 's@.*=> (/ucrt64/bin/[^[:space:]]+\.dll).*@\1@p' |
    sort -u)
if (( ${#dlls[@]} < 2 )); then
    echo 'Dependency discovery returned too few DLLs; package is not reliable' >&2
    ldd bin/CipherGBA.exe >&2
    exit 1
fi
for dll in "${dlls[@]}"; do
    [[ -f "$dll" ]] || { echo "Missing DLL $dll" >&2; exit 1; }
    cp -n "$dll" "$dest/"
done
echo "Windows redistributable dependency DLLs:"
find "$dest" -maxdepth 1 -name '*.dll' -printf '%f\n' | sort
(cd "$dest" && sha256sum *.exe *.dll > SHA256SUMS.txt)
# Run out of the staged folder with UCRT64/bin removed from PATH as an
# inexpensive missing-DLL smoke check (Windows system DLLs remain visible).
(cd "$dest" && PATH='/usr/bin:/c/Windows/System32:/c/Windows' ./CipherGBA.exe --version)
echo "Staged: $dest"
