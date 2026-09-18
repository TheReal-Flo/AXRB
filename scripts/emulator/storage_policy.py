"""Read-ahead policy for the Android 16 emulator's virtio-backed /data disk."""
import re
import shlex


def apply_storage_policy(shell, target_kib=1024):
    if target_kib not in (128, 1024):
        raise ValueError('Supported read-ahead values: 128 or 1024 KiB')
    if shell('getprop ro.hardware') != 'ranchu' or shell('getprop ro.build.version.sdk') != '36':
        return {'status': 'skipped', 'reason': 'Requires the Android 16 emulator'}
    mounts = [line.split() for line in shell('cat /proc/mounts').splitlines()]
    data = [m for m in mounts if len(m) >= 4 and m[1] == '/data' and m[2] in ('ext4', 'f2fs')]
    if len(data) != 1 or not data[0][0].startswith('/dev/block/'):
        return {'status': 'skipped', 'reason': 'Unrecognized data filesystem'}
    device = shell('readlink -f ' + shlex.quote(data[0][0])).removeprefix('/dev/block/')
    pending, devices, leaves = [device], [], []
    while pending:
        name = pending.pop()
        if name in devices:
            continue
        if len(devices) >= 8 or not re.fullmatch(r'dm-\d+|vd[a-z]+', name):
            return {'status': 'skipped', 'reason': 'Data is not a recognized virtio disk'}
        devices.append(name)
        children = shell(f'ls -1 /sys/class/block/{name}/slaves').splitlines()
        if children:
            pending.extend(children)
        else:
            leaves.append(name)
    if not leaves or not all(re.fullmatch(r'vd[a-z]+', name) for name in leaves):
        return {'status': 'skipped', 'reason': 'Data is not backed by virtio'}
    paths = [f'/sys/class/block/{name}/queue/read_ahead_kb' for name in devices]
    original = {path: int(shell('cat ' + path)) for path in paths}
    # Preserve values deliberately configured outside our default/tuned pair.
    if any(value not in (128, 1024) for value in original.values()):
        return {'status': 'skipped', 'reason': 'Custom read-ahead configuration'}
    changed = []
    try:
        for path, value in original.items():
            if value == target_kib:
                continue
            changed.append(path)
            shell(f'echo {target_kib} > {path}')
            if int(shell('cat ' + path)) != target_kib:
                raise RuntimeError('Storage read-ahead did not verify')
    except Exception:
        for path in reversed(changed):
            shell(f'echo {original[path]} > {path}')
        raise
    return {'status': 'applied' if changed else 'unchanged',
            'read_ahead_kib': target_kib, 'devices': devices}
