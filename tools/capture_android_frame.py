"""Capture one AXRB TCP RGBA frame as PNG, without a headset or dependencies."""
import argparse
import socket
import struct
import zlib
from pathlib import Path


def read_exact(connection, size):
    result = bytearray()
    while len(result) < size:
        block = connection.recv(size - len(result))
        if not block:
            raise EOFError("Android disconnected during a frame")
        result.extend(block)
    return bytes(result)


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--port', type=int, default=38491)
    args = parser.parse_args()
    with socket.socket() as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(('127.0.0.1', args.port))
        server.listen(1)
        server.settimeout(60)
        connection, _ = server.accept()
        with connection:
            connection.settimeout(15)
            header = read_exact(connection, 64)
            magic, version, kind, size, width, height, layers, fmt, bpp, _ = struct.unpack_from('<IHH7I', header)
            sequence, _, payload_size = struct.unpack_from('<QQQ', header, 40)
            if (magic, version, kind, size, fmt, bpp) != (0x49585241, 1, 2, 64, 1, 4):
                raise ValueError('Unsupported AXRB frame header')
            if not width or not height or not layers or payload_size != width * height * layers * 4 or payload_size > 128 * 1024 * 1024:
                raise ValueError('Invalid image dimensions or payload size')
            rgba = read_exact(connection, payload_size)[:width * height * 4]
    # OpenGL's first row is the bottom of the image. Export the first layer.
    rows = b''.join(b'\0' + rgba[y * width * 4:(y + 1) * width * 4] for y in reversed(range(height)))
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(png)
    colors = len(set(zip(rgba[0::4], rgba[1::4], rgba[2::4])))
    print(f'Captured frame {sequence}: {width}x{height}, {colors} unique RGB colors -> {args.output}')


if __name__ == '__main__':
    main()
