"""Apply a capacity-based Unreal texture pool on the Windows Nvidia emulator.

No package-specific rules. Existing Vulkan allocation requirements are unchanged.
Only existing Unreal saved-config directories and extracted engine libraries are
recognized. Unknown layouts are skipped rather than guessed.
"""
import argparse
import base64
import hashlib
import json
import re
import shlex
import subprocess
from pathlib import Path

BEGIN = '; AXRB managed Unreal memory policy begin'
END = '; AXRB managed Unreal memory policy end'


def pool_mib(capacity):
    # Half the dedicated VRAM, with at least 2 GiB left for the compositor,
    # render targets and other applications. This is a ceiling, not a reservation.
    return max(0, int(min(capacity // 2, capacity - 2048)) // 256 * 256)


def pool_percentage(pool, guest_heap_mib):
    if guest_heap_mib <= 0:
        raise ValueError('Invalid guest device-local heap')
    return pool * 100 // guest_heap_mib


def configure(text, pool, guest_heap_mib):
    if BEGIN in text or END in text:
        raise ValueError('Unexpected existing AXRB block; use the recorded original')
    # The RHI's initialization percentage is separate from device-profile CVar
    # precedence. Fix the initialized pool so a smaller mobile profile cannot
    # subsequently reset it. The percentage can exceed 100 because Gfxstream's
    # advertised heap is smaller than the physical host VRAM used for budgeting.
    percentage = pool_percentage(pool, guest_heap_mib)
    return text.rstrip() + '\n\n' + BEGIN + '\n[SystemSettings]\n' + (
        f'r.Streaming.PoolSize={pool}\n'
        'r.Streaming.UseFixedPoolSize=1\n'
        'r.Streaming.LimitPoolSizeToVRAM=0\n'
        'r.Streaming.FullyLoadUsedTextures=0\n'
        f'[TextureStreaming]\nPoolSizeVRAMPercentage={percentage}\n'
    ) + END + '\n'


def digest(text):
    return hashlib.sha256(text.encode()).hexdigest()


def run(argv, timeout=30):
    return subprocess.run(list(map(str, argv)), check=True, capture_output=True,
                          encoding='utf-8', errors='strict', timeout=timeout,
                          creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)).stdout.strip()


def apply(args):
    if not re.fullmatch(r'[a-zA-Z0-9_.]+', args.package):
        raise ValueError('Invalid package')
    adb = [args.sdk / 'platform-tools/adb.exe', '-s', args.serial]
    def shell(command):
        return run(adb + ['shell', command])
    def root(command):
        return shell('su 0 sh -c ' + shlex.quote(command))
    def read(path):
        # Base64 preserves exact newlines for backup/conflict detection.
        return base64.b64decode(root('base64 ' + shlex.quote(path))).decode('utf-8')
    state_path = args.state_dir / (re.sub(r'[^a-zA-Z0-9_.-]', '_', args.serial) + '-' + args.package + '.json')
    state = json.loads(state_path.read_text()) if state_path.exists() else None
    if args.restore:
        if not state:
            return {'status': 'unchanged', 'reason': 'No managed policy'}
        path = state['path']
        current = read(path)
        if digest(current) != state['applied_sha256']:
            raise RuntimeError('Config changed outside AXRB; refusing to overwrite it')
        desired = state['original']
    else:
        package = shell('dumpsys package ' + args.package)
        match = re.search(r'(?:legacyNativeLibraryDir|nativeLibraryDir)=(\S+)', package)
        if not match:
            return {'status': 'skipped', 'reason': 'No extracted native library directory'}
        if root('if test -d ' + shlex.quote(match[1]) + '; then echo yes; fi') != 'yes':
            return {'status': 'skipped', 'reason': 'Native libraries are not extracted'}
        libraries = root('find ' + shlex.quote(match[1]) + ' -maxdepth 2 -type f')
        if not any(Path(p).name in ('libUnreal.so', 'libUE4.so') for p in libraries.splitlines()):
            return {'status': 'skipped', 'reason': 'Not a recognized Unreal app'}
        # Disable the guest VRAM clamp only on the measured hardware path.
        gpu = json.loads(shell('cmd gpu vkjson'))['devices']
        if len(gpu) != 1 or gpu[0]['properties']['vendorID'] != 0x10de or gpu[0]['properties']['deviceType'] != 2:
            return {'status': 'skipped', 'reason': 'Requires one Nvidia hardware Vulkan device'}
        host = run(['nvidia-smi', '--query-gpu=memory.total', '--format=csv,noheader,nounits']).splitlines()
        if len(host) != 1:
            return {'status': 'skipped', 'reason': 'Ambiguous host GPU selection'}
        capacity = int(host[0].strip())
        pool = pool_mib(capacity)
        if pool < 1024:
            return {'status': 'skipped', 'reason': 'Insufficient dedicated VRAM for this policy'}
        heaps = [int(h['size'], 16) if isinstance(h['size'], str) else int(h['size'])
                 for h in gpu[0]['memory']['memoryHeaps'] if int(h['flags']) & 1]
        if len(heaps) != 1 or heaps[0] < 1024 * 1024:
            return {'status': 'skipped', 'reason': 'Ambiguous guest device-local memory heap'}
        guest_heap_mib = heaps[0] // (1024 * 1024)
        percentage = pool_percentage(pool, guest_heap_mib)
        if percentage < 1:
            return {'status': 'skipped', 'reason': 'Cannot represent the pool as a positive heap percentage'}
        files = '/data/user/0/' + args.package + '/files'
        if root('if test -d ' + shlex.quote(files) + '; then echo yes; fi') != 'yes':
            return {'status': 'skipped', 'reason': 'No saved files; launch once first'}
        directories = root('find ' + shlex.quote(files) + ' -maxdepth 7 -type d -name Android').splitlines()
        candidates = [p + '/Engine.ini' for p in directories
                      if re.fullmatch(re.escape(files) + r'/(UnrealGame|UE4Game)/[^/]+(?:/[^/]+)?/Saved/Config/Android', p)
                      and p.split('/')[-4] != 'Engine']
        if len(candidates) != 1:
            return {'status': 'skipped', 'reason': 'No unique Unreal saved config; launch once first'}
        path = candidates[0]
        exists = root('if test -f ' + shlex.quote(path) + '; then echo yes; fi') == 'yes'
        current = read(path) if exists else ''
        if state:
            if state['path'] != path or digest(current) != state['applied_sha256']:
                raise RuntimeError('Config changed outside AXRB; refusing to overwrite it')
            original = state['original']
        else:
            original = current
        desired = configure(original, pool, guest_heap_mib)
        state = {'path': path, 'original': original, 'applied_sha256': digest(desired),
                 'pool_mib': guest_heap_mib * percentage // 100,
                 'host_vram_mib': capacity, 'guest_heap_mib': guest_heap_mib,
                 'pool_percentage': percentage}
    if root('pidof ' + args.package + ' || true'):
        raise RuntimeError('Stop the game before changing its memory policy')
    uid = root('stat -c %u ' + shlex.quote('/data/user/0/' + args.package))
    if not uid.isdigit() or int(uid) < 10000:
        raise RuntimeError('Cannot determine app UID')
    # Save rollback information before changing the app. Retain it on failure.
    args.state_dir.mkdir(parents=True, exist_ok=True)
    if not args.restore:
        state_path.write_text(json.dumps(state, indent=2))
    temporary = path + '.axrb-tmp'
    encoded = base64.b64encode(desired.encode()).decode()
    root('umask 077; printf %s ' + shlex.quote(encoded) + ' | base64 -d > ' + shlex.quote(temporary) +
         ' && chown ' + uid + ':' + uid + ' ' + shlex.quote(temporary) +
         ' && mv ' + shlex.quote(temporary) + ' ' + shlex.quote(path) +
         ' && restorecon ' + shlex.quote(path))
    if read(path) != desired:
        raise RuntimeError('Written memory policy did not verify')
    if args.restore:
        state_path.unlink()
        return {'status': 'restored', 'path': path}
    return {'status': 'applied', 'path': path, 'pool_mib': state['pool_mib'],
            'host_vram_mib': capacity, 'guest_heap_mib': guest_heap_mib,
            'pool_percentage': percentage}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--package', required=True)
    parser.add_argument('--state-dir', type=Path, default=Path(__file__).resolve().parents[1] / 'build-windows-game/memory-policy')
    parser.add_argument('--restore', action='store_true')
    print(json.dumps(apply(parser.parse_args())))
