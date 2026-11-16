#!/usr/bin/env python3
"""On-runner, synthetic input checks for ZIP/raw ROM loading.

No ROM assets, test binaries, or copyrighted data are checked into the repo.
The generated header is only used to exercise file validation and ZIP parsing.
"""
import pathlib
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib

binary = pathlib.Path(sys.argv[1]).resolve()
data = bytearray(0x2000)
data[0xA0:0xAC] = b"PORTABLETEST"
data[0xAC:0xB0] = b"CI02"
data[0xB2] = 0x96
data[0xBD] = (-sum(data[0xA0:0xBD]) - 0x19) & 0xFF
expected = f"{zlib.crc32(data):08X}"

def test(path, accepted):
    result = subprocess.run([str(binary), "--verify-rom", str(path)],
                            capture_output=True, text=True, timeout=15)
    good = result.returncode == 0 and f"CRC32={expected}" in result.stdout
    if accepted != good:
        print(result.stdout, result.stderr, file=sys.stderr)
        raise RuntimeError(f"ROM check: {path} accepted={accepted}, actual={good}")

with tempfile.TemporaryDirectory(prefix="ciphergba-rom-") as root:
    d = pathlib.Path(root)
    raw = d / "synthetic.gba"
    raw.write_bytes(data)
    test(raw, True)

    for kind in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
        zipped = d / ("stored.zip" if kind == zipfile.ZIP_STORED else "deflated.zip")
        with zipfile.ZipFile(zipped, "w", compression=kind) as archive:
            archive.writestr("nested/ROM.GBA", data)
            archive.writestr("docs/notes.txt", "CI archive generated at runtime")
        test(zipped, True)

    ambiguous = d / "ambiguous.zip"
    with zipfile.ZipFile(ambiguous, "w", compression=zipfile.ZIP_STORED) as archive:
        archive.writestr("rom1.gba", data)
        archive.writestr("rom2.gba", data)
    test(ambiguous, False)

    invalid = d / "invalid.zip"
    with zipfile.ZipFile(invalid, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("game.gba", data)
    image = bytearray(invalid.read_bytes())
    central = image.index(b"PK\x01\x02")
    struct.pack_into("<I", image, central + 16, 0x10203040)
    invalid.write_bytes(image)
    test(invalid, False)

print("Synthetic ROM checks passed: raw, stored, deflated, ambiguous, corrupt CRC")
