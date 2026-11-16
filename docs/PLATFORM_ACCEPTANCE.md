# CipherGBA — v0.2 frontend platform validation checklist

This document separates automated project checks from **manual testing still required**. The workflow only becomes evidence for a particular release commit after the corresponding GitHub Actions run completes successfully on `main`.

## Automated checks in the cross-platform workflow

- Configure and compile native Linux X11 and portable SDL2 on Linux, Windows and both macOS architectures.
- Validate consistent branding assets; inspect native packaged icons and macOS `.app` bundle/signature.
- Run headless CLI/synthetic ROM boot tests where supported, including ZIP cartridge loading.
- Run synthetic battery/save-state persistence, input/data import, CPU I/O timing and AVI tests where configured.
- Build/check Linux AppImage and Flatpak packages; assemble release ZIPs; validate source archive and per-file SHA-256 checksums.

The 6,998-case private GBA test suite is **not** included in GitHub Actions and requires locally supplied test media. Automated package checks do not establish interactive desktop functionality.

## Manual release acceptance on each target OS

- [ ] Run the distributed application on a clean machine without a build toolchain; verify application icon and About branding.
- [ ] Verify the CP93-style Library / Settings / System / TAS / Cheats / Info menus, two-column no-ROM dashboard, clickable Open ROM/BIOS/Input buttons, and no full-screen logo replacement.
- [ ] Verify all eleven in-window keyboard/gamepad input mappings, including Fast Forward; save, restart, and confirm persistence. Confirm returning without saving restores the prior configuration.
- [ ] Verify cheat management; close and reopen the screen without losing emulator state.
- [ ] Confirm that fullscreen hides desktop chrome and that moving the pointer to the top reveals the menu.
- [ ] Load a legally obtained `.gba` and a supported `.zip`, with both HLE and (if available) original BIOS.
- [ ] Play long enough to check video scaling, audio stability, controller connection/reconnection and mapping persistence.
- [ ] Toggle fullscreen/windowed, pause/mute/fast-forward; check keyboard and native file dialogs.
- [ ] Verify SRAM/Flash/EEPROM saves, save/load states, screenshots, cheats, TAS and AVI capture where exposed.
- [ ] Verify the SDL2 data importer is non-destructive with a backup test folder.
- [ ] On Flatpak, access game files through desktop file portals and verify sandboxed data after restart.
- [ ] On macOS, record Gatekeeper behavior and acknowledge that the app is not notarized.
- [ ] Compare performance on representative hardware against the 59.73 FPS target; retain measurements rather than asserting universal full speed.
- [ ] Run a full independently sourced GBA suite in both firmware modes and investigate any deviation from [the accuracy record](ACCURACY.md).

CI artifacts and completed manual results should be identified by commit SHA before publication. Unchecked items above are **limitations**, not completed passes.
