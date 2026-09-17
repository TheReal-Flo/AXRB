"""Prepare the inspected Pinball FX VR 1.9 ovrport APK for AXRB on Windows.

Writes a separate unsigned APK; run zipalign and apksigner afterwards.
Original APK and expansion archives are never modified.
"""
import argparse
import hashlib
import struct
import zipfile
from pathlib import Path
from arm64_vector_math_fallback import rewrite

LIBRARY = 'lib/arm64-v8a/libUnreal.so'
SHA256 = '128baf6d29ee3ae9d1c61b7b02dc49dd8b6fc0c96a0c5619b922f6c987b1d300'


def patch_unreal(data):
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise ValueError('Unknown libUnreal.so: inspect this game version before patching')
    result = bytearray(data)
    offset = 0x4ae10d0
    if struct.unpack_from('<I', result, offset)[0] != 0xf0fdd2a1:
        raise ValueError('Unexpected optional ASTC memory-layout probe')
    # On allocation failure only, skip the optional ASTC linear/optimal layout
    # comparison. The optimal probe image has already been destroyed; the
    # optimization flag stays false. Real texture failures remain fatal.
    struct.pack_into('<I', result, offset, 0x14000019)
    result, counts = rewrite(result)
    if counts != {'frsqrte_2d': 949, 'frecpe_2d': 0}:
        raise ValueError(f'Unexpected math instruction counts: {counts}')
    if result.count(b'libvulkan.so\0') != 1:
        raise ValueError('Unexpected Vulkan library references')
    result = result.replace(b'libvulkan.so\0', b'libaxrbvk.so\0')
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--vulkan-shim', required=True, type=Path,
                   help='ARM64 libaxrbvk.so built from pinball_vulkan_compat.cpp')
    a = p.parse_args()
    if a.input.resolve() == a.output.resolve() or a.output.exists():
        p.error('Output must be a new, separate file')
    shim = a.vulkan_shim.read_bytes()
    if shim[:5] != b'\x7fELF\x02' or struct.unpack_from('<H', shim, 18)[0] != 183:
        p.error('Vulkan shim must be an ARM64 ELF library')
    with zipfile.ZipFile(a.input) as src:
        patched = patch_unreal(src.read(LIBRARY))
        with zipfile.ZipFile(a.output, 'w') as dst:
            for entry in src.infolist():
                if entry.filename.upper().startswith('META-INF/'):
                    continue
                dst.writestr(entry, patched if entry.filename == LIBRARY else src.read(entry))
            dst.writestr('lib/arm64-v8a/libaxrbvk.so', shim, compress_type=zipfile.ZIP_STORED)
    print(f'Wrote {a.output}: optional ASTC probe + 949 ARM64 math fallbacks + Vulkan core aliases')


if __name__ == '__main__':
    main()
