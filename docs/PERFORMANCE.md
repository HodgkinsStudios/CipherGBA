# Performance and build configuration

The original Game Boy Advance refresh rate is approximately **59.7275 FPS** (16.74 ms per frame). CipherGBA targets that rate without changing GBA hardware clocks or deliberately dropping guest frames to inflate speed. Real-time gameplay performance depends on host CPU, audio device, graphics server and display mode.

## Build choices

`./build.sh` selects a Release build, LTO and local native CPU tuning. Because `-march=native` is host-specific, locally optimized binaries should not be redistributed as generic Linux builds. For portable builds:

```sh
CIPHERGBA_NATIVE_TUNING=OFF ./build.sh
# or explicitly:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCIPHERGBA_NATIVE_TUNING=OFF -DPIXELGBA_ENABLE_LTO=ON
cmake --build build --target CipherGBA -j 2
```

CI builds use a portable configuration. The SDL2 frontend links the core static library; the X11 frontend loads the separate shared library.

## Linux X11 instrumentation

The status bar reports FPS, `CORE ms` (emulated frame time), and `X11 ms` (host presentation work). To log timing:

```sh
CIPHERGBA_PERF_LOG=1 ./run.sh --rom /path/to/game.gba 2> ciphergba-perf.log
```

Optional presentation comparisons:

```sh
CIPHERGBA_DISABLE_XSHM_ASYNC=1 ./run.sh --rom /path/to/game.gba
CIPHERGBA_DISABLE_XSHM=1 ./run.sh --rom /path/to/game.gba
CIPHERGBA_LEGACY_PACING=1 ./run.sh --rom /path/to/game.gba
```

Compare identical ROMs, scenes, states, host setups and audio devices. Frame-time logs and synthetic benchmarks are diagnostic; they do **not** establish a guaranteed real-game FPS. Asynchronous compositing, host scheduling or an audio queue can be the bottleneck even if core processing time is low. [Development priorities](DEVELOPMENT.md) and [Platform checklist](PLATFORM_ACCEPTANCE.md) describe the remaining work.
