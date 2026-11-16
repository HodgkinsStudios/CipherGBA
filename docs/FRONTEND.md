# Desktop frontend

CipherGBA provides two host frontends for the same C++17 GBA emulator core:

- **Native Linux (X11/XWayland):** Xlib-based desktop interface, separate shared core library.
- **SDL2 (Windows, macOS, Linux):** Portable desktop interface, statically linked core.

Both use the same classic desktop structure. Host-specific font rendering, input capture, file dialogs, and save paths may differ.

## Main window

- Default **1050 × 720** logical-pixel window (minimum **730 × 540**).
- Dark emulator viewport, with no splash screen replacing the game area.
- **38 px** top menu bar and **30 px** bottom status bar.
- Aspect-preserving integer scaling for the game display; optional stretched fullscreen.
- Application artwork appears in the application icon and **Help → Info**, not as an overlay on the emulator viewport.

## Menu structure

| Menu | Main actions |
| --- | --- |
| **File** | Load ROM, load BIOS, reset ROM, quit |
| **Input** | GBA Controller submenu |
| **Configure** | Controller Input, AVI recording indicators and output location, emulator speed (100%, 50%, 25%) |
| **System** | Fullscreen, stretched fullscreen, windowed, pause, mute, individual audio channels, save/load state, slot selection, screenshot, AVI recording |
| **TAS** | Record/stop input recording, view/stop input playback, reset before recording |
| **Cheats** | Configure GBA Cheat Codes, enable/disable cheats |
| **Help** | Info |

Fast-forward and rewind controls are not included.

## Controller configuration

The input-configuration screen supports all ten GBA buttons: **A, B, Start, Select, Up, Down, Left, Right, L, R**. It displays keyboard and gamepad assignments together.

The user can save changed mappings, restore default mappings, or cancel. Emulated gameplay stops advancing while the configuration page is open. SDL2 supports controller buttons and axis directions, with input stored in the host's per-user data folder. SDL2 and native X11 mapping file formats are host-specific.

## Cheats and input movies

The Cheats page presents a saved per-game list with Add, Remove, Save, and Return actions. Supported formats and syntax are described in [Cheat codes](CHEATS.md). TAS recording and playback can be managed through the TAS menu.

## Display and host behavior

Fullscreen hides the menu bar until it is needed, and a separate choice enables stretched output. Standard scaling preserves the Game Boy Advance's aspect ratio. In-window About and configuration views are available on both desktop frontends.

Native file dialogs, gamepad hotplug behavior, rendering fonts, and application-data paths remain platform-specific. See [Platforms](PLATFORMS.md) and the [manual acceptance checklist](PLATFORM_ACCEPTANCE.md). Compilation and headless smoke tests are not a substitute for interactive verification on each operating system.
