# Changelog

A record of significant CipherGBA releases and maintenance updates. The [README](README.md) describes the current project; this document retains version history.

## Unreleased — documentation maintenance

- Refreshed and reorganized the main README, preserving the HodgkinsStudios logo at the top.
- Moved historical release and frontend-transition explanations out of the current README.
- Updated platform, frontend, verification, and publishing documentation for clarity.
- Documentation-only change: **no new version, release tag, binaries, or emulator code changes**.

## [v0.3.0](https://github.com/HodgkinsStudios/CipherGBA/releases/tag/v0.3.0) — 2026-11-16

**Classic desktop interface unified across desktop targets**

- Restored the native Linux X11 desktop presentation and its seven top-level menus: File, Input, Configure, System, TAS, Cheats, Help.
- Provided a matching SDL2 interface for Windows, macOS (Intel and Apple Silicon), and portable Linux.
- Reestablished compact menu and status bars, a dark no-ROM viewport, and in-window controller, cheat, and About screens.
- Added SDL2 gamepad button/axis remapping, retaining compatibility with older input-map files.
- Left the GBA emulation core unchanged. Fast-forward and rewind controls remained absent.
- Updated multiplatform packages, release checks, documentation, and branding placement.
- Cross-platform CI and release-asset verification passed; real-device acceptance on every platform was not claimed.

## [v0.2.0](https://github.com/HodgkinsStudios/CipherGBA/releases/tag/v0.2.0) — 2026-11-16

**Desktop interface expansion**

- Introduced a CP93-inspired Library / Settings / System / TAS / Cheats / Info interface and ROM-library dashboard across X11 and SDL2.
- Added dedicated input remapping, cheat-management views, and expanded desktop tools.
- Preserved the existing GBA core and maintained normal/reduced-speed playback without fast-forward or rewind controls.
- Updated platform release packages and CI checks.
- This interface was superseded by the classic desktop layout in v0.3.0.

## [v0.1.0](https://github.com/HodgkinsStudios/CipherGBA/releases/tag/v0.1.0) — 2026-11-16

**Initial public cross-platform release**

- Published the C++17 Game Boy Advance emulator with its separately buildable core.
- Added the native Linux X11 frontend and SDL2 builds for Windows, macOS, and Linux.
- Included integrated BIOS HLE and optional original BIOS loading, GBA ROM/ZIP loading, saves and save states, screenshots, TAS, AVI recording, and supported cheat codes.
- Established unified application-logo branding and cross-platform CI artifact packaging.
- Documented the original **6,994/6,998** scored test-suite baseline in each BIOS mode and four unresolved HBlank cases.

Historical implementation details remain in [Frontend design provenance](docs/FRONTEND_CLASSIC_V01.md) and [Local verification record](docs/VALIDATION.md).
