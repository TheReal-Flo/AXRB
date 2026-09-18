import importlib.util
from pathlib import Path
import shlex
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('audio_policy',
    Path(__file__).resolve().parents[2] / 'scripts/emulator/audio_policy.py')
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)


class Guest:
    def __init__(self):
        self.hardware = 'ranchu'
        self.sdk = '36'
        self.stock_hash = policy.STOCK_SHA256
        self.mounted = False
        self.owned = True
        self.frames = 1088
        self.adapter_hash = ''
        self.fail_start = False
        self.mutations = []

    def run(self, *args):
        if args[0] == 'push':
            self.adapter_hash = policy.hashlib.sha256(Path(args[1]).read_bytes()).hexdigest()
            self.mutations.append('push')
            return ''
        command = shlex.split(args[1])[4]
        if command == 'getprop ro.hardware': return self.hardware
        if command == 'getprop ro.build.version.sdk': return self.sdk
        if command == 'cat /proc/mounts':
            return f'/dev/block/dm-55 {policy.HAL_DIR} ext4 rw 0 0' if self.mounted else ''
        if command.startswith('stat '): return '1:2\n1:2' if self.owned else '1:2\n1:3'
        if command.startswith('sha256sum '):
            path = command.split()[1]
            stock = path.endswith(policy.ORIGINAL) or (not self.mounted and path == f'{policy.HAL_DIR}/{policy.HAL_NAME}')
            return (self.stock_hash if stock else self.adapter_hash) + '  ' + path
        if command.startswith('ls -Z '):
            return 'u:object_r:vendor_file:s0 ' + policy.HAL_NAME
        if command == 'getprop init.svc.vendor.audio-hal': return 'running'
        if command.startswith('dumpsys '):
            return f'  HAL frame count: {self.frames}\n-   HAL frame count: 768\n'
        self.mutations.append(command)
        if command.startswith('mount --bind '): self.mounted = True
        if command.startswith('umount '): self.mounted = False
        if command == 'setprop ctl.restart vendor.audio-hal':
            self.frames = 960 if self.mounted and not self.fail_start else 1088
        return ''


class AudioPolicyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.library = Path(self.temp.name) / 'adapter.so'
        self.library.write_bytes(b'test adapter')
        self.builder = patch.object(policy, 'build_adapter', return_value=self.library)
        self.builder.start()
        self.addCleanup(self.builder.stop)

    def apply(self, guest):
        return policy.apply_audio_policy(guest.run, Path('sdk'), Path('root'))

    def test_apply_and_repeat_without_restarting_audio(self):
        guest = Guest()
        self.assertEqual(self.apply(guest)['status'], 'applied')
        self.assertTrue(guest.mounted)
        previous = list(guest.mutations)
        self.assertEqual(self.apply(guest)['status'], 'unchanged')
        self.assertEqual(guest.mutations, previous)

    def test_unrecognized_image_and_external_overlay_are_untouched(self):
        for attribute, value in [('hardware', 'physical'), ('sdk', '35'),
                                 ('stock_hash', 'unknown'), ('owned', False)]:
            with self.subTest(attribute=attribute):
                guest = Guest()
                if attribute == 'owned': guest.mounted = True
                setattr(guest, attribute, value)
                self.assertEqual(self.apply(guest)['status'], 'skipped')
                self.assertEqual(guest.mutations, [])

    def test_failed_audio_restart_restores_stock_driver(self):
        guest = Guest()
        guest.fail_start = True
        with patch.object(policy.time, 'sleep'), self.assertRaisesRegex(RuntimeError, '960-frame'):
            self.apply(guest)
        self.assertFalse(guest.mounted)
        self.assertEqual(guest.frames, 1088)
        self.assertIn('umount ' + policy.HAL_DIR, guest.mutations)

    def test_historical_frame_counts_do_not_hide_live_output_failure(self):
        def shell(command):
            return 'running' if command.startswith('getprop ') else '-   HAL frame count: 960\n'
        self.assertFalse(policy.audio_ready(shell, 960))


if __name__ == '__main__': unittest.main()
