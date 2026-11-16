# CipherGBA v0.3 — original Linux v0.1 frontend reference

**Authoritative design source:** `CipherGBA-0.1.0.zip`, specifically `CipherGBA_Frontend/src/AppX11.cpp` from the native Linux implementation. This source was directly compiled and its 1050×720 no-ROM startup screen inspected.

## Visual contract

| Element | Original Linux v0.1 appearance | Portable v0.3 |
| --- | --- | --- |
| Window | 1050×720, minimum 730×540 | Same logical dimensions |
| Top bar | Dark navy, height 38 | Same 38 logical pixels |
| Menus | **File, Input, Configure, System, TAS, Cheats, Help** | Same seven titles and order |
| Background with no ROM | Empty deep-black/navy viewport | Same empty viewport; **no CP93 dashboard** |
| Bottom bar | 30px dark status line, `NO ROM  SLOT 1  100%` | Same layout and state fields |
| Menu dropdowns | 32px rows, narrow separators, hover highlights | Same row spacing, check indicators, submenus |
| Controller mapping | Ten rows of keyboard + gamepad assignments | In-window ten-row mapping, Save/Defaults/Cancel |
| Cheat codes | In-window list, Add/Remove/Save/Return | In-window editor; compile RAW/CB/GS/AR codes |
| About | Embedded logo, system information, Return | In-window embedded logo, Return |
| Gameplay | Aspect-preserving integer scaling, stretched fullscreen option | Matching scaling intent |

The original X11 native font uses the desktop's Xlib fonts. The SDL2 renderer draws a compact embedded 5×7 mixed-case bitmap font to avoid external fonts and to run without SDL_ttf on Windows/macOS. The icon and About artwork use the existing canonical source and are never used as a main-window splash.

## Menu definitions

- **File:** Load ROM, Load BIOS, Reset ROM, separator, Quit
- **Input:** GBA Controller → Controller Input
- **Configure:** Controller Input, Record With Toast, Record With Timer, Custom AVI Output Location, Emulator Speed → 100/50/25%
- **System:** Full Screen, Full Screen (Stretched), Windowed, Pause/Resume, separator, Mute, APU → six channels, Save State, Load State, Save State Slot → 1–4, Screenshot, separator, Record AVI, Stop Recording
- **TAS:** Record Inputs, Stop Recording Inputs, View Recorded Inputs, Stop Viewing Inputs, Reset Before Input Recording
- **Cheats:** Configure GBA Cheat Codes, Enable Cheats
- **Help:** Info

Per instruction, **no fast-forward or rewind controls** are included, although other original frontend functionality is preserved.

## Hardware and release boundaries

The emulator's newer `CipherGBA_Core/` source is authoritative and was **not** replaced by the v0.1 core. The restoration affects only presentation, host-side input/UI, documentation and packaging. The v0.1 and v0.2 GitHub releases remain historical; v0.3 uses newly compiled exact-main-commit assets.

See [README](../README.md) and [platform acceptance](PLATFORM_ACCEPTANCE.md). Cross-platform CI proves successful compilation and targeted tests, not byte-identical font rendering or real-device controller operation on every OS.
