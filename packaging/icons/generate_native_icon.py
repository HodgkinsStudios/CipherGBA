#!/usr/bin/env python3
"""Convert the approved CipherGBA PNG into native macOS ICNS / Windows ICO.

No additional Python packages or image-generation dependencies. Uses the
standard-library PNG reader already maintained for Linux Flatpak packaging.
The original PNG stays untouched and is never replaced in the source tree.
"""
import pathlib
import struct
import sys
import tempfile

ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"linux"))
from resize_png import resize_to_rgba  # noqa: E402


def icns(png):
    # ICNS ic08: compressed PNG 256x256 image, recognized by modern macOS.
    element=b"ic08"+struct.pack(">I",8+len(png))+png
    return b"icns"+struct.pack(">I",8+len(element))+element


def ico(png):
    # Windows Vista+ PNG-based 256x256 icon. 0 encodes 256 in ICO DIR.
    header=struct.pack("<HHH",0,1,1)
    entry=struct.pack("<BBBBHHII",0,0,0,0,1,32,len(png),22)
    return header+entry+png


def main():
    if len(sys.argv)!=4 or sys.argv[3] not in ("icns","ico"):
        sys.exit("Usage: generate_native_icon.py INPUT.png OUTPUT.icns|ico icns|ico")
    source=pathlib.Path(sys.argv[1])
    destination=pathlib.Path(sys.argv[2])
    with tempfile.TemporaryDirectory() as tmp:
        prepared=pathlib.Path(tmp)/"icon_256.png"
        resize_to_rgba(source,prepared)
        png=prepared.read_bytes()
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_bytes(icns(png) if sys.argv[3]=="icns" else ico(png))
    print(f"Created {sys.argv[3]} icon: {destination}")


if __name__=="__main__":
    main()
