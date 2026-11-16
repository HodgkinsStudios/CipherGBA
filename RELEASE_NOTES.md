# CipherGBA v0.3.0 — original Linux v0.1 frontend across all platforms

**HodgkinsStudios · GitHub release `v0.3.0`**

## Corrected frontend

The v0.3 release restores the **Linux UI from the actual CipherGBA v0.1.0 source archive**, rather than the different CP93 dashboard introduced in v0.2. The v0.1 interface is now the design reference on native Linux X11, Windows x64, macOS arm64/x86_64, and SDL2 Linux packages.

- Original seven-item **File / Input / Configure / System / TAS / Cheats / Help** menubar, 38px title/menu chrome, compact 30px status strip, and blank dark viewport before loading a game.
- Matching dropdown text, nested GBA Controller, Emulator Speed, audio APU and save-state slot menus, separators and check marks.
- In-window controller input mapping, cheat code editing and branded About panel across X11 and SDL2.
- Preserved windowed, fullscreen, stretched display, ROM loading, BIOS import, screenshots, saves, audio controls, TAS and AVI features.
- Fast-forward and rewind controls are intentionally **not** exposed.
- Application logo remains in platform icons, README and About—not in the emulator's main screen.

The newer Game Boy Advance emulator core in `CipherGBA_Core/` is **unchanged**. The old v0.1 source is used as a frontend design reference only.

## Downloads

Windows x64, macOS Intel/Apple Silicon, Linux native X11, Linux AppImage and Flatpak, full C++17 source, combined all-platform archive and SHA-256 manifest. Every binary is built and packaged from the same validated GitHub `main` commit.

## Tests and known limitations

Automated platform CI compiles the packages and runs headless ROM/startup, persistence, AV and other selected smoke tests. Real-device visual verification, keyboard/gamepad sessions, audio consistency and file-picker behavior across every OS still require manual acceptance.

The historical suite accuracy baseline remains **6,994/6,998 in each BIOS mode**; four HBlank assertions are unresolved. v0.3 makes no new accuracy or full-speed claims.

No Nintendo ROMs or BIOS files are shipped. macOS builds are ad-hoc signed, not notarized. See [README](README.md), [frontend reference](docs/FRONTEND_CLASSIC_V01.md) and [platform checklist](docs/PLATFORM_ACCEPTANCE.md).
