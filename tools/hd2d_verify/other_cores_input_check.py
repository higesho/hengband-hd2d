#!/usr/bin/env python3
"""Angband 向けの入力・選択肢の変更（共通の狙う選択器・反復回数の接頭辞）が、Angband 以外のコアを
壊していないかを、実コアを stdio v1 で駆動して確かめる。

  targeting … 投げる（v / t）の狙いの選択器を開き、`targeting` に `extras` が載らないこと、
              取消・決定が従来どおり働くこと、余計な `aim_extra`／生の `<` `>` `x` `p` `m` `g` `k` が
              何も起こさないことを見る。モンスターが見えていれば候補が出ること（狙う対象があるとき）も報告する。
  repeat    … 反復回数（`0` → 数字 → 退格 → 数字 → Enter → コマンド）が指定回数だけ繰り返されるか。
              時計（`lighting.day_minute`）か位置で測る。窓の経路（`set_number`）と生キーの経路の両方。
  click     … 地図のクリック（`input_event` の `map_click`。AH-57。`hello_ack.features` に `map_click` を
              申告する Angband だけへ送る）が、申告しないコアへ迷い込んでも何も起こさないこと
              （位置も時計も動かず、壊れた `map_click` も落とさず、続くキーが従来どおり効く）。

    python tools/hd2d_verify/other_cores_input_check.py targeting heng,tang,genso,frox,silq
    python tools/hd2d_verify/other_cores_input_check.py repeat all
    python tools/hd2d_verify/other_cores_input_check.py click all

作業フォルダのセーブは写しで、開発ツリーのセーブには触らない（inventory_actions_audit と同じ起こし方）。
終了コード 0 = 失敗なし（未確認は失敗にしない）。
"""
from __future__ import annotations

import argparse
import json
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import inventory_actions_audit as ia  # noqa: E402

ESC = 27
ENTER = 13
BACKSPACE = 0x7F

# 投げる操作の鍵。item は持ち物の選び方（Enter = 先頭の行を選ぶ窓、それ以外は文字）。
THROW = {
    "heng": ("v", [ENTER]),
    "tang": ("v", [ENTER]),
    "genso": ("v", [ENTER]),
    "frox": ("v", [ord("b")]),
    "silq": ("t", [ord("a")]),
}


def targeting_of(frame: dict) -> dict:
    return frame.get("targeting") or {}


def msgs(frame: dict, n: int = 3) -> list[str]:
    return [m.get("text_utf8", "") for m in (frame.get("messages") or [])][-n:]


class Rec:
    def __init__(self) -> None:
        self.rows: list[tuple[str, str, str, str]] = []

    def add(self, core: str, item: str, status: str, detail: str = "") -> None:
        self.rows.append((core, item, status, detail))
        print(f"  [{status}] {core:8} {item:34} {detail}", flush=True)

    @property
    def failed(self) -> int:
        return sum(1 for r in self.rows if r[2] == "FAIL")


def start(name: str, work: Path) -> ia.Core:
    spec = ia.CORES[name]
    root = work / f"root-{name}"
    root.mkdir(parents=True, exist_ok=True)
    ia.build_root(spec, root)
    return ia.boot(spec, root, work / f"{name}.err.log")


def ui_action(c: ia.Core, action: str, value: int = 0, settle: float = 0.6) -> None:
    c.rid += 1
    rev = targeting_of(c.f).get("revision") or c.m.get("revision") or 1
    c.send({"t": "input_event", "events": [{"e": "ui_action", "request_id": c.rid, "revision": int(rev),
                                            "target": "", "action": action, "value": value}]})
    c.settle(settle, 8)


def until(c: ia.Core, pred, timeout: float = 3.0) -> bool:
    """条件が成り立つフレームが届くまで（最大 timeout 秒）受け取る。固定の待ちより速い。"""
    end = time.monotonic() + timeout
    while time.monotonic() < end and not c.eof:
        if pred(c.f):
            return True
        c.drain(0.05)
    return bool(pred(c.f))


def open_aim(c: ia.Core, name: str) -> bool:
    key, pick = THROW[name]
    c.keys(*[ord(x) for x in key])
    until(c, lambda f: bool(f.get("menu_open")) or bool(targeting_of(f).get("active")), 3.0)
    c.settle(0.15, 1)
    c.keys(*pick)
    until(c, lambda f: bool(targeting_of(f).get("active")), 3.0)
    c.settle(0.15, 1)
    return bool(targeting_of(c.f).get("active"))


def cancel_out(c: ia.Core) -> None:
    for _ in range(4):
        if ia.in_game(c.f):
            return
        c.keys(ESC)
        c.settle(0.5, 6)


def check_targeting(name: str, work: Path, rec: Rec) -> None:
    try:
        c = start(name, work)
    except Exception as exc:  # noqa: BLE001
        rec.add(name, "boot", "FAIL", str(exc))
        return
    try:
        # モンスターを探す（町を歩く。最大 12 回。見つからなければ地面の点で続ける）。
        seen = 0
        for attempt in range(60):
            if not open_aim(c, name):
                rec.add(name, "aim picker opens", "FAIL", f"targeting not active; msgs={msgs(c.f)}")
                return
            seen = len(targeting_of(c.f).get("candidates") or [])
            if seen:
                break
            ui_action(c, "aim_cancel", settle=0.15)
            until(c, lambda f: not targeting_of(f).get("active"), 2.0)
            cancel_out(c)
            # 壁に当たり続けないよう、毎回 4 方向へ少しずつ歩き、最後に待つ。
            for d in "8624" if attempt % 2 == 0 else "2468":
                c.keys(*[ord(d)] * 3)
                c.settle(0.15, 2)
            c.keys(*[ord("s")] * 4)
            until(c, ia.in_game, 3.0)
            c.settle(0.3, 3)
        if not targeting_of(c.f).get("active") and not open_aim(c, name):
            rec.add(name, "aim picker opens", "FAIL", "could not reopen")
            return
        t = targeting_of(c.f)
        rec.add(name, "aim picker opens", "PASS", f"mode={t.get('mode')} candidates={seen}"
                + ("" if seen else " (no monster in view: ground point)"))
        rec.add(name, "targeting.extras absent", "PASS" if "extras" not in t else "FAIL",
                f"targeting keys={sorted(t)}" if "extras" in t else "no 'extras' key")
        # 余計な操作は何も起こさない。
        before = (t.get("selected"), t.get("active"))
        ui_action(c, "aim_extra", ord("<"))
        c.keys(*[ord(x) for x in "<>xpmgk"])
        c.settle(0.6, 8)
        t2 = targeting_of(c.f)
        ok = bool(t2.get("active")) and "extras" not in t2
        rec.add(name, "aim_extra / raw < > x p m g k ignored", "PASS" if ok else "FAIL",
                f"before={before} after={(t2.get('selected'), t2.get('active'))}")
        # 取消（窓の命令）
        ui_action(c, "aim_cancel")
        cancel_out(c) if targeting_of(c.f).get("active") else None
        rec.add(name, "aim_cancel closes", "PASS" if not targeting_of(c.f).get("active") and ia.in_game(c.f) else "FAIL",
                f"in_game={ia.in_game(c.f)} msgs={msgs(c.f, 1)}")
        # 取消（生キー ESC）
        open_aim(c, name)
        c.keys(ESC)
        c.settle(0.8, 8)
        rec.add(name, "raw ESC closes", "PASS" if not targeting_of(c.f).get("active") else "FAIL", "")
        cancel_out(c)
        # 決定（窓の命令）: 投げる
        open_aim(c, name)
        can = bool(targeting_of(c.f).get("can_confirm"))
        ui_action(c, "aim_confirm", settle=1.0)
        if not targeting_of(c.f).get("active"):
            cancel_out(c)  # 命中の -続く- を閉じる
        done = not targeting_of(c.f).get("active") and ia.in_game(c.f)
        if not can:
            # 狙う対象が無く、コアが決定を受け付けない（can_confirm 偽）。この場合は閉じないのが正しい。
            rec.add(name, "aim_confirm executes", "未確認", f"can_confirm=False (no target in view); picker stays open={not done}")
        else:
            rec.add(name, "aim_confirm executes", "PASS" if done else "FAIL", f"msgs={msgs(c.f, 2)}")
        cancel_out(c)
        # 決定（生キー Enter）
        opened = open_aim(c, name)
        if not opened:
            rec.add(name, "raw Enter executes", "未確認", "picker did not reopen (nothing left to throw?)")
        elif not targeting_of(c.f).get("can_confirm"):
            rec.add(name, "raw Enter executes", "未確認", "can_confirm=False (no target in view)")
        else:
            c.keys(ENTER)
            c.settle(1.0, 8)
            if not targeting_of(c.f).get("active"):
                cancel_out(c)  # 近くの敵の反撃で -続く- が出ることがある
            done = not targeting_of(c.f).get("active") and ia.in_game(c.f)
            rec.add(name, "raw Enter executes", "PASS" if done else "FAIL", f"msgs={msgs(c.f, 2)}")
        cancel_out(c)
    finally:
        c.close()


def clock(frame: dict):
    m = (frame.get("lighting") or {}).get("day_minute")
    return m


def pos(frame: dict):
    return (frame.get("player_gx"), frame.get("player_gy"))


def find_count_entry(c: ia.Core, pads: list[dict]) -> int | None:
    for p in reversed(pads):
        for e in p.get("entries", []):
            if "繰り返し回数" in (e.get("label_utf8") or "") or "repeat count" in (e.get("label_utf8") or "").lower():
                return int((e.get("seq_original") or [48])[0])
    return None


REPEAT_CMD = {
    # 時計の進みで測るコア: 1 回で進む量に対し、指定回数ぶん進むか。
    "heng": ord(","), "tang": ord(","), "genso": ord(","), "frox": ord(","),
}


def check_repeat(name: str, work: Path, rec: Rec) -> None:
    spec = ia.CORES[name]
    root = work / f"root-{name}"
    root.mkdir(parents=True, exist_ok=True)
    ia.build_root(spec, root)
    pads: list[dict] = []
    orig = ia.Core.drain

    def drain(self, seconds):  # pad_commands を拾う
        end = time.monotonic() + seconds
        seen = 0
        import queue
        while True:
            left = end - time.monotonic()
            if left <= 0:
                return seen
            try:
                m = self.q.get(timeout=min(left, 0.1))
            except queue.Empty:
                continue
            if m is None:
                self.eof = True
                return seen
            seen += 1
            if m.get("t") == "pad_commands":
                pads.append(m)
            if m.get("t") == "frame":
                self.frames.append(m)

    ia.Core.drain = drain  # type: ignore[assignment]
    try:
        try:
            c = ia.boot(spec, root, work / f"{name}.err.log")
        except Exception as exc:  # noqa: BLE001
            rec.add(name, "boot", "FAIL", str(exc))
            return
        try:
            entry = find_count_entry(c, pads)
            if entry is None:
                rec.add(name, "repeat count command exists", "未確認",
                        "pad_commands has no repeat-count entry (core has no such command)")
                return
            measured_by_clock = clock(c.f) is not None
            cmd = REPEAT_CMD.get(name)
            if name.startswith("rogue"):
                # 2 つの経路は別の起動で見る（1 つめの移動が残す -More- などに 2 つめが巻き込まれないように）。
                c.close()
                for path in ("raw", "window"):
                    c = ia.boot(spec, root, work / f"{name}-{path}.err.log")
                    try:
                        check_repeat_rogue(c, name, entry, rec, path)
                    finally:
                        c.close()
                return
            # 基準: 単発の待機 30 回ぶんの時計の進み（1 回では時計の分解能に届かない）
            ref = None
            if measured_by_clock and cmd:
                t0 = clock(c.f)
                for _ in range(30):
                    c.keys(cmd)
                    c.settle(0.15, 3)
                c.settle(0.6, 6)
                ref = (clock(c.f) or 0) - (t0 or 0)
            c.keys(entry); c.settle(0.8, 8)
            num = c.f.get("numeric") or {}
            rec.add(name, "count window opens", "PASS" if num.get("active") else "FAIL",
                    f"prompt={num.get('prompt_utf8')!r} max={num.get('max')}")
            if not num.get("active"):
                return
            # 生キー: 3, 1, 退格, 0, Enter -> 30（退格で 1 桁消えること）
            vals = []
            for k in (ord("3"), ord("1"), BACKSPACE, ord("0")):
                c.keys(k); c.settle(0.4, 4)
                vals.append((c.f.get("numeric") or {}).get("value"))
            rec.add(name, "digits/backspace update value", "PASS" if vals == [3, 31, 3, 30] else "FAIL",
                    f"3,1,BS,0 -> {vals}")
            # 原作は数字のあとに打ったコマンドのキーで回数を確定する（Enter は変愚系では取消になる）。
            def reopen_raw():
                cancel_out(c)
                c.keys(entry); c.settle(0.8, 8)
                for k in (ord("3"), ord("0")):
                    c.keys(k); c.settle(0.4, 4)

            def reopen_window():
                cancel_out(c)
                c.keys(entry); c.settle(0.8, 8)
                c.send({"t": "input_event", "events": [{"e": "set_number", "value": 30, "digits": 1}]})
                c.settle(0.6, 6)

            def measure(label, reopen):
                """繰り返しの途中で町の住民が視界に入ると中断され、回数 30 の測定が外れる（実行のたびに結果が
                変わっていた）。外れたときだけ、窓を開き直して最大 3 回まで測り直す。構造の異常（窓が開かない・
                数字が入らない）は、ここに来る前の項目で FAIL になる。"""
                if not (measured_by_clock and cmd):
                    rec.add(name, label, "未確認", "no clock")
                    return
                tries = []
                ok = False
                for attempt in range(3):
                    if attempt:
                        reopen()
                    t1 = clock(c.f)
                    c.keys(cmd); c.settle(2.0, 15)
                    gained = (clock(c.f) or 0) - (t1 or 0)
                    tries.append(gained)
                    ok = bool(ref) and 0.6 * ref <= gained <= 1.6 * ref + 1  # 時計は 1 分刻みなので +1 は丸め
                    if ok:
                        break
                rec.add(name, label, "PASS" if ok else "FAIL",
                        f"ref(30 single holds)={ref} gained={tries[0] if len(tries) == 1 else tries}")
            measure("raw path: count 30 repeats 30 times", reopen_raw)
            cancel_out(c)
            # 窓の経路: set_number（窓の決定は DEL x5 + 数字）
            cancel_out(c)
            c.keys(entry); c.settle(0.8, 8)
            rec.add(name, "window reopens", "PASS" if (c.f.get("numeric") or {}).get("active") else "FAIL",
                    f"numeric={c.f.get('numeric')}")
            c.send({"t": "input_event", "events": [{"e": "set_number", "value": 30, "digits": 1}]})
            c.settle(0.6, 6)
            v = (c.f.get("numeric") or {}).get("value")
            rec.add(name, "window path: set_number shows 30", "PASS" if v == 30 else "FAIL", f"value={v}")
            measure("window path: count 30 repeats 30 times", reopen_window)
            cancel_out(c)
        finally:
            c.close()
    finally:
        ia.Core.drain = orig  # type: ignore[assignment]


def check_repeat_rogue(c: ia.Core, name: str, entry: int, rec: Rec, path: str) -> None:
    """Rogue 系は時計が無い。回数 5 のあとに移動キーを打ち、位置が 2 マス以上動くかで見る（窓・生キーの両経路）。"""
    cancel_out(c)
    for _once in (path,):
        moved_any = False
        seen = ""
        allseen = []
        for key in (".",) if name == "rogue2" else "lhjk":
            c.keys(entry); c.settle(0.8, 8)
            num = c.f.get("numeric") or {}
            if not num.get("active"):
                seen = f"window did not open (numeric={num}) frame_aw={c.f.get('awaiting_command')}"
                cancel_out(c)
                continue
            if path == "raw":
                c.keys(ord("5")); c.settle(0.4, 4)
                v = (c.f.get("numeric") or {}).get("value")
            else:
                c.send({"t": "input_event", "events": [{"e": "set_number", "value": 5, "digits": 1}]})
                c.settle(0.6, 6)
                v = (c.f.get("numeric") or {}).get("value")
            before = pos(c.f)
            c.keys(ord(key)); c.settle(1.5, 10)
            after = pos(c.f)
            d = abs((after[0] or 0) - (before[0] or 0)) + abs((after[1] or 0) - (before[1] or 0))
            seen = f"value={v} dir={key} moved={d}"
            allseen.append(f"{key}:{d}")
            cancel_out(c)
            if name == "rogue2":
                # RogueClone2 の回数は '.'（休む）と 's'（探す）だけに効き、移動には効かない（原作 play.c）。
                # 時計が無く実測できないので、窓が値を受けて閉じ、コマンドへ戻れたことだけを見る。
                moved_any = v == 5 and bool(c.f.get("awaiting_command"))
                seen += " (repetition not measured: no clock)"
                break
            if d >= 2:
                moved_any = True
                break
        rec.add(name, f"{path} path: count 5 then a move", "PASS" if moved_any else "FAIL", seen + " all=" + ",".join(allseen))


def check_click(name: str, work: Path, rec: Rec) -> None:
    """`map_click`（AH-57）を、申告していないコアへ送っても何も起きない。壊れた形でも落ちない。"""
    try:
        c = start(name, work)
    except Exception as exc:  # noqa: BLE001
        rec.add(name, "boot", "FAIL", str(exc))
        return
    try:
        c.settle(0.6, 4)
        p0, t0 = pos(c.f), clock(c.f)
        frames0 = len(c.frames)
        events = [
            {"e": "map_click", "x": 5, "y": 5, "button": "left"},
            {"e": "map_click", "x": 9, "y": 9, "button": "right", "mods": ["shift", "ctrl"]},
            {"e": "map_click", "x": 3, "button": "left"},                  # y が無い（壊れている）
            {"e": "map_click", "x": 300, "y": 3, "button": "left"},        # 範囲外
            {"e": "map_click", "x": 4, "y": 4, "button": "middle"},        # 知らないボタン
            {"e": "map_click", "x": 4, "y": 4, "button": "left", "mods": ["meta"]},
            {"e": "map_teleport", "x": 4, "y": 4},                         # 誰も知らない種類
        ]
        c.send({"t": "input_event", "events": events})
        c.settle(0.8, 6)
        alive = not c.eof and c.proc.poll() is None
        same = pos(c.f) == p0 and clock(c.f) == t0 and ia.in_game(c.f)
        rec.add(name, "map_click ignored (no move, no time)", "PASS" if alive and same else "FAIL",
                f"alive={alive} pos {p0}->{pos(c.f)} clock {t0}->{clock(c.f)} in_game={ia.in_game(c.f)} frames+={len(c.frames) - frames0}")
        # 続くキーは従来どおり効く。時計が動くコア（変愚系・幻想・Frox）は待つ。動かないコアは歩く
        # （Rogue 系は hjkl、Sil-Q は数字。Sil-Q の `l` は見る（目標選び）を開いてしまい、以降のキーを
        # 「目標選びでは使えない」と断る）。壁に当たっても次の方向を試す。
        moved = False
        detail = ""
        walk = "lhjk." if name.startswith("rogue") else "6428"
        for key in ",s." + walk if clock(c.f) else walk:
            b = (pos(c.f), clock(c.f))
            for _ in range(6):
                c.keys(ord(key))
                c.settle(0.15, 2)
            c.settle(0.5, 4)
            a2 = (pos(c.f), clock(c.f))
            if a2 != b:
                moved = True
                detail = f"key {key!r}: {b} -> {a2}"
                break
        rec.add(name, "keys still work after map_click", "PASS" if moved and not c.eof else "FAIL", detail or "nothing changed")
    finally:
        c.close()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=("targeting", "repeat", "click"))
    ap.add_argument("cores", help="comma list or all")
    ap.add_argument("--lang", choices=("ja", "en"), default="ja")
    ap.add_argument("--exe", action="append", default=[], help="NAME=PATH: 別の実行体で試す（変更前との比較用）")
    a = ap.parse_args()
    for item in a.exe:
        k, v = item.split("=", 1)
        ia.CORES[k].exe = v
    ia.UI_LANG = a.lang
    names = (list(THROW) if a.mode == "targeting"
             else [n for n in ia.CORES if n != "angband" and (a.mode != "click" or not n.endswith("-dbg"))]) \
        if a.cores == "all" else a.cores.split(",")
    rec = Rec()
    with tempfile.TemporaryDirectory(prefix="other-cores-input-") as tmp:
        for n in names:
            print(f"== {a.mode} {n} (lang={a.lang})", flush=True)
            work = Path(tmp) / n
            work.mkdir()
            try:
                {"targeting": check_targeting, "repeat": check_repeat, "click": check_click}[a.mode](n, work, rec)
            except Exception as exc:  # noqa: BLE001
                rec.add(n, "exception", "FAIL", repr(exc))
    print(json.dumps({"failed": rec.failed, "total": len(rec.rows)}))
    return 1 if rec.failed else 0


if __name__ == "__main__":
    sys.exit(main())
