import importlib.util
from pathlib import Path
import unittest
import argparse
import base64
import json
import tempfile
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('policy', Path(__file__).resolve().parents[2] / 'scripts/emulator/unreal_memory_policy.py')
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)


class PolicyTests(unittest.TestCase):
    def test_budget_leaves_headroom(self):
        for capacity in (2048, 3072, 4096, 8192, 16303, 24564):
            pool = policy.pool_mib(capacity)
            self.assertLessEqual(pool, capacity // 2)
            self.assertGreaterEqual(capacity - pool, 2048)
            self.assertEqual(pool % 256, 0)
        self.assertEqual(policy.pool_mib(16303), 7936)

    def test_preserves_user_settings_and_overrides_old_force_load(self):
        original = '[SystemSettings]\nr.Streaming.FullyLoadUsedTextures=1\ncustom=7\n[Other]\nx=2\n'
        changed = policy.configure(original, 7936, 2048)
        self.assertTrue(changed.startswith(original))
        self.assertIn('r.Streaming.FullyLoadUsedTextures=0', changed)
        self.assertIn('r.Streaming.PoolSize=7936', changed)
        self.assertEqual(changed.count(policy.BEGIN), 1)
        self.assertIn('PoolSizeVRAMPercentage=387', changed)

    def test_unknown_managed_block_is_not_overwritten(self):
        with self.assertRaises(ValueError):
            policy.configure(policy.BEGIN + '\nuser edit', 4096, 2048)

    def test_percentage_rounds_down_and_handles_uncapped_heaps(self):
        for host, guest in ((16303, 2048), (8192, 2048), (16303, 16384), (24564, 24576)):
            target = policy.pool_mib(host)
            percentage = policy.pool_percentage(target, guest)
            effective = guest * percentage // 100
            self.assertLessEqual(effective, target)
            self.assertLess(target - effective, guest / 100 + 1)
        with self.assertRaises(ValueError):
            policy.pool_percentage(4096, 0)


class AdapterTests(unittest.TestCase):
    def fixtures(self, vendor=0x1002, capacity=8192):
        guest = {'properties': {'vendorID': vendor, 'deviceID': 123, 'deviceType': 2},
                 'memory': {'memoryHeaps': [{'size': '0x80000000', 'flags': 1}]}}
        host = {'vendor_id': vendor, 'device_id': 123, 'flags': 0,
                'dedicated_video_bytes': capacity * 1024**2, 'shared_system_bytes': 32 * 1024**3}
        return guest, host

    def test_amd_and_nvidia_match_correct_adapter(self):
        for vendor in (0x1002, 0x10de):
            guest, host = self.fixtures(vendor)
            other = {**host, 'vendor_id': 0x10de if vendor == 0x1002 else 0x1002,
                     'dedicated_video_bytes': 24 * 1024**3}
            self.assertEqual(policy.memory_budget([guest], [other, host]), (8192, 4096, 2048))

    def test_shared_ram_does_not_inflate_integrated_vram(self):
        guest, host = self.fixtures(capacity=512)
        guest['properties']['deviceType'] = 1
        with self.assertRaisesRegex(ValueError, 'dedicated VRAM'):
            policy.memory_budget([guest], [host])

    def test_ambiguous_software_and_mismatched_adapters(self):
        guest, host = self.fixtures()
        for adapters in ([], [host, host], [{**host, 'flags': 2}], [{**host, 'device_id': 999}]):
            with self.assertRaises(ValueError):
                policy.memory_budget([guest], adapters)
        guest['properties']['deviceType'] = 4
        with self.assertRaises(ValueError):
            policy.memory_budget([guest], [host])

    def test_failed_query_restores_previous_budget_without_losing_settings(self):
        with tempfile.TemporaryDirectory() as directory:
            args = argparse.Namespace(package='com.example.game', serial='emulator-5584',
                                      sdk=Path('sdk'), state_dir=Path(directory), restore=False)
            original = '[Audio]\nVolume=0.5\n'
            current = policy.configure(original, 7936, 2048)
            state_path = Path(directory) / 'emulator-5584-com.example.game.json'
            state_path.write_text(json.dumps({'path': '/config/Engine.ini', 'original': original,
                                             'applied_sha256': policy.digest(current)}))
            writes = []
            def command(argv, **kwargs):
                cmd = str(argv[-1])
                if 'dumpsys package' in cmd: return 'nativeLibraryDir=/lib'
                if 'test -d' in cmd: return 'yes'
                if 'find /lib' in cmd: return '/lib/libUnreal.so'
                if 'cmd gpu vkjson' in cmd: return '{"devices": []}'
                if 'base64 /config' in cmd: return base64.b64encode((original if writes else current).encode()).decode()
                if 'pidof' in cmd: return ''
                if 'stat -c' in cmd: return '10001'
                if 'umask 077' in cmd: writes.append(cmd); return ''
                raise AssertionError(cmd)
            with patch.object(policy, 'run', side_effect=command), patch.object(policy, 'host_adapters', side_effect=OSError('DXGI unavailable')):
                self.assertEqual(policy.apply(args)['status'], 'restored')
            self.assertFalse(state_path.exists())
            self.assertEqual(len(writes), 1)


if __name__ == '__main__':
    unittest.main()
