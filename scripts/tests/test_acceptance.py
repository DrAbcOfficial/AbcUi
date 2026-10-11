import importlib.util
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('acceptance_support', Path(__file__).resolve().parents[1] / 'acceptance_support.py')
support = importlib.util.module_from_spec(spec)
spec.loader.exec_module(support)


class AcceptanceTests(unittest.TestCase):
    def test_exit_requires_zero_and_no_kill(self):
        proc = SimpleNamespace(alive=False)
        for code, killed, expected in [(0, False, True), (1, False, False), (-1073741819, False, False), (0, True, False)]:
            self.assertEqual(support.clean_exit(proc, SimpleNamespace(exit_code=code, killed=killed)), expected)
        self.assertFalse(support.clean_exit(SimpleNamespace(alive=True), SimpleNamespace(exit_code=0, killed=False)))

    def test_skip_is_not_pass(self):
        check = support.Check()
        check.skip('manual', 'not performed')
        self.assertEqual(check.count, 0)
        self.assertEqual(check.skipped, ['manual'])

    def test_old_screenshot_is_not_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            old = Path(directory) / 'old.png'
            old.write_bytes(b'old')
            self.assertIsNone(support.latest_screenshot(directory, {'old.png'}))
            new = Path(directory) / 'new.png'
            new.write_bytes(b'new')
            self.assertEqual(Path(support.latest_screenshot(directory, {'old.png'})), new)

    def test_connection_closed_when_stop_raises(self):
        calls = []
        def stop(**kwargs):
            raise RuntimeError('stop failed')
        proc = SimpleNamespace(alive=True, stop=stop)
        rcon = SimpleNamespace(close=lambda: calls.append('close'))
        with self.assertRaises(RuntimeError):
            support.cleanup_session(proc, rcon)
        self.assertEqual(calls, ['close'])

    def test_dead_process_still_closes_connection(self):
        calls = []
        support.cleanup_session(SimpleNamespace(alive=False), SimpleNamespace(close=lambda: calls.append('close')))
        self.assertEqual(calls, ['close'])


if __name__ == '__main__':
    unittest.main()
