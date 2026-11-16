# Development and optimization priorities

CipherGBA v0.1 aims for native GBA timing (approximately 59.73 emulated frames per second) while preserving deterministic CPU/PPU/APU behavior, accurate battery saves and platform portability. **Sustained full-speed gameplay on every target machine has not been established.**

| Area | Implementation | Verification still needed |
| --- | --- | --- |
| CPU | ARM7TDMI interpreter, timing/bus prefetch optimizations | Broad hardware-referenced instruction-phase tests |
| PPU | Scanline rendering and layered BG/OBJ composition fast paths | Mid-scanline writes and complete video regressions |
| Audio | GBA APU generation with host audio output | Stable latency/pacing and playback across devices |
| Event scheduling | Timers, DMA, IRQ coordination and fast paths | Additional cycle-level edge cases and priority ordering |
| Display | Linux X11 shared-memory presentation and portable SDL2 | Fullscreen/windowed performance and compositor-specific paths |
| Compiler | Release/LTO, optional local CPU tuning | Representative real-game speed benchmarks on target hardware |
| Portability | Windows, macOS, X11, AppImage and Flatpak recipes | Clean-device interactive acceptance on every platform |

## Technical safeguards

- No game-specific timing hacks or test-ROM special cases.
- Do not improve reported test scores by changing assertions or reclassifying unresolved failures as passes.
- Revalidate deterministic state continuation and persistent game saves after core changes.
- Keep the emulator core separately buildable; avoid coupling core logic to one OS frontend.
- Build only the emulator target by default. Synthetic regression tools are optional CI targets.

For performance diagnostics and host tuning, read [Performance](PERFORMANCE.md). For the four unresolved HBlank checks and how to reproduce them, read [Accuracy](ACCURACY.md).
