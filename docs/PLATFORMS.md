# Platforms and frontend differences

CipherGBA v0.1 maintains two desktop frontends on top of the same C++17 emulation core.

| Target | Frontend | Build/package | Remaining validation |
| --- | --- | --- | --- |
| Linux X11 / XWayland x86_64 | Native X11 | CMake, executable + shared core | Desktop game/audio and host performance across configurations |
| Linux portable x86_64 | SDL2 | AppImage | Clean Linux distribution and audio/controller compatibility |
| Linux Flatpak x86_64 | SDL2 | Freedesktop 25.08 runtime | File chooser portals, save persistence on various desktop environments |
| Windows x64 | SDL2 | ZIP containing EXE and required DLLs | Real-machine UI, gamepad, audio and runtime dependency verification |
| macOS arm64 | SDL2 | `.app` bundle in ZIP | Real-machine UI/audio/controller; notarization not provided |
| macOS x86_64 | SDL2 | `.app` bundle in ZIP | Real-machine UI/audio/controller; notarization not provided |

The X11 interface preserves native Linux menus and detailed controls. SDL2 presents a visible, clickable **File / System / Config / Help** menu bar in its main window, with native file-picker integrations where supported. The menu bar hides automatically during fullscreen gameplay unless the pointer moves to the top; F1 also opens the legacy menu dialog. With no ROM loaded, the SDL2 window shows a text-only Open ROM prompt, never a large logo image. The artwork remains in the application icon and About view. Some presentation and configuration options differ. Both use the same shared emulation source; Linux X11 links a shared core, while SDL2 links the optional core static target. Build recipes and package verification can be reviewed in [portability.yml](../.github/workflows/portability.yml).

## Data and migration

The Linux native frontend stores data under its application `CipherGBA_Data` directory; the SDL2 frontend uses platform-specific per-user locations. The SDL2 `--import-data DIR` option copies supported existing data **without overwriting** destination saves. Files in the supplied source directory are unchanged. Do not import platform-specific keyboard mappings. Make backups before transferring states, and note that binary save-states may not transfer between host ABIs.

## Distribution and security

The Windows ZIP must include its DLL dependencies; macOS apps are ad-hoc signed but not notarized. The Linux AppImage is a self-contained distributable for supported x86_64 systems, not a guarantee of operation on all distributions. Flatpak depends on compatible runtimes and desktop portals. No Nintendo BIOS or game files are packaged. Check package file hashes against `SHA256SUMS.txt` when downloading from the release.

See [platform acceptance](PLATFORM_ACCEPTANCE.md) for a repeatable device-test procedure.
