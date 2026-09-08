"""実セーブ・ゲームを使わず、検査基盤の失敗時の保護を確認する。"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from runtime import CfgGuard, SaveGuard, run_process


class RuntimeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='hd2d-runtime-test-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.save = self.root / 'save'
        self.save.mkdir()
        (self.save / 'player').write_bytes(b'original')
        (self.save / 'sub').mkdir()
        (self.save / 'sub' / 'other').write_bytes(b'nested')
        self.fixtures = self.root / 'fixtures'
        self.fixtures.mkdir()
        (self.fixtures / 'DBG').write_bytes(b'fixture')

    def assert_original(self):
        self.assertEqual((self.save / 'player').read_bytes(), b'original')
        self.assertEqual((self.save / 'sub' / 'other').read_bytes(), b'nested')
        self.assertFalse((self.save / 'DBG').exists())

    def test_missing_fixture_does_not_move_save(self):
        with self.assertRaises(FileNotFoundError):
            with SaveGuard(self.save, self.fixtures, 'missing'):
                self.fail('entered')
        self.assert_original()

    def test_copy_failure_restores_save(self):
        with patch('runtime.shutil.copy2', side_effect=OSError('copy failed')):
            with self.assertRaises(OSError):
                with SaveGuard(self.save, self.fixtures, 'DBG'):
                    self.fail('entered')
        self.assert_original()

    def test_failure_in_body_restores_entire_directory(self):
        with self.assertRaises(RuntimeError):
            with SaveGuard(self.save, self.fixtures, 'DBG'):
                self.assertFalse((self.save / 'player').exists())
                (self.save / 'newdir').mkdir()
                raise RuntimeError('game failed')
        self.assert_original()
        self.assertFalse((self.save / 'newdir').exists())

    def test_new_save_directory_is_removed(self):
        path = self.root / 'new-save'
        with SaveGuard(path, self.fixtures, None):
            (path / 'new').write_bytes(b'new')
        self.assertFalse(path.exists())

    def test_settings_restore_and_remove_new_files(self):
        (self.root / 'hd2d.cfg').write_bytes(b'original')
        with CfgGuard(self.root):
            (self.root / 'hd2d.cfg').write_bytes(b'changed')
            (self.root / 'new-core.cfg').write_bytes(b'new')
        self.assertEqual((self.root / 'hd2d.cfg').read_bytes(), b'original')
        self.assertFalse((self.root / 'new-core.cfg').exists())

    def test_observation_keeps_each_direction_order_and_payload(self):
        from playthrough import comparable_observation as compare
        a = '  {"dir":"in","t":"frame","payload":"a"}'
        b = '  {"dir":"out","t":"input","payload":"b"}'
        c = '  {"dir":"out","t":"input","payload":"c"}'
        self.assertEqual(compare('\n'.join([a,b,c])), compare('\n'.join([b,a,c])))
        self.assertNotEqual(compare('\n'.join([a,b,c])), compare('\n'.join([a,c,b])))
        self.assertNotEqual(compare('\n'.join([a,b,c])), compare('\n'.join([a,b])))

    @unittest.skipUnless(os.name == 'nt', 'Windows Job')
    def test_process_exit_and_output(self):
        result = run_process([sys.executable, '-c', 'print("output"); raise SystemExit(7)'],
                             cwd=self.root, env=os.environ.copy(), timeout=10)
        self.assertEqual(result.returncode, 7)
        self.assertIn(b'output', result.stdout)

    @unittest.skipUnless(os.name == 'nt', 'Windows Job')
    def test_timeout_kills_descendants_but_not_unrelated_process(self):
        unrelated = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'],
                                     creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            code = ('import subprocess,sys,time; '
                    'p=subprocess.Popen([sys.executable,"-c","import time; time.sleep(30)"]); '
                    'print(p.pid,flush=True); time.sleep(30)')
            with self.assertRaises(subprocess.TimeoutExpired) as caught:
                run_process([sys.executable, '-c', code], cwd=self.root,
                            env=os.environ.copy(), timeout=2)
            self.assertIsNone(unrelated.poll())
            pid = int(caught.exception.output.strip())
            import ctypes
            from ctypes import wintypes as wt
            k = ctypes.WinDLL('kernel32', use_last_error=True)
            k.OpenProcess.restype = wt.HANDLE
            k.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
            k.GetExitCodeProcess.argtypes = [wt.HANDLE, ctypes.POINTER(wt.DWORD)]
            k.CloseHandle.argtypes = [wt.HANDLE]
            h = k.OpenProcess(0x1000, False, pid)
            if h:
                try:
                    exit_code = wt.DWORD()
                    self.assertTrue(k.GetExitCodeProcess(h, ctypes.byref(exit_code)))
                    self.assertNotEqual(exit_code.value, 259)  # STILL_ACTIVE
                finally:
                    k.CloseHandle(h)
        finally:
            unrelated.kill()
            unrelated.wait()


if __name__ == '__main__':
    unittest.main()
