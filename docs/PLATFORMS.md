# Supported desktop platforms

CipherGBA runs on Windows, macOS, and Linux using its C++17 core and two desktop frontends.

| Platform | Frontend | Distribution | Additional notes |
| --- | --- | --- | --- |
| Windows x64 | SDL2 | ZIP with required DLLs | Keep bundled files together |
| macOS Apple Silicon | SDL2 | App ZIP | Ad-hoc signed; not notarized |
| macOS Intel x86_64 | SDL2 | App ZIP | Ad-hoc signed; not notarized |
| Linux X11/XWayland x86_64 | Native Xlib | ZIP with shared core library | X11-capable desktop required |
| Linux x86_64 | SDL2 | AppImage | Mark file executable before running |
| Linux x86_64 | SDL2 | Flatpak | Requires freedesktop 25.08 runtime |

Every desktop frontend presents the same seven-menu layout, game viewport, and compact status bar. The native X11 frontend uses its desktop font and host input APIs; SDL2 uses an embedded bitmap font, SDL controller button/axis mappings, and OS-specific file dialogs.

## Files and local settings

The native Linux frontend uses its own `CipherGBA_Data` data directory. Portable SDL2 builds use per-user application-data locations appropriate to their host operating system. Keyboard/gamepad configuration files are frontend-specific and should not be interchanged blindly.

The optional SDL2 command-line argument `--import-data DIR` copies compatible existing BIOS, save, state, TAS, and cheat files from an existing data folder **without overwriting** destination files. It does not import incompatible X11 keyboard mappings.

## Building

On Linux, CMake defaults to the native X11 frontend. Pass `-DCIPHERGBA_FRONTEND=SDL2` to select SDL2, or `-DPIXELGBA_BUILD_GUI=OFF` to build the core without a UI. The project includes packaging workflows for the other supported operating systems.

See the [main README](../README.md) for installation instructions and exact package links, [frontend documentation](FRONTEND.md) for menu behavior, and [platform acceptance](PLATFORM_ACCEPTANCE.md) for real-device testing.

Automated cross-platform CI verifies compilation, selected smoke tests, and release package assembly, but it does not certify graphics, sound, controller operation, or full-speed gameplay on all physical systems.
