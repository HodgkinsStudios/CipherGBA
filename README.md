<p align="center"><img src="assets/branding/ciphergba_icon.png" alt="CipherGBA application logo" width="520"></p>
<h1 align="center">CipherGBA v0.3</h1>
<p align="center">A portable C++17 Game Boy Advance emulator by <strong>HodgkinsStudios</strong></p>

<p align="center"><a href="https://github.com/HodgkinsStudios/CipherGBA/releases/tag/v0.3.0">Download v0.3.0</a> · <a href="RELEASE_NOTES.md">Release notes</a> · <a href="docs/ACCURACY.md">Accuracy report</a> · <a href="https://github.com/HodgkinsStudios/CipherGBA/actions/workflows/portability.yml">Cross-platform builds</a></p>

CipherGBA emulates the Nintendo Game Boy Advance using a standalone C++17 core, integrated BIOS high-level emulation (HLE), and optional user-supplied original 16 KiB GBA BIOS firmware.

## The original Linux v0.1 frontend — on every desktop

**v0.3 restores the original Linux v0.1 user interface as the reference across all supported desktop operating systems.** The desktop has the seven top-level menus **File / Input / Configure / System / TAS / Cheats / Help**, the slim 38-pixel menu bar, dark emulator viewport, and the compact 30-pixel bottom status bar. There is **no CP93 launcher/dashboard** and **no giant logo on the main window**.

The menus provide ROM/BIOS selection, reset and exit; keyboard/gamepad input remapping; AVI recording options and reduced playback speed; fullscreen/windowed, audio channels, save states and screenshots; input movie recording/playback; cheat code editing; and About. The input and cheat configuration pages open **inside the emulator window**, as in the original Linux build.

The native Linux X11 frontend uses the uploaded v0.1 layout and Xlib renderer. **Windows, macOS and portable Linux use a matching SDL2 implementation of the same UI**. SDL2 uses an embedded, mixed-case bitmap font in place of the X11 desktop font, and uses each OS's native ROM/BIOS file chooser. Some advanced controller mapping and host file-selection behavior is platform-specific; see [frontend parity](docs/FRONTEND_CLASSIC_V01.md) and [platform notes](docs/PLATFORMS.md).

Fast-forward and rewind controls are **not included**. The current emulator core has **not been replaced or modified** during the frontend restoration.

<p align="center"><img src="docs/images/main_window.png" alt="Original Linux v0.1 style: seven-menu CipherGBA emulator with a game loaded" width="700"></p>
<p align="center"><em>Original Linux v0.1-style interface with a test suite loaded. The v0.3 frontends retain this menu/status layout.</em></p>

## Main features

- GBA cartridges in `.gba` and supported `.zip` archives.
- Aspect-preserving integer scaling; fullscreen and optional fullscreen stretching; windowed operation.
- Keyboard and gamepad input, saved remapping of the ten GBA buttons, controller detection/reconnection.
- GBA sound channel controls, pause, reset and 100%/50%/25% playback settings.
- Persistent SRAM, Flash and EEPROM saves; four save-state slots; PNG screenshots.
- TAS input recording/playback; AVI video/audio capture.
- Supported RAW, CodeBreaker, GameShark and Action Replay cheat codes ([details](docs/CHEATS.md)).
- Standalone core compiled as a shared library on native Linux and as a static library in SDL2 builds.
- Consistent HodgkinsStudios branding in the README, desktop icon and About panel—not in place of the game viewport.

## v0.3 release downloads

All packages are generated and hash-checked from a single verified GitHub commit. **Automated compilation and headless checks are not equivalent to real-device GUI acceptance.** The older v0.1.0 and v0.2.0 releases remain in the GitHub release history.

| Platform | Package |
| --- | --- |
| Windows x64 | `CipherGBA-v0.3-Windows-x64.zip` |
| macOS Apple Silicon | `CipherGBA-v0.3-macOS-arm64.zip` |
| macOS Intel | `CipherGBA-v0.3-macOS-x86_64.zip` |
| Native Linux X11 / XWayland x86_64 | `CipherGBA-v0.3-Linux-X11-x86_64.zip` |
| Portable Linux AppImage x86_64 | `CipherGBA-v0.3-Linux-x86_64.AppImage` |
| Portable Linux Flatpak x86_64 | `CipherGBA-v0.3-Linux-x86_64.flatpak` |
| All-platform bundle | `CipherGBA-v0.3-ALL-PLATFORMS.zip` |
| Source-only | `CipherGBA-v0.3-Source.zip` |

**Windows:** unzip fully and run `CipherGBA.exe` alongside the packaged DLLs. **macOS:** unzip and launch `CipherGBA.app`; bundles are ad-hoc signed and are **not notarized**. **Native Linux:** keep `CipherGBA` and `libCipherGBA_Core.so.1` together. **AppImage:** `chmod +x CipherGBA-v0.3-Linux-x86_64.AppImage` and run it. **Flatpak:** `flatpak install --user ./CipherGBA-v0.3-Linux-x86_64.flatpak`, then `flatpak run io.github.hodgkinsstudios.CipherGBA`. The Flatpak requires the freedesktop 25.08 runtime.

## Build from source (Ubuntu)

Build the original-style native Linux X11 frontend:

```sh
sudo apt update
sudo apt install -y build-essential cmake libx11-dev zlib1g-dev python3
./build.sh
./run.sh
```

Build the cross-platform SDL2 frontend on Linux:

```sh
sudo apt install -y libsdl2-dev libdbus-1-dev
cmake -S . -B build-sdl -DCMAKE_BUILD_TYPE=Release -DCIPHERGBA_FRONTEND=SDL2 -DPIXELGBA_ENABLE_LTO=OFF
cmake --build build-sdl --target CipherGBA -j 2
./bin/CipherGBA
```

The Ubuntu Code::Blocks project file is included. To build the core library without a GUI, configure with `-DPIXELGBA_BUILD_GUI=OFF`. The [CI workflow](.github/workflows/portability.yml) contains the Windows/macOS/Linux packaging recipes.

### Keyboard shortcuts (SDL2)

| Action | Key |
| --- | --- |
| Menu / Open ROM / Import BIOS | F1 / Ctrl+O / Ctrl+B |
| Controller configuration | Ctrl+I |
| Reset ROM / Mute | F2 / F4 |
| Save state / Load state | F5 / F8 |
| State slot / Pause | F6 / F7 |
| Screenshot | F9 |
| Stretch scaling | F10 |
| Fullscreen toggle | F11 or Escape |
| TAS menu / AVI toggle | F3 / F12 |

Default GBA controls: Z=A, X=B, Backspace=Select, Enter=Start, arrow keys=D-pad, A=L and S=R. SDL2 stores input settings and save data in the operating system's per-user application data folder. The optional `--import-data DIR` command copies supported data from an old Linux `CipherGBA_Data` folder **without overwriting** destination saves.

## Accuracy and limitations

The **previously recorded** suite baseline is **6,994/6,998 cases (99.9428%)** in both original BIOS and integrated HLE modes. Four HBlank-related assertions (`Hblank`, `Flip 1`, `Flip 2`, `Flip 3`) remain unresolved. This frontend-focused release does **not** claim a fresh 6,998-case retest or 100% hardware accuracy. See [the accuracy record](docs/ACCURACY.md).

CI checks source compilation, package contents, and synthetic startup/persistence smoke tests; clean-device UI, gameplay audio, native file dialogs, controller operation and sustained full speed on every machine remain [manual acceptance items](docs/PLATFORM_ACCEPTANCE.md).

## Legal information

No Nintendo game ROMs or BIOS firmware are included. Use your own lawfully obtained files. CipherGBA is an independent emulator and is not endorsed by Nintendo. No project-wide reuse license has been selected; the cheat lookup data includes [file-specific MPL-2.0 notices](THIRD_PARTY_NOTICES.md).
