# CipherGBA v0.3 desktop platforms and original UI parity

The same current `CipherGBA_Core` implementation powers two desktop renderers. The **Linux X11 frontend from v0.1.0** is the sole UI layout reference for v0.3; the CP93 dashboard design is **not** used.

| Target | Renderer | Distributed as | Validation still required |
| --- | --- | --- | --- |
| Ubuntu Linux X11/XWayland x86_64 | Original Xlib layout | Native executable + shared core | Full game/audio/controller desktop sessions |
| Linux x86_64 portable | Matched SDL2 layout | AppImage | Host desktop and file chooser compatibility |
| Linux x86_64 Flatpak | Matched SDL2 layout | Flatpak | Sandboxed file portals and persistent data |
| Windows x64 | Matched SDL2 layout | ZIP with DLL dependencies | Windows keyboard/gamepad/audio and DPI behavior |
| macOS Apple Silicon | Matched SDL2 layout | Ad-hoc-signed app in ZIP | Real macOS behavior and Gatekeeper |
| macOS Intel x86_64 | Matched SDL2 layout | Ad-hoc-signed app in ZIP | Real macOS behavior and Gatekeeper |

## Original interface common to all

- Default 1050×720 window; menu bar 38 pixels tall and status strip 30 pixels tall, scaled for high-DPI displays.
- **File | Input | Configure | System | TAS | Cheats | Help**, in that exact order. Hover/open dropdown and nested selections.
- No-ROM state is a dark/empty viewport with the status bar; no launcher, dashboard, or logo replacing the viewport.
- Controller mapping (ten GBA buttons), cheat management and About displayed within the primary window.
- Fullscreen hides the top menu until the pointer returns to it. Stretched fullscreen remains an explicit selection.
- Original audio/ROM/state/cheat/TAS/recording actions exposed via the corresponding menus. No fast-forward or rewind controls.

**Renderer differences:** X11 uses native desktop Xlib font rendering and Linux joystick axes; SDL2 uses embedded mixed-case bitmap glyphs, SDL gamepad buttons and portable input binding files. These are implementation differences, not intentional changes to the original screen layout. Native ROM and BIOS file pickers are host-specific. SDL2 also provides an in-window directory-path editor for changing AVI output location because directory selection APIs are not universal across all targets.

Native Linux X11 stores its original `CipherGBA_Data` files. SDL2 uses host-specific per-user application-data directories. The optional `--import-data DIR` is non-destructive; it does not import incompatible X11 keyboard mapping files.

The [full interface specification](FRONTEND_CLASSIC_V01.md), [acceptance checklist](PLATFORM_ACCEPTANCE.md), and [build workflow](../.github/workflows/portability.yml) distinguish source-level UI parity from device-specific interaction tests.
