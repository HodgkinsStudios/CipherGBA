#!/usr/bin/env python3
"""Create a self-contained synthetic GBA boot-loop ROM for UI smoke tests.

No licensed game or BIOS is used or distributed. The guest ARM instruction
B . loops infinitely while the GBA PPU still advances and renders frames.
"""
import pathlib
import sys

data=bytearray(0x4000)
data[:4]=bytes.fromhex("FEFFFFEA")  # ARM B to current PC (little endian)
data[0xA0:0xAC]=b"SMOKE TEST  "
data[0xAC:0xB0]=b"SM02"
data[0xB2]=0x96
data[0xBD]=(-sum(data[0xA0:0xBD])-0x19)&255

path=pathlib.Path(sys.argv[1])
path.parent.mkdir(parents=True,exist_ok=True)
path.write_bytes(data)
print("Created synthetic smoke ROM:",path)
