"""検査用の状態退避と、検査自身が起動するプロセスの管理。"""
from __future__ import annotations

import ctypes
from ctypes import wintypes as wt
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import uuid


class SaveGuard:
    """元のディレクトリを丸ごと退避し、準備中の例外でも元へ戻す。"""

    def __init__(self, save_dir, fixtures, fixture):
        self.path = Path(save_dir).absolute()
        self.source = Path(fixtures) / fixture if fixture else None
        self.stash = None
        self.existed = False

    def __enter__(self):
        if self.source is not None and not self.source.is_file():
            raise FileNotFoundError(f'固定セーブが無い: {self.source}')
        # 同じ親に退避して rename を使う。部分的なファイル移動を避ける。
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.stash = Path(tempfile.mkdtemp(prefix='.hd2d-save-', dir=self.path.parent))
        self.existed = self.path.exists()
        try:
            if self.existed:
                self.path.rename(self.stash / 'original')
        except BaseException:
            self.stash.rmdir()
            self.stash = None
            raise
        try:
            self.path.mkdir()
            if self.source is not None:
                shutil.copy2(self.source, self.path / self.source.name)
                panels = self.source.with_name(self.source.name + '.sdl2panels')
                if panels.is_file():
                    shutil.copy2(panels, self.path / panels.name)
        except BaseException:
            self.__exit__(None, None, None)
            raise
        return self

    def __exit__(self, *exc):
        if self.stash is None:
            return False
        # path は検査が作ったディレクトリ。復元に失敗した場合、退避先は消さない。
        if self.path.exists():
            if self.path.is_symlink() or self.path.resolve() != self.path:
                raise RuntimeError(f"検査用セーブの削除先が変わりました。退避先: {self.stash}")
            shutil.rmtree(self.path)
        if self.existed:
            (self.stash / 'original').rename(self.path)
        self.stash.rmdir()
        self.stash = None
        return False


class CfgGuard:
    """既存の設定を復元し、検査が新しく作った設定も取り除く。"""

    def __init__(self, root):
        self.root = Path(root)

    def __enter__(self):
        self.saved = {p.name: p.read_bytes() for p in self.root.glob('*.cfg') if p.is_file()}
        return self

    def __exit__(self, *exc):
        for p in self.root.glob('*.cfg'):
            if p.is_file() and p.name not in self.saved:
                p.unlink()
        for name, data in self.saved.items():
            p = self.root / name
            if not p.is_file() or p.read_bytes() != data:
                p.write_bytes(data)
        return False


def _kernel():
    k = ctypes.WinDLL('kernel32', use_last_error=True)
    for name, restype, argtypes in (
        ('CreateJobObjectW', wt.HANDLE, [ctypes.c_void_p, wt.LPCWSTR]),
        ('OpenJobObjectW', wt.HANDLE, [wt.DWORD, wt.BOOL, wt.LPCWSTR]),
        ('AssignProcessToJobObject', wt.BOOL, [wt.HANDLE, wt.HANDLE]),
        ('TerminateJobObject', wt.BOOL, [wt.HANDLE, wt.UINT]),
        ('CloseHandle', wt.BOOL, [wt.HANDLE]),
        ('GetCurrentProcess', wt.HANDLE, []),
        ('SetInformationJobObject', wt.BOOL, [wt.HANDLE, ctypes.c_int, ctypes.c_void_p, wt.DWORD]),
    ):
        f = getattr(k, name)
        f.restype, f.argtypes = restype, argtypes
    return k


def _checked(value):
    if not value:
        raise ctypes.WinError(ctypes.get_last_error())
    return value


class _BasicLimit(ctypes.Structure):
    _fields_ = [('process_time', ctypes.c_int64), ('job_time', ctypes.c_int64),
                ('flags', wt.DWORD), ('min_ws', ctypes.c_size_t), ('max_ws', ctypes.c_size_t),
                ('active', wt.DWORD), ('affinity', ctypes.c_size_t),
                ('priority', wt.DWORD), ('scheduling', wt.DWORD)]


class _ExtendedLimit(ctypes.Structure):
    _fields_ = [('basic', _BasicLimit), ('io', ctypes.c_uint64 * 6),
                ('process_mem', ctypes.c_size_t), ('job_mem', ctypes.c_size_t),
                ('peak_process', ctypes.c_size_t), ('peak_job', ctypes.c_size_t)]


def run_process(argv, *, cwd, env, timeout):
    """専用 Job 内で実行する。戻る前に子孫も終了し、出力を回収する。

    子の Python が Job に参加してからゲームを起動するので、起動と Job 登録の
    間にコアだけが管理外へ出る競合がない。名前でプロセスを検索・終了しない。
    """
    if os.name != 'nt':
        raise RuntimeError('この実行体検査は Windows 専用です')
    k = _kernel()
    name = 'Local\\hd2d-verify-' + uuid.uuid4().hex
    job = _checked(k.CreateJobObjectW(None, name))
    proc = None
    try:
        limits = _ExtendedLimit()
        limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        _checked(k.SetInformationJobObject(job, 9, ctypes.byref(limits), ctypes.sizeof(limits)))
        proc = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), '--child', name, *argv],
                                cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            raw, _ = proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            _checked(k.TerminateJobObject(job, 1))
            proc.kill()  # Job 登録前に止まった補助プロセスも自身のハンドルで終了する。
            raw, _ = proc.communicate()
            raise subprocess.TimeoutExpired(argv, timeout, output=raw)
        return subprocess.CompletedProcess(argv, proc.returncode, raw)
    finally:
        k.CloseHandle(job)
        if proc is not None and proc.poll() is None:
            proc.kill()
            proc.communicate()


def _child(name, argv):
    k = _kernel()
    job = _checked(k.OpenJobObjectW(0x0001, False, name))  # JOB_OBJECT_ASSIGN_PROCESS
    try:
        _checked(k.AssignProcessToJobObject(job, k.GetCurrentProcess()))
    finally:
        k.CloseHandle(job)  # 親だけが Job の寿命を持つ。
    return subprocess.call(argv, stdin=subprocess.DEVNULL, stdout=sys.stdout, stderr=sys.stderr)


if __name__ == '__main__':
    if len(sys.argv) < 4 or sys.argv[1] != '--child':
        raise SystemExit('検査プロセス専用の入口です')
    raise SystemExit(_child(sys.argv[2], sys.argv[3:]))
