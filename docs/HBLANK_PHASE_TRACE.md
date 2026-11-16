# HBlank guest-instruction phase notes

CipherGBA's ROM-free timing tests exercise synthetic CPU instructions and scheduler activity; they do **not** independently establish physical ARM7TDMI bus-phase correctness. The suite's four outstanding HBlank timing assertions require additional hardware-referenced investigation.

## Scheduled PPU boundaries

The synthetic timing coverage asserts the HBlank flag and interrupt at line clock 1006, status reset at 1232 clocks per scanline, VBlank at line 160, and total 228 scanlines / 280,896 master clocks per frame. Passing these invariants does not prove that guest `LDRH` reads sample DISPSTAT/Timer0 at the same phase as physical hardware.

## Diagnostic guest sequence

A diagnostic trace of Thumb code executing from IWRAM observed polling of DISPSTAT around the HBlank edge at approximately eight-cycle intervals. Representative samples:

| Guest polling PC | PPU line / phase | HBlank status |
| --- | --- | --- |
| `03000146` | 162 / 1000 | 0 |
| `03000146` | 162 / 1008 | 1 |
| `0300016E` | 163 / 998 | 0 |
| `0300016E` | 163 / 1006 | 1 |
| `03000198` | 164 / 1001 | 0 |
| `03000198` | 164 / 1011 | 1 |

As this sample shows, a small instruction/data-bus timing difference can shift a polling loop by a full iteration. This is a potential explanation, **not a demonstrated cause** of the failed checks.

## Next verification work

- Compare a pinned suite ROM/toolchain on physical GBA hardware, including successive IRQ/HALT returns.
- Trace guest `LDRH` sampling, taken Thumb branch refill and timer latch phase using a hardware-referenced test.
- Validate any general CPU-cycle correction against both BIOS modes and complete GBA suite checks.
- Avoid hardcoding specific program counters, special-casing the suite ROM or shifting PPU HBlank boundaries without evidence.

Synthetic regressions can be built with `-DCIPHERGBA_BUILD_PERSISTENCE_SMOKE=ON` and executed through `CipherGBA_CpuPhaseSmoke`. Their PASS status would prove internal consistency only. See [Accuracy report](ACCURACY.md) for the current unresolved four checks.
