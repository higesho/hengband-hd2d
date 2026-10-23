# -*- coding: utf-8 -*-
"""コアが「次の入力を待っている」状態になるまでだけ待つ（固定の待ちの代わり）。

検査器がコアへキーを送ったあと、反応を取り切るために `settle`（静かになるまで最低 1〜3 秒）を使っていた。
ところがコアは 1 キーに 30 ミリ秒ほどで答える（実測 2026-10-22。「待ち方とセーブの決まり」）。
キーごとに 1〜2 秒余計に待っていたことになる。

ここでは次の 2 つの合図だけで「終わった」を決める。

  * **フレームが届いた**（画面が変わった）
  * **コアのプロセスが忙しい**（`QueryProcessCycleTime` の増え方。待機中の 10 ミリ秒ごとの見回りは
    32 ミリ秒あたり約 40 万サイクル、キー 1 つの処理は 300 万〜3000 万サイクル）

終わりの判定は次のとおり。

  1. 合図が 1 度でもあれば、最後の合図から `idle_s`（既定 0.15 秒）だけ何も無かったら終わり。
  2. 合図が 1 度も無ければ（画面が変わらないキー）、`no_response_s`（既定 0.30 秒）で終わり。
  3. どちらでも `cap_s` が上限（長い処理を待つ上限。超えたら打ち切る）。
  4. `until`（画面の状態を見る関数）を渡したときは、それが真になった時点で終わる（期待した表示が出たら次へ）。
  5. マシン全体の CPU が混んでいる（0.4 を超える）ほど、`idle_s`・`no_response_s` を最大 6 倍まで伸ばす（`system_busy`）。直近 8 回のキーへの反応の遅れの中央値が
     大きいときも、その 5 倍まで伸ばす。混んだマシンで短い待ちが「反応が無い」と取り違えるのを防ぐ。
  6. `after_msg=True` のときは、直前に届いたメッセージの時刻を最初の合図に数える（期待のフレームを待ち終えた直後の仕上げ用）。
  7. 検査器が `idle_cpu_signal = False` を持つときは、コアの忙しさを合図に数えない（フレームの到着だけ）。Rogue 系のコアは待機中も 10 ミリ秒ごとに
     画面を組み直す作りで、待機中が約 1 億 7 千万サイクル/秒（ほかのコアの約 14 倍）と、キー 1 つの処理と見分けがつかない（`rl_attack_fx_check.py`）。

サイクル数が読めない環境（Windows 以外・ハンドルが無い）では `None` を返し、呼び出し側が従来の待ち方へ戻る。
環境変数 `HD2D_SETTLE_LEGACY=1` で従来の待ち方へ戻せる（前後の時間の比較用）。
"""
from __future__ import annotations

import ctypes
import os
import sys
import time

#: 1 秒あたりのサイクル数がこれを超えたら「忙しい」。待機中は約 1250 万/秒、キー 1 つの処理は 9000 万/秒以上。
BUSY_CYCLES_PER_S = 60_000_000
IDLE_S = 0.15
NO_RESPONSE_S = 0.30
STEP_S = 0.015


def legacy() -> bool:
    return os.environ.get("HD2D_SETTLE_LEGACY") == "1"


_k32 = None
_load_prev = None   # 直前の GetSystemTimes の値 (idle, total)


class _FT(ctypes.Structure):
    _fields_ = [("lo", ctypes.c_uint32), ("hi", ctypes.c_uint32)]

    def value(self) -> int:
        return (self.hi << 32) | self.lo


def system_busy() -> float | None:
    """マシン全体の CPU の忙しさ（0〜1）。前回の呼び出しからの間を見る。初回・読めないときは None。

    ほかの作業（組み立て・仮想マシンなど）でマシンが混んでいると、コアが走らせてもらえず反応が遅れる。
    そのとき短い待ちで「反応が無い」と決めると取りこぼすので、待ちを伸ばす目安に使う。
    """
    global _load_prev
    if sys.platform != "win32":
        return None
    idle, kern, user = _FT(), _FT(), _FT()
    if not ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kern), ctypes.byref(user)):
        return None
    cur = (idle.value(), kern.value() + user.value())
    prev, _load_prev = _load_prev, cur
    if prev is None or cur[1] <= prev[1]:
        return None
    return max(0.0, min(1.0, 1.0 - (cur[0] - prev[0]) / (cur[1] - prev[1])))


def load_scale(busy: float | None) -> float:
    """待ちの倍率。マシンの忙しさが 0.4 以下なら 1 倍、0.9 以上なら 6 倍（その間は直線）。"""
    if busy is None or busy <= 0.4:
        return 1.0
    return min(6.0, 1.0 + (busy - 0.4) * 10.0)


def _kernel32():
    global _k32
    if _k32 is None and sys.platform == "win32":
        from ctypes import wintypes
        k = ctypes.windll.kernel32
        k.QueryProcessCycleTime.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_ulonglong)]
        k.QueryProcessCycleTime.restype = wintypes.BOOL
        _k32 = k
    return _k32


def cycles(proc) -> int | None:
    """子プロセスが今までに使った CPU サイクル数。読めなければ None。"""
    k = _kernel32()
    handle = getattr(proc, "_handle", None)
    if k is None or handle is None:
        return None
    value = ctypes.c_ulonglong(0)
    if not k.QueryProcessCycleTime(int(handle), ctypes.byref(value)):
        return None
    return int(value.value)


def settle_idle(session, cap_s: float = 25.0, idle_s: float = IDLE_S, no_response_s: float = NO_RESPONSE_S,
                until=None, after_msg: bool = False) -> bool | None:
    t_start = time.monotonic()
    stats = {"frames": 0, "busy_steps": 0, "steps": 0, "scale": 1.0}
    r = _settle_idle(session, cap_s, idle_s, no_response_s, until, after_msg, stats)
    if os.environ.get("HD2D_IDLE_TRACE") == "1":
        dt = time.monotonic() - t_start
        if dt > 1.0:
            import traceback
            fr = [f for f in traceback.extract_stack(limit=6)[:-1] if "idle_wait" not in f.filename and "protocol_driver" not in f.filename]
            where = f"{os.path.basename(fr[-1].filename)}:{fr[-1].lineno}" if fr else "?"
            print(f"[idle-trace] {dt:.1f}s at {where} msgs={stats['frames']} busy_steps={stats['busy_steps']}/{stats['steps']} scale={stats['scale']:.1f}", file=sys.stderr, flush=True)
    return r


def _settle_idle(session, cap_s, idle_s, no_response_s, until, after_msg, stats):
    """`session`（`drain(timeout)` と `proc` を持つ検査器の接続）を、コアが待機に戻るまで読む。

    戻り値: `until` を渡したときは、期待した表示が出たら True・出なければ False。渡さなければ True（上限に達したら False）。
    サイクル数が読めないときは None（呼び出し側が従来の待ち方をする）。
    """
    prev = cycles(session.proc)
    if prev is None:
        return None
    t0 = time.monotonic()
    end = t0 + cap_s
    scale = load_scale(system_busy())            # 前回の呼び出し以降のマシンの混み具合。混んでいれば待ちを伸ばす
    # 直近のキーへの反応の遅れ（最初のメッセージが届くまで）の大きい方も目安にする（混んでいなくてもコアが遅い場合）
    lat = getattr(session, "latency_hist", [])
    if lat:
        # 中央値で見る（長い処理を伴うキー 1 回の遅れで、あとの待ちまで伸ばさない）
        recent = sorted(lat[-8:])
        scale = max(scale, min(6.0, 5.0 * recent[len(recent) // 2] / max(no_response_s, 1e-3)))
    idle_s *= scale
    no_response_s *= scale
    stats["scale"] = scale
    first_t = None
    last_load_t = t0
    last_signal = None
    if after_msg:
        # 直前に届いたメッセージ（呼び出し側が期待のフレームを待って受け取ったもの）も合図に数える。
        # キーを送った直後には使わない（古いメッセージを合図と取り違え、コアの反応を待たずに返ってしまう）。
        last_signal = getattr(session, "last_msg_t", None)
    prev_t = t0
    if until is not None and until(session):
        return True
    while True:
        now = time.monotonic()
        if now >= end:
            return until(session) if until is not None else False
        got = session.drain(STEP_S)
        now = time.monotonic()
        cur = cycles(session.proc)
        busy = False
        if now - last_load_t >= 0.25:                # 待っている間にマシンが混み始めたら、待ちを伸ばす
            new_scale = load_scale(system_busy())
            if new_scale > scale:
                idle_s *= new_scale / scale
                no_response_s *= new_scale / scale
                scale = new_scale
                stats["scale"] = scale
            last_load_t = now
        if cur is not None:
            dt = max(now - prev_t, 1e-3)
            busy = (cur - prev) / dt > BUSY_CYCLES_PER_S and getattr(session, "idle_cpu_signal", True)
            prev = cur
        prev_t = now
        stats["steps"] += 1
        stats["frames"] += got
        if busy:
            stats["busy_steps"] += 1
        if got or busy:
            last_signal = now
            if first_t is None and not after_msg:
                first_t = now
                hist = getattr(session, "latency_hist", None)
                if hist is None:
                    hist = session.latency_hist = []
                hist.append(now - t0)
                del hist[:-32]
        if until is not None and until(session):
            return True
        if until is not None:
            # 期待の表示を待っている間は、合図が途切れても上限まで待つ（画面が変わるのを待つ）。
            continue
        if last_signal is not None:
            if now - last_signal >= idle_s:
                return True
        elif now - t0 >= no_response_s:
            return True
