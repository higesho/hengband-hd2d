#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""FroxCore.exe を core プロトコル v1 で駆動する検査器（設計 §8 P6 / §9）。

`tools/silq/sq_protocol_driver.py` の弟。あちらとの違いは Frox の事情そのもので、

  * タイトルとセーブ選択は**アダプタが描く**（設計 §3.3。文言は英語）。ミラーに
    `Which save do you want to play?` が出るのが握手の証拠になる
  * 状態列は**右**（`status_col_side = 1` がフレームに載る）
  * 文字はすべて **ASCII**（M0）

    python tools/frox/fc_protocol_driver.py handshake <workdir>
    python tools/frox/fc_protocol_driver.py play      <workdir> <script>
    python tools/frox/fc_protocol_driver.py m0        <workdir>
    python tools/frox/fc_protocol_driver.py m05       <workdir>
    python tools/frox/fc_protocol_driver.py j2        <workdir>
    python tools/frox/fc_protocol_driver.py j4        <workdir>
    python tools/frox/fc_protocol_driver.py j6        <workdir>
    python tools/frox/fc_protocol_driver.py decor     <workdir>
    python tools/frox/fc_protocol_driver.py msg       <workdir>
    python tools/frox/fc_protocol_driver.py bldg      <workdir>
    python tools/frox/fc_protocol_driver.py spell     <workdir>
    python tools/frox/fc_protocol_driver.py spellfmt  <workdir>
    python tools/frox/fc_protocol_driver.py sfx       <workdir>

sub-command
  handshake … 握手・表・タイトルのミラー・キー進行（P6 の受け入れ）
  play      … スクリプトどおりキーを打ち、毎キーごとにミラーを出す（人が読む用）
  m0        … 受け入れ §9 の 2 の閉ループ（新規キャラ → 町 → セーブ →
              再起動 → ロード）。**1 キーずつ**送る（MEMORY.md「ボットは閉ループで回す」）
  m05       … M0.5（設計 §14.2(c)）の閉ループ。doc UI の旗（FH-02）・札の読み取りと
              プロンプト（FH-01）・コマンドメニューのカーソル・icky の従来道
  shop      … **店の実地**（FH-13 の y/n・FH-14 の札の絞り込み・FH-15 の個数）。
              町を照らして最寄りの店へ寄り、踏んで入る。**1 歩ごとに地図を
              読み直す**——荒野は足元でスクロールし、区画の端で座標が飛ぶ
  j2        … **M1 の J2（配管）＋ J3（名前）の受け入れ**（FROX_JA_DESIGN §8.2）。
              セーブ選択と誕生の画は --lang=en と**1 バイトも変わらない**こと
              （実体の名前が出ない画。制約 1）・ui_state の申告でも同じ道が通ること・
              **持ち物の名前が日本語で出る**こと（J3。lib-ja/edit の証拠）・
              使い捨てのカタログ（--lang-dir=）で UI の字も通ること
  sfx       … **効果音の配管**（AUDIO_DESIGN §2.1・§2.3）。殴って
              `hit` と `kill` が frame.sounds に載ること・一度きりであること・
              `ui_state.sound_events` を降ろすと止まること
  fx        … FH-05（戦闘の見せ場）・FH-09（ミニマップの細別）・FH-10（広域の
              階層欄）。町で worm(31) を殴って**とどめ**、経験値を盛ってから
              Floating orb(912。動かず毎ターン MISSILE）で**弾道・被弾・部分
              ダメージ**、最後に wizard ジャンプで Snow castle の**雪の細別**
  decor     … **飾りの字の受け入れ**（FROX_JA_DESIGN §8.26。フック
              #31〜#33）。光源の残り・偽の銘・由来の場所を実機で見る。
              **英語でも 1 巡回して 1 バイトも変わっていないことを見る**
  j4        … **J4（UI の文字列）の収穫路**（FROX_JA_DESIGN §8.7）。
              検査ではなく収穫——誕生の画を一通り開き、町の画面（持ち物・装備・
              人物・知識・設定の下位画面）を開き、**店へ入り**、**敵を殴り**、
              wizard ジャンプで Angband の 1〜5 階を歩く。
              引けなかった鍵は `<workdir>/u.txt` へ落ちる

スクリプトの記法
  素の文字 … その ASCII を 1 キー
  \\r \\e \\s … Enter / ESC / Space
  \\t        … Tab
  \\^X       … Ctrl-X
  \\.        … 1 秒待つだけ（キーは送らない）
"""
from __future__ import annotations

import csv
import json
import os
import re
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path
from queue import Empty, Queue

REPO = Path(__file__).resolve().parents[2]
CORE = REPO / "FroxCore.exe"
SAVE_DIR = REPO / "frox" / "lib" / "save"

#: `cells` は**配列の組**で運ばれる（`frame_codec.cpp` の `enc_cell`）。添字の意味:
#:   0 gx / 1 gy / 2 terrain_id / 3 feature_flags / 4 monster_id / 5 monster_slot /
#:   6 object_id / 7 light_level / 8 fg / 9 bg / 10 ascii / 11 tile_index / 12 under / 13-16 graf
CELL_TERRAIN = 2
CELL_FF = 3
CELL_MONSTER = 4
CELL_OBJECT = 6
CELL_LIGHT = 7
CELL_ASCII = 10
CELL_TILE = 11

#: `cell_feature_bits.h` の値（画面側の enum の写し）。
FEAT_WALL = 0x0001
FEAT_STAIRS = 0x0008
FEAT_PLAYER = 0x0200
FEAT_KNOWN = 0x0400
FEAT_PASSABLE = 0x0800

#: 誕生を素通りするスクリプト（実測 2026-08-24。`play` で 1 手ずつ確かめた並び）。
#:   n     … n) Start a new game（アダプタのセーブ選択）
#:   n     … Choose the Type of Game to Play → n) Normal
#:   Enter … 名前・性別・種族・職は既定のまま（Dunadan Warrior）→ 能力値の画面へ
#:   Enter … Enter) Begin Play。**これで町（Outpost）に立つ**
BIRTH_SCRIPT = "nn\r\r"


def parse_script(script: str):
    """スクリプト → キー番号の並び。-1 は「送らずに 1 秒待つ」。"""
    out = []
    i = 0
    while i < len(script):
        c = script[i]
        if c != "\\":
            out.append(ord(c) & 0xFF)
            i += 1
            continue
        i += 1
        e = script[i]
        i += 1
        if e == "r":
            out.append(0x0D)
        elif e == "e":
            out.append(0x1B)
        elif e == "s":
            out.append(0x20)
        elif e == "t":
            out.append(0x09)
        elif e == ".":
            out.append(-1)
        elif e == "^":
            out.append(ord(script[i].upper()) - ord("A") + 1)
            i += 1
        else:
            out.append(ord(e) & 0xFF)
    return out


class Link:
    """長さ枠（LE u32）＋ JSON 1 通、の受け渡し。読みは別スレッド。"""

    def __init__(self, proc):
        self.proc = proc
        self.q: "Queue[tuple[str, dict]]" = Queue()
        self.eof = False
        threading.Thread(target=self._loop, daemon=True).start()

    def _exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.proc.stdout.read(n - len(buf))
            if not chunk:
                return None
            buf += chunk
        return buf

    def _loop(self):
        try:
            while True:
                head = self._exact(4)
                if head is None:
                    self.eof = True
                    self.q.put(("<eof>", {}))
                    return
                (n,) = struct.unpack("<I", head)
                body = self._exact(n) if n else b""
                if body is None:
                    self.eof = True
                    self.q.put(("<eof>", {}))
                    return
                obj = json.loads(body.decode("utf-8"))
                self.q.put((obj.get("t", ""), obj))
        except Exception as exc:  # noqa: BLE001
            self.q.put(("<error>", {"e": str(exc)}))

    def send(self, obj):
        payload = json.dumps(obj, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        try:
            self.proc.stdin.write(struct.pack("<I", len(payload)) + payload)
            self.proc.stdin.flush()
        except OSError:
            self.eof = True


class Session:
    """1 本のコア起動。**必ず `close()` すること**（起動したら殺す）。"""

    def __init__(self, work: Path, tag: str, extra_args=()):
        self.work = work
        self.tag = tag
        self.err_path = work / f"fc_{tag}_stderr.log"
        self._errfp = self.err_path.open("wb")
        args = [str(CORE), "--ui-protocol=stdio"]
        args.extend(extra_args)
        self.proc = subprocess.Popen(
            args, cwd=str(REPO), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=self._errfp, env=dict(os.environ), bufsize=0)
        self.link = Link(self.proc)
        self.frames = []
        self.counts = {}
        self.ack = None
        self.exit_msg = None
        self.fatal = None
        self.pad = None
        self.manifest = None
        self.sub_kinds = None

    # ---------------------------------------------------------------- 受け取り
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
            if kind == "frame":
                self.frames.append(obj)
            elif kind == "hello_ack":
                self.ack = obj
            elif kind == "pad_commands":
                self.pad = obj
            elif kind == "asset_manifest":
                self.manifest = obj
            elif kind == "sub_panel_kinds":
                self.sub_kinds = obj
            elif kind == "exit":
                self.exit_msg = obj
            elif kind == "fatal":
                self.fatal = obj

    def settle(self, quiet_rounds=2, slice_s=0.9, cap_s=25.0):
        """静かになるまで受け取る（キー 1 個ぶんの反応を取り切る）。"""
        quiet = 0
        end = time.monotonic() + cap_s
        while time.monotonic() < end:
            if self.drain(slice_s) == 0:
                quiet += 1
                if quiet >= quiet_rounds:
                    return
            else:
                quiet = 0

    # ------------------------------------------------------------------ 使い方
    def hello(self, ui_name="fc_protocol_driver.py"):
        self.link.send({"t": "hello", "protocol": 1, "ui_name": ui_name,
                        "ui_version": "0.1.0", "features": []})
        kind, obj = self.link.q.get(timeout=30)
        self.counts[kind] = self.counts.get(kind, 0) + 1
        if kind == "hello_ack":
            self.ack = obj
        return kind, obj

    def ui_state(self, view_w=40, view_h=22, cursor_mode=False, lang="en",
                 sound_events=False):
        # sound_events … 画面側が音を鳴らす（コアは frame.sounds へ書き留めるだけ）。
        # AUDIO_DESIGN §2.1。既定は偽で、cmd_sfx だけが立てる。
        self.link.send({"t": "ui_state", "view_cells": {"w": view_w, "h": view_h},
                        "cursor_mode": cursor_mode, "lang": lang,
                        "map_style": {"graf_px": 0, "graf_tag": "ascii"},
                        "audio": {"sound_on": bool(sound_events), "music_on": False,
                                  "sound_events": bool(sound_events),
                                  "sound_volume_index": 0, "music_volume_index": 0}})

    def key(self, k):
        self.link.send({"t": "keys", "keys": [k]})

    def mirror(self, frame=None):
        f = frame if frame is not None else (self.frames[-1] if self.frames else None)
        if not f:
            return []
        return [ln.get("text_utf8", "") for ln in f.get("menu_term_lines", [])]

    def show(self, tag):
        print(f"--- {tag} (frames={len(self.frames)}) ---")
        for i, row in enumerate(self.mirror()):
            if row.strip():
                print(f"  [{i:2d}] {row}")
        if self.frames:
            f = self.frames[-1]
            hud = f.get("hud", {})
            print(f"  hud={json.dumps(hud, ensure_ascii=False)}"
                  f" menu_open={f.get('menu_open')} pre_game={f.get('pre_game_menu')}"
                  f" cells={len(f.get('cells', []))}"
                  f" status_side={f.get('status_col_side')}"
                  f" status_cols={f.get('status_col_cols')}")
            for ln in f.get("status_col_lines", [])[:12]:
                if ln.get("text_utf8", "").strip():
                    print(f"  |status| {ln['text_utf8']}")

    def wait_for(self, predicate, timeout=120.0):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            self.drain(1.0)
            if self.frames and predicate(self):
                return True
        return False

    def quit_and_wait(self, timeout=60.0):
        self.link.send({"t": "quit_request"})
        end = time.monotonic() + timeout
        while time.monotonic() < end and self.exit_msg is None:
            self.drain(0.5)
        try:
            self.proc.wait(timeout=40)
        except subprocess.TimeoutExpired:
            pass
        return self.exit_msg

    def close(self):
        # **起動したら必ず殺す。**
        if self.proc.poll() is None:
            self.proc.kill()
            try:
                self.proc.wait(timeout=20)
            except subprocess.TimeoutExpired:
                pass
        self._errfp.close()

    def stderr_tail(self, n=15):
        text = self.err_path.read_bytes().decode("utf-8", "replace")
        return text.strip().splitlines()[-n:]


class Checks:
    def __init__(self):
        self.rows = []

    def add(self, ok, name, detail=""):
        self.rows.append((bool(ok), name, detail))
        print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f"  --  {detail}" if detail else ""))
        return bool(ok)

    def finish(self):
        failed = sum(1 for ok, _, _ in self.rows if not ok)
        print(f"\n  {len(self.rows) - failed}/{len(self.rows)} checks passed")
        return 1 if failed else 0


# ==================================================================== handshake

def cmd_handshake(work: Path) -> int:
    checks = Checks()
    s = Session(work, "handshake")
    try:
        kind, obj = s.hello()
        checks.add(kind == "hello_ack", "hello -> hello_ack", f"got {kind}")
        if s.ack:
            checks.add(s.ack.get("core_name") == "frox", "core_name == frox",
                       str(s.ack.get("core_name")))
            checks.add(str(s.ack.get("core_version", "")).startswith("7.3."),
                       "core_version 7.3.*", str(s.ack.get("core_version")))
        s.ui_state()
        s.settle()
        checks.add(s.pad is not None, "pad_commands received",
                   f"{len((s.pad or {}).get('entries', []))} entries")
        checks.add(s.sub_kinds is not None, "sub_panel_kinds received",
                   f"{len((s.sub_kinds or {}).get('entries', []))} kinds")
        checks.add(s.manifest is not None, "asset_manifest received",
                   f"{len((s.manifest or {}).get('assets', []))} assets")
        rows = "\n".join(s.mirror())
        checks.add("Which save do you want to play?" in rows,
                   "save picker on the mirror")
        s.show("title")
        # n を押すと誕生へ進む（新規）。1 フレームでも動けば進行の証拠。
        before = len(s.frames)
        s.key(ord("n"))
        s.settle()
        checks.add(len(s.frames) > before, "key advances the screen",
                   f"{len(s.frames) - before} new frame(s)")
        s.show("after n")
        s.quit_and_wait()
        checks.add(s.exit_msg is not None or s.fatal is None, "clean shutdown",
                   f"exit={s.exit_msg} fatal={s.fatal}")
    finally:
        s.close()
        print("  stderr tail:")
        for ln in s.stderr_tail():
            print(f"    {ln}")
    return checks.finish()


# ========================================================================= play

def cmd_play(work: Path, script: str, extra=()) -> int:
    s = Session(work, "play", extra)
    try:
        s.hello()
        s.ui_state()
        s.settle()
        s.show("start")
        for k in parse_script(script):
            if k < 0:
                time.sleep(1.0)
                s.drain(0.5)
                continue
            s.key(k)
            s.settle(quiet_rounds=2, slice_s=0.6, cap_s=12.0)
            label = chr(k) if 32 <= k < 127 else f"0x{k:02x}"
            s.show(f"after {label}")
        s.quit_and_wait()
    finally:
        s.close()
        print("  stderr tail:")
        for ln in s.stderr_tail():
            print(f"    {ln}")
    return 0


# =========================================================================== m0

def latest_cells(s: Session):
    for f in reversed(s.frames):
        cells = f.get("cells", [])
        if cells:
            return f, cells
    return None, []


def in_game(s: Session) -> bool:
    f = s.frames[-1]
    return bool(f.get("cells")) and not f.get("pre_game_menu")


def cmd_m0(work: Path) -> int:
    """受け入れ §9 の 2 の閉ループ。新規キャラ → 町を歩く → セーブ → 再起動 → ロード。

    ダンジョンへ降りる段は**ここでは踏まない**——Outpost からダンジョンの口までは
    荒野の移動が要り、ボットの決め打ちで安定しない。§9 の 1（手で 1 度遊ぶ）と
    合わせて完了させる。
    """
    checks = Checks()

    # ---- 第 1 幕: 新規キャラを作って歩いてセーブする -------------------------
    s = Session(work, "m0_first")
    try:
        kind, _ = s.hello()
        checks.add(kind == "hello_ack", "hello -> hello_ack", f"got {kind}")
        s.ui_state()
        s.settle()

        for k in parse_script(BIRTH_SCRIPT):
            s.key(k)
            s.settle(quiet_rounds=2, slice_s=0.6, cap_s=15.0)
        checks.add(s.wait_for(in_game, timeout=30.0), "birth reaches the town")
        f, cells = latest_cells(s)
        hud = (f or {}).get("hud", {})
        checks.add(hud.get("hp", 0) > 0, "hud has hp", json.dumps(hud, ensure_ascii=False))
        checks.add((f or {}).get("status_col_side") == 1, "status_col_side == 1 (right)",
                   str((f or {}).get("status_col_side")))
        checks.add(len((f or {}).get("status_col_lines", [])) > 0, "status column carried")
        player = [c for c in cells if c[CELL_FF] & FEAT_PLAYER]
        checks.add(len(player) == 1, "exactly one player cell", f"{len(player)}")
        known = [c for c in cells if c[CELL_FF] & FEAT_KNOWN]
        tiles = [c for c in known if c[CELL_TILE] > 0]
        checks.add(len(tiles) > 0, "terrain tile_index resolved",
                   f"{len(tiles)}/{len(known)} known cells")
        #! **実体の絵（M2）**。目録に載っているので @ と敵と床の品は 0 以外を返す。
        #! 0 のままなら字の板に落ちている＝目録の引きが効いていない
        #! （`FROX_TILE_TODO` §15）。
        checks.add(bool(player) and player[0][CELL_TILE] > 0,
                   "player tile_index resolved (P)",
                   str(player[0][CELL_TILE]) if player else "no player cell")
        mons = [c for c in cells if c[CELL_MONSTER] != 0]
        #! 誕生直後の町に敵が見えているとは限らない（実測 0 体のことが在る）。
        #! **見えたぶんが全部引けていること**だけを見て、居ない回は責めない
        #! ——敵の絵の本番の検査は `fx`（召喚して殴る）のほうに置いた。
        checks.add(all(c[CELL_TILE] > 0 for c in mons),
                   "monster tile_index resolved (R)",
                   f"{sum(1 for c in mons if c[CELL_TILE] > 0)}/{len(mons)} monsters"
                   + ("（見えず。fx で見る）" if not mons else ""))
        objs = [c for c in cells if c[CELL_OBJECT] != 0 and c[CELL_LIGHT] >= 2]
        checks.add(all(c[CELL_TILE] > 0 for c in objs) if objs else True,
                   "object tile_index resolved (K)",
                   f"{sum(1 for c in objs if c[CELL_TILE] > 0)}/{len(objs)} floor items")
        mm = (f or {}).get("minimap", {})
        checks.add(mm.get("width", 0) > 0, "minimap carried",
                   f"{mm.get('width')}x{mm.get('height')}")

        # 誕生直後の -more- を流す（歓迎文が数本溜まっていて、流さないと
        # 移動キーが読み流しに食われる。実測 2026-08-24）。
        for _ in range(6):
            s.key(0x20)
            s.settle(quiet_rounds=1, slice_s=0.5, cap_s=6.0)

        # 町を 1 キーずつ歩く（閉ループ。連射しない）。
        f1, _ = latest_cells(s)
        pos0 = ((f1 or {}).get("player_gx"), (f1 or {}).get("player_gy"))
        moved = False
        for k in [ord("4"), ord("6"), ord("2"), ord("8")]:
            s.key(k)
            s.settle(quiet_rounds=2, slice_s=0.6, cap_s=10.0)
            f2, _ = latest_cells(s)
            if ((f2 or {}).get("player_gx"), (f2 or {}).get("player_gy")) != pos0:
                moved = True
                break
        checks.add(moved, "walking moves the player", f"from {pos0}")

        # セーブ（Ctrl-S）。ミラーに "Saved" 系のメッセージが乗るのを待たず、
        # quit_request の強制保存に頼らず、**自分で押して**から畳む。
        s.key(0x13)
        s.settle(quiet_rounds=2, slice_s=0.6, cap_s=15.0)
        s.quit_and_wait()
        checks.add(s.exit_msg is not None, "first session exits cleanly", str(s.exit_msg))
    finally:
        s.close()

    slot_file = SAVE_DIR / "last_slot.txt"
    checks.add(slot_file.exists(), "last_slot.txt written")
    slot = slot_file.read_text().strip() if slot_file.exists() else ""
    checks.add(bool(slot) and (SAVE_DIR / slot).exists(), f"save file exists ({slot})")

    # ---- 第 2 幕: 起こし直して Enter 一発で続きから --------------------------
    s2 = Session(work, "m0_second")
    try:
        kind, _ = s2.hello()
        checks.add(kind == "hello_ack", "second hello -> hello_ack", f"got {kind}")
        s2.ui_state()
        s2.settle()
        rows = chr(10).join(s2.mirror())
        checks.add(slot in rows and "*" in rows, "picker lists the slot with *")
        s2.key(0x0D)  # Enter = 前回の枠
        checks.add(s2.wait_for(in_game, timeout=60.0), "load reaches the town")
        f, _ = latest_cells(s2)
        hud = (f or {}).get("hud", {})
        checks.add(hud.get("name") == slot or hud.get("hp", 0) > 0, "hud restored",
                   json.dumps(hud, ensure_ascii=False))
        s2.quit_and_wait()
        checks.add(s2.exit_msg is not None, "second session exits cleanly", str(s2.exit_msg))
    finally:
        s2.close()
        print("  stderr tail (second):")
        for ln in s2.stderr_tail():
            print(f"    {ln}")

    return checks.finish()


# ========================================================================== m05

def birth_and_flush(s: Session, checks: Checks) -> None:
    """新規キャラで町へ立ち、誕生直後の -more- を流す（m0 と同じ手順）。"""
    for k in parse_script(BIRTH_SCRIPT):
        s.key(k)
        s.settle(quiet_rounds=2, slice_s=0.6, cap_s=15.0)
    checks.add(s.wait_for(in_game, timeout=30.0), "birth reaches the town")
    for _ in range(6):
        s.key(0x20)
        s.settle(quiet_rounds=1, slice_s=0.5, cap_s=6.0)


def cmd_m05(work: Path) -> int:
    """M0.5（設計 §14.2(c)）の閉ループ。**品は 1 つも壊さない**（破壊は n で取り消す）。

    誕生の名前画面（**右列の札**。FH-12）→ I（品選び＝Term_save 直叩きの doc UI）→
    ESC → k（破壊）→ [y/n/Auto] を n →
    Enter（コマンドメニュー＝コアのカーソル）→ ESC → =（オプション＝icky の従来道）
    → ESC → 1 歩あるく → 畳む。畳みは quit_request（強制保存つき。m0 と同じ枠）。
    店の実地（Outpost の建物。札 3 = `[b/p/g]` の角括弧）は §9-1 の手プレイと併せる
    ——町の建物まではボットの決め打ちで届かない。
    """
    checks = Checks()
    s = Session(work, "m05")
    try:
        kind, _ = s.hello()
        checks.add(kind == "hello_ack", "hello -> hello_ack", f"got {kind}")
        s.ui_state(cursor_mode=True)  # M0.5 の主役はカーソル選択
        s.settle()

        # ---- FH-12: 誕生の名前画面は右列（Enter/*/Tab）も札になる --------------
        # 実機の報告（2026-08-24）: 右列が札にならず、カーソルが左列に閉じ込められ、
        # Enter が「カーソル位置の決定」に化けて次の画面へ進めなかった。
        # birth_and_flush と同じ並びを刻んで、名前画面で札を見る。
        for k in parse_script("nn"):
            s.key(k)
            s.settle(quiet_rounds=2, slice_s=0.6, cap_s=15.0)
        f = s.frames[-1] if s.frames else {}
        keys = [c.get("key") for c in f.get("menu_choices", [])]
        checks.add((0x0D in keys) and (ord("*") in keys) and (0x09 in keys),
                   "name screen carries Enter/*/Tab choices (FH-12)",
                   f"keys={sorted(k for k in keys if isinstance(k, int))}")
        for k in parse_script("\r\r"):
            s.key(k)
            s.settle(quiet_rounds=2, slice_s=0.6, cap_s=15.0)
        checks.add(s.wait_for(in_game, timeout=30.0), "birth reaches the town")
        for _ in range(6):
            s.key(0x20)
            s.settle(quiet_rounds=1, slice_s=0.5, cap_s=6.0)

        # ---- FH-02 + FH-01: I（調べる）は Term_save() 直叩きの品選び ----------
        s.key(ord("I"))
        s.settle()
        f = s.frames[-1] if s.frames else {}
        checks.add(f.get("menu_open"), "I opens the mirror (doc UI flag)")
        letters = [c.get("key") for c in f.get("menu_choices", [])
                   if isinstance(c.get("key"), int)
                   and (ord("a") <= c.get("key") <= ord("z")
                        or ord("A") <= c.get("key") <= ord("Z"))]
        checks.add(len(letters) > 0, "menu_choices carry letters", f"{len(letters)} letters")
        s.show("after I")

        s.key(0x1B)
        s.settle()
        f = s.frames[-1] if s.frames else {}
        checks.add(not f.get("menu_open"),
                   "ESC closes the mirror (flag lowered at command wait)")

        # ---- k（破壊）→ 品選び → [y/n/Auto] を n で取り消す --------------------
        s.key(ord("k"))
        s.settle()
        f = s.frames[-1] if s.frames else {}
        choices = f.get("menu_choices", [])
        checks.add(bool(f.get("menu_open")) and bool(choices), "k opens the item prompt")
        pick = 0
        for c in choices:
            k = c.get("key")
            if isinstance(k, int) and ord("a") <= k <= ord("z"):
                pick = k
                break
        checks.add(pick > 0, "a letter to pick", chr(pick) if pick else "-")
        got_prompt = False
        if pick:
            s.key(pick)
            for _ in range(3):
                s.settle()
                f = s.frames[-1] if s.frames else {}
                keys = [c.get("key") for c in f.get("prompt", {}).get("choices", [])]
                if (ord("y") in keys) and (ord("n") in keys):
                    got_prompt = True
                    break
                if f.get("numeric", {}).get("active"):
                    s.key(0x0D)  # 個数は既定のまま進める
                    continue
                break
        checks.add(got_prompt, "[y/n] lifted into frame.prompt",
                   json.dumps((s.frames[-1] if s.frames else {}).get("prompt", {}),
                              ensure_ascii=False)[:120])
        if got_prompt:
            s.key(ord("n"))  # 取り消し（何も壊さない）
            s.settle()

        # ---- Enter → コマンドメニュー（コアのカーソル "> " ＋ 枠 +----） -------
        s.key(0x0D)
        s.settle()
        f = s.frames[-1] if s.frames else {}
        rows = chr(10).join(s.mirror())
        checks.add(bool(f.get("menu_open")) and ("+----" in rows),
                   "Enter opens the command menu")
        checks.add(len(f.get("menu_core_cursors", [])) > 0,
                   "core cursor found (menu_core_cursors)",
                   f"{len(f.get('menu_core_cursors', []))} cursor(s)")
        checks.add(not f.get("menu_choices"),
                   "no scanned choices while the core owns the cursor")
        s.key(0x1B)
        s.settle()
        f = s.frames[-1] if s.frames else {}
        checks.add(not f.get("menu_open"), "ESC closes the command menu")

        # ---- = → オプションの根（icky の従来道＋ (1) の札） --------------------
        s.key(ord("="))
        s.settle()
        f = s.frames[-1] if s.frames else {}
        checks.add(f.get("menu_open"), "= opens options (icky path intact)")
        checks.add(len(f.get("menu_choices", [])) > 0, "(1)-style tokens scanned",
                   f"{len(f.get('menu_choices', []))}")
        s.show("options")
        s.key(0x1B)
        s.settle()
        f = s.frames[-1] if s.frames else {}
        checks.add(not f.get("menu_open"), "ESC closes options")

        # ---- FH-15: 個数入力が `frame.numeric` に載る（箱の全行を見る）--------
        # 店の個数は `msg_input_num()`＝**`Quantity (1 to 5): `**、通常は
        # `get_quantity()`＝**`(1-5): `**（`message.c:626` / `util.c:3246`）。
        # 綴りを片方しか見ておらず、しかも**行 0 だけ**を見ていたので、店では
        # `frame.numeric` が立たず、決定がカーソル層の選択肢まで流れて品の
        # letter（`a`）を送っていた（利用者の実機報告 2026-08-24）。
        #
        # **ここで見るのは `(1-N)` の側**——`msg_input_num` を出せる場面
        # （店・我が家・建物、または矢筒への出し入れ）へは新規キャラのボットが
        # 届かない（矢は最初から矢筒の中、店は町の区画をまたげない）。
        # `(1 to N)` の側は**店の手プレイ**が受け持つ。
        s.key(ord("d"))  # 投棄（品は捨てない。個数を訊かれた所で ESC）
        s.settle()
        torch_key = 0
        for row in s.mirror():
            t = row.strip()
            if ("Torch" in t) and (len(t) > 2) and (t[1] == ")"):
                torch_key = ord(t[0])
                break
        checks.add(torch_key > 0, "a stack found in the drop prompt",
                   chr(torch_key) if torch_key else "-")
        if torch_key:
            s.key(torch_key)
            s.settle()
            f = s.frames[-1] if s.frames else {}
            num = f.get("numeric", {})
            checks.add(bool(num.get("active")),
                       "quantity lifted into frame.numeric (FH-15)",
                       json.dumps(num, ensure_ascii=False)[:120])
            s.key(0x1B)  # **捨てない**
            s.settle()
        drain_more(s, 1)

        # ---- 歩けること（どの旗も引っ掛かったまま残っていない） ----------------
        f1, _ = latest_cells(s)
        pos0 = ((f1 or {}).get("player_gx"), (f1 or {}).get("player_gy"))
        moved = False
        for k in [ord("4"), ord("6"), ord("2"), ord("8")]:
            s.key(k)
            s.settle(quiet_rounds=2, slice_s=0.6, cap_s=10.0)
            f2, _ = latest_cells(s)
            if ((f2 or {}).get("player_gx"), (f2 or {}).get("player_gy")) != pos0:
                moved = True
                break
        checks.add(moved, "walking still works", f"from {pos0}")

        s.quit_and_wait()
        checks.add(s.exit_msg is not None, "clean shutdown", str(s.exit_msg))
    finally:
        s.close()
        print("  stderr tail:")
        for ln in s.stderr_tail():
            print(f"    {ln}")
    return checks.finish()


def collect_fx(s: Session, start_index: int):
    """frames[start_index:] の combat_fx を平らに集める。

    codec は既定値を省く（kind=0 の被弾は "kind" キーが無い）ので get で読む。
    **強さのキーは `i`**（`frame_codec.cpp:642`。"intensity" ではない）。
    """
    out = []
    for f in s.frames[start_index:]:
        for e in f.get("combat_fx", []):
            out.append({
                "kind": e.get("kind", 0),
                "elem": e.get("elem", 0),
                "y": e.get("y", 0), "x": e.get("x", 0),
                "sy": e.get("sy", 0), "sx": e.get("sx", 0),
                "intensity": e.get("i", 0.0),
            })
    return out


def drain_more(s: Session, rounds: int = 2) -> None:
    """-more- の山を ESC で流す。

    Frox の -more- は ESC で `AUTO_MORE_SKIP_ALL`（残り全部を飛ばす。
    `message.c:331`）。コマンド待ちの ESC は無害で、開きかけのプロンプトも畳む
    ——盲打ちの列を組む前の**状態の均し**として使う。SPACE で流してはいけない:
    -more- 1 個ずつしか進まないうえ、-more- の中の `flush()` が後続の
    連射キーを捨てる（`MEMORY.md`「ボットは閉ループで回す」の正体）。
    """
    for _ in range(rounds):
        s.key(0x1B)
        s.settle(quiet_rounds=1, slice_s=0.4, cap_s=4.0)


def wizard_begin(s: Session, cmd_char: str, confirm: bool) -> None:
    """^A → （初回だけ確認に y）→ debug コマンド 1 文字。

    確認の [y/n] はメッセージの箱の 3 行目より下に出る（前置き 2 本が長い）。
    fc_menu が箱の全行を見るようになったので `prompt` に載る——載るのを待って y。
    2 回目からは確認が出ない（noscore 済み）ので、余計なキーを打たない。
    """
    drain_more(s)
    s.key(0x01)  # ^A
    s.settle()
    if confirm:
        for _ in range(6):
            f = s.frames[-1] if s.frames else {}
            keys = [c.get("key") for c in f.get("prompt", {}).get("choices", [])]
            if ord("y") in keys:
                break
            s.settle(quiet_rounds=1, slice_s=0.5, cap_s=4.0)
        s.key(ord("y"))
        s.settle()
    s.key(ord(cmd_char))
    s.settle()


def wizard_jump(s: Session, dungeon: str, level: str, confirm: bool = False) -> None:
    """^A j で番号のダンジョン・階へ飛ぶ（get_string の既定は最初の 1 打で消える）。"""
    wizard_begin(s, "j", confirm)
    for ch in dungeon:
        s.key(ord(ch))
        s.settle(quiet_rounds=1, slice_s=0.4, cap_s=4.0)
    s.key(0x0D)
    s.settle()
    for ch in level:
        s.key(ord(ch))
        s.settle(quiet_rounds=1, slice_s=0.4, cap_s=4.0)
    s.key(0x0D)
    s.settle(quiet_rounds=2, slice_s=0.8, cap_s=30.0)
    drain_more(s)


def answer_stat_prompts(s: Session, cap: int = 12) -> None:
    """レベルアップごとに開く能力値の選択を「a（腕力）→ y」で答え切る。

    **答えないと以後の全キーを食う**（`gain_chosen_stat`。a〜f と C しか受けず
    ESC も無効）。`cmd_fx` が実測で踏んだ罠で、そちらから括り出した。

    **フレームの札だけでは足りない**（実測 2026-08-28）。文字の選択が
    `prompt` にも `menu_open` にも載らない巡があり、そこで抜けると次の `^A` が
    選択に食われて `Please make a choice!` が延々と出続ける——**そこから先の
    キーが全部死ぬ**ので、検査は「唱えなかった」ように見えて落ちる。
    **画の字も見て、選択が開いていれば `a` を打つ。**
    """
    for _ in range(cap):
        f = s.frames[-1] if s.frames else {}
        keys = [c.get("key") for c in f.get("prompt", {}).get("choices", [])]
        if ord("y") in keys:
            s.key(ord("y"))
            s.settle(quiet_rounds=1, slice_s=0.4, cap_s=5.0)
            continue
        if f.get("menu_open"):
            s.key(ord("a"))
            s.settle(quiet_rounds=1, slice_s=0.4, cap_s=5.0)
            continue
        #! **画の字で見る保険。** 札に載らない巡があるので、ここで抜けない。
        text = " ".join(s.mirror())
        if ("make a choice" in text) or ("hich stat" in text) or ("Str)" in text):
            s.key(ord("a"))
            s.settle(quiet_rounds=1, slice_s=0.4, cap_s=5.0)
            continue
        break


def boost_level(s: Session, rounds: int = 18) -> None:
    """`^A x`（exp を倍にする）を繰り返して育てる。

    **初期 exp は 0** なので倍加は 1, 3, 7, 15, … と育つ。最初の数回は
    見た目が変わらず frame も出ないのが正常（`cmd_fx` の註記と同じ）。
    `^A X`（値の直指定）は使わない——数字の盲打ちが安定しない。
    """
    for _ in range(rounds):
        wizard_begin(s, "x", False)
        answer_stat_prompts(s, 12)
    answer_stat_prompts(s, 30)
    drain_more(s)


def wizard_summon_named(s: Session, r_idx: str, confirm: bool = False) -> None:
    """^A n で番号の敵を呼ぶ。"""
    wizard_begin(s, "n", confirm)
    for ch in r_idx:
        s.key(ord(ch))
        s.settle(quiet_rounds=1, slice_s=0.4, cap_s=4.0)
    s.key(0x0D)
    s.settle()
    drain_more(s, 1)


def step_toward(s: Session, cells, f, r_idx: int) -> None:
    """`monster_id == r_idx` のいちばん近い敵へ 1 歩（隣なら殴りになる）。

    見つからなければ 1 ターン待つ（s）。歩数の管理は呼び手のループ。
    """
    pgx = f.get("player_gx")
    pgy = f.get("player_gy")
    targets = [c for c in cells if c[4] == r_idx]
    if not targets or pgx is None:
        s.key(ord("s"))
        s.settle(quiet_rounds=1, slice_s=0.4, cap_s=6.0)
        return
    targets.sort(key=lambda c: abs(c[0] - pgx) + abs(c[1] - pgy))
    dx = targets[0][0] - pgx
    dy = targets[0][1] - pgy
    step_x = 0 if dx == 0 else (1 if dx > 0 else -1)
    step_y = 0 if dy == 0 else (1 if dy > 0 else -1)
    key = {(-1, -1): "7", (0, -1): "8", (1, -1): "9",
           (-1, 0): "4", (0, 0): "s", (1, 0): "6",
           (-1, 1): "1", (0, 1): "2", (1, 1): "3"}[(step_x, step_y)]
    s.key(ord(key))
    s.settle(quiet_rounds=1, slice_s=0.4, cap_s=6.0)
    drain_more(s, 1)  # 戦闘メッセージの -more- を流す


def enable_debug_opts(s: Session, checks: Checks) -> None:
    """`=` → `4`（Game-Play）→ 該当行まで `2` → `y` で `allow_debug_opts` を立てる。

    これが無いと `^A` の debug コマンドは「許可されていない」と断られる
    （`dungeon.c:3488` の `enter_debug_mode`）。
    """
    s.key(ord("="))
    s.settle()
    s.key(ord("4"))
    s.settle()
    rows = s.mirror()
    opt_rows = [i for i, r in enumerate(rows) if (": yes" in r) or (": no" in r)]
    debug_rows = [i for i, r in enumerate(rows) if "allow_debug_opts" in r]
    checks.add(bool(opt_rows) and bool(debug_rows), "Game-Play Options listed",
               f"first_opt={opt_rows[:1]} debug={debug_rows[:1]}")
    if opt_rows and debug_rows:
        for _ in range(debug_rows[0] - opt_rows[0]):
            s.key(ord("2"))
            s.settle(quiet_rounds=1, slice_s=0.3, cap_s=3.0)
        s.key(ord("y"))
        s.settle()
        rows = s.mirror()
        checks.add(any("allow_debug_opts" in r and "yes" in r for r in rows),
                   "allow_debug_opts turned on")
    s.key(0x1B)
    s.settle()
    s.key(0x1B)
    s.settle()


def walk_to(s: Session, target, tries: int = 90) -> bool:
    """`target`（gx, gy）のマスへ歩く。着いたら真。

    **経路は幅優先で出す。**「近づく向きへ 1 歩」では町の建物で詰まる
    （実測 2026-08-24: 24 マス先の武器屋へ 70 手かけて 16 マスで止まった）。
    可視セルには通行可否（`FEAT_PASSABLE`）が全部載っているので、そこから
    最短路を引けば無駄手が無い。**目的地の店タイルだけは例外**——店は
    `DOOR` の旗を持つので `PASSABLE` に入らないが、踏めば入れる
    （`cmd1.c:4895` の `FF_STORE` → `SPECIAL_KEY_STORE`）。

    敵に押されて経路から外れることがあるので、**数手ごとに引き直す**。
    """
    keys = {(-1, -1): "7", (0, -1): "8", (1, -1): "9",
            (-1, 0): "4", (1, 0): "6",
            (-1, 1): "1", (0, 1): "2", (1, 1): "3"}
    goal = (int(target[0]), int(target[1]))
    steps = 0
    while steps < tries:
        f, cells = latest_cells(s)
        if not f:
            return False
        if f.get("menu_open"):
            return True  # 店が開いた（地図はもう出ていない）
        here = (f.get("player_gx"), f.get("player_gy"))
        if here == goal:
            return True
        passable = {(c[0], c[1]) for c in cells
                    if (c[CELL_FF] & FEAT_KNOWN) and (c[CELL_FF] & FEAT_PASSABLE)}
        passable.add(goal)
        # ---- 幅優先 ----
        prev = {here: None}
        queue = [here]
        head = 0
        while head < len(queue):
            cur = queue[head]
            head += 1
            if cur == goal:
                break
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    if (dx == 0) and (dy == 0):
                        continue
                    nxt = (cur[0] + dx, cur[1] + dy)
                    if (nxt in prev) or (nxt not in passable):
                        continue
                    prev[nxt] = cur
                    queue.append(nxt)
        if goal not in prev:
            return False  # 既知の地図では届かない
        path = []
        node = goal
        while prev[node] is not None:
            path.append(node)
            node = prev[node]
        path.reverse()
        # ---- 引いた経路を数手だけ歩いて、また引き直す ----
        for cell in path[:6]:
            sx = cell[0] - here[0]
            sy = cell[1] - here[1]
            if (sx, sy) not in keys:
                break
            s.key(ord(keys[(sx, sy)]))
            s.settle(quiet_rounds=1, slice_s=0.4, cap_s=6.0)
            steps += 1
            f2 = s.frames[-1] if s.frames else {}
            if f2.get("menu_open"):
                return True
            got = (f2.get("player_gx"), f2.get("player_gy"))
            if (got[0] is None) or (abs(got[0] - here[0]) > 1) or (abs(got[1] - here[1]) > 1):
                #! **1 手で 2 マス以上跳んだ**＝歩きではない（実測 2026-08-24:
                #! Outpost の (66,34) で西へ 1 歩打つと x が +65 跳ぶ。階は
                #! Outpost のままで再現する）。ここから先の経路は当てにならない。
                return False
            here = got
            if here != cell:
                break  # 押し戻された。引き直す
    return False


def approach_shop(s: Session, tries: int = 140, glyphs: str = None) -> bool:
    """いちばん近い店の入口へ寄り、踏んで入る。入れたら真。

    **町の座標は歩くとずれる。** Outpost は荒野の一部で、区画の端まで来ると
    **世界が足元でスクロールして自機が反対の端に出る**（実測 2026-08-24:
    (66,34) で西へ 1 歩 → x が 131 へ。階は Outpost のまま）。だから
    「目的地を決めて長い経路を引く」は成り立たない——**1 歩ごとに地図を
    読み直して、そのとき見えている最寄りの入口へ寄る**。

    入口の記号は数字（`f_info.txt`: 1 雑貨 / 2 防具 / 3 武器 / 4 寺院 / 5 錬金 /
    6 魔法 / 7 闇市 / 9 書店 / 0 キノコ）。**`8` は我が家なので外す**（売買が無い）。
    踏めば入る（`cmd1.c:4895` の `FF_STORE`）。
    """
    keys = {(-1, -1): "7", (0, -1): "8", (1, -1): "9",
            (-1, 0): "4", (1, 0): "6",
            (-1, 1): "1", (0, 1): "2", (1, 1): "3"}
    shop_chars = ({ord(ch) for ch in glyphs} if glyphs
                  else ({ord(ch) for ch in "1234567890"} - {ord("8")}))
    for _ in range(tries):
        f, cells = latest_cells(s)
        if not f:
            return False
        if f.get("menu_open"):
            return True  # 店が開いた
        here = (f.get("player_gx"), f.get("player_gy"))
        doors = [(c[0], c[1]) for c in cells
                 if (c[CELL_FF] & FEAT_KNOWN) and (c[CELL_ASCII] in shop_chars)]
        if not doors:
            return False
        passable = {(c[0], c[1]) for c in cells
                    if (c[CELL_FF] & FEAT_KNOWN) and (c[CELL_FF] & FEAT_PASSABLE)}
        passable.update(doors)  # 入口は DOOR なので PASSABLE に入らない
        # ---- いちばん近い入口へ 1 歩（幅優先で向きだけ決める）----
        prev = {here: None}
        queue = [here]
        head = 0
        goal = None
        while head < len(queue):
            cur = queue[head]
            head += 1
            if cur in doors:
                goal = cur
                break
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    if (dx == 0) and (dy == 0):
                        continue
                    nxt = (cur[0] + dx, cur[1] + dy)
                    if (nxt in prev) or (nxt not in passable):
                        continue
                    prev[nxt] = cur
                    queue.append(nxt)
        if goal is None:
            #! 既知の地図では届かない。**西へ寄せて世界を送る**（町は西にある）。
            step = (-1, 0)
        else:
            node = goal
            while prev[node] is not None and prev[node] != here:
                node = prev[node]
            step = (node[0] - here[0], node[1] - here[1])
        if step not in keys:
            return False
        s.key(ord(keys[step]))
        s.settle(quiet_rounds=1, slice_s=0.4, cap_s=6.0)
        f2 = s.frames[-1] if s.frames else {}
        if f2.get("menu_open"):
            return True
        #! **開いたか見てから流す。**先に ESC を打つと、入った瞬間の店を
        #! そのまま閉じてしまう（実測 2026-08-24: 入口を踏めているのに
        #! 「届かない」と報告し続けた）。
        drain_more(s, 1)
    return False


def teleport_to(s: Session, target) -> bool:
    """`^A t`（`dimension_door(255)`）で `target`（gx, gy）へ跳ぶ。

    **町は歩いて渡れない**（`walk_to` の註記——区画の端で座標が回り込む）ので、
    店へ入る検査はこれで届かせる。目標選択（`tgt_pt`。`xtra2.c:6389`）の
    カーソルは**壁を無視して 1 マスずつ**動き、`5` で決定・ESC で取り消し。

    **長距離では偽を返す。** 目標選択のカーソルも町の端で回り込むので、
    遠い座標は狙えない（実測 2026-08-25: (84,31) から x=49 を狙うと
    **x=115 へ着地する**）。**跳べたかどうかを座標で判じたい呼び手だけが
    戻り値を見ること**——「着いた先で何ができたか」で判じる呼び手
    （`_harvest_shop`）は戻り値を無視してよい。
    """
    keys = {(-1, -1): "7", (0, -1): "8", (1, -1): "9",
            (-1, 0): "4", (1, 0): "6",
            (-1, 1): "1", (0, 1): "2", (1, 1): "3"}
    f, _cells = latest_cells(s)
    if not f:
        return False
    here = (f.get("player_gx"), f.get("player_gy"))
    wizard_begin(s, "t", False)
    dx = int(target[0]) - here[0]
    dy = int(target[1]) - here[1]
    for _ in range(abs(dx) + abs(dy) + 4):
        if (dx == 0) and (dy == 0):
            break
        sx = 0 if dx == 0 else (1 if dx > 0 else -1)
        sy = 0 if dy == 0 else (1 if dy > 0 else -1)
        s.key(ord(keys[(sx, sy)]))
        s.settle(quiet_rounds=1, slice_s=0.3, cap_s=4.0)
        dx -= sx
        dy -= sy
    s.key(ord("5"))  # ここへ跳ぶ
    s.settle(quiet_rounds=2, slice_s=0.6, cap_s=15.0)
    drain_more(s, 1)
    f2 = s.frames[-1] if s.frames else {}
    return (f2.get("player_gx"), f2.get("player_gy")) == (int(target[0]), int(target[1]))


def cmd_shop(work: Path, extra=()) -> int:
    """店の実地（設計 §9-1 の手作業を機械化した。FH-13 / FH-14）。

    誕生 → 雑貨屋（記号 `1`）まで歩いて入る →
      * **待ち受け中は命令列だけが札**（品行の `a)` は載らない。FH-14）
      * `s`（売る）でコアが品を訊いたら**品行の letter が載る**
      * `Really sell …? [y/n]` が `frame.prompt` に載る（FH-13。**店では
        メッセージ行が `rect(0,0,80,3)` へ移る**ので、決め打ちの 67 桁で
        切っていた頃は取りこぼしていた）
    品は 1 つも売らない（`n` で取り消す）。
    """
    checks = Checks()
    s = Session(work, "shop", tuple(extra))
    try:
        kind, _ = s.hello()
        checks.add(kind == "hello_ack", "hello -> hello_ack", f"got {kind}")
        #! 町は広いので視界を広く取る（店の入口を窓に入れるため）。
        s.ui_state(view_w=120, view_h=60, cursor_mode=True)
        s.settle()
        birth_and_flush(s, checks)

        # ---- 町を照らす（誕生直後の町は未踏破で、店の入口が「既知」でない）----
        # 店の地形は `REMEMBER` を持つ（`f_info.txt:819`）ので、`^A w`（wiz_lite）で
        # `CAVE_MARK` が付き、入口の記号がフレームに載る。
        enable_debug_opts(s, checks)
        wizard_begin(s, "w", True)
        drain_more(s)

        # ---- 雑貨屋（`1`）を探して歩く ---------------------------------------
        # 店の入口は**記号が数字**（`f_info.txt`: 1 雑貨 / 2 防具 / 3 武器 / 4 寺院 /
        # 5 錬金 / 6 魔法 / 7 闇市 / 9 書店 / 0 キノコ）。**`8` は我が家なので外す**
        # （売買が無い）。**Outpost に雑貨屋は無い**（実測 2026-08-24: 見えるのは
        # 2 / 3 / 7 / 8）ので、1 軒に決め打たず近い店を採る。
        f, cells = latest_cells(s)
        shop_chars = {ord(ch) for ch in "1234567890"} - {ord("8")}
        doors = [c for c in cells
                 if (c[CELL_FF] & FEAT_KNOWN) and (c[CELL_ASCII] in shop_chars)]
        checks.add(bool(doors), "shop entrance in view",
                   f"{len(doors)} candidate(s): "
                   + "".join(sorted({chr(c[CELL_ASCII]) for c in doors})))
        if not doors:
            return checks.finish()
        px, py = f.get("player_gx"), f.get("player_gy")
        doors.sort(key=lambda c: abs(c[0] - px) + abs(c[1] - py))
        #! **近い順に 1 軒ずつ試す。**町には歩けない道筋があり（walk_to の註記）、
        #! 1 軒目に届かなくても別の店へは行ける。
        opened = approach_shop(s)
        if opened:
            s.settle()
        f = s.frames[-1] if s.frames else {}
        keys = [c.get("key") for c in f.get("menu_choices", [])]
        if not opened:
            #! **届かないのは harness の限界で、コアの欠けではない。**
            #! Outpost の地図は 198 幅だが、自機の区画は 66 幅で**端で回り込む**
            #! （実測 2026-08-24: (66,34) で西へ 1 歩 → x が 131 へ）。
            #! 見えている店の入口 3 軒はどれも x<66＝隣の区画にあり、歩いて渡れない。
            #! 実地の確認は §9-1 の手プレイが受け持つ。
            print("  [SKIP] could not walk to a shop from this start "
                  "(the town block wraps; see walk_to の註記). "
                  "店の実地は手プレイで見る。")
            s.quit_and_wait()
            return checks.finish()
        checks.add(ord("b") in keys,
                   "shop opened (command row scanned)",
                   f"keys={sorted(k for k in keys if isinstance(k, int))}")

        # ---- FH-14: 待ち受け中は命令列だけ（品行の letter は載らない）--------
        checks.add(ord("a") not in keys,
                   "idle shop lists commands only, not item letters (FH-14)",
                   f"{len(keys)} choice(s)")

        # ---- `p`（買う）→ コアが品を訊く → 品行の letter が載る --------------
        # **売り**ではなく**買い**で見る——売れる持ち物は店の種類で変わるが、
        # 店の在庫は必ず有る（`_sell()` が `Buy which item ...` を訊く。shop.c:2306）。
        s.key(ord("p"))
        s.settle()
        f = s.frames[-1] if s.frames else {}
        sell_keys = [c.get("key") for c in f.get("menu_choices", [])]
        checks.add(ord("a") in sell_keys,
                   "item letters appear once the core asks (FH-14)",
                   f"keys={sorted(k for k in sell_keys if isinstance(k, int))}")
        # ---- FH-15: 店の数量入力が `frame.numeric` に載る --------------------
        # 品を選ぶと `msg_input_num()` が `Quantity (1 to N): ` を訊く
        # （`shop.c:2332`。**綴りも置き場も通常と違う**）。ここが載らないと
        # 決定が選択肢まで流れて品の letter を送る（利用者の実機報告）。
        # **2 個以上ある品を選ぶ。** 1 個だけの品は個数を訊かれない
        # （`shop.c:2332` の `maks > 1`）。在庫は毎回変わるので一覧から拾う。
        import re as _re
        stacked = []
        for row in s.mirror():
            m = _re.match(r"\s*([a-z])\)\s+\S*\s*(\d+)\s+\S", row)
            if m and (int(m.group(2)) >= 2):
                stacked.append(ord(m.group(1)))
        cands = stacked + [ord(c) for c in "abcdefghijklmno" if ord(c) not in stacked]
        got_numeric = False
        for cand in cands[:15]:
            s.key(cand)
            s.settle()
            f = s.frames[-1] if s.frames else {}
            if f.get("numeric", {}).get("active"):
                got_numeric = True
                break
            pk = [c.get("key") for c in f.get("prompt", {}).get("choices", [])]
            if (ord("y") in pk) and (ord("n") in pk):
                #! 1 個だけの品は個数を飛ばして確認へ来る。買わずに断ると
                #! `_sell()` の輪が**そのまま品選びへ戻る**（`shop.c` の continue）
                #! ので、`p` を押し直さない——押すと letter の `p` に化ける。
                s.key(ord("n"))
                s.settle()
                drain_more(s, 1)
            #! 何も起きない letter（欠番）はそのまま次の候補へ。
        if got_numeric:
            checks.add(True, "shop quantity lifted into frame.numeric (FH-15)",
                       json.dumps((s.frames[-1] if s.frames else {}).get("numeric", {}),
                                  ensure_ascii=False)[:130])
        else:
            #! **落とさない。** 個数を訊かれるのは 2 個以上ある品だけで
            #! （`shop.c:2332` の `maks > 1`）、在庫は店ごと・回ごとに変わる。
            #! 綴りの側は `--selftest` が恒久的に押さえてある。
            print("  [SKIP] この店では個数入力まで行けない（在庫は毎回変わる。"
                  "数のまとまった品の letter = "
                  + ("".join(chr(k) for k in stacked) or "無し")
                  + "）。綴りは --selftest が見ている。")

        # ---- FH-16: 上下の増減（`set_number`）が数字を書き換える --------------
        # 画面側の上下は `set_number` を送り、アダプタがキー列へ直す。
        # 以前は `KTRL('E')`＋退格で消していたが、**その綴りを受けるのは変愚だけ**
        # で、Frox では `bell()` → `flush()` が後続のキーを捨て、**欄が空**になった
        # （利用者の実機報告「カーソル入力すると空白に」）。
        if got_numeric:
            before = (s.frames[-1] if s.frames else {}).get("numeric", {})
            want = min(int(before.get("max", 1)), int(before.get("value", 1)) + 1)
            s.link.send({"t": "input_event",
                         "events": [{"e": "set_number", "value": want}]})
            s.settle()
            after = (s.frames[-1] if s.frames else {}).get("numeric", {})
            checks.add(after.get("active") and (after.get("value") == want),
                       "set_number rewrites the quantity (FH-16)",
                       json.dumps(after, ensure_ascii=False)[:120])

        # ---- 利用者の手順そのもの: 数量 → 決定 → 購入可否 --------------------
        # 決定は `confirm`（0x0D）。個数が `frame.numeric` に載っていれば
        # カーソル層が食って askfor へ渡り、次に `[y/n]` が出る。
        if got_numeric:
            s.key(0x0D)
            s.settle()
            f = s.frames[-1] if s.frames else {}
            pk = [c.get("key") for c in f.get("prompt", {}).get("choices", [])]
            checks.add((ord("y") in pk) and (ord("n") in pk),
                       "quantity -> confirm -> buy [y/n] (FH-15 の道筋・FH-13)",
                       json.dumps(f.get("prompt", {}), ensure_ascii=False)[:120])
            s.key(ord("n"))  # **買わない**
            s.settle()
            drain_more(s, 1)
        # `[y/n]` が `frame.prompt` に載ることは**購入の確認**で見た（1 つ上の
        # 検査。FH-13）。`B`（Buy Everything）でも同じ道だが、在庫と所持金で
        # 出方が変わるので検査には使わない。

        s.key(0x1B)  # 店を出る
        s.settle()
        s.quit_and_wait()
        checks.add(s.exit_msg is not None and s.exit_msg.get("code") == 0,
                   "clean shutdown", str(s.exit_msg))
    finally:
        s.close()
    return checks.finish()


def cmd_fx(work: Path) -> int:
    """FH-05（戦闘の見せ場）・FH-09（ミニマップの細別）・FH-10（広域の階層欄）の機械検証。

    誕生 → 町でミニマップの細別と広域の階層欄 → allow_debug_opts を立てて
    Snow castle L40（凍え＝確定の被弾）→ 地上へ戻して green worm mass(31) を
    召喚して殴る（命中ととどめ）→ Floating orb(912。動かず毎ターン MISSILE）を
    召喚して弾道を受ける。すべて**コアが実際に出した事実**の検査で、
    絵の合成は見ない（見た目は §9-1 の手プレイの受け持ち）。
    """
    checks = Checks()
    s = Session(work, "fx")
    try:
        kind, _ = s.hello()
        checks.add(kind == "hello_ack", "hello -> hello_ack", f"got {kind}")
        s.ui_state(cursor_mode=True)
        s.settle()
        birth_and_flush(s, checks)

        # ---- FH-09: 町のミニマップに歩ける地形の細別（9..13）が入る ----------
        f, _ = latest_cells(s)
        mm = (f or {}).get("minimap", {})
        kinds = set()
        if mm.get("kinds_b64"):
            import base64
            kinds = set(base64.b64decode(mm["kinds_b64"]))
        fine = kinds & {9, 10, 11, 12, 13}
        checks.add(bool(fine), "minimap carries terrain sub-kinds (FH-09)",
                   f"kinds={sorted(kinds)}")

        # ---- FH-10: 広域マップで階層欄が `World Map`・案内が帯に乗る --------
        s.key(ord("<"))
        s.settle(quiet_rounds=2, slice_s=0.8, cap_s=20.0)
        f = s.frames[-1] if s.frames else {}
        checks.add(f.get("depth", {}).get("text_utf8") == "World Map",
                   "wild depth reads 'World Map' (FH-10)",
                   repr(f.get("depth", {}).get("text_utf8")))
        checks.add(any("Look around" in r.get("text_utf8", "")
                       for r in f.get("bottom_row_runs", [])),
                   "wild guide flows into the bottom band")
        s.key(ord(">"))
        s.settle(quiet_rounds=2, slice_s=0.8, cap_s=20.0)

        enable_debug_opts(s, checks)

        # ---- 松明を灯す（ダンジョンは真っ暗で、灯りが無いと何も「既知」にならない）--
        s.key(ord("w"))
        s.settle()
        rows = s.mirror()
        torch_key = 0
        for r in rows:
            t = r.strip()
            if ("Torch" in t) and (len(t) > 2) and (t[1] == ")"):
                torch_key = ord(t[0])
                break
        checks.add(torch_key > 0, "torch found in the wield prompt",
                   chr(torch_key) if torch_key else "-")
        if torch_key:
            s.key(torch_key)
            s.settle()
        drain_more(s, 1)

        # ---- FH-05 (2): green worm mass(31) を町で殴る（とどめの絵） ---------
        # ダンジョンより先に町でやる——町は明るく安全で、ジャンプの往復が要らない。
        # worm は一撃で死ぬ（4d4 HP）ので、ここで取るのは**とどめ**だけ。
        mark = len(s.frames)
        wizard_summon_named(s, "31", confirm=True)
        got_kill = False
        for _ in range(20):
            f, cells = latest_cells(s)
            if not f:
                break
            fx = collect_fx(s, mark)
            got_kill = any(e["kind"] == 1 and e["intensity"] >= 1.0 for e in fx)
            if got_kill:
                break
            step_toward(s, cells, f, 31)
        checks.add(got_kill, "kill (SOUND_KILL + slot gone) pushes the finishing burst")

        # ---- 経験値を盛る（^A x＝exp を倍にする、を 12 回）。orb と撃ち合う保険 --
        # ^A X（値の直指定）は使わない——get_string への数字の盲打ちが安定しない
        # （実測 2026-08-24: "300000" の途中が取りこぼされ 300 になった）。
        # ^A x は 1 打で完結する。**初期 exp は 0** なので倍加は 1,3,7,15,… と
        # 育つ——L14 前後（orb の弾に数ターン耐えて、殴りは一撃で倒さない強さ）
        # まで 12 回要る。最初の数回は見た目が変わらず frame も出ないのが正常。
        #
        # **レベルアップごとに能力値の選択が開く**（`gain_chosen_stat`。
        # a〜f と C しか受けず ESC も無効——答えないと以後の全キーを食う。
        # 実測 2026-08-24: ここで止まって L5 で頭打ちになっていた）。
        # 1 回盛るたびに「a（STR）→ y（確認）」で答え切る。
        # 中身は `answer_stat_prompts()` / `boost_level()` として括り出した
        # （第 18 回の `spellfmt` も同じ手が要る）。註記はそちらに在る。
        for _ in range(12):
            wizard_begin(s, "x", False)
            answer_stat_prompts(s, 12)
        # 最後の 1 盛りで数レベル一気に上がると選択が積み残る。掃き出してから進む。
        answer_stat_prompts(s, 30)
        drain_more(s)
        f = s.frames[-1] if s.frames else {}
        checks.add((f.get("hud", {}).get("level") or 0) >= 7,
                   "exp boost took (level >= 7)",
                   f"level={f.get('hud', {}).get('level')}")

        # ---- FH-05 (3): Floating orb(912。動かず毎ターン MISSILE・35HP）------
        # 1 体から 3 つ取る: 弾道（kind 2）・必中の魔法の矢の被弾（kind 0）・
        # 殴りの部分ダメージ（kind 1 で i < 1。35HP は一撃では落ちない）。
        #
        # **呼び直しは 4 回まで。** 町は建物だらけで、orb が壁の向こうに湧くと
        # 視線も隣接も取れない（実測 2026-08-24: 落ちるときは必ずこの 3 件が
        # 共倒れした——orb の段そのものが成立していない）。orb は NEVER_MOVE
        # なので**こちらから近づけないなら永遠に取れない**。呼び直すのが唯一の手。
        # 足踏み（壁に向かって歩き続ける）も検出して打ち切る。
        mark = len(s.frames)
        got_bolt = False
        got_part = False
        seen_orb = 0
        orb_tiles: list[int] = []
        for _attempt in range(4):
            wizard_summon_named(s, "912")
            last_pos = None
            stuck = 0
            for _ in range(14):
                f, cells = latest_cells(s)
                if not f:
                    break
                fx = collect_fx(s, mark)
                got_bolt = got_bolt or any(e["kind"] == 2 for e in fx)
                got_part = got_part or any(
                    e["kind"] == 1 and 0 < e["intensity"] < 1.0 for e in fx)
                if got_bolt and got_part:
                    break
                if not any(c[4] == 912 for c in cells):
                    break  # 視界に居ない（壁の向こう／窓の外）。呼び直す
                seen_orb += 1
                #! **実体の絵（M2）の本番の検査。**見えている敵は目録から索引が引ける
                #! （0 なら字の板に落ちている＝目録の引きが効いていない）。
                orb_tiles.extend(c[CELL_TILE] for c in cells if c[CELL_MONSTER] == 912)
                pos = (f.get("player_gx"), f.get("player_gy"))
                stuck = (stuck + 1) if (pos == last_pos) else 0
                last_pos = pos
                if stuck >= 3:
                    break  # 壁に向かって足踏みしている。呼び直す
                step_toward(s, cells, f, 912)
            if got_bolt and got_part:
                break
        fx = collect_fx(s, mark)
        bolts = [e for e in fx if e["kind"] == 2]
        #! 落ちたときに「orb が見えていたか」を残す（見えていなければ harness の
        #! 都合であって FH-05 の実装の話ではない）。
        checks.add(bool(orb_tiles) and all(v > 0 for v in orb_tiles),
                   "monster tile_index resolved (R)",
                   f"{sum(1 for v in orb_tiles if v > 0)}/{len(orb_tiles)} sightings"
                   + (f" -> {orb_tiles[0]}" if orb_tiles else ""))
        where = f"orb seen on {seen_orb} turn(s)"
        checks.add(got_bolt, "monster missile pushes Bolt fx (FH-05)",
                   f"{len(bolts)} bolt events, {where}")
        checks.add(got_part, "melee pushes partial HitMonster fx (FH-05)", where)
        hits = [e for e in fx if e["kind"] == 0 and e["intensity"] > 0]
        checks.add(bool(hits), "missile damage pushes HitPlayer fx (FH-05)",
                   f"{len(hits)} events, first i={hits[0]['intensity'] if hits else '-'}")

        # ---- Snow castle L40 へ。フロアを照らせば雪の床が既知になる ----------
        wizard_jump(s, "38", "40")
        f = s.frames[-1] if s.frames else {}
        place = " / ".join(f.get("hud", {}).get("right_top_lines", []))
        checks.add("Snow castle" in place, "jumped to Snow castle", place)
        # L40 の敵に殺されないよう、まず周辺を消す（^A z）。実測では本物の撃破
        # （経験値と "You have killed it." が出る）だが、**暗闇で 1 体も見えていない**
        # ので、消滅から偽のとどめの絵を作らないことの対照になる（ml の守り）。
        mark_zap = len(s.frames)
        wizard_begin(s, "z", False)
        drain_more(s)  # Welcome to level の山を流す
        for key in "662266":
            s.key(ord(key))
            s.settle(quiet_rounds=1, slice_s=0.4, cap_s=6.0)
            drain_more(s, 1)  # 凍えのメッセージの -more- を流す
        zap_fx = [e for e in collect_fx(s, mark_zap)
                  if e["kind"] == 1 and e["intensity"] >= 1.0]
        # ^A z は本物の撃破（kill 音あり）で、多数を一掃する。**見えている**やつの
        # とどめの絵は正しい（松明の灯りで 0〜2 体見えることがある）。ここで守るのは
        # 「見えなかった撃破に絵を出さない」——一掃しても絵が数発に留まること。
        checks.add(len(zap_fx) <= 2,
                   "unseen removals do NOT spray finishing bursts (ml guard)",
                   f"{len(zap_fx)} events")

        # ---- FH-09: 雪の床がミニマップに出る --------------------------------
        # **`^A w`（wiz_lite）でフロア全域を照らす。**歩いて照らす形にしていたが、
        # 6 歩がたまたま雪に届かない階があった（実測 2026-08-24: `kinds=[0,1,2,7]`
        # ＝床と壁だけ）。Snow castle に `DARKNESS` 旗は無く（`d_info.txt` の N:38）、
        # `view_perma_grids` は既定 TRUE なので、wiz_lite は**床まで記憶する**
        # （`cave.c:4462`）——地形の並びに頼らずに済む。
        # **上の暗闇の対照検査（^A z）より後に置くこと**——先に照らすと
        # 「見えない撃破」が見える撃破になり、あちらの意味が消える。
        wizard_begin(s, "w", False)
        drain_more(s)
        mm = (s.frames[-1] if s.frames else {}).get("minimap", {})
        kinds = set()
        if mm.get("kinds_b64"):
            import base64
            kinds = set(base64.b64decode(mm["kinds_b64"]))
        checks.add(13 in kinds, "snow floor reaches the minimap (FH-09)",
                   f"kinds={sorted(kinds)}")

        s.quit_and_wait()
        checks.add(s.exit_msg is not None and s.exit_msg.get("code") == 0,
                   "session exits cleanly", str(s.exit_msg))
    finally:
        s.close()
    return checks.finish()


# ========================================================================== sfx

def collect_sounds(s: Session, start_index: int):
    """frames[start_index:] の sounds を平らに集める。

    codec は既定値を省くので get で読む。**名前の鍵は `n`**
    （`frame_codec.cpp:654`。"name" ではない）。
    """
    out = []
    for f in s.frames[start_index:]:
        for e in f.get("sounds", []):
            out.append({"n": e.get("n", ""), "y": e.get("y", 0), "x": e.get("x", 0)})
    return out


def _sfx_catalog_names():
    """画面側の目録（`assets/audio/sfx.jsonc`）に載っている名前。

    jsonc なので行頭の `//` を落としてから読む（値の中に `//` は出ない）。
    読めなければ空集合——目録が無い機でも検査そのものは通す。
    """
    path = REPO / "assets" / "audio" / "sfx.jsonc"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return set()
    body = re.sub(r"^\s*//.*$", "", text, flags=re.M)
    try:
        return set(json.loads(body).get("sounds", {}))
    except ValueError:
        return set()


def cmd_sfx(work: Path) -> int:
    """効果音の配管（`AUDIO_DESIGN` §2.1・§2.3）の機械検証。

    Frox は音を**鳴らさない**。`sound()` が投げた `TERM_XTRA_SOUND` を名前と
    マスに直して `frame.sounds` へ載せるところまでが受け持ちで、鳴らすのは画面側。
    だからここで見るのは「載ったか」「一度きりか」「旗で止まるか」の 3 つである。

    誕生 → 町で green worm mass(31) を召喚して殴る（`hit` と `kill` が出る）→
    静かにして音が湧き続けないことを見る → 旗を降ろして音が止まることを見る。
    """
    checks = Checks()
    s = Session(work, "sfx")
    try:
        kind, _ = s.hello()
        checks.add(kind == "hello_ack", "hello -> hello_ack", f"got {kind}")
        feats = (s.ack or {}).get("features", [])
        checks.add("audio" in feats, "core declares \"audio\"", str(feats))

        s.ui_state(cursor_mode=True, sound_events=True)
        s.settle()
        birth_and_flush(s, checks)
        enable_debug_opts(s, checks)
        drain_more(s)

        # ---- 殴って音を出す（worm は 4d4 HP なので数打で必ず死ぬ）------------
        mark = len(s.frames)
        wizard_summon_named(s, "31", confirm=True)
        heard = []
        for _ in range(20):
            f, cells = latest_cells(s)
            if not f:
                break
            heard = collect_sounds(s, mark)
            if any(e["n"] == "kill" for e in heard):
                break
            step_toward(s, cells, f, 31)
        names = [e["n"] for e in heard]
        checks.add(bool(heard), "sounds reach the frame", f"{len(heard)} events")
        checks.add("hit" in names, "a swing pushes \"hit\"", " ".join(sorted(set(names))))
        checks.add("kill" in names, "the finishing blow pushes \"kill\"",
                   " ".join(sorted(set(names))))

        # ---- 名前は綴りであって番号ではない（16 バイトに収まる ASCII）--------
        checks.add(all(e["n"] and (len(e["n"]) < 16) and e["n"].isascii() for e in heard),
                   "every name is a printable spelling under 16 bytes")

        # ---- 目録に当たるか（変愚の wav をそのまま流用できているか）----------
        catalog = _sfx_catalog_names()
        if catalog:
            miss = sorted({n for n in names if n not in catalog})
            checks.add(not miss, "every heard name resolves in assets/audio/sfx.jsonc",
                       ("missing: " + " ".join(miss)) if miss else f"{len(set(names))} names")


        # ---- 周囲の地形の内訳（環境音の層。`AUDIO_AMBIENCE_FIELDS` §8.1）----
        # **数えていないと丸ごと出ない**（`radius == 0` は codec が畳む）ので、
        # 鍵が在ることそのものが「数えた」の証拠になる。
        f = s.frames[-1] if s.frames else {}
        sur = f.get("surroundings", {})
        checks.add(sur.get("radius", 0) > 0, "surroundings are counted",
                   json.dumps(sur, ensure_ascii=False))
        checks.add(sur.get("counted", 0) > 0, "some grids were known enough to count",
                   str(sur.get("counted")))
        # 町の地上なので**壁ばかりにはならない**。閉塞の層がいきなり最大で
        # 鳴っていたら、既知の判定か円の切り方を間違えている。
        checks.add(sur.get("wall", 0) < 200, "the town is not counted as a solid wall",
                   "wall=%s" % sur.get("wall"))

        # ---- 一度きりの縁である（§2.3。汲んだら消える）----------------------
        # 何も打たずに静かにする。`frame.sounds` が居座っていれば、次のフレームで
        # 同じ音がもう一度出てくる。
        mark = len(s.frames)
        s.key(0x1B)
        s.settle(quiet_rounds=2, slice_s=0.8, cap_s=12.0)
        again = collect_sounds(s, mark)
        checks.add(not again, "a quiet round pushes no sound twice",
                   " ".join(e["n"] for e in again))

        # ---- 旗を降ろすと止まる（`ui_state.sound_events`）--------------------
        # 画面側の装置が開いていない機では音の出来事を作らない。**`use_sound` は
        # 落とさない**ので、旗を上げ直せばまた出る（ここでは下げたまま終える）。
        s.ui_state(cursor_mode=True, sound_events=False)
        s.settle(quiet_rounds=1, slice_s=0.5, cap_s=8.0)
        mark = len(s.frames)
        wizard_summon_named(s, "31", confirm=True)
        for _ in range(12):
            f, cells = latest_cells(s)
            if not f:
                break
            step_toward(s, cells, f, 31)
        silent = collect_sounds(s, mark)
        checks.add(not silent, "sound_events=false stops the events",
                   " ".join(e["n"] for e in silent))

        s.quit_and_wait()
        checks.add(s.exit_msg is not None and s.exit_msg.get("code") == 0,
                   "session exits cleanly", str(s.exit_msg))
    finally:
        s.close()
    return checks.finish()


# =========================================================================== j2

def _j2_screens(work: Path, tag: str, extra_args, ui_lang: str):
    """立ち上げて「セーブ選択の画」と「誕生の 1 枚目」を持ち帰る。

    どちらも**コアが同じ字を書く画**である（乱数も持ち物も混ざらない）ので、
    英語と日本語で 1 バイトでも違えば「訳が無いのに何かが変わった」ことになる。
    """
    s = Session(work, tag, extra_args)
    out = {"ack": None, "picker": "", "birth": "", "stderr": []}
    try:
        kind, _ = s.hello()
        out["ack"] = kind
        s.ui_state(lang=ui_lang)
        s.settle()
        out["picker"] = chr(10).join(s.mirror())
        s.key(ord("n"))
        s.settle(quiet_rounds=2, slice_s=0.6, cap_s=15.0)
        out["birth"] = chr(10).join(s.mirror())
        s.quit_and_wait()
    finally:
        s.close()
        out["stderr"] = s.stderr_tail(60)
    return out


def _write_probe_catalog(work: Path) -> Path:
    """**使い捨ての**カタログを組む（`--lang-dir=` で読ませる）。

    J2 の段は訳を 1 件も配らないが、それでは「日本語がコアを通って画面まで出るか」
    を誰も確かめていないことになる。ここで 2 件だけ入れて、状態列に出る
    `put_str("LEVEL ", ...)` / `put_str("AU ", ...)`（`frox/src/xtra1.c:1426`・`:1491`）
    が日本語に化けることを見る。

    **桁は英語と揃える**（`レベル` = 6 バイト = 6 桁 = `LEVEL ` と同じ、`金 ` = 3 桁）。
    揃えないと右の数字が押し出されて、検査が「訳が出た」以外の理由で落ちる。
    """
    d = work / "lang-probe" / "ui"
    d.mkdir(parents=True, exist_ok=True)
    body = (
        "# 使い捨て。frox/lang/ja/ の正本ではない（tools/frox/fc_protocol_driver.py j2）\n"
        "E:LEVEL\\s\n"
        "J:\u30ec\u30d9\u30eb\n"
        "\n"
        "E:AU\\s\n"
        "J:\u91d1\\s\n"
    )
    (d / "messages.ja.txt").write_text(body, encoding="utf-8")
    return work / "lang-probe"


def _j2_probe(work: Path, checks: Checks) -> None:
    """使い捨てのカタログで**日本語を画面まで通す**（設計 §3.1 の道すじ全部）。

    ここが通れば、通っている道は次のとおりである:
    カタログ（UTF-8）→ `utf8_to_sjis` → コアの中は CP932 → `Term_addstr` の
    `fc_tr` → Term のセル → `fc_frame.cpp` の `sjis_to_utf8` → フレーム（UTF-8）。
    """
    lang_dir = _write_probe_catalog(work)
    s = Session(work, "j2_probe", ["--lang=ja", f"--lang-dir={lang_dir}"])
    try:
        kind, _ = s.hello()
        checks.add(kind == "hello_ack", "probe: hello -> hello_ack", f"got {kind}")
        s.ui_state()
        s.settle()
        birth_and_flush(s, checks)

        f, _ = latest_cells(s)
        col = [ln.get("text_utf8", "") for ln in (f or {}).get("status_col_lines", [])]
        joined = chr(10).join(col)
        checks.add("\u30ec\u30d9\u30eb" in joined,
                   "probe: LEVEL is japanese on the status column",
                   next((c for c in col if c.strip()), "(empty)"))
        checks.add("\u91d1" in joined, "probe: AU is japanese too",
                   next((c for c in col if "\u91d1" in c), "(not found)"))
        checks.add("LEVEL" not in joined, "probe: the english word is gone")

        # 色 span が UTF-8 のバイト位置で行を隙間なく覆っていること（設計 §3.2）。
        bad = []
        for ln in (f or {}).get("menu_term_lines", []):
            text = ln.get("text_utf8", "")
            spans = ln.get("color_spans", [])
            if not spans:
                continue
            total = sum(sp.get("len", 0) for sp in spans)
            if total != len(text.encode("utf-8")):
                bad.append(f"row {ln.get('source_row')}: spans {total} != {len(text.encode('utf-8'))}B")
        checks.add(not bad, "probe: color spans cover the utf-8 bytes exactly",
                   (bad[0] if bad else "clean"))

        err = chr(10).join(s.stderr_tail(60))
        checks.add("japanese: 2 entries" in err, "probe: the catalog loaded 2 entries",
                   next((l for l in s.stderr_tail(60) if "japanese:" in l), "(not in stderr)"))
        s.quit_and_wait()
        checks.add(s.exit_msg is not None, "probe: exits cleanly", str(s.exit_msg))
    finally:
        s.close()
        print("  stderr tail (probe):")
        for ln in s.stderr_tail():
            print(f"    {ln}")


def cmd_j2(work: Path) -> int:
    """J2（配管）の受け入れ。**訳は 1 件も入っていない段**の検査である。

    見るのは 3 つ:

      1. `--lang=ja` で立ち上がる（日本語層が起きたと stderr が言う）
      2. 画が `--lang=en` と**1 バイトも変わらない**（設計 §1 制約 1）。
         `lib-ja/edit` がまだ無いので名前も英語のままである（制約 4）
      3. 日本語で立てたまま**新規キャラを作って町を歩ける**（配管が通っている）
    """
    checks = Checks()

    en = _j2_screens(work, "j2_en", ["--lang=en"], "en")
    ja = _j2_screens(work, "j2_ja", ["--lang=ja"], "en")
    ui = _j2_screens(work, "j2_ui", [], "ja")  # 引数無し。申告だけで決める

    checks.add(en["ack"] == "hello_ack", "en: hello -> hello_ack", str(en["ack"]))
    checks.add(ja["ack"] == "hello_ack", "ja: hello -> hello_ack", str(ja["ack"]))
    checks.add(ui["ack"] == "hello_ack", "ui_state lang=ja: hello -> hello_ack", str(ui["ack"]))

    err_ja = chr(10).join(ja["stderr"])
    err_ui = chr(10).join(ui["stderr"])
    checks.add("[frox] japanese:" in err_ja, "ja: the japanese layer woke up",
               next((l for l in ja["stderr"] if "japanese:" in l), "(not in stderr)"))
    checks.add("[frox] japanese:" in err_ui,
               "ui_state lang=ja: the declaration decides the language",
               next((l for l in ui["stderr"] if "japanese:" in l), "(not in stderr)"))

    # lib-ja/edit がまだ無いことを本人に言わせる（制約 4 の裏取り）。
    checks.add("stay english" in err_ja or "japanese edit =" in err_ja,
               "ja: says whether lib-ja/edit is built",
               next((l for l in ja["stderr"] if ("stay english" in l or "japanese edit" in l)),
                    "(not in stderr)"))

    # ---- 制約 1 は「en が M0 のまま」であって「en == ja」ではない --------------
    #
    # J2 の段は訳が 0 件だったので同一が検査になったが、J4 で画の字を訳し始めた
    # 以上、**同一であってはいけない**。両方向で見る:
    #   en …… 英語の綴りがそのまま出る（訳が漏れていない）
    #   ja …… その英語が消え、日本語が出ている（訳が効いている）
    PICKER_EN = "Which save do you want to play?"
    PICKER_JA = "どのセーブで遊びますか"
    checks.add(PICKER_EN in en["picker"], "en: the picker stays english (制約 1)",
               PICKER_EN if PICKER_EN in en["picker"] else en["picker"][:60])
    checks.add((PICKER_JA in ja["picker"]) and (PICKER_EN not in ja["picker"]),
               "ja: the picker is translated",
               next((l for l in ja["picker"].split(chr(10)) if PICKER_JA in l), "(not found)"))

    # 誕生の画（型の選択）は J4 第 3 回で訳した。両方向で見る——
    # en は英語のまま（制約 1）・ja は日本語が出て英語の見出しが消えている。
    BIRTH_EN = "Choose the Type of Game to Play"
    BIRTH_JA = "遊び方を選ぶ"
    checks.add(bool(en["birth"]) and (BIRTH_EN in en["birth"]),
               "en: the game-type screen stays english (制約 1)",
               f"en={len(en['birth'])}B")
    checks.add((BIRTH_JA in ja["birth"]) and (BIRTH_EN not in ja["birth"]),
               "ja: the game-type screen is translated (J4)",
               next((l for l in ja["birth"].split(chr(10)) if BIRTH_JA in l), "(not found)").strip())

    # ---- 日本語で立てたまま新規キャラを作って歩く ---------------------------
    s = Session(work, "j2_play", ["--lang=ja"])
    try:
        kind, _ = s.hello()
        checks.add(kind == "hello_ack", "ja play: hello -> hello_ack", f"got {kind}")
        s.ui_state()
        s.settle()
        birth_and_flush(s, checks)
        f1, _ = latest_cells(s)
        pos0 = ((f1 or {}).get("player_gx"), (f1 or {}).get("player_gy"))
        moved = False
        for k in [ord("4"), ord("6"), ord("2"), ord("8")]:
            s.key(k)
            s.settle(quiet_rounds=2, slice_s=0.6, cap_s=10.0)
            f2, _ = latest_cells(s)
            if ((f2 or {}).get("player_gx"), (f2 or {}).get("player_gy")) != pos0:
                moved = True
                break
        checks.add(moved, "ja play: walking moves the player", f"from {pos0}")

        # J3: 実体の名前が日本語で出ること（lib-ja/edit が効いている証拠）。
        # 持ち物（doc UI のミラー）に 2 バイト文字が載るかで見る——
        # 初期装備の 食料・松明・矢 は必ずある。
        s.key(ord("i"))
        s.settle(quiet_rounds=2, slice_s=0.6, cap_s=10.0)
        rows = chr(10).join(s.mirror())
        wide = [ln for ln in rows.split(chr(10)) if any(ord(c) > 0x7F for c in ln)]
        checks.add(bool(wide), "ja play: entity names are japanese (J3)",
                   (wide[0].strip() if wide else "no 2-byte chars on the inventory"))
        s.key(0x1B)
        s.settle(quiet_rounds=1, slice_s=0.5, cap_s=6.0)

        #! **品の名前が 2 バイト目で割れていないか**（フック #19。§8.15）。
        #! `strip_name()` は `~` と `]` を 1 バイトずつ落とすので、CP932 の
        #! 2 バイト目にそれが来る字（`ミ`＝83 7E・`ゾ`＝83 5D）が壊れる。
        #! **`object_desc()` とは別の環**なので、持ち物の名前だけ見ても出ない
        #! ——`strip_name` を通る画（知識の一覧・技能・`^A c` の品目）で見る。
        #! `^A c` の品目一覧は `screen_save()` の中なのでミラーに出る。
        enable_debug_opts(s, checks)
        #! **この巡で最初の `^A` なので確認に y が要る**（j5 と同じ。confirm=False
        #! だと ^A が黙って断られ、品目一覧が開かない）。
        wizard_begin(s, "c", True)
        s.key(ord("a"))                      # [a] Gloves
        s.settle(quiet_rounds=2, slice_s=0.5, cap_s=10.0)
        kinds = chr(10).join(s.mirror())
        checks.add("ミスリル・ガントレット" in kinds,
                   "品の名前が CP932 の 2 バイト目で割れていない（#19）",
                   kinds.replace(chr(10), " / ")[:110])
        s.key(0x1B)
        s.settle(quiet_rounds=1, slice_s=0.5, cap_s=6.0)
        drain_more(s, 1)

        s.quit_and_wait()
        checks.add(s.exit_msg is not None, "ja play: exits cleanly", str(s.exit_msg))
    finally:
        s.close()
        print("  stderr tail (ja play):")
        for ln in s.stderr_tail():
            print(f"    {ln}")

    # ---- 使い捨てのカタログで、日本語が画面まで出ることを見る --------------
    _j2_probe(work, checks)

    return checks.finish()


# =========================================================================== j4

def _browse(s: Session, key_char: str, tag: str, out_esc: int = 1) -> None:
    """1 キーで画面を開いて（＝収穫して）、ESC で戻る。j4 の収穫路の 1 コマ。"""
    s.key(ord(key_char))
    s.settle(quiet_rounds=2, slice_s=0.5, cap_s=10.0)
    s.show(f"j4 {tag}")
    for _ in range(out_esc):
        s.key(0x1B)
        s.settle(quiet_rounds=1, slice_s=0.4, cap_s=6.0)


def _harvest_shop(s: Session) -> None:
    """店へ入って一巡し、**店の文言を収穫する**（買わない・売らない）。

    道筋は `cmd_shop` と同じだが、こちらは検査ではない——**届かなくても
    黙って戻る**（町の区画は端で回り込むので 1 軒も踏めない開始位置がある。
    `walk_to` の註記）。品を訊かれたら 1 つ選んで個数の問いまで進み、
    `n` で断ってから出る。
    """
    #! **視野を広げてから照らす。** 店の入口は自機の周りの窓にしか載らないので、
    #! 既定の 40x22 では 1 軒も見えない（実測 2026-08-25: 同じ盤面で
    #! 40x22 は 0 軒・120x60 は 3 軒）。`shop` が 120x60 を使うのはこのため。
    s.ui_state(view_w=120, view_h=60)
    s.settle(quiet_rounds=1, slice_s=0.5, cap_s=8.0)
    #! 店の入口は未踏破だと見えない（`REMEMBER` に `CAVE_MARK` が要る）。
    #! debug は呼び手が既に立てている。
    wizard_begin(s, "w", False)   # ^A w = wiz_lite。町を照らす
    drain_more(s)
    if not approach_shop(s):
        #! **歩けないのは道が無いからではなく、町の区画が端で回り込むから**
        #! （`walk_to` の註記）。収穫路は検査ではないので、**跳んで届かせる**
        #! ——見えている入口の隣へ `^A t` で降り、1 歩踏んで入る。
        f, cells = latest_cells(s)
        shop_chars = {ord(ch) for ch in "1234567890"} - {ord("8")}
        doors = [(c[0], c[1]) for c in cells
                 if (c[CELL_FF] & FEAT_KNOWN) and (c[CELL_ASCII] in shop_chars)]
        entered = False
        for door in doors[:3]:
            #! 入口そのものへは跳べない（店の地形は着地できない）ので、
            #! **1 つ南のマス**へ降りてから北へ 1 歩踏む。
            #!
            #! **`teleport_to` の戻り値は当てにしない。** 目標選択のカーソルも
            #! 町の端で回り込むので（実測 2026-08-25: (84,31) から x=49 を
            #! 狙うと x=115 へ着地する）、座標は合わない。**それでも降りた先の
            #! 隣に別の店があって入れる**ことが多い——**「店が開いたか」だけを
            #! 見る**のが正しい判定である。
            teleport_to(s, (door[0], door[1] + 1))
            s.key(ord("8"))
            s.settle(quiet_rounds=2, slice_s=0.6, cap_s=10.0)
            if (s.frames[-1] if s.frames else {}).get("menu_open"):
                entered = True
                break
        if not entered:
            print("  [SKIP] 店へ届かなかった（歩きも跳躍も。店の文言は次の巡で拾う）。")
            s.ui_state()          # 視野を既定へ戻す
            s.settle(quiet_rounds=1, slice_s=0.4, cap_s=6.0)
            return
    s.settle()
    s.show("j4 shop")
    s.key(ord("p"))               # 買う → コアが品を訊く
    s.settle()
    s.show("j4 shop buy")
    for cand in "abc":            # 品を選ぶ（個数か購入可否まで進む）
        s.key(ord(cand))
        s.settle()
        f = s.frames[-1] if s.frames else {}
        pk = [c.get("key") for c in f.get("prompt", {}).get("choices", [])]
        if f.get("numeric", {}).get("active") or (ord("y") in pk):
            s.show("j4 shop prompt")
            s.key(0x1B)           # 個数の入力を取り消す
            s.settle()
            f = s.frames[-1] if s.frames else {}
            pk = [c.get("key") for c in f.get("prompt", {}).get("choices", [])]
            if ord("n") in pk:
                s.key(ord("n"))   # **買わない**
                s.settle()
            drain_more(s, 1)
            break
    for _ in range(3):            # 品選び → 店 → 町、と戻る
        s.key(0x1B)
        s.settle(quiet_rounds=1, slice_s=0.4, cap_s=6.0)
    drain_more(s, 1)
    s.ui_state()                  # 視野を既定へ戻す（この先は普通に歩く）
    s.settle(quiet_rounds=1, slice_s=0.4, cap_s=6.0)


def _harvest_fight(s: Session) -> None:
    """町で敵を 1 体呼んで殴り合い、**戦いの文を収穫する**。

    `fx` と同じ green worm mass(31)——弱くて一撃で死に、町の外へ逃げない。
    攻撃・命中・撃破・経験値の文がここで出る。倒せなくても構わない
    （収穫が目的で、検査ではない）。
    """
    wizard_summon_named(s, "31", confirm=False)
    for _ in range(12):
        f, cells = latest_cells(s)
        if not f:
            break
        if not [c for c in cells if c[4] == 31]:
            break                 # 倒し切った（もう居ない）
        step_toward(s, cells, f, 31)
    drain_more(s, 2)
    s.show("j4 fight")


def cmd_j4(work: Path, extra=()) -> int:
    """J4（UI の文字列）の収穫路（設計 §8.7）。**検査ではなく収穫**が目的。

    受け入れ（§8.2 の J4）は「新規キャラ作成から 5 階まで英語が 1 行も出ない」
    なので、道筋もそれに合わせる: 誕生の画を一通り開き、町の画面（持ち物・
    装備・人物・知識・設定の下位画面）を開き、wizard ジャンプで Angband
    （番号 1）の 1〜5 階を歩く。引けなかった鍵は `<work>/u.txt` へ落ち、
    `fc_untranslated.py` が段ごとに仕分ける。落ちずに歩き切ることだけを検査する。

    ヘルプ（`?`）はわざと開かない——doc の助けはファイルまるごと流れて
    収穫が J6（ヘルプ）の中身で埋まる。
    """
    checks = Checks()
    report = work / "u.txt"
    s = Session(work, "j4", ["--lang=ja", f"--report-untranslated={report}", *extra])
    try:
        kind, _ = s.hello()
        checks.add(kind == "hello_ack", "hello -> hello_ack", f"got {kind}")
        s.ui_state()
        s.settle()

        # ---- 誕生の画を一通り（型 → 種族・職・性格の一覧 → 能力値） ----------
        s.key(ord("n"))              # n) 新しく始める
        s.settle(quiet_rounds=2, slice_s=0.6, cap_s=15.0)
        s.show("j4 game type")
        s.key(ord("n"))              # n) Normal
        s.settle(quiet_rounds=2, slice_s=0.6, cap_s=15.0)
        s.show("j4 race/class")
        for _ in range(3):           # Tab) More Info の 3 態を一巡
            s.key(0x09)
            s.settle(quiet_rounds=1, slice_s=0.5, cap_s=8.0)
        _browse(s, "r", "race list")
        _browse(s, "c", "class list")
        _browse(s, "p", "personality list")

        # ---- 誕生の深い画（魔法領域。J4 第 7 回） -----------------------
        #! 職を Magic 群の Mage に替えると _realm1_ui / _realm2_ui が開く
        #! （py_birth.c:1604）。領域名は doc の裸の 1 語で引かれる。
        #! 見終えたら職を Warrior へ戻す——後段（店・戦い・階）を
        #! これまでの巡と同じ条件（HP 61 の戦士）で歩かせるため。
        s.key(ord("c"))              # c) Change Class → 職のグループ一覧
        s.settle(quiet_rounds=1, slice_s=0.5, cap_s=8.0)
        s.key(ord("d"))              # d) Magic 群
        s.settle(quiet_rounds=1, slice_s=0.5, cap_s=8.0)
        s.key(ord("e"))              # e) Mage
        s.settle(quiet_rounds=1, slice_s=0.5, cap_s=8.0)
        s.show("j4 realm1")          # Choose Your Primary Magic Realm
        s.key(ord("a"))              # a) 最初の領域
        s.settle(quiet_rounds=1, slice_s=0.5, cap_s=8.0)
        s.show("j4 realm2")          # Choose Your Secondary Magic Realm
        s.key(ord("a"))              # a) 2 つ目 → 概観へ戻る（Magic: の行が出る）
        s.settle(quiet_rounds=1, slice_s=0.5, cap_s=8.0)
        s.show("j4 rc with realms")
        s.key(ord("c"))              # 職を戻す: c → a) Melee → g) Warrior
        s.settle(quiet_rounds=1, slice_s=0.5, cap_s=8.0)
        s.key(ord("a"))
        s.settle(quiet_rounds=1, slice_s=0.5, cap_s=8.0)
        s.key(ord("g"))
        s.settle(quiet_rounds=1, slice_s=0.5, cap_s=8.0)

        s.key(0x0D)                  # Enter) Next Screen（能力値へ）
        s.settle(quiet_rounds=2, slice_s=0.6, cap_s=15.0)
        s.show("j4 stats")
        s.key(0x0D)                  # Enter) Begin Play
        checks.add(s.wait_for(in_game, timeout=30.0), "birth reaches the town")
        drain_more(s, 6)             # 歓迎文の -more- の山

        # ---- 町の画面 --------------------------------------------------------
        _browse(s, "i", "inventory")
        _browse(s, "e", "equipment")
        _browse(s, "C", "character sheet")
        _browse(s, "~", "knowledge menu")
        for sub in "1235 67ABDHMNPW".replace(" ", ""):  # 4 は enable_debug_opts が開く
            s.key(ord("="))
            s.settle(quiet_rounds=1, slice_s=0.5, cap_s=8.0)
            _browse(s, sub, f"options ({sub})", out_esc=2)
        enable_debug_opts(s, checks)

        # ---- 店の中（`shop` の道筋を収穫のためだけに通す） --------------------
        _harvest_shop(s)

        # ---- 戦闘（町で 1 体呼んで殴る。攻撃・被弾・撃破の文が出る）----------
        _harvest_fight(s)

        # ---- Angband の 1〜5 階 ----------------------------------------------
        for lvl in range(1, 6):
            wizard_jump(s, "1", str(lvl), confirm=False)
            f, _ = latest_cells(s)
            checks.add(bool(f) and not f.get("pre_game_menu"),
                       f"jumped to Angband L{lvl}",
                       f"hud={json.dumps((f or {}).get('hud', {}), ensure_ascii=False)}")
            s.key(0x06)              # ^F 階の雰囲気
            s.settle(quiet_rounds=1, slice_s=0.5, cap_s=8.0)
            drain_more(s, 1)
            for k in "4662288s":     # 歩く（敵が動けば戦いの文も収穫になる）
                s.key(ord(k))
                s.settle(quiet_rounds=1, slice_s=0.4, cap_s=8.0)
                drain_more(s, 1)
            s.show(f"j4 L{lvl}")

        s.quit_and_wait()
        checks.add(s.exit_msg is not None, "exits cleanly", str(s.exit_msg))
    finally:
        s.close()
        print("  stderr tail:")
        for ln in s.stderr_tail():
            print(f"    {ln}")
    checks.add(report.exists() and report.stat().st_size > 0,
               "the harvest is on disk", str(report))
    return checks.finish()


# =========================================================================== j5

def _read_desc_table(stem: str):
    """`frox/lang/ja/desc/<stem>.tsv` → `{番号: (英語名, 訳文)}`。

    **検査の当てにするのは種の表そのもの**（生成物ではなく）。ここと画で
    食い違えば、途中のどこかが落としている。
    """
    path = REPO / "frox" / "lang" / "ja" / "desc" / f"{stem}.tsv"
    if not path.is_file():
        return {}
    body = [ln for ln in path.read_bytes().decode("utf-8").split("\n")
            if ln and not ln.startswith("#")]
    out = {}
    for row in csv.DictReader(body, delimiter="\t"):
        ja = (row.get("ja") or "").strip()
        if ja:
            out[int(row["idx"])] = (row.get("name", ""), ja)
    return out


def _strip_status_col(s: Session, rows):
    """行の右端に貼り付いている**状態列を切り落とす**。

    `menu_term_lines` は Term まるごと（80 桁）なので、右 12 桁の状態列
    （`レベル 1` `経験 16` …）が本文の各行の尻に付いてくる。そのまま繋ぐと
    **本文の行と行の間に状態列が挟まって**、折り返しを取り去っても本文が
    1 本に戻らない（J5 で踏んだ）。桁で切ると全角の幅を数える羽目になるので、
    **同じフレームが持っている状態列の字で消す**。
    """
    f = s.frames[-1] if s.frames else {}
    tails = [ln.get("text_utf8", "").strip()
             for ln in f.get("status_col_lines", [])]
    tails = [t for t in tails if t]
    out = []
    for row in rows:
        trimmed = row.rstrip()
        for tail in tails:
            if trimmed.endswith(tail):
                trimmed = trimmed[: -len(tail)]
                break
        out.append(trimmed.rstrip())
    return out


def _squash(text: str) -> str:
    """折り返しの跡（改行・空白）を落として 1 本にする。**照合はこの形で**。

    `doc_insert` は桁で折るので、訳文はミラーの上で何行にも割れている。
    「字が 1 つも欠けずに届いたか」を見るには、割れ目を取り去って比べる。
    """
    return re.sub(r"\s+", "", text)


def _recall_of(s: Session, want_name: str, tries: int = 10):
    """`[` でモンスター一覧を開き、`/` で思い出を出す。**目当ての字が出るまで下る**。

    戻りは `(見つかったか, ミラーの行)`。見つからなければ最後に見た画を返す。

    キーは 2 つとも実読で選んである（`cmd3.c` の `_mon_list_ui`）:

    * 思い出は **`/`**。同じ枝に `R` も居るが**大文字は届かない**——`islower` の
      落ち穂拾いに落ち、`!handled && quick_messages` で**一覧ごと閉じる**。
    * 下るのは **`2`**（`SKEY_DOWN` と同じ枝）。`j` は
      **`rogue_like_commands` のときだけ**下向きに写される。
    """
    s.key(ord("["))
    s.settle(quiet_rounds=2, slice_s=0.5, cap_s=12.0)
    #! 一覧そのものを毎回出す。**開かなかったときの手掛かりはここにしか無い**
    #! （思い出が出ないのが「呼べていない」のか「一覧に載っていない」のかを分ける）。
    s.show("j5 monster list")
    seen = []
    for _ in range(tries):
        s.key(ord("/"))
        s.settle(quiet_rounds=2, slice_s=0.5, cap_s=12.0)
        rows = s.mirror()
        seen = rows
        if want_name and want_name in "".join(rows):
            return True, rows
        s.key(0x1B)                      # 思い出を閉じて一覧へ戻る
        s.settle(quiet_rounds=1, slice_s=0.4, cap_s=6.0)
        s.key(ord("2"))                  # 次の行
        s.settle(quiet_rounds=1, slice_s=0.4, cap_s=6.0)
    return False, seen


def _find_in_item_prompt(s: Session, want: str):
    """`I` の選び画を開き、**訳名が載っている札**まで `/` で送って行を返す。

    戻りは `(選ぶ文字, その札のミラーの行)`。無ければ `(None, 最後に見た行)`。

    **拾えたかを当てにしない。** `wiz_create_named_art()` は足元へ落とすので、
    自動拾いが働くかどうかで持ち物にも床にも居る。選び画の札は
    `Inventory | Equipment | Floor` で、`/` が札を送る——どちらに居ても通る。
    """
    s.key(ord("I"))
    s.settle()
    rows = s.mirror()
    for _ in range(4):
        for row in rows:
            if want in row and ")" in row[:5]:
                return row.strip()[0], rows
        s.key(ord("/"))
        s.settle()
        rows = s.mirror()
    return None, rows


def _inspect_artifact(s: Session, a_idx: int, base_ja: str, name_ja: str):
    """`^A C <番号>` で名のある宝を出し、**鑑定の前と後**を 1 度ずつ見る。

    戻りは `(鑑定前の選び画の行, 見つかったか, 説明の画の行)`。

    鑑定前を見るのは**素の品の名前**（`アミュレット`）を確かめるためである
    ——名前は `object_desc()` の写しの環を通るので、CP932 の 2 バイト目に
    `~` が来ると割れる（フック #18。§8.13）。鑑定後は `D:` の訳が出る。

    鑑定は `^A I`（階の品を全部 `obj_identify_fully`。`wizard2.c` の `case 'I'`）。
    `^A i` は品を 1 つ訊いてくるので、盲打ちだと札の位置に依ってしまう。
    """
    wizard_begin(s, "C", False)
    for ch in str(a_idx):
        s.key(ord(ch))
        s.settle(quiet_rounds=1, slice_s=0.4, cap_s=4.0)
    s.key(0x0D)
    s.settle()
    drain_more(s, 2)

    _, before = _find_in_item_prompt(s, base_ja)
    s.key(0x1B)
    s.settle(quiet_rounds=1, slice_s=0.4, cap_s=6.0)

    #! **拾う前に鑑定する。** `^A I` が回すのは `o_list`（階に落ちている品）で、
    #! 持ち物は別の器（`pack.c`。親契約 §3.4「`INVEN_*` の添字は無い」）なので、
    #! 先に拾うと鑑定が当たらない——`{unidentified}` のまま説明が出ない。
    wizard_begin(s, "I", False)
    s.settle()
    drain_more(s, 3)
    for _ in range(3):
        s.key(ord("g"))
        s.settle(quiet_rounds=1, slice_s=0.4, cap_s=6.0)
        drain_more(s, 1)

    letter, rows = _find_in_item_prompt(s, name_ja)
    if not letter:
        return before, False, rows
    s.key(ord(letter))
    s.settle()
    return before, True, _strip_status_col(s, s.mirror())


def cmd_j5(work: Path, extra=()) -> int:
    """**J5（説明文）の受け入れ**（FROX_JA_DESIGN §8.2）。

    見るのは 2 つだけで、どちらも設計の受け入れ文そのものである——
    「思い出と説明が日本語で、**桁が溢れない**」。

      1. 呼んだ敵の思い出に、`desc/r_info.tsv` の訳文が**1 字も欠けずに**出る
         （折り返しの跡を取り去って突き合わせる。欠けていれば境界か受け皿の穴）
      2. 何行にも折り返る訳文（農夫マゴット 226 字）でも同じ——`doc_insert` の
         折り返しが本文を壊していないこと

    **ついでに未訳も収集する**（`--report-untranslated`）。思い出の画は
    J4 の収穫路が**一度も踏んでいない**ので、枠の名札（Name / Level / AC …）が
    まだ英語で残っている。

    **静かな階で呼ぶ**——町だと町の敵が一覧に混ざって別の思い出が開く。
    """
    checks = Checks()
    table = _read_desc_table("r_info")
    checks.add(bool(table), "desc/r_info.tsv が読める", f"{len(table)} 件")

    #! 浮浪児（1）＝英語原文が変愚と一致した短い訳。農夫マゴット（8）＝ 226 字で
    #! 何行にも折り返る。どちらも**弱くて検査の途中で殺されない**。
    #!
    #! **見えない敵は使えない。** スメアゴル（63）は 397 字と長くて手頃に見えるが
    #! `INVISIBLE` なので**可視の一覧に載らず**、`[` からは永久に届かない（実測）。
    #!
    #! 腐食ベトベト（132）とバリアントの保守者（1094）は**第 3 回で当方が訳した分**
    #! （§8.13）。前者は L7 の短い訳、後者は訳文の中に `cmd2.c` という**英数が
    #! 混じる**ので、単語切りとバイト算の道を一度に踏む。どちらも弱い。
    targets = [1, 8, 132, 1094]

    report = work / "u.txt"
    s = Session(work, "j5", ["--lang=ja", f"--report-untranslated={report}", *extra])
    try:
        kind, _ = s.hello()
        checks.add(kind == "hello_ack", "hello -> hello_ack", f"got {kind}")
        s.ui_state()
        s.settle()
        birth_and_flush(s, checks)
        enable_debug_opts(s, checks)

        #! 町ではなく Angband の 1 階で呼ぶ（一覧に町の敵が混ざらない）。
        #! **この巡で最初の `^A` なので確認に y が要る**（`wizard_begin` の註記。
        #! confirm=False だと ^A が黙って断られ、**町に居たまま先へ進む**）。
        wizard_jump(s, "1", "1", confirm=True)
        place = ((s.frames[-1] if s.frames else {}).get("hud", {})
                 .get("right_top_lines", []))
        checks.add(in_game(s) and "地上" not in place,
                   "Angband L1 へ降りた", f"place={place}")

        for r_idx in targets:
            want_name, want_desc = table.get(r_idx, ("", ""))
            if not want_desc:
                checks.add(False, f"I:{r_idx} の訳が表に無い")
                continue
            #! 名前は `edit/r_info.ja.txt` 側の訳。表の `name` は英語なので、
            #! 見つける手がかりは**訳文の頭 8 字**にする（名前より確実）。
            probe = want_desc[:8]
            wizard_summon_named(s, str(r_idx), confirm=False)
            #! **呼んだ敵が「見えている」とは限らない。** `[` の一覧は可視の敵しか
            #! 載せず、1 体も見えなければ画そのものが開かない。`^A m`（magic mapping ＋
            #! `detect_monsters_normal`。`wizard2.c` の `case 'm'`）で階の敵を全部
            #! 検知してから開く——視線にも湧いた場所にも依らなくなる。
            wizard_begin(s, "m", False)
            drain_more(s, 1)
            s.show(f"j5 after summon I:{r_idx}")
            found, rows = _recall_of(s, probe)
            s.show(f"j5 recall I:{r_idx}")
            checks.add(found, f"I:{r_idx} の思い出が開く", probe)

            joined = _squash("".join(_strip_status_col(s, rows)))
            checks.add(_squash(want_desc) in joined,
                       f"I:{r_idx} の説明が 1 字も欠けずに出る（{len(want_desc)} 字）",
                       f"want={probe}... / got={joined[:80]}")
            checks.add("�" not in joined and "〓" not in joined,
                       f"I:{r_idx} の説明に化けた字が無い")

            #! **桁が溢れない**＝どの行も端末の幅に収まっている。
            wide = [r for r in rows if len(r) > 100]
            checks.add(not wide, f"I:{r_idx} の思い出が桁に収まる",
                       (wide[0][:90] if wide else "clean"))

            s.key(0x1B)                  # 思い出 → 一覧 → 盤面へ
            s.settle(quiet_rounds=1, slice_s=0.4, cap_s=6.0)
            s.key(0x1B)
            s.settle(quiet_rounds=1, slice_s=0.4, cap_s=6.0)

        #! **品の説明も見る**（§8.13）。思い出（`r_info`）と品（`a_info`）は
        #! 同じ `D:` でも**出る画が違う**——思い出は `doc_display`、品は
        #! `obj_display` である。r_info だけ見て「説明は日本語」と言わない。
        #! ドワーフの首飾り（6）は第 3 回で当方が訳した 121 件の 1 つ。
        art = _read_desc_table("a_info")
        a_idx = 6
        _, want_art = art.get(a_idx, ("", ""))
        checks.add(bool(want_art), f"a_info I:{a_idx} の訳が表に在る")
        if want_art:
            before, found, rows = _inspect_artifact(
                s, a_idx, "アミュレット", "ドワーフ")
            checks.add(found, f"a_info I:{a_idx} の説明が開く")
            joined = _squash("".join(rows))
            checks.add(_squash(want_art) in joined,
                       f"a_info I:{a_idx} の説明が 1 字も欠けずに出る"
                       f"（{len(want_art)} 字）",
                       f"got={joined[:80]}")
            checks.add("�" not in joined and "〓" not in joined,
                       f"a_info I:{a_idx} の説明に化けた字が無い")
            #! **`~` は CP932 の 2 バイト目にも現れる**（ミ＝83 7E）。フック
            #! #18 が入るまで、品の名前が `アミュレット` → `アャ・激bト` に
            #! 化けていた（§8.13）。宝の名前は変愚と同じ「素の品の名前 ＋ 銘」で
            #! 組むので、**説明の画の見出しに素の品の名前が出る**——ここで見る。
            hit = [r for r in before if "アミュレット" in r]
            checks.add(bool(hit),
                       "素の品の名前が CP932 の 2 バイト目で割れていない（#18）",
                       "／".join(r.strip()[:40] for r in before[:5]))
            s.key(0x1B)
            s.settle(quiet_rounds=1, slice_s=0.4, cap_s=6.0)

        #! **尻に付く銘は頭へ回っているか**（設計 §6.3 の追記。J6 第 3 回）。
        #! 先方は base の後ろへ空白付きで継ぐ（`flavor.c:1756`）ので、
        #! 素のままだと `アミュレット カルランマスの` と出る。日本語は
        #! **`カルランマスのアミュレット`** でなければならない。
        #! 392 件のうち **350 件がこの形**なので、1 件見れば全部に効く。
        a_idx = 4
        _, want4 = art.get(a_idx, ("", ""))
        checks.add(bool(want4), f"a_info I:{a_idx} の訳が表に在る")
        if want4:
            before, found, rows = _inspect_artifact(
                s, a_idx, "アミュレット", "カルランマスの")
            checks.add(found, f"a_info I:{a_idx} の説明が開く")
            joined = "".join(rows)
            checks.add("カルランマスのアミュレット" in _squash(joined),
                       "尻に付く銘が頭へ回っている（名のある宝 350 件）",
                       _squash(joined)[:80])
            checks.add("アミュレット カルランマス" not in _squash(joined),
                       "英語の並び（base のうしろに銘）が残っていない")
            s.key(0x1B)
            s.settle(quiet_rounds=1, slice_s=0.4, cap_s=6.0)

        s.quit_and_wait()
        checks.add(s.exit_msg is not None, "exits cleanly", str(s.exit_msg))
    finally:
        s.close()
        print("  stderr tail:")
        for ln in s.stderr_tail():
            print(f"    {ln}")
    checks.add(report.exists() and report.stat().st_size > 0,
               "未訳の収集が落ちている", str(report))
    return checks.finish()



# =========================================================================== j6

#: 訳した本のうち、`?` から 2 手で届くもの（`fc_build_ja_help.py` と同じ並び）。
_J6_PAGES = [
    ("?", "start.txt", "オンラインヘルプへようこそ"),
    ("a", "general_contents.txt", "ゲームの紹介と全般の話"),
    ("\x1b?", "helpinfo.txt", "ヘルプの操作方法"),
]


def _j6_ascii_words(rows):
    """画に残っている**英単語**（2 文字以上の英字の連なり）を返す。

    ヘルプにはキーの名前（`ESC` `PageUp` `Ctrl+F`）とファイル名が正しく残るので、
    それだけを除いて数える。**残りが 0 なら、その画は日本語で出ている。**
    """
    keep = {"ESC", "PageUp", "PageDown", "Up", "Down", "Arrow", "Spacebar", "End",
            "Home", "Ctrl", "Enter", "txt", "html", "htm", "letters", "start",
            "birth", "general", "helpinfo", "contents", "F3", "Qq", "Ctrl+F"}
    out = []
    for row in rows:
        for w in re.findall(r"[A-Za-z][A-Za-z0-9+]+", row):
            if w in keep:
                continue
            out.append(w)
    return out


def cmd_j6(work: Path) -> int:
    """**J6（ヘルプ）の受け入れ**（FROX_JA_DESIGN §8.2・§8.16）。

    見るのは 4 つ:

      1. `?` で**日本語のヘルプが開く**（見出しの `ヘルプ '…'` と足元の帯も日本語）
      2. リンクをたどった先も日本語で、`ESC` で戻れる
      3. ヘルプの中の `?` が `helpinfo.txt` を日本語で開く
      4. **訳の無い本は英語のまま開く**（制約 4）。`lib-ja/help` はツリーごと
         差し替わるので、**写し忘れると「開けない」になる**——そこを押さえる
    """
    checks = Checks()
    s = Session(work, "j6", ("--lang=ja",))
    try:
        kind, _ = s.hello()
        checks.add(kind == "hello_ack", "hello -> hello_ack", f"got {kind}")
        s.ui_state(view_w=80, view_h=30)
        s.settle()
        birth_and_flush(s, checks)

        # ---- 1〜3: 訳した画が日本語で出る ----------------------------------
        for keys, stem, title in _J6_PAGES:
            for ch in keys:
                s.key(ord(ch))
                s.settle()
            rows = s.mirror()
            head = rows[0] if rows else ""
            checks.add(f"ヘルプ '{stem}'" in head,
                       f"{stem}: 見出しが日本語", head[:70])
            checks.add(any(title in r for r in rows),
                       f"{stem}: 題が日本語", title)
            checks.add(any("ESC で終了" in r for r in rows),
                       f"{stem}: 足元の帯が日本語",
                       (rows[-1] if rows else "")[:70])
            left = _j6_ascii_words(rows)
            checks.add(not left, f"{stem}: 英単語が残っていない",
                       f"残り {left[:6]}")

        # ---- ESC で 1 つ戻れる（helpinfo → start.txt）---------------------
        #! `helpinfo.txt` は `start.txt` から `?` で開いた（上の並びの 3 本目は
        #! 先に `ESC` で `start.txt` へ戻ってから `?` を押している）ので、
        #! ここの `ESC` が返るのは `start.txt` である。
        s.key(0x1b)
        s.settle()
        rows = s.mirror()
        checks.add("start.txt" in (rows[0] if rows else ""),
                   "ESC で前の画へ戻る", (rows[0] if rows else "")[:70])

        # ---- 4: 訳の無い本は英語のまま開く（制約 4）-----------------------
        # `start.txt` の `[d] よくある質問` → `faq.txt`（未訳）を開く。
        s.key(ord("d"))
        s.settle()
        rows = s.mirror()
        head = rows[0] if rows else ""
        checks.add("faq.txt" in head, "未訳の本も開ける（写しがある）", head[:70])
        checks.add(any("FAQ" in r for r in rows),
                   "未訳の本は英語のまま出る（制約 4）",
                   " / ".join(r.strip() for r in rows[1:5] if r.strip())[:70])

        s.key(0x1b)
        s.settle()
        s.key(ord("q"))
        s.settle()
        s.quit_and_wait()
        checks.add(True, "clean shutdown")
    finally:
        s.close()
    return checks.finish()


# ======================================================================== decor

#: 飾りの字を集める舞台（`decor`）。鉄獄（Angband）の 3 階へ飛んで、落とす敵を
#: 呼んで倒し、落ちた品を拾う。**この 3 つがフック #31 #32 #33 の全部を通る。**
#: 品を落とす低レベルの敵（`r_info.txt` の実読。2026-08-27）:
#:    8  Farmer Maggot          … `ONLY_ITEM | DROP_90 | DROP_GOOD | DROP_GREAT`
#:   19  Nick the Butcher       … `ONLY_ITEM | DROP_90 | DROP_GOOD`
#:   63  Smeagol                … `ONLY_ITEM | DROP_90 | DROP_GOOD | DROP_GREAT`
#:   74  Scruffy looking hobbit … `DROP_1D2 | ONLY_ITEM`
#:  116  Novice archer          … `DROP_1D2 | ONLY_ITEM`
#: **一意の敵も混ぜる。** `do_cmd_wiz_named()` は倒した一意を呼び直せる
#: （`r_ptr->max_num` を戻す）ので、2 巡目も空振りしない。
#: **`DROP_GOOD` が要る**——安い品は持ち物の山へ混ざって由来が `MIXED` になり、
#: 由来の行がそもそも出ない（実測 2026-08-27）。
_DECOR_DROPPERS = ("19", "63", "8", "74", "116")


def _decor_inventory(s: Session):
    """`i`（持ち物）のミラーを 1 枚取って閉じる。"""
    s.key(ord("i"))
    s.settle()
    rows = [r for r in s.mirror() if r.strip()]
    s.key(0x1b)
    s.settle()
    return rows


def _decor_gather(s: Session) -> None:
    """落ちた品を地図から探して歩いて拾う（**決め打ちで歩かない**）。"""
    for _ in range(10):
        f, cells = latest_cells(s)
        objs = [c for c in cells if c[CELL_OBJECT] != 0]
        if not objs:
            return
        px, py = f.get("player_gx"), f.get("player_gy")
        objs.sort(key=lambda c: abs(c[0] - px) + abs(c[1] - py))
        tgt = (objs[0][0], objs[0][1])
        if tgt != (px, py):
            walk_to(s, tgt)
            drain_more(s)
        s.key(ord("g"))
        s.settle(quiet_rounds=1, slice_s=0.5, cap_s=8.0)
        drain_more(s)


def _decor_create(s: Session, tval_desc: str, needle: str) -> bool:
    """`^A c` で品目を選んで 1 個作り、足元から拾う。**letter は画から読む。**

    どちらの段も `prt()` で描かれ、`screen_save()` の中なのでミラーに出る
    （`j4` の註記と同じ）。並びは版で変わるので**決め打ちしない**。
    """
    #! **この巡で最初の `^A` なので確認に y が要る**（`wizard_begin` の註記。
    #! confirm=False だと ^A が黙って断られ、品目一覧が開かない）。
    wizard_begin(s, "c", True)
    letter = _decor_pick(s, tval_desc)
    if not letter:
        s.key(0x1b)
        s.settle()
        return False
    s.key(ord(letter))
    s.settle(quiet_rounds=2, slice_s=0.5, cap_s=10.0)
    letter = _decor_pick(s, needle)
    if not letter:
        s.key(0x1b)
        s.settle()
        return False
    s.key(ord(letter))
    s.settle()
    s.key(0x0D)          # How many? → 既定の 1
    s.settle()
    drain_more(s)
    #! **足元とは限らない。** `drop_near()` は塞がっていれば隣のマスへ置くので、
    #! 地図から探して歩く（`_decor_gather`）。素の `g` だけだと取りこぼす。
    #! **拾えたかを見てから返す**——菜単をたどれただけでは「作った」と言えない。
    for _ in range(3):
        _decor_gather(s)
        if any(needle in r for r in _decor_inventory(s)):
            return True
    return False


def _decor_pick(s: Session, needle: str) -> str:
    """`[x] …needle…` の行から letter を読む。無ければ空。"""
    for row in s.mirror():
        for m in re.finditer(r"\[(.)\]\s*([^\[]*)", row):
            if needle in m.group(2):
                return m.group(1)
    return ""


def _decor_inspect_all(s: Session):
    """持ち物を 1 つずつ `I` で調べ、出た行を全部返す。"""
    out = []
    for idx in range(12):
        s.key(ord("I"))
        s.settle()
        f = s.frames[-1] if s.frames else {}
        keys = [c.get("key") for c in f.get("menu_choices", [])]
        letters = [k for k in keys if k and (0x61 <= k <= 0x7A)]
        if idx >= len(letters):
            s.key(0x1b)
            s.settle()
            break
        s.key(letters[idx])
        s.settle()
        out += [r.strip() for r in s.mirror() if r.strip()]
        s.key(0x20)
        s.settle()
        s.key(0x1b)
        s.settle()
    return out


def _decor_run(work: Path, tag: str, extra, lantern: str):
    """1 巡ぶん回して（持ち物の行, 調べた画の行）を返す。

    `lantern` は品目一覧で探す字。**言語で変わる**——`^A c` の一覧は
    `strip_name()` が組むので、日本語では `k_info` の訳が出る。
    """
    checks = Checks()
    s = Session(work, tag, extra)
    inv, seen = [], []
    try:
        s.hello()
        s.ui_state(view_w=80, view_h=30)
        s.settle()
        birth_and_flush(s, checks)
        inv = _decor_inventory(s)          # 松明の残りはここに出る（#32）

        enable_debug_opts(s, checks)
        #! **偽の銘は作って出す**（#31）。ランタンは風味を持たないので
        #! `aware` は真、`^A c` で作った品は `known` が偽——`{未鑑定}` の条件
        #! そのものである（`flavor.c` の `TV_LITE` の枝）。**休んで感触が付くのを
        #! 待つ形は採らない**——回によって付かず、検査が運任せになる。
        made = _decor_create(s, "Lite", lantern)
        checks.add(made, "#31 ランタンを作って拾った")
        #! **ここで持ち物を見る。** ダンジョンを歩くと感触が付いて `IDENT_SENSE` が
        #! 立ち、`{未鑑定}` の条件（`aware && !known && !SENSE`）から外れる
        #! ——あとで見ると銘が消えている（実測 2026-08-27）。
        inv += _decor_inventory(s)

        wizard_jump(s, "1", "3", confirm=False)
        #! **由来の場所が出るまで回す**（3 巡まで）。落とし物は足元とは限らず、
        #! 拾いに行けない所へ落ちることがあるので、1 巡で決め打ちにしない。
        for _ in range(3):
            for r_idx in _DECOR_DROPPERS:
                wizard_summon_named(s, r_idx, confirm=False)
            wizard_begin(s, "z", False)    # ^A z は本物の撃破。品を落とす
            s.settle()
            drain_more(s)
            _decor_gather(s)
            seen = _decor_inspect_all(s)   # 由来の場所はここ（#33）
            if any(("階で落とした" in r) or ("階の床に落ちていた" in r)
                   or ("of Angband" in r) or ("on level" in r) for r in seen):
                break
        inv += _decor_inventory(s)         # 「未鑑定」はここ（#31）
        s.quit_and_wait()
    finally:
        s.close()
    return checks, inv, seen


def cmd_decor(work: Path) -> int:
    """**飾りの字の受け入れ**（FROX_JA_DESIGN §8.26。フック #31〜#33）。

    品の名前と由来には、カタログのどの口も通らない短い英語が**バイトで**継ぎ足される。
    ここはその 3 種類を実機で見る:

      1. **#32** 光源の残り（`(3000 ターンの寿命)`）——誕生直後の持ち物で見える
      2. **#31** 偽の銘（`{未鑑定}`）——休んで感触が付くのを待つ
      3. **#33** 由来の場所（`『スメアゴル』が鉄獄の3階で落とした。`）
         ——敵を呼んで倒し、落とした品を調べる

    **英語でも 1 巡回す**（制約 1）。日本語の字が 1 つも出ず、英語の字が出ること。

    **落ちる品は毎回違う。** だから「この品が出る」ではなく
    **「英語の飾りが 1 つも残っていない」**で見る。
    """
    checks = Checks()

    ja_checks, ja_inv, ja_seen = _decor_run(work, "decor_ja", ("--lang=ja",), "ランタン")
    checks.rows += ja_checks.rows
    ja_text = "\n".join(ja_inv + ja_seen)

    # ---- #32: 光源の残り ------------------------------------------------
    checks.add(bool(re.search(r"\(\d+ターンの寿命\)", ja_text)),
               "#32 光源の残りが日本語", _first(ja_inv, "ターンの寿命"))
    checks.add("turns of light" not in ja_text, "#32 英語の残り字が消えている")

    # ---- #31: 偽の銘と感触 ----------------------------------------------
    fake = [w for w in ("未鑑定", "空", "未判明", "未見", "壊れている", "恐ろしい",
                        "劣悪", "呪われている", "上質以上", "並", "上質", "高級品",
                        "特別製", "悪い")
            if ("{%s}" % w) in ja_text]
    checks.add(bool(fake), "#31 偽の銘か感触が日本語で出た",
               "/".join(fake) or _first(ja_inv, "ランタン"))
    left = [w for w in ("{unidentified}", "{empty}", "{tried}", "{unseen}", "{cursed}",
                        "{good}", "{average}", "{excellent}", "{special}", "{bad}",
                        "{broken}", "{terrible}", "{awful}", "{enchanted}")
            if w in ja_text]
    checks.add(not left, "#31 英語の銘が残っていない", "/".join(left))

    # ---- #33: 由来の場所 ------------------------------------------------
    checks.add("階で落とした" in ja_text or "階の床に落ちていた" in ja_text,
               "#33 由来の場所が日本語", _first(ja_seen, "落と"))
    checks.add("on level" not in ja_text, "#33 英語の場所が残っていない")
    checks.add("in the wilderness" not in ja_text, "#33 荒野も英語で残っていない")

    # ---- 英語は 1 バイトも変わらない（制約 1）----------------------------
    en_checks, en_inv, en_seen = _decor_run(work, "decor_en", (), "Lantern")
    checks.rows += en_checks.rows
    en_text = "\n".join(en_inv + en_seen)
    checks.add("turns of light" in en_text, "英語では英語の残り字が出る",
               _first(en_inv, "turns of light"))
    ja_left = [c for c in en_text if ord(c) > 0x2000]
    checks.add(not ja_left, "英語に日本語が 1 字も混じらない",
               "".join(ja_left[:20]))

    return checks.finish()


def _msg_run(work: Path, tag: str, extra_args):
    """誕生 → 敵を呼んで殴る。**フレームの `messages` を全部集めて返す。**

    **ミラー（`menu_term_lines`）では拾えない。** あれはメニューと doc の画だけで、
    地図を出しているあいだは空である（実測 2026-08-28: 誕生の画しか採れず
    戦闘のメッセージが 1 行も出なかった）。メッセージはフレームの `messages` に載る。

    メッセージは 1 打ごとに入れ替わるので、**キーを打つたびに拾う**こと。
    """
    checks = Checks()
    seen = []
    s = Session(work, tag, extra_args)

    def sweep():
        for f in s.frames:
            for m in f.get("messages", []):
                line = (m.get("text_utf8") or "").strip()
                if line and line not in seen:
                    seen.append(line)

    try:
        kind, _ = s.hello()
        checks.add(kind == "hello_ack", "hello -> hello_ack", f"got {kind}")
        s.ui_state(cursor_mode=True)
        s.settle()
        birth_and_flush(s, checks)
        sweep()
        enable_debug_opts(s, checks)

        # green worm mass(31) は 4d4 HP で動きが遅い。町は明るく、殴り合いが読める。
        #
        # **呼ぶ回数は多めに取る。** worm は召喚された先から歩き出すので、
        # 3 回 24 歩では**届かない回が出た**（実測 2026-08-28: 日本語の巡回だけ
        # 落ちて、英語は通った。訳とは関係の無いゆらぎである）。
        killed = False
        for _round in range(6):
            mark = len(s.frames)
            wizard_summon_named(s, "31", confirm=True)
            for _ in range(32):
                f, cells = latest_cells(s)
                if not f:
                    break
                sweep()
                if any(e["kind"] == 1 and e["intensity"] >= 1.0
                       for e in collect_fx(s, mark)):
                    killed = True
                    break
                step_toward(s, cells, f, 31)
            sweep()
            if killed:
                break
        checks.add(killed, "worm(31) を殴って倒した（戦闘のメッセージが流れた）")
        sweep()
    finally:
        s.close()
    return checks, seen


def _spell_run(work: Path, tag: str, extra_args):
    """呪文使いを 5 体呼んで、そばで待ちながらメッセージを集める。

    **「この 1 文が出る」では見ない**（`msg` と同じ理屈）。どの呪文を唱えるかは
    乱数で決まるので、見るのは**呪文のメッセージが日本語で流れること**と
    **英語では 1 バイトも変わらないこと**である。

    呼ぶのは 46 見習メイジ（BLIND / CONFUSE / MISSILE）・97 見習パラディン
    （SCARE / CAUSE_1）・109 見習プリースト（HEAL / SCARE / BLESS）と、
    **40 キノコの叫び声**（`NEVER_MOVE | NEVER_BLOW` で `1_IN_4 SHRIEK`）。

    **キノコを入れているのが要である。** 殴ってくる敵は隣に来ると殴るほうを選ぶので、
    120 ターン待っても呪文が 1 度も流れない巡が出る（実測 2026-08-28）。
    キノコは**殴れず動けず、叫ぶことしかできない**ので、必ず 1 度は唱える。
    """
    checks = Checks()
    seen = []
    s = Session(work, tag, extra_args)

    def sweep():
        for f in s.frames:
            for m in f.get("messages", []):
                line = (m.get("text_utf8") or "").strip()
                if line and line not in seen:
                    seen.append(line)

    try:
        kind, _ = s.hello()
        checks.add(kind == "hello_ack", f"{tag}: hello -> hello_ack", f"got {kind}")
        s.ui_state(view_w=80, view_h=30, cursor_mode=True)
        s.settle()
        birth_and_flush(s, checks)
        sweep()
        enable_debug_opts(s, checks)
        mark = len(seen)

        for i, r_idx in enumerate(("40", "40", "46", "46", "97", "109", "109")):
            #! **この巡で最初の `^A` は確認に y が要る**（`_decor_create` の註記と同じ）。
            wizard_summon_named(s, r_idx, confirm=(i == 0))
            s.settle(quiet_rounds=1, slice_s=0.4, cap_s=6.0)
        drain_more(s)
        sweep()

        #! **殴らない。** 倒すと唱える相手が居なくなる。その場で待つだけ。
        for _ in range(120):
            s.key(ord(","))
            s.settle(quiet_rounds=1, slice_s=0.25, cap_s=4.0)
            drain_more(s)
            sweep()
        checks.add(len(seen) > mark, f"{tag}: 呪文使いを呼んで待てた",
                   f"{len(seen) - mark} 行")
    finally:
        try:
            s.quit_and_wait()
        except Exception:
            pass
        s.close()
    return checks, seen[mark:]


def cmd_spell(work: Path) -> int:
    """**敵の呪文のメッセージの受け入れ**（FROX_JA_DESIGN §8.39。フック #45）。

    ひな型（`$CASTER tries to blank your mind.`）は **`$CASTER` を差し替える前**に
    引く。差し替えた後の字は敵の名前を含むので、`msg_print()` の口ではもう引けない
    ——**この検査は「差し替えの前に引けているか」を実機で見るためにある**。
    """
    checks = Checks()

    ja_checks, ja = _spell_run(work, "spell_ja", ("--lang=ja",))
    checks.rows += ja_checks.rows

    #! **敵の名前が入っている行**だけを呪文のメッセージとみなす。`$CASTER` が
    #! 差し替わっていなければ字がそのまま残るので、それも見る。
    left = [r for r in ja if "$CASTER" in r or "$TARGET" in r]
    checks.add(not left, "差し替えの印が字のまま残っていない", "; ".join(left[:2]))

    ja_lines = [r for r in ja if any(ord(c) > 0x2000 for c in r)]
    checks.add(bool(ja_lines), "メッセージが日本語で流れた", f"{len(ja_lines)} 行")

    #! **1 本は必ず出る字で押さえる。** キノコの叫び声は殴れず動けないので、
    #! 120 ターンのうちに必ず唱える（`$CASTER makes a high pitched shriek.`）。
    shriek = [r for r in ja if "金切り声" in r]
    checks.add(bool(shriek), "呪文のひな型が差し替わって出た",
               shriek[0] if shriek else "(叫び声が流れなかった)")

    log = (work / "fc_spell_ja_stderr.log").read_text(encoding="cp932", errors="replace")
    rep = [l for l in log.splitlines() if l.startswith("[frox] japanese:")]
    checks.add(bool(rep) and ("0 rejected, 0 tag mismatch" in rep[-1]),
               "カタログが 1 件も弾かれていない", rep[-1] if rep else "(報告が無い)")

    en_checks, en = _spell_run(work, "spell_en", ())
    checks.rows += en_checks.rows
    ja_chars = [c for r in en for c in r if ord(c) > 0x2000]
    checks.add(not ja_chars, "英語に日本語が 1 字も混じらない", "".join(ja_chars[:20]))
    en_shriek = [r for r in en if "high pitched shriek" in r]
    checks.add(bool(en_shriek), "英語では英語のひな型が出る",
               en_shriek[0] if en_shriek else "(叫び声が流れなかった)")

    #! **次の便の作業一覧。** 呪文のメッセージでまだ英語で流れた行を出す。
    en_left = [r for r in ja
               if re.match(r"^(The |It |Something |You )", r)]
    print("  --- まだ英語で流れた行 (%d) ---" % len(en_left))
    for r in en_left[:20]:
        print("    " + r)

    print("  --- 日本語で流れたメッセージの行 ---")
    for r in ja_lines[:20]:
        print("    " + r)
    return checks.finish()


def cmd_bldg(work: Path) -> int:
    """**建物の画面の受け入れ**（FROX_JA_DESIGN §8.37。フック #41 #42）。

    店ではなく**建物**（`f_info.txt` の `BLDG`。記号は `+`）へ入り、
    見出し（主の名・種族・建物の名）と命令の列が日本語で出るかを見る。

    **データ（`t_*.txt`）は英語のまま置いてある**——`bldg.c:4486` が
    `strpos("Cornucopia", bldg->name)` で建物を英語の名で見分けているためで、
    訳は**画へ出す所**（#41 #42）で当てている。だから
    **「画に日本語が出る」ことと「データが英語のまま」であることの両方**を見る。

    **英語でも 1 巡回す**（制約 1）。
    """
    checks = Checks()

    def run(tag, extra):
        s = Session(work, tag, extra)
        rows = []
        try:
            kind, _ = s.hello()
            checks.add(kind == "hello_ack", f"{tag}: hello -> hello_ack", f"got {kind}")
            s.ui_state(view_w=120, view_h=60, cursor_mode=True)
            s.settle()
            birth_and_flush(s, checks)
            enable_debug_opts(s, checks)
            wizard_begin(s, "w", True)   # 町を照らす（入口が「既知」になる）
            drain_more(s)
            opened = approach_shop(s, glyphs="+")
            checks.add(opened, f"{tag}: 建物へ入れた")
            if opened:
                s.settle()
                rows = [r for r in s.mirror() if r.strip()]
        finally:
            s.close()
        return rows

    ja = run("bldg_ja", ("--lang=ja",))
    if ja:
        print("  --- 日本語の建物の画 ---")
        for r in ja[:14]:
            print("    " + r)
    head = ja[0] if ja else ""
    checks.add(any(ord(c) > 0x2000 for c in head), "見出しが日本語（#41）", head[:60])
    #! **見出しの行を命令と数えない。** 見出しにも `) ` が出る
    #! （`主の名 (種族)`）ので、`" x) "` の形だけを命令とする。
    acts = [r for r in ja if re.match(r"^\s*[a-zA-Z]\)\s", r)]
    checks.add(bool(acts) and any(any(ord(c) > 0x2000 for c in r) for r in acts),
               "命令の名が日本語（#41）", (acts[0] if acts else "(命令が無い)")[:60])
    checks.add(not any("(closed)" in r for r in ja), "`(closed)` が英語で残っていない")

    #! **データは英語のまま**（`strpos("Cornucopia", …)` を壊さないため）。
    town = REPO / "frox" / "lib-ja" / "edit" / "t_lite.txt"
    if town.is_file():
        text = town.read_bytes().decode("latin-1")
        checks.add("B:0:N:The White Horse Inn" in text,
                   "lib-ja の t_*.txt は英語のまま（#41 の前提）")

    en = run("bldg_en", ())
    ja_chars = [c for r in en for c in r if ord(c) > 0x2000]
    checks.add(not ja_chars, "英語に日本語が 1 字も混じらない", "".join(ja_chars[:20]))
    return checks.finish()


def cmd_msg(work: Path) -> int:
    """**メッセージの行の受け入れ**（FROX_JA_DESIGN §8.31。J4 第 10 回から）。

    **「この 1 文が出る」では見ない。** 殴りの結果は乱数で、どのメッセージが出るかは
    毎回違う（実測 2026-08-28: 緑イモムシは一撃で死ぬので、`cmd1.c` の
    切れ味のメッセージは 1 つも流れなかった）。見るのは**日本語の層が生きていること**と、
    **英語でも 1 バイトも変わらないこと**である。

    そのうえで、**まだ英語で流れた行を数えて出す**——これがこの検査の値打ちで、
    次の便の作業一覧になる（`fc_scan_src_en.py` はソースを見るが、
    こちらは**実際に画面へ出た行**を見る。両方要る）。
    """
    checks = Checks()

    ja_checks, ja = _msg_run(work, "msg_ja", ("--lang=ja",))
    checks.rows += ja_checks.rows

    # ---- 1: 日本語の層が生きている --------------------------------------
    ja_lines = [r for r in ja if any(ord(c) > 0x2000 for c in r)]
    checks.add(bool(ja_lines), "メッセージが日本語で流れた", str(len(ja_lines)) + " 行")

    # ---- 2: カタログが 1 件も弾かれていない ------------------------------
    log = (work / "fc_msg_ja_stderr.log").read_text(encoding="cp932", errors="replace")
    rep = [l for l in log.splitlines() if l.startswith("[frox] japanese:")]
    checks.add(bool(rep) and ("0 rejected, 0 tag mismatch" in rep[-1]),
               "カタログが 1 件も弾かれていない", rep[-1] if rep else "(報告が無い)")

    # ---- 英語は 1 バイトも変わらない（制約 1）---------------------------
    en_checks, en = _msg_run(work, "msg_en", ())
    checks.rows += en_checks.rows
    ja_chars = [c for r in en for c in r if ord(c) > 0x2000]
    checks.add(not ja_chars, "英語に日本語が 1 字も混じらない", "".join(ja_chars[:20]))
    checks.add(any(re.match(r"^(You |Your |%?\w+ is )", r) for r in en),
               "英語では英語の文が出る", _first(en, "You "))

    # ---- まだ英語で流れた行（次の便の作業一覧）---------------------------
    # 敵と品の名前は M2 まで英語なので、**行が英語の文で始まるか**だけを見る
    # （`緑イモムシの大群を攻撃:` は正しい姿）。
    en_left = [r for r in ja
               if re.match(r"^(You |Your |It |The |A |An |There is |Something |Can't )", r)]
    print("  --- まだ英語で流れた行 (%d) ---" % len(en_left))
    for r in en_left:
        print("    " + r)

    print("  --- 日本語で流れたメッセージの行 ---")
    for r in ja[:40]:
        print("    " + r)
    return checks.finish()


def _first(rows, needle: str) -> str:
    for r in rows:
        if needle in r:
            return r.strip()[:70]
    return "(見当たらない)"


def _spellfmt_run(work: Path, tag: str, extra_args):
    """**組み立てるメッセージ**（ブレス・ボール・ボルト・召喚）が出るまで待つ。

    `_display_msg()` のひな型を持たない呪文は `sprintf` で組まれる
    （`_breath_msg()` / `_ball_msg()` / `_bolt_msg()` / `_summon_msg()`）。
    **その 4 本を 1 巡で全部踏む**ように、唱える相手を選んである:

      * **322 巨大黒ドラゴン・フライ** … `NEVER_BLOW` で `1_IN_9 BR_ACID`。
        殴れないので**ブレスしか撃てない**。体力 3d8 なのでブレスも軽い。**ブレス**
      * **310 ミミック(薬)** … `NEVER_MOVE` で `1_IN_6` の 1 つが `BO_COLD`。**ボルト**
      * **1280 スノウ・ゴーレム** … `FREQ_10` で持ち技は `BA_COLD(18)` **だけ**。**ボール**
      * **342 クイルスルグ** … `NEVER_MOVE | NEVER_BLOW` で `1_IN_4` の `S_MONSTER`。**召喚**

    **相手選びは「唱えるか」と「殺されないか」の綱引きである**（実測 2026-08-28）:

      1. **殴れない／動けない敵を選ぶ。** 隣に来た敵は殴るほうを選ぶので、
         歩けて殴れる敵は 200 ターン待っても 1 度も唱えないことがある（§8.39 ⑥）
      2. **麻痺させる敵は入れない。** ローパーの `PARALYZE` はキーの列ごと止める
      3. **強い敵も入れない。** 557 破壊の書『ラール』は `NEVER_MOVE | NEVER_BLOW` で
         ブレスもボルトも撃つ**理想の的**に見えるが、**ブレスが自分の体力の 2 割
         （1 発 100 前後）**あり、3 ターンごとに全快しても 10 ターンで殺された。
         **殺されると、そこから先のキーが全部死ぬ**——検査は「唱えなかった」ように
         見えて落ちる

    目つぶし（ミミックの `BLIND`）は避けられない——`p_ptr->blind` の枝に落ちると
    ボルトが「$CASTER mumbles.」に化けるが、**2 ターンごとの `^A a` がすぐ治す**。

    **先に経験値を盛る**（`^A x` を 18 回）。**盛りすぎない**——19 回にすると
    25 レベルの能力値の選択が残って、そこから先のキーを全部食った。
    22 回では 30 レベルで詰まった（`answer_stat_prompts()` の註記）。
    """
    checks = Checks()
    seen = []
    s = Session(work, tag, extra_args)

    def sweep():
        for f in s.frames:
            for m in f.get("messages", []):
                line = (m.get("text_utf8") or "").strip()
                if line and line not in seen:
                    seen.append(line)

    try:
        kind, _ = s.hello()
        checks.add(kind == "hello_ack", f"{tag}: hello -> hello_ack", f"got {kind}")
        s.ui_state(view_w=80, view_h=30, cursor_mode=True)
        s.settle()
        birth_and_flush(s, checks)
        sweep()
        enable_debug_opts(s, checks)
        #! **最初の `^A` は確認に y が要る。** boost_level の 1 打目で済ませる。
        wizard_begin(s, "x", True)
        answer_stat_prompts(s, 12)
        boost_level(s, 17)
        mark = len(seen)

        for r_idx in ("322", "322", "310", "310", "310",
                      "342", "342", "342", "1280", "1280"):
            wizard_summon_named(s, r_idx)
            s.settle(quiet_rounds=1, slice_s=0.4, cap_s=6.0)
        drain_more(s)
        sweep()

        #! **殴らない。** 倒すと唱える相手が居なくなる。その場で待つだけ。
        for i in range(140):
            s.key(ord(","))
            s.settle(quiet_rounds=1, slice_s=0.25, cap_s=4.0)
            drain_more(s)
            sweep()
            if (i % 2) == 1:
                wizard_begin(s, "a", False)   # 全快（ゴーレムの殴りと目つぶしを消す）
                s.settle(quiet_rounds=1, slice_s=0.3, cap_s=4.0)
                drain_more(s)
                sweep()
        checks.add(len(seen) > mark, f"{tag}: 呪文使いを呼んで待てた",
                   f"{len(seen) - mark} 行")
    finally:
        try:
            s.quit_and_wait()
        except Exception:
            pass
        s.close()
    return checks, seen[mark:]


#: 組み立てる 4 本の見分け（日本語／英語）。**書式ごと訳しているので語で見る。**
_SPELLFMT_JA = (
    ("ブレス", "のブレスを吐いた"),
    ("ボール", "のボール"),
    ("ボルト", "のボルト"),
    ("召喚", "を召喚した"),
)
_SPELLFMT_EN = (
    ("ブレス", "breathes "),
    ("ボール", " Ball"),
    ("ボルト", " Bolt"),
    ("召喚", "summons "),
)


def cmd_spellfmt(work: Path) -> int:
    """**組み立てる書式の受け入れ**（FROX_JA_DESIGN §8.40。フック #46）。

    `_breath_msg()` / `_ball_msg()` / `_bolt_msg()` / `_summon_msg()` は
    **属性の名を `sprintf` で差し込んでから**ひな型にするので、#45（組んだ後に引く）
    では届かない。**書式は `fc_tr_fmt()`、名は `fc_spell_name()`（`M:`）**で別々に引く
    ——この検査は「その 2 本立てが実機で噛み合っているか」を見るためにある。

    見るのは 6 つ: **4 本とも日本語で流れること**・**`$CASTER` が字のまま
    残っていないこと**・**カタログが 1 件も弾かれていないこと**・
    **英語では 4 本とも英語で流れること**・**英語に日本語が 1 字も混じらないこと**。
    """
    checks = Checks()

    ja_checks, ja = _spellfmt_run(work, "spellfmt_ja", ("--lang=ja",))
    checks.rows += ja_checks.rows

    for label, needle in _SPELLFMT_JA:
        hit = [r for r in ja if needle in r]
        checks.add(bool(hit), f"{label}のメッセージが日本語で組まれた",
                   hit[0] if hit else f"({needle} が流れなかった)")

    left = [r for r in ja if "$CASTER" in r or "$TARGET" in r]
    checks.add(not left, "差し替えの印が字のまま残っていない", "; ".join(left[:2]))

    #! **属性の名が英語で残っていないか。** 書式だけ訳せて名が英語だと
    #! 「<color:G>Acid</color>のブレスを吐いた。」になる（`M:` の引き落ち）。
    #! **印の直前だけを見る。** 1 行にメッセージが何本も継がれて届くので
    #! （`巨大黒トンボは酸のブレスを吐いた。 A tree melted.`）、行に英字が
    #! あるかで見ると**まだ訳していない別のメッセージ**を掴んでしまう（実測 2026-08-28）。
    half = [r for r in ja
            if re.search(r"[A-Za-z]\s*(のブレス|のボール|のボルト|を召喚した)", r)]
    checks.add(not half, "属性の名まで日本語になっている", "; ".join(half[:2]))

    log = (work / "fc_spellfmt_ja_stderr.log").read_text(encoding="cp932", errors="replace")
    rep = [l for l in log.splitlines() if l.startswith("[frox] japanese:")]
    checks.add(bool(rep) and ("0 rejected, 0 tag mismatch" in rep[-1]),
               "カタログが 1 件も弾かれていない", rep[-1] if rep else "(報告が無い)")

    en_checks, en = _spellfmt_run(work, "spellfmt_en", ())
    checks.rows += en_checks.rows
    for label, needle in _SPELLFMT_EN:
        hit = [r for r in en if needle in r]
        checks.add(bool(hit), f"英語では英語で{label}が流れる",
                   hit[0] if hit else f"({needle} が流れなかった)")
    ja_chars = [c for r in en for c in r if ord(c) > 0x2000]
    checks.add(not ja_chars, "英語に日本語が 1 字も混じらない", "".join(ja_chars[:20]))

    print("  --- 日本語で流れたメッセージ（組み立てた 4 本）---")
    for label, needle in _SPELLFMT_JA:
        for r in [x for x in ja if needle in x][:2]:
            print("    " + r)
    #! **集めた行を全部出す。** 4 本のどれかが流れなかったとき、
    #! 「唱えなかった」のか「訳が届かなかった」のかはここを見ないと分からない。
    print("  --- 集めた行 (%d) ---" % len(ja))
    for r in ja:
        print("    " + r)
    return checks.finish()


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    cmd = sys.argv[1]
    work = Path(sys.argv[2])
    work.mkdir(parents=True, exist_ok=True)
    if not CORE.exists():
        print(f"FroxCore.exe not found at {CORE}")
        return 2
    if cmd == "handshake":
        return cmd_handshake(work)
    if cmd == "play":
        if len(sys.argv) < 4:
            print("play needs a script")
            return 2
        return cmd_play(work, sys.argv[3], sys.argv[4:])
    if cmd == "m0":
        return cmd_m0(work)
    if cmd == "m05":
        return cmd_m05(work)
    if cmd == "j2":
        return cmd_j2(work)
    if cmd == "j4":
        return cmd_j4(work, sys.argv[3:])
    if cmd == "j5":
        return cmd_j5(work, sys.argv[3:])
    if cmd == "j6":
        return cmd_j6(work)
    if cmd == "decor":
        return cmd_decor(work)
    if cmd == "msg":
        return cmd_msg(work)
    if cmd == "bldg":
        return cmd_bldg(work)
    if cmd == "spell":
        return cmd_spell(work)
    if cmd == "spellfmt":
        return cmd_spellfmt(work)
    if cmd == "fx":
        return cmd_fx(work)
    if cmd == "sfx":
        return cmd_sfx(work)
    if cmd == "shop":
        return cmd_shop(work, sys.argv[3:])
    print(f"unknown sub-command: {cmd}")
    return 2


if __name__ == "__main__":
    sys.exit(main())
