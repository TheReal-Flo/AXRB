"""Compare read-ahead on existing assets; run with games stopped.

Only reads the supplied file. --drop-guest-cache clears Android's page cache
between workloads, not the Windows cache. Queue settings are restored on exit.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('storage_policy', ROOT / 'scripts/emulator/storage_policy.py')
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)


def run(args, timeout=120):
    return subprocess.run(list(map(str, args)), check=True, capture_output=True,
                          text=True, timeout=timeout).stdout.strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, default=Path(os.environ['LOCALAPPDATA']) / 'Android/Sdk')
    parser.add_argument('--serial', required=True)
    parser.add_argument('--file', required=True, help='Existing Android asset at least 512 MiB')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--read-ahead-kib', type=int, nargs='+', choices=(128, 1024), default=[128, 1024, 1024, 128])
    parser.add_argument('--drop-guest-cache', action='store_true')
    args = parser.parse_args()
    if not args.serial.startswith('emulator-'):
        parser.error('Only emulator serials are supported')
    build = ROOT / 'out/android/storage-probe'
    build.mkdir(parents=True, exist_ok=True)
    compilers = sorted((args.sdk / 'ndk').glob('*/toolchains/llvm/prebuilt/windows-x86_64/bin/x86_64-linux-android29-clang++.cmd'),
                       key=lambda p: tuple(int(n) for n in p.parents[5].name.split('.')))
    if not compilers:
        raise RuntimeError('Install an Android NDK')
    binary = build / 'storage-read-probe'
    run([compilers[-1], '-O2', '-std=c++20', '-static-libstdc++',
         ROOT / 'tests/android/storage_read_probe.cpp', '-o', binary])
    adb = [args.sdk / 'platform-tools/adb.exe', '-s', args.serial]
    def shell(command):
        return run(adb + ['shell', 'su 0 sh -c ' + shlex.quote(command)])
    remote = '/data/local/tmp/axrb-storage-read'
    run(adb + ['push', binary, remote])
    shell('chmod 755 ' + remote)
    original = {}
    # Record every setting the policy touches before its first change, so even
    # an interrupted comparison can restore each queue's original value.
    def measured_shell(command):
        if command.startswith('echo '):
            path = command.split(' > ', 1)[1]
            if path not in original:
                original[path] = int(shell('cat ' + path))
        return shell(command)
    report = {'file': args.file, 'fingerprint': shell('getprop ro.build.fingerprint'),
              'guest_cache_cleared': args.drop_guest_cache, 'host_cache_cleared': False, 'runs': []}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    try:
        for ahead in args.read_ahead_kib:
            applied = policy.apply_storage_policy(measured_shell, ahead)
            if applied['status'] == 'skipped':
                raise RuntimeError(applied['reason'])
            for pattern, size, block in [('sequential', 512 * 1024**2, 65536), ('random', 16 * 1024**2, 4096)]:
                if args.drop_guest_cache:
                    shell('sync; echo 3 > /proc/sys/vm/drop_caches')
                for pass_index in range(2):
                    result = json.loads(shell(f'{remote} {shlex.quote(args.file)} {size} {block} {pattern}'))
                    row = dict(read_ahead_kib=ahead, pattern=pattern, pass_index=pass_index, **result)
                    report['runs'].append(row)
                    args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
                    print(json.dumps(row), flush=True)
    finally:
        errors = []
        for path, value in original.items():
            try:
                shell(f'echo {value} > {path}')
                if int(shell('cat ' + path)) != value:
                    raise RuntimeError(f'Restore did not verify: {path}')
            except Exception as error:
                errors.append(str(error))
        if errors:
            raise RuntimeError('Could not restore read-ahead: ' + '; '.join(errors))


if __name__ == '__main__':
    main()
