import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('storage_policy',
    Path(__file__).resolve().parents[2] / 'scripts/emulator/storage_policy.py')
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)


class Guest:
    def __init__(self, leaf='vdc', value=128):
        self.leaf = leaf
        self.values = {f'/sys/class/block/{name}/queue/read_ahead_kb': value for name in ('dm-55', leaf)}
        self.writes = []
        self.fail = None

    def shell(self, command):
        fixed = {'getprop ro.hardware': 'ranchu', 'getprop ro.build.version.sdk': '36',
                 'cat /proc/mounts': '/dev/block/dm-55 /data ext4 rw,noatime 0 0',
                 'readlink -f /dev/block/dm-55': '/dev/block/dm-55',
                 'ls -1 /sys/class/block/dm-55/slaves': self.leaf,
                 f'ls -1 /sys/class/block/{self.leaf}/slaves': ''}
        if command in fixed:
            return fixed[command]
        if command.startswith('cat '):
            return str(self.values[command[4:]])
        parts = command.split()
        if len(parts) == 4 and parts[0] == 'echo' and parts[2] == '>':
            value, path = int(parts[1]), parts[3]
            self.writes.append((path, value))
            if path == self.fail and value == 1024:
                raise RuntimeError('Injected sysfs write failure')
            self.values[path] = value
            return ''
        raise AssertionError(command)


class StoragePolicyTests(unittest.TestCase):
    def test_data_mapper_and_backing_disk_are_tuned_once(self):
        guest = Guest()
        self.assertEqual(policy.apply_storage_policy(guest.shell)['status'], 'applied')
        self.assertEqual(set(guest.values.values()), {1024})
        self.assertEqual(policy.apply_storage_policy(guest.shell)['status'], 'unchanged')
        self.assertEqual(len(guest.writes), 2)
        policy.apply_storage_policy(guest.shell, 128)
        self.assertEqual(set(guest.values.values()), {128})

    def test_physical_disks_are_untouched(self):
        guest = Guest(leaf='sda')
        self.assertEqual(policy.apply_storage_policy(guest.shell)['status'], 'skipped')
        self.assertEqual(guest.writes, [])

    def test_custom_values_are_preserved(self):
        guest = Guest(value=512)
        self.assertEqual(policy.apply_storage_policy(guest.shell)['status'], 'skipped')
        self.assertEqual(guest.writes, [])

    def test_partial_failure_restores_changed_queues(self):
        guest = Guest()
        guest.fail = '/sys/class/block/vdc/queue/read_ahead_kb'
        with self.assertRaises(RuntimeError):
            policy.apply_storage_policy(guest.shell)
        self.assertEqual(set(guest.values.values()), {128})

    def test_other_android_versions_are_untouched(self):
        guest = Guest()
        def shell(command):
            return '34' if command == 'getprop ro.build.version.sdk' else guest.shell(command)
        self.assertEqual(policy.apply_storage_policy(shell)['status'], 'skipped')
        self.assertEqual(guest.writes, [])


if __name__ == '__main__':
    unittest.main()
