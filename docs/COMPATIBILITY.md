# Compatibility and real-game testing

Game Boy Advance software uses a wide range of CPU/PPU/APU behaviors, cartridge save types and peripheral features. **No comprehensive per-title compatibility certification is currently included in the v0.3 source release.** A successful ROM load or a short headless boot does not establish full playability.

The portable SDL2 frontend supports a `--smoke-frames N` argument to execute a fixed number of emulated frames using a supplied ROM. For an initial local test with an owned game:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./bin/CipherGBA --rom /path/to/your/game.gba --smoke-frames 120
```

For Windows or macOS, invoke the same CLI options on a locally compiled binary, with headless SDL video/audio drivers where supported. Repeat using an independently obtained original BIOS via `--bios /path/to/your/gba_bios.bin` if desired. This is a smoke test only; a thorough compatibility report should also record in-game progress, graphical correctness, audio, saves, input, and frame pacing on actual devices.

Private cartridges, user saves and firmware should never be committed to this repository or packaged in releases. See [Accuracy](ACCURACY.md) for the separate numerical hardware test suite and [Platform validation](PLATFORM_ACCEPTANCE.md) for manual acceptance.
