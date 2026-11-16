# CipherGBA branding — canonical artwork

The single official CipherGBA logo is [ciphergba_icon.png](ciphergba_icon.png),
created November 15, 2026 for HodgkinsStudios. The README displays this same PNG.

The portable and native frontends use it consistently:

- Linux X11/About: compressed pixels generated at CMake configuration with
  `packaging/branding/generate_embedded_artwork.py` (384px About, 64px/32px WM icons).
- SDL2 Linux/Windows/macOS: embedded pixels for the dedicated About window
  and the SDL application window icon. The no-ROM main window has text and
  usable menus, never a logo splash.
- Windows EXE resource: 256px native ICO generated from the same source PNG.
- macOS app bundle: 256px native ICNS generated from the same source PNG.
- Linux desktop/Flatpak/AppImage: installed PNG from the same source; Flatpak
  produces the required exact 256×256 resource during CMake configuration.

The generator is stdlib-only Python and runs automatically during CMake
configuration on every platform. Generated headers and native icon files are
build products, **not** retained as separate copies in Git.

CipherGBA is an independent emulator and is not endorsed by Nintendo.
