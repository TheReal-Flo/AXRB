import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('policy', Path(__file__).resolve().parents[1] / 'tools/unreal_memory_policy.py')
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


if __name__ == '__main__':
    unittest.main()
