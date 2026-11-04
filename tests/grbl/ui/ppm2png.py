#!/usr/bin/env python3
"""Convert binary PPM (P6) previews to PNG without third-party packages."""
import struct, sys, zlib

def convert(source, target):
    data = open(source, 'rb').read()
    header, rest = data.split(b'\n255\n', 1)
    width, height = map(int, header.split()[1:3])
    rows = b''.join(b'\x00' + rest[y * width * 3:(y + 1) * width * 3] for y in range(height))
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) \
        + chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b'')
    open(target, 'wb').write(png)

if __name__ == '__main__':
    for path in sys.argv[1:]:
        convert(path, path.rsplit('.', 1)[0] + '.png')
