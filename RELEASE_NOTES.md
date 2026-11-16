# CipherGBA v0.2.0 — CP93 desktop frontend restoration

**HodgkinsStudios · Release `v0.2.0`**

CipherGBA v0.2 restores the finished CP93-style desktop presentation for the existing, newer C++17 GBA emulator core. All emulator hardware code in `CipherGBA_Core` remains unchanged from the preceding main branch.

## Desktop experience

- **Library / Settings / System / TAS / Cheats / Info** navigation across the native Linux X11 and portable SDL2 implementations.
- Two-column GBA Library startup dashboard with functioning Open ROM, Import BIOS, and Configure Input actions. The project logo remains in the README, desktop icon and About dialog rather than replacing the emulator window.
- Dedicated keyboard/gamepad binding screens for the **ten standard GBA buttons**, with Save, Reset Defaults and Return. SDL2 can read the previous V1/V2 input-map formats without using obsolete auxiliary bindings.
- Dedicated cheat management pages and accessible TAS, audio channel, save-state, screenshots, AVI capture and fullscreen controls; native dialog implementations vary by operating system.
- Normal and reduced-speed settings only. No accelerated-play or rewind controls are presented.
- Updated app metadata, About versions, platform documentation and headless no-ROM UI regression test.

## Supported packages

Windows x64, macOS arm64, macOS Intel x86_64, Linux native X11 x86_64, Linux AppImage and Flatpak x86_64, plus full source and combined all-platform ZIP. Release assets are assembled exclusively from a successful, exact-commit cross-platform CI run with SHA-256 checks.

## Accuracy and scope

The previously recorded 6,994/6,998 suite baseline in each BIOS mode remains the documented reference, not a new retest or guarantee. The four HBlank-related differences remain unresolved. This is a **frontend and packaging release**, not an emulator-core accuracy change.

CI compilation, packaging and synthetic smoke checks are not proof of interactive GUI, audio and controller behavior on every real device. See [platform acceptance](docs/PLATFORM_ACCEPTANCE.md) and [restoration notes](docs/FRONTEND_CP93.md). No Nintendo BIOS or game ROMs are included.

## Upgrade

Version 0.1.0 remains available as a historical release. Existing cartridge saves are not overwritten by the optional SDL2 data importer. See [README](README.md) and [platform documentation](docs/PLATFORMS.md) for installation, data locations and build instructions.
