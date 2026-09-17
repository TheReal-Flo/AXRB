"""Legacy entry point; setup now uses the system Vulkan layer, never game edits."""
import struct
from android_runtime_policy import main

# Historical pure-data helper retained for regression coverage only.
OLD = b'libvulkan.so\0'
NEW = b'libaxrbvk.so\0'

def redirect_library(data):
    if data[:6] != b'\x7fELF\x02\x01' or len(data) < 64 or struct.unpack_from('<H', data, 18)[0] != 183:
        raise ValueError('Expected little-endian ARM64 ELF64')
    return data.replace(OLD, NEW), data.count(OLD)


if __name__ == "__main__": main()
