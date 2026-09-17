"""Read-only checks for a separately booted AXRB Digitalis Android 16 guest."""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--serial', default='emulator-5582')
    args = parser.parse_args()
    adb = [str(args.sdk / 'platform-tools/adb.exe'), '-s', args.serial]
    def shell(command):
        return subprocess.run([*adb, 'shell', command], capture_output=True,
                              text=True, check=True, timeout=15).stdout.strip()
    props = {p: shell('getprop ' + p) for p in (
        'ro.build.version.sdk', 'ro.build.version.release', 'ro.dalvik.vm.native.bridge',
        'ro.dalvik.vm.isa.arm64', 'ro.product.cpu.abilist', 'ro.enable.native.bridge.exec')}
    problems = []
    if props['ro.build.version.sdk'] != '36': problems.append('Requires Android 16/API 36')
    if props['ro.dalvik.vm.native.bridge'] != 'libberberis_arm64.so': problems.append('Digitalis native bridge is not selected')
    if props['ro.dalvik.vm.isa.arm64'] != 'x86_64': problems.append('ARM64 ISA mapping is absent')
    if 'arm64-v8a' not in props['ro.product.cpu.abilist'].split(','): problems.append('ARM64 ABI is not advertised')
    for filename in ('/system/lib64/libberberis_arm64.so', '/system/lib64/arm64/libc.so',
                     '/system/bin/arm64/linker64', '/system/etc/ld.config.arm64.txt'):
        if shell('if [ -s ' + filename + ' ]; then echo present; fi') != 'present':
            problems.append('Missing ' + filename)
    print(json.dumps({'properties': props, 'ready_for_atomic_tests': not problems,
                      'problems': problems}, indent=2))
    return bool(problems)


if __name__ == '__main__': raise SystemExit(main())
