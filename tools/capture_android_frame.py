"""Capture one AXRB TCP RGBA frame as PNG, without a headset or dependencies."""
import argparse
import socket
import struct
import zlib
import json
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
            if (magic, kind, fmt, bpp) != (0x49585241, 2, 1, 4) or (version, size) not in ((1, 64), (2, 160)):
                raise ValueError('Unsupported AXRB frame header')
            if not width or not height or not layers or payload_size != width * height * layers * 4 or payload_size > 128 * 1024 * 1024:
                raise ValueError('Invalid image dimensions or payload size')
            metadata = read_exact(connection, 96) if version == 2 else None
            payload = read_exact(connection, payload_size)
    eye_bytes = width * height * 4
    write_png(args.output, width, height, payload[:eye_bytes])
    if metadata:
        count, reserved = struct.unpack_from('<II', metadata)
        if count != 2 or reserved != 0 or layers != 2:
            raise ValueError('Invalid stereo metadata')
        views = [struct.unpack_from('<11f', metadata, 8 + eye * 44) for eye in range(2)]
        args.output.with_suffix('.json').write_text(json.dumps({'sequence': sequence, 'views_position_quaternion_fov': views}, indent=2))
        right = args.output.with_name(args.output.stem + '-right.png')
        write_png(right, width, height, payload[eye_bytes:eye_bytes * 2])
        differences = sum(payload[i:i+3] != payload[eye_bytes+i:eye_bytes+i+3] for i in range(0, eye_bytes, 4))
        print(f'Stereo: {differences}/{width * height} pixels differ; right eye -> {right}')
    print(f'Captured frame {sequence}: {width}x{height}, layers={layers} -> {args.output}')


def write_png(path, width, height, rgba):
    # OpenGL's first row is the bottom of the image.
    rows = b''.join(b'\0' + rgba[y * width * 4:(y + 1) * width * 4] for y in reversed(range(height)))
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


if __name__ == '__main__':
    main()
