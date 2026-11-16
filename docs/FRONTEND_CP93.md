# CipherGBA — CP93 desktop frontend restoration

## Purpose

Restore the desktop interface from the provided `CipherGBA_CP93_2026-11-01_HLE_WAIT_PIPELINE_6993_FULL_SOURCE.zip` as the **visual and navigational reference** for the upcoming v0.2 release.

The emulation core on GitHub `main` is authoritative. The older archive is **not** a source of core patches or BIOS changes. This pass changes desktop frontend code, desktop branding/font support, checks, and documentation only.

## Baseline comparison

| UI area | CP93 reference | New frontend implementation |
| --- | --- | --- |
| Navigation | Library / Settings / System / TAS / Cheats / Info | Six sections on X11 and SDL2 |
| Startup | Two-column library dashboard | Two-column dashboard with functioning Open ROM, Import BIOS, and Input buttons |
| Application artwork | Studio identity, not a full-screen logo placeholder | Program icon, README header, and About; never replaces main display |
| Input remapping | Dedicated keyboard/gamepad page, Save, Reset Defaults, Return | Embedded remapping page in SDL2 and X11 |
| Cheats | Dedicated cheat lab | Dedicated management page; platform-specific code entry workflows remain different |
| Playback | Fullscreen modes, audio, states, capture, TAS | Available through corresponding live menus and backend actions |
| Existing GBA hardware | Historical CP93 implementation | **Unchanged** GitHub core |

The design language uses dark navy surfaces, cyan highlights, warm status accents, a larger header, and a persistent status strip. The SDL2 interface uses an embedded bitmap font because shipping native X11 fonts across Windows/macOS is not portable. These are **visual adaptations** of CP93, not a byte-identical port of its X11 renderer.

## Platform rendering

- **Linux native X11:** Xlib draws the CP93-style library dashboard and six-tab bar. Existing Linux controller, cheat, audio and state backends remain in place.
- **Linux portable (AppImage/Flatpak), Windows x64, macOS arm64/x86_64:** SDL2 draws corresponding CP93-style navigation, dashboard, and editor pages. Native file pickers remain platform dependent.
- **Fullscreen:** game content retains aspect-preserving/scaling behavior; toolbar becomes available again by moving the pointer to the top.
- **About:** branded artwork stays in the About view; the application icon uses the same canonical image.

## Validation boundaries

Cross-platform GitHub Actions check source compilation, packaging, and selected headless smoke flows. They do not prove that every menu and dialog has been visually compared side by side on actual Linux, Windows and macOS computers. Before release, use [the platform acceptance checklist](PLATFORM_ACCEPTANCE.md) to test startup, menu dropdowns, the three dashboard buttons, panel navigation, keyboard/gamepad remapping, cheats, fullscreen behavior, and an actual ROM session on each supported OS.

## Release policy

The published v0.1.0 binaries predate this restoration. **v0.2 must be built from a separately validated commit.** Do not reuse v0.1.0 assets or republish the earlier release. Automatic publication is disabled; publication requires a deliberate v0.2 release workflow after validation.
