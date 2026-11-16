#!/usr/bin/env python3
"""Run a synthetic ARM SRAM-write ROM twice and verify battery persistence.

No game ROM, original BIOS or existing user saves are required or modified.
"""
import os
import pathlib
import struct
import subprocess
import sys
import tempfile

if len(sys.argv)!=2:
    sys.exit("Usage: check_battery_persistence.py path/to/CipherGBA")
binary=str(pathlib.Path(sys.argv[1]).resolve())

def generate(path,byte):
    raw=bytearray(0x1000)
    # ARM: LDR r0,[pc,#8]; MOV r1,#imm; STRB r1,[r0]; B .;
    # literal address 0x0E000000 is Game Pak SRAM.
    for i,word in enumerate((0xE59F0008,0xE3A01000|byte,0xE5C01000,
                             0xEAFFFFFE,0x0E000000)):
        struct.pack_into("<I",raw,i*4,word)
    raw[0xA0:0xAC]=b"BATTERYTEST!"
    raw[0xAC:0xB0]=b"BK02"
    raw[0xB2]=0x96
    raw[0x1E0:0x1E6]=b"SRAM_V"
    raw[0xBD]=(-sum(raw[0xA0:0xBD])-0x19)&255
    path.write_bytes(raw)

with tempfile.TemporaryDirectory(prefix="ciphergba-battery-") as temp:
    base=pathlib.Path(temp)
    rom=base/"battery.gba"
    root=base/"sandbox_data"
    save=root/"HodgkinsStudios"/"CipherGBA"/"saves"/"BK02.sav"
    env=dict(os.environ,SDL_VIDEODRIVER="dummy",SDL_AUDIODRIVER="dummy",
             XDG_DATA_HOME=str(root),APPIMAGE_EXTRACT_AND_RUN="1")
    for value in (0x5A,0xA5):
        generate(rom,value)
        subprocess.run([binary,"--rom",str(rom),"--smoke-frames","45"],
                       env=env,timeout=90,check=True)
        observed=save.read_bytes()
        assert len(observed)==32768,(len(observed),save)
        assert observed[0]==value,(value,observed[0],save)
    print("SRAM 32KiB persistence + second-run replacement PASS (5A -> A5)")
