# CipherGBA v0.3 — cross-platform frontend acceptance checklist

**Reference:** native Linux X11 frontend of the user-supplied `CipherGBA-0.1.0.zip`. v0.3 is a frontend/release-packaging update; hardware core source is not modified.

## Automated verification

- [ ] Build native Linux X11, Linux SDL2, Windows x64, macOS Intel, macOS ARM, AppImage and Flatpak from the same commit.
- [ ] Run ROM-free SDL2 startup/render smoke test; main window opens without modal prompts or oversized branding.
- [ ] Verify the seven-menu definitions and nested submenu labels match the original v0.1 source.
- [ ] Run the synthetic ROM/persistence/AVI and other configured CI checks; verify release-package SHA-256 manifests.
- [ ] Confirm the Git diff contains zero modified files under `CipherGBA_Core/`.
- [ ] Confirm no fast-forward, rewind or CP93 dashboard UI controls remain.

## Manual desktop acceptance on each OS

- [ ] Launch from the final package; verify a **blank dark viewport** and compact `File / Input / Configure / System / TAS / Cheats / Help` bar, with a bottom status strip showing no ROM and the state slot.
- [ ] Open every menu and submenu (GBA Controller; Emulator Speed; APU; Save State Slot), verify the original item order, separators, hover and checked indicators.
- [ ] Click Input / Configure → Controller Input; remap, save, reset defaults and cancel; restart and confirm mappings persist.
- [ ] In Cheats, add, toggle, remove and save a supported code; verify the page stays in the main window.
- [ ] Open Help → Info; logo appears **only in About**, and Return returns to the original blank viewport or running game.
- [ ] Load a legally obtained `.gba` and `.zip` using native file pickers; test optional original BIOS and HLE.
- [ ] Play a game and test native aspect scaling, fullscreen, fullscreen stretched, frame pacing, audio channels, pause, mute, and restore to windowed.
- [ ] Save and load state slots, battery saves, screenshots, TAS recording/playback and AVI output folder and capture.
- [ ] Connect/disconnect/reconnect a keyboard and gamepad; test ten mapped buttons and OS-specific input capture.
- [ ] Inspect macOS notarization warnings (the app is ad-hoc signed), Flatpak file portals, Windows portable DLLs and per-user save directories.
- [ ] Confirm no fast-forward/rewind actions or CP93 dashboard have returned.
- [ ] Verify performance on actual machines; do not infer universal 59.73 FPS from compilation tests.

CI is not a substitute for checked real-device results. The historical 6,998-case suite baseline is not retested by these frontend checks. Record the tested release commit SHA alongside any manual results.
