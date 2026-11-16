#!/usr/bin/env python3
"""Generate portable, relocation-safe C++ artwork from the canonical GBA icon.

Pure Python stdlib: Windows, macOS and Linux builds use exactly the same image.
Never manually edit the generated BrandArtwork.hpp header.
"""
import pathlib
import sys
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'linux'))
from resize_png import load_rgb_or_rgba


def scale_rgba(source, size):
    width, height, channels, rows = source
    # Bilinear filtering avoids jagged edges at small OS icon sizes.
    result = bytearray(size * size * 4)
    for y in range(size):
        fy = (y + .5) * height / size - .5
        a = max(0, min(height - 1, int(fy)))
        b = max(0, min(height - 1, a + 1))
        yf = max(0., min(1., fy - a))
        for x in range(size):
            fx = (x + .5) * width / size - .5
            left = max(0, min(width - 1, int(fx)))
            right = max(0, min(width - 1, left + 1))
            xf = max(0., min(1., fx - left))
            o = (y * size + x) * 4
            for ch in range(4):
                v00 = rows[a][left*channels+ch] if ch < channels else 255
                v10 = rows[a][right*channels+ch] if ch < channels else 255
                v01 = rows[b][left*channels+ch] if ch < channels else 255
                v11 = rows[b][right*channels+ch] if ch < channels else 255
                value = (1-yf)*((1-xf)*v00+xf*v10) + yf*((1-xf)*v01+xf*v11)
                result[o+ch] = round(value)
    return bytes(result)


def main():
    if len(sys.argv) != 3:
        raise SystemExit('Usage: generate_embedded_artwork.py ICON.png OUTPUT.hpp')
    src = load_rgb_or_rgba(sys.argv[1])
    output = pathlib.Path(sys.argv[2])
    output.parent.mkdir(parents=True, exist_ok=True)
    lines = ['// Generated from assets/branding/ciphergba_icon.png. DO NOT EDIT.',
             '#pragma once', '#include <cstdint>', '#include <cstddef>',
             '#include <vector>', '#include <zlib.h>',
             'namespace pixelgba { namespace artwork {',
             'struct CompressedImage { const unsigned char* data; std::size_t length; int width; int height; };',
             'inline bool decode(const CompressedImage& image,std::vector<unsigned char>& pixels){',
             '  const auto bytes=std::size_t(image.width)*image.height*4u;',
             '  pixels.resize(bytes);uLongf n=static_cast<uLongf>(bytes);',
             '  return uncompress(pixels.data(),&n,image.data,static_cast<uLong>(image.length))==Z_OK && n==bytes;',
             '}']
    for label, size in [('kAboutArtwork', 384), ('kIcon64', 64), ('kIcon32', 32)]:
        rgba = scale_rgba(src, size)
        packed = zlib.compress(rgba, level=9)
        data_name = label + 'Data'
        lines.append('inline constexpr unsigned char ' + data_name + '[] = {')
        for i in range(0, len(packed), 24):
            lines.append('  ' + ', '.join('0x%02x' % v for v in packed[i:i+24]) + ',')
        lines.append('};')
        lines.append('inline constexpr CompressedImage '+label+'={'+data_name+',sizeof('+data_name+'),'+str(size)+','+str(size)+'};')
    lines.append('} } // namespace pixelgba::artwork')
    output.write_text('\n'.join(lines)+'\n', encoding='utf-8')
    print('Generated', output, output.stat().st_size, 'bytes')


if __name__ == '__main__':
    main()
