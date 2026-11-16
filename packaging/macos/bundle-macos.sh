#!/usr/bin/env bash
# Bounded, unattended macOS dylib bundling for development CI.
# No shell prompts, no dylibbundler, no Homebrew dependency at runtime.
set -euo pipefail
if [[ $# != 1 ]]; then echo "Usage: $0 /path/to/CipherGBA.app" >&2; exit 2; fi
app="$1"
exe="$app/Contents/MacOS/CipherGBA"
libs="$app/Contents/Frameworks"
[[ -x "$exe" ]] || { echo "Missing bundle executable: $exe" >&2;exit 2; }
mkdir -p "$libs"
# Homebrew alias directories cover both native Apple Silicon and x86_64.
dirs=(/opt/homebrew/lib /usr/local/lib)
for formula in sdl2 sdl3 zlib; do
  prefix="$(brew --prefix "$formula" 2>/dev/null || true)"
  [[ -z "$prefix" ]] || dirs+=("$prefix/lib")
done
echo "Bundle executable dependencies:"
otool -L "$exe"
queue=("$exe")
index=0
while ((index < ${#queue[@]})); do
  binary="${queue[$index]}"
  index=$((index + 1))
  if (( index > 48 )); then
    echo "Dependency traversal exceeded bound: refusing bad bundle" >&2
    exit 1
  fi
  echo "Inspecting: $binary"
  # Supports ordinary Homebrew .dylib links. Apple system frameworks are
  # platform dependencies and must not be copied into third-party bundles.
  while IFS= read -r dependency; do
    [[ -z "$dependency" ]] && continue
    case "$dependency" in
      /System/Library/*|/usr/lib/*) continue ;;
    esac
    name="${dependency##*/}"
    [[ "$name" == "$(basename "$binary")" ]] && continue
    if [[ "$name" != *.dylib ]]; then
      echo "Non-system framework or unexpected dependency: $dependency" >&2
      exit 1
    fi
    output="$libs/$name"
    if [[ ! -f "$output" ]]; then
      source=""
      [[ -f "$dependency" ]] && source="$dependency"
      if [[ -z "$source" ]]; then
        for dir in "${dirs[@]}"; do
          if [[ -f "$dir/$name" ]]; then source="$dir/$name"; break; fi
        done
      fi
      if [[ -z "$source" ]]; then
        echo "Cannot resolve $dependency for $binary" >&2
        exit 1
      fi
      cp -L "$source" "$output"
      chmod u+w "$output"
      install_name_tool -id "@rpath/$name" "$output"
      queue+=("$output")
      echo "Added: $name"
    fi
    install_name_tool -change "$dependency" "@rpath/$name" "$binary"
  done < <(otool -L "$binary" | tail -n +2 | awk '{print $1}')
  if [[ "$binary" == "$exe" ]]; then
    install_name_tool -add_rpath "@executable_path/../Frameworks" "$binary" || true
  else
    install_name_tool -add_rpath "@loader_path" "$binary" || true
  fi
done
# Ad-hoc sign the modified Mach-O files for local testing; release signing
# and notarization are not performed in CI.
codesign --force --deep --sign - "$app"
# Directly launching the signed .app executable in headless macOS CI can
# block in host launch-services/dyld before main(). The pre-bundle CLI smoke
# already ran; validate package structure here, and do real GUI launch checks
# on a desktop host before a release.
codesign --verify --deep --strict "$app"
echo "Bundled libraries:"
find "$libs" -maxdepth 1 -name '*.dylib' -print
echo "Direct executable dependencies:"
otool -L "$exe"
if otool -L "$exe" | grep -E '/opt/homebrew/|/usr/local/Cellar/'; then
  echo "Homebrew dependency still linked by executable" >&2
  exit 1
fi
