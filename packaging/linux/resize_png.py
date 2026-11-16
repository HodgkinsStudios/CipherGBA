#!/usr/bin/env python3
"""Create a 256x256 PNG application icon with Python's standard library.

Only used for Linux distribution packaging; the approved original art is
unchanged. Supports ordinary non-interlaced 8-bit RGB and RGBA source PNGs.
No additional Python imaging dependencies are required.
"""
import binascii
import struct
import sys
import zlib
from pathlib import Path

SIGNATURE = b"\x89PNG\r\n\x1a\n"

def chunk(kind, payload):
    crc = binascii.crc32(kind + payload) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", crc)

def load_rgb_or_rgba(path):
    raw = Path(path).read_bytes()
    if not raw.startswith(SIGNATURE):
        raise ValueError("not a PNG image")
    offset = len(SIGNATURE)
    header = None
    packed = bytearray()
    while offset + 12 <= len(raw):
        length, = struct.unpack_from(">I", raw, offset)
        offset += 4
        name = raw[offset:offset+4]
        offset += 4
        if offset + length + 4 > len(raw):
            raise ValueError("truncated PNG chunk")
        data = raw[offset:offset+length]
        offset += length + 4
        if name == b"IHDR":
            header = struct.unpack(">IIBBBBB", data)
        elif name == b"IDAT":
            packed.extend(data)
        elif name == b"IEND":
            break
    if header is None:
        raise ValueError("PNG missing IHDR")
    width, height, depth, color, compression, filtering, interlace = header
    if not (0 < width <= 8192 and 0 < height <= 8192):
        raise ValueError("invalid PNG dimensions")
    if depth != 8 or color not in (2, 6) or compression or filtering or interlace:
        raise ValueError("source must be an 8-bit non-interlaced RGB/RGBA PNG")
    channels = 4 if color == 6 else 3
    stride = width * channels
    pixels = zlib.decompress(packed)
    if len(pixels) != height * (stride + 1):
        raise ValueError("unexpected PNG image data length")
    rows = []
    prev = bytearray(stride)
    pos = 0
    for _ in range(height):
        filt = pixels[pos]
        pos += 1
        row = bytearray(pixels[pos:pos+stride])
        pos += stride
        if filt not in (0,1,2,3,4):
            raise ValueError("invalid PNG filter")
        for x in range(stride):
            a = row[x-channels] if x >= channels else 0
            b = prev[x]
            c = prev[x-channels] if x >= channels else 0
            if filt == 1:
                predictor = a
            elif filt == 2:
                predictor = b
            elif filt == 3:
                predictor = (a+b)//2
            elif filt == 4:
                p = a+b-c
                pa,pb,pc = abs(p-a),abs(p-b),abs(p-c)
                predictor = a if pa<=pb and pa<=pc else (b if pb<=pc else c)
            else:
                predictor = 0
            row[x] = (row[x] + predictor) & 255
        rows.append(row)
        prev = row
    return width,height,channels,rows

def resize_to_rgba(src,dst):
    width,height,channels,rows=load_rgb_or_rgba(src)
    output=bytearray()
    for y in range(256):
        srcrow=rows[min(height-1,(y*height)//256)]
        output.append(0)
        for x in range(256):
            pos=min(width-1,(x*width)//256)*channels
            output.extend(srcrow[pos:pos+3])
            output.append(srcrow[pos+3] if channels==4 else 255)
    ihdr=struct.pack(">IIBBBBB",256,256,8,6,0,0,0)
    png=SIGNATURE+chunk(b"IHDR",ihdr)+chunk(b"IDAT",zlib.compress(output,9))+chunk(b"IEND",b"")
    Path(dst).parent.mkdir(parents=True,exist_ok=True)
    Path(dst).write_bytes(png)

if __name__=="__main__":
    if len(sys.argv)!=3:
        sys.exit("Usage: resize_png.py INPUT.png OUTPUT.png")
    resize_to_rgba(sys.argv[1],sys.argv[2])
