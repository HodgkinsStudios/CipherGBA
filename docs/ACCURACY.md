# Accuracy and verification

CipherGBA implements both integrated BIOS HLE and original BIOS operation. The recorded full numerical suite baseline below was obtained with a privately supplied `suite.gba` and original 16 KiB BIOS; neither is included in the release. These are **observed suite scores**, not proof of universal hardware accuracy.

## Recorded scored results

| BIOS mode | Passed | Scored | Percentage |
| --- | ---: | ---: | ---: |
| Integrated HLE BIOS | **6,994** | 6,998 | 99.9428% |
| Original BIOS | **6,994** | 6,998 | 99.9428% |

The GBA suite has 13 numerically scored categories; an additional Video category is visual-only and excluded from the 6,998 numerical total. The four reported failures occur in the Miscellaneous Edge Cases HBlank group (`8/12` in that group):

| Check | CipherGBA observed | Supplied ROM expected |
| --- | --- | --- |
| `Hblank` | `0x000004D3` | `0x000004D0` |
| `Flip 1` | `0x00000080` | `0x00000087` |
| `Flip 2` | `0x000003ED` | `0x000003EC` |
| `Flip 3` | `0x000000E3` | `0x000000E5` |

These four checks remain **unresolved failures**. The same mismatch in both BIOS modes indicates a shared behavior, but does not determine whether timing in the emulator or assumptions in the suite are at fault. Upstream discussion documents HBlank test fragility on hardware: [suite issue #19](https://github.com/mgba-emu/suite/issues/19), [issue #24](https://github.com/mgba-emu/suite/issues/24), and [issue #27](https://github.com/mgba-emu/suite/issues/27). Those reports do not validate CipherGBA's results or establish that any of the four are false positives. Do not claim 100% accuracy.

## Test provenance and procedure

The supplied accuracy ROM tested in the recorded run had SHA-256:

`8cf68cd31c5468a70aca8a1c5b63dc372fe755c40b446cf6aa6d0fc6e3a1f035`

The ROM's exact reproducible build recipe and full compiler provenance have not been established. A private BIOS was supplied in real-firmware runs. Each scored category was executed in a fresh core instance and its displayed numerical score was read through the portable core API. The helper ships as [`.github/scripts/verify_suite_scores.py`](../.github/scripts/verify_suite_scores.py), and requires the independently provided suite and BIOS:

```sh
python3 .github/scripts/verify_suite_scores.py \
  --core ./bin/libCipherGBA_Core.so.1 \
  --suite /path/to/your/suite.gba \
  --bios /path/to/your/gba_bios.bin \
  --mode both --out ./local-suite-scores
```

This command creates local JSON detail files and a summary. Use [`.github/scripts/accuracy_capture.py`](../.github/scripts/accuracy_capture.py) for optional framebuffer evidence; screenshots or framebuffer hashes alone do **not** establish numerical test results.

## Follow-up work

The four HBlank measurements involve guest CPU instruction/data-bus read phase in addition to PPU scanline boundaries and HALT/IRQ behavior. ROM-free CPU/PPU clock checks cover important internal invariants but cannot prove physical bus phase timing. See [HBlank technical notes](HBLANK_PHASE_TRACE.md) for a diagnostic sample and proposed next tests. Correct generic hardware behavior only after independent evidence; never special-case a test ROM or manipulate scoring.

Accuracy score alone does not guarantee full-game compatibility, link-cable features, frame pacing, or perfect sound. For host validation see [Platform checklist](PLATFORM_ACCEPTANCE.md) and [Performance](PERFORMANCE.md).
