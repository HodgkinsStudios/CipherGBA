# Cross-platform acceptance checklist

This checklist separates **automated verification** from **manual desktop validation**. Mark an item complete only when there is recorded evidence for the specific binary and operating system.

## Automated build and package checks

The GitHub Actions workflow at [portability.yml](../.github/workflows/portability.yml) is the authoritative record of individual build/test results.

- [ ] Native Linux X11 and Linux SDL2 compile and complete their configured tests.
- [ ] Windows x64, macOS Intel, and macOS Apple Silicon packages compile and assemble.
- [ ] Linux AppImage and Flatpak packages assemble successfully.
- [ ] Headless SDL2 no-ROM window smoke tests complete.
- [ ] Synthetic persistence, CPU/timing, and related configured CI tests complete.
- [ ] Full release archive, individual packages, and SHA-256 manifest agree.

**The published v0.3.0 release passed its automated platform matrix and release assembly.** The checklist above is reusable for future exact-commit runs; do not infer that every entry has been rerun following documentation-only changes.

## Manual tests on each operating system

- [ ] Open the installed application and verify the dark no-ROM viewport, seven-menu bar and compact status line.
- [ ] Open all menus and submenus; verify separators, hover behavior and checked options.
- [ ] Use controller configuration to remap a keyboard key and a gamepad button or axis, save it, restart, reset defaults, and cancel without losing the previous mapping.
- [ ] Open Help → Info; confirm the logo appears there and the Return action restores the emulator window.
- [ ] Load user-owned `.gba` files and supported `.zip` archives using the host file picker; repeat with optional original BIOS.
- [ ] Play a game in windowed, fullscreen, and stretched modes; verify aspect ratio, pacing, APU channels, and sound.
- [ ] Verify keyboard/gamepad hotplug and reconnection, all standard buttons, and saved mappings.
- [ ] Exercise cartridge saves, save-state slots, screenshots, cheats, TAS input movies, and AVI capture.
- [ ] Confirm user data persists in the expected application-data directory.
- [ ] Verify Windows runtime DLL loading, macOS Gatekeeper behavior, and Flatpak file access/portals.
- [ ] Confirm there are no fast-forward/rewind controls.
- [ ] Measure representative real-game performance. Do not infer sustained full speed from a successful compile or startup smoke test.

See [Accuracy](ACCURACY.md) for the distinct 6,998-case numerical suite. That baseline must not be represented as a new full-suite run when only frontend or documentation changes have occurred.
