# GBA cheat-code support

CipherGBA supports encrypted **GameShark Advance v1/v2**, encrypted **Action Replay / GameShark v3/v4**, **CodeBreaker/Xploder**, and direct RAW cheats. The editor uses explicit prefixes because both GameShark v1/v2 and Action Replay v3/v4 have 16-digit code lines but incompatible encryption keys.

| Format | Input example | Description |
| --- | --- | --- |
| Raw | `RAW:02000100:0000002A:32` | Address, hex value, decimal width (8/16/32) |
| CodeBreaker | `CB:32000100 002A` | 8+4 hexadecimal digits; plain or activated encryption |
| GameShark v1/v2 | `GS:XXXXXXXX YYYYYYYY` | 8+8 hex digits, GameShark v1/v2 encrypted |
| Action Replay v3/v4 | `AR:XXXXXXXX YYYYYYYY` | 8+8 hex digits, AR v3/v4 encrypted |
| Decrypted GameShark | `GSRAW:02000100 0000002A` | Explicit decrypted GS v1/v2 operation |
| Decrypted Action Replay | `ARRAW:00200100 0000002A` | Explicit decrypted AR v3/v4 operation |

Unprefixed 12-digit codes default to CodeBreaker, unprefixed 16-digit codes default to GameShark v1/v2. Prefer explicit prefixes. The `XXXXXXXX` examples are placeholders, not actual encrypted cheat codes.

**Multiline codes:** Put related code lines in one entry separated by semicolons, e.g. `CB:72000100 002A;32000120 0001`. This is required for conditionals, CodeBreaker encryption seed activation, fill/list parameters, and GameShark/Action Replay multi-code operations. You can paste multiple lines with **Ctrl+V**; newlines are converted into semicolons automatically.

**How to use:** Load a ROM → Cheats → Configure GBA Cheat Codes → ADD → type or paste one code group → Enter → RETURN → Cheats → Enable Cheats. The menu checkbox enables or disables all cheats. Individual entries are toggled by clicking them; the cheat list supports the mouse wheel. Each ROM has a separate persisted cheat file in the local CipherGBA_Data/cheats directory (`CGCHEAT2` format). Existing raw-cheat files are imported on first load and migrated when saved.

## Supported operations

- **CodeBreaker:** 8/16-bit assignments, OR, AND, add, conditional equal/not-equal/signed greater/signed less/bit-test, repeat-and-fill, byte lists, and optional encrypted-code reseeding (`9` master line). Game-ID and physical-device hook lines are accepted as metadata because cheats are applied once each emulated frame.
- **GameShark Advance v1/v2:** TEA encryption and device-key reseeding, 8/16/32-bit assignments, conditional next-code checks, multi-address value lists, cheat-cartridge button-gated writes, and 16-bit cartridge ROM patches.
- **Action Replay v3/v4:** TEA encryption and reseeding, 8/16/32-bit writes, repeated writes, additions, indirect pointer writes, conditionals, multi-operation blocks, button-gated writes, slide codes and ROM patches. Certain rare hardware-only commands may be rejected with a specific error rather than silently misapplied.
- **RAW:** 8/16/32-bit writes at writable GBA memory addresses.

**Button cheat codes:** Because no physical GameShark/Action Replay dongle exists, its external button is mapped to **GBA L + R held simultaneously**.

**Firmware accuracy and ROM handling:** The ROM patch overlay changes only values served through cartridge reads, not the ROM file, cartridge checksum, or game identity. Disabling cheats clears the overlay. Cheat writes run after each emulated frame; that is not instruction-exact emulation of an external cheat cartridge's code-handler hooks. Codes for a different game region/revision may be unsafe. Entering a code does not guarantee it is appropriate for the loaded game.

**Security and validation:** Bad lengths, unsupported opcodes, unfinished continuation blocks, writes outside mapped writable regions and misaligned writes are rejected. A code is compiled before it becomes active and the parser limits code size and repeat counts.

**References:** GBA cheat format details: [GBATEK](https://mgba-emu.github.io/gbatek/) and mGBA source. The device's original 256-byte reseeding substitution tables (four arrays) are included in `CipherGBA_Frontend/src/CheatSeedTables.hpp` with attribution and file-specific MPL-2.0 licensing. See `docs/licenses/MPL-2.0.txt`. CipherGBA's project-wide license remains unselected; see [third-party notices](../THIRD_PARTY_NOTICES.md).
