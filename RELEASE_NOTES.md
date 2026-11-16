# CipherGBA v0.3.0 — Release notes

**HodgkinsStudios · [Official release](https://github.com/HodgkinsStudios/CipherGBA/releases/tag/v0.3.0)**

## Desktop interface

- Consistent **File / Input / Configure / System / TAS / Cheats / Help** menus on native Linux X11 and portable SDL2 builds.
- Dark emulator viewport, compact menu/status bars, and in-window controller configuration, cheat management, and About screen.
- Keyboard and gamepad bindings, including SDL2 controller button/axis remapping.
- Windowed, fullscreen, stretched display, audio controls, game saves/states, screenshots, TAS input movies, and AVI capture.
- No fast-forward or rewind controls. Application artwork is reserved for icons, documentation, and About.

## Packages and verification

Distributions are available for **Windows x64, macOS Intel/Apple Silicon, Linux X11, Linux AppImage, and Linux Flatpak**, plus complete source and an all-platform archive. Each package has an entry in the SHA-256 manifest.

Automated build, packaging, and selected smoke checks passed before publication. Real-device GUI, audio, controller, and performance acceptance remain separate manual verification tasks.

## Emulator accuracy

The existing emulator core was not changed for this interface release. The previously recorded BIOS/HLE suite result remains **6,994/6,998 scored checks in each BIOS mode** with four unresolved HBlank cases. It is not a universal hardware-accuracy guarantee.

CipherGBA does not include Nintendo ROMs or BIOS firmware. macOS binaries are ad-hoc signed and not notarized.

For current installation and build instructions, read the [README](README.md). For prior release changes, see [Changelog](CHANGELOG.md), [Frontend](docs/FRONTEND.md), and [Platform acceptance](docs/PLATFORM_ACCEPTANCE.md).
