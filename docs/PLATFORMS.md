# Platforms, CP93 interface, and frontend differences

CipherGBA maintains two desktop frontend implementations over its current C++17 emulation core, with the CP93 navigation/dashboard design being restored for the planned v0.2 release. No CP93 core sources replace the current core.

| Target | Frontend | Build/package | Remaining validation |
| --- | --- | --- | --- |
| Linux X11 / XWayland x86_64 | Native X11 | CMake, executable + shared core | Desktop game/audio and host performance across configurations |
| Linux portable x86_64 | SDL2 | AppImage | Clean Linux distribution and audio/controller compatibility |
| Linux Flatpak x86_64 | SDL2 | Freedesktop 25.08 runtime | File chooser portals, save persistence on various desktop environments |
| Windows x64 | SDL2 | ZIP containing EXE and required DLLs | Real-machine UI, gamepad, audio and runtime dependency verification |
| macOS arm64 | SDL2 | `.app` bundle in ZIP | Real-machine UI/audio/controller; notarization not provided |
| macOS x86_64 | SDL2 | `.app` bundle in ZIP | Real-machine UI/audio/controller; notarization not provided |

The X11 and SDL2 frontends now expose the CP93 six-tab navigation: **Library, Settings, System, TAS, Cheats, Info**. Each opens a real clickable menu. At startup, a two-column library dashboard offers Open ROM, Import BIOS, and Configure Input; the screen is not covered by an application logo. Native X11 uses Xlib for rendering, while Windows/macOS/portable Linux use an SDL2 implementation with platform file dialogs. Both expose in-window input and cheat management, although some editing workflows and advanced controls differ. SDL2's F1 fallback opens its legacy SDL dialog menu. Fullscreen gameplay hides the desktop chrome until the pointer reaches the top, and artwork appears only in icons and About views. Linux X11 links the shared core; SDL2 links the static core built from the same current sources. See [the restoration notes](FRONTEND_CP93.md) and [cross-platform CI](../.github/workflows/portability.yml).

## Data and migration

The Linux native frontend stores data under its application `CipherGBA_Data` directory; the SDL2 frontend uses platform-specific per-user locations. The SDL2 `--import-data DIR` option copies supported existing data **without overwriting** destination saves. Files in the supplied source directory are unchanged. Do not import platform-specific keyboard mappings. Make backups before transferring states, and note that binary save-states may not transfer between host ABIs.

## Distribution and security

The Windows ZIP must include its DLL dependencies; macOS apps are ad-hoc signed but not notarized. The Linux AppImage is a self-contained distributable for supported x86_64 systems, not a guarantee of operation on all distributions. Flatpak depends on compatible runtimes and desktop portals. No Nintendo BIOS or game files are packaged. Check package file hashes against `SHA256SUMS.txt` when downloading from the release.

See [platform acceptance](PLATFORM_ACCEPTANCE.md) for a repeatable device-test procedure.
