#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""TangbandCore.exe の受け入れスモーク（TANGBAND_CORE_STUDY の T0/T1 出口）。

プロトコルの器は tools/gensoband/gb_protocol_driver.py から**読み込み専用**で借りる
（Session/Link/Checks。あちらは幻想蛮怒作業の持ち物なので改変しない）。

    python tools/tangband/tb_smoke.py <workdir>

見るもの:
  1. hello_ack: core_name=tangband、core_version に短愚蛮怒の版が乗る
  2. 誕生画面まで到達し、閉ループ（ミラーを読んで 1 キー）で誕生を完走できる
  3. ゲーム内に入って数ターンで「終末まで 残り N日」が届く（doomsday 既定 ON の証拠）
  4. データ分離: tangband/lib/data に .raw が生え、セーブも tangband/lib/save に閉じる。
     変愚側 lib/data には触れていない
起動したら必ず殺す（Session.close）。
"""
from __future__ import annotations

import json
import sys
import time
from pathlib import Path
from queue import Empty

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools" / "gensoband"))
import gb_protocol_driver as gb  # noqa: E402

gb.CORE = REPO / "TangbandCore.exe"
TB_SAVE = REPO / "tangband" / "lib" / "save"
TB_DATA = REPO / "tangband" / "lib" / "data"
HENG_DATA = REPO / "lib" / "data"


class TbSession(gb.Session):
    """gb.Session ＋ 受けた JSON 全量の生ログ（メッセージの運ばれ方に依存しないため）。"""

    def __init__(self, *a, **kw):
        super().__init__(*a, **kw)
        self.raw = []

    def drain(self, timeout):
        end = time.monotonic() + timeout
        seen = 0
        while True:
            left = end - time.monotonic()
            if left <= 0:
                return seen
            try:
                kind, obj = self.link.q.get(timeout=min(left, 0.2))
            except Empty:
                continue
            seen += 1
            self.counts[kind] = self.counts.get(kind, 0) + 1
            self.raw.append(obj)
            if kind == "frame":
                self.frames.append(obj)
            elif kind == "hello_ack":
                self.ack = obj
            elif kind == "asset_manifest":
                self.manifest = obj
            elif kind == "exit":
                self.exit_msg = obj
            elif kind == "fatal":
                self.fatal = obj


def raw_contains(session, needle):
    for obj in session.raw:
        if needle in json.dumps(obj, ensure_ascii=False):
            return True
    return False


def latest_mirror_text(session):
    return "\n".join(session.mirror())


def decide_birth_key(mirror):
    """誕生画面のミラーから次の 1 手（複数キー可）を決める閉ループの頭脳。"""
    if "初期オプション((*)" in mirror:
        # 誕生の最初に出るオプション一覧。オートローラー（上から 13 行目）だけ
        # OFF にして抜ける——最低値設定の無限画面に入らないため。行順は実物ダンプで確認済み。
        return [ord("2")] * 12 + [ord("n"), 0x1B], "options: autoroller off, ESC"
    for label, key in (("性別", "a"), ("種族", "a"), ("職業", "a"), ("性格", "a"), ("魔法", "a")):
        if (label + "を選んで") in mirror:
            return [ord(key)], f"{label} -> {key}"
    if "よろしい" in mirror or "[y/n]" in mirror or "(y/n)" in mirror:
        return [ord("y")], "confirm -> y"
    return [0x0D], "fallback -> Enter"


def main():
    work = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    work.mkdir(parents=True, exist_ok=True)
    ck = gb.Checks()

    slot = "tbsmoke"
    TB_SAVE.mkdir(parents=True, exist_ok=True)
    for leftover in TB_SAVE.glob(f"{slot}*"):
        leftover.unlink()
    tb_before = {p.name: p.stat().st_mtime_ns for p in TB_SAVE.glob("*") if p.is_file()}
    heng_before = {p.name: p.stat().st_mtime_ns for p in HENG_DATA.glob("*")} if HENG_DATA.exists() else {}

    s = TbSession(work, "tbsmoke", savefile=slot)
    try:
        kind, ack = s.hello()
        print(f"  <- {kind}: core_name={ack.get('core_name')} core_version={ack.get('core_version')!r}")
        ck.add(kind == "hello_ack" and ack.get("protocol") == 1, "hello_ack protocol=1", f"got {kind}")
        ck.add(ack.get("core_name") == "tangband", "core_name is tangband", str(ack.get("core_name")))
        ck.add("26" in str(ack.get("core_version", "")), "core_version carries 26.x",
               str(ack.get("core_version")))

        s.ui_state()
        print("[tb] waiting for pre-game menu (N/L/Q)")
        menu = s.wait_for(lambda x: any("New Game" in r for r in x.mirror()), timeout=120)
        ck.add(menu, "pre-game menu shown (title carries the tangband banner)")
        if menu:
            ck.add(any("短愚蛮怒 26.0.4" in r for r in s.mirror()),
                   "title shows 短愚蛮怒 26.0.4", "menu mirror")
            s.key(0x0D)  # cursor sits on N) New Game
        print("[tb] waiting for birth screens")
        reached = s.wait_for(
            lambda x: any(("性別" in r) or ("種族" in r) or ("名前" in r) for r in x.mirror()),
            timeout=180)
        ck.add(reached, "birth screen reached (menu mirror)")
        if not reached:
            s.show("stuck")
            return ck.finish()

        # ---- 閉ループで誕生を完走する（1 決定 → settle → 次を読む）----
        # 「ゲーム内」は cells の有無では判らない（起動メニューにも背景 cells が乗る）。
        # menu_open と pre_game_menu が両方降りた最新フレームで判定する。
        def is_ingame(x):
            if not x.frames:
                return False
            f = x.frames[-1]
            return bool(f.get("cells")) and not f.get("menu_open") and not f.get("pre_game_menu")

        trail = []
        ingame = False
        for step in range(80):
            if is_ingame(s):
                ingame = True
                break
            mirror = latest_mirror_text(s)
            keys, why = decide_birth_key(mirror)
            top = next((r.strip() for r in s.mirror() if r.strip()), "")
            print(f"    step{step:02d}: {why:24s} | {top[:52]}")
            trail.append(why)
            for k in keys:
                s.key(k)
            s.settle(quiet_rounds=2, slice_s=0.6, cap_s=12.0)
        ck.add(ingame, "birth completed; reached in-game (menus down)",
               f"steps={len(trail)}")
        mblob = json.dumps(s.manifest or {}, ensure_ascii=False)
        ck.add("tilework/sfc/R1417.png" in mblob, "manifest carries R1417 (doomsday serpent)",
               "index=%s" % (s.manifest and next((e.get("index") for e in s.manifest.get("assets", [])
                                                  if e.get("id") == 1417 and e.get("kind") == "R"), "?")))
        if not ingame:
            s.show("birth-stuck")
            return ck.finish()

        # ---- 数ターン過ごして世界ティックを跨ぐ（10 ターンで process_world が走る）----
        for _ in range(15):
            s.key(ord("s"))
            s.drain(0.4)
        s.settle()
        msg_text = ""
        for obj in s.raw:
            blob = json.dumps(obj, ensure_ascii=False)
            i = blob.find("終末まで")
            if i >= 0:
                msg_text = blob[max(0, i - 8): i + 24]
                break
        ck.add(bool(msg_text), "doomsday countdown message arrived", msg_text)

        # ---- help が短愚蛮怒版で出る（tangband/lib/help 分離の確認）----
        s.key(ord("?"))
        s.settle(quiet_rounds=2, slice_s=0.6, cap_s=15.0)
        ck.add(any("短愚蛮怒" in r for r in s.mirror()), "help screen shows tangband branding",
               next((r.strip()[:40] for r in s.mirror() if "短愚蛮怒" in r), ""))
        s.key(0x1B)
        s.settle(quiet_rounds=1, slice_s=0.5, cap_s=8.0)

        # ---- Ctrl-X で保存して終了（quit_request は保存せずに畳む）----
        s.key(0x18)
        end = time.monotonic() + 30
        while time.monotonic() < end and s.exit_msg is None and s.proc.poll() is None:
            s.drain(0.5)
        s.quit_and_wait(timeout=10)
    finally:
        s.close()

    # ---- データ分離の実地確認（近代コアは .raw キャッシュを吐かないので、セーブの着地だけ見る）----
    tb_after = {p.name: p.stat().st_mtime_ns for p in TB_SAVE.glob("*") if p.is_file()}
    # 誕生画面の既定名 PLAYER が既にある場合は上書き保存になる。
    # 名前の増加だけで判定すると、保存成功を失敗と誤認する。
    new_files = sorted(name for name, stamp in tb_after.items() if tb_before.get(name) != stamp)
    real_saves = [n for n in new_files if not n.endswith(".sdl2panels")]
    ck.add(bool(real_saves), "save landed in tangband/lib/save", ", ".join(new_files) or "none")
    heng_after = {p.name: p.stat().st_mtime_ns for p in HENG_DATA.glob("*")} if HENG_DATA.exists() else {}
    ck.add(heng_before == heng_after, "hengband lib/data untouched",
           f"{len(heng_before)} -> {len(heng_after)} entries")

    return ck.finish()


if __name__ == "__main__":
    sys.exit(main())
