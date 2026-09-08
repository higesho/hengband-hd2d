#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""SilCore.exe を core プロトコル v1 で駆動する検査器（設計 §8 P2 / P3）。

`tools/gensoband/gb_protocol_driver.py` の弟。あちらとの違いは Sil-Q の事情そのもので、

  * 目録（asset_manifest）は**握手と一緒に来る**（地形の代表タイル。P3 で入った）
  * `--savefile=` が **無い**（枠の決め方はコアの中。設計 §3.3）
  * タイトルは **Sil-Q 自身が描く**（`initial_menu()`）ので、ミラーに
    `a) Tutorial` / `b) New character` が出るのが握手の証拠になる
  * 文字はすべて **ASCII**

    python tools/silq/sq_protocol_driver.py handshake <workdir>
    python tools/silq/sq_protocol_driver.py play      <workdir> <script>
    python tools/silq/sq_protocol_driver.py m0        <workdir>
    python tools/silq/sq_protocol_driver.py p4        <workdir>
    python tools/silq/sq_protocol_driver.py sfx       <workdir>

sub-command
  handshake … 握手・表・タイトルのミラー・キー進行（P2 の受け入れ）
  play      … スクリプトどおりキーを打ち、毎キーごとにミラーを出す（人が読む用）
  m0        … P3 = M0 の受け入れ（目録・cells・tile_index・ミニマップ・階の素性・
              セーブ → 再起動 → ロード）
  p4        … P4 の受け入れ（左の状態列・最下段の帯・階層。**中身を印字する**）
  sfx       … **効果音の配管**（AUDIO_DESIGN §2.1・§2.3）。壁にぶつかって
              frame.sounds に載ること・一度きりであること・
              `ui_state.sound_events` を降ろすと止まること

スクリプトの記法
  素の文字 … その ASCII を 1 キー
  \\r \\e \\s … Enter / ESC / Space
  \\t        … Tab
  \\^X       … Ctrl-X
  \\.        … 1 秒待つだけ（キーは送らない）
"""
from __future__ import annotations

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
CORE = REPO / "SilCore.exe"
SAVE_DIR = REPO / "silq" / "lib" / "save"
TERRAIN_MAP = REPO / "silq" / "tilework" / "terrain_map.csv"
#: 実体タイル（M2）。目録の後半はここから来る。
ENTITY_TILE_DIR = REPO / "silq" / "tilework" / "128"

#: `cells` は**配列の組**で運ばれる（`frame_codec.cpp` の `enc_cell`）。添字の意味:
#:   0 gx / 1 gy / 2 terrain_id / 3 feature_flags / 4 monster_id / 5 monster_slot /
#:   6 object_id / 7 light_level / 8 fg / 9 bg / 10 ascii / 11 tile_index / 12 under / 13-16 graf
CELL_FF = 3
CELL_LIGHT = 7
CELL_ASCII = 10
CELL_TILE = 11

#: `cell_feature_bits.h` の値。**コアと同じ数を書く**（画面側の enum の写し）。
FEAT_WALL = 0x0001
FEAT_STAIRS = 0x0008
FEAT_PLAYER = 0x0200
FEAT_KNOWN = 0x0400
FEAT_PASSABLE = 0x0800

#: 誕生を素通りしてダンジョンへ降りるスクリプト（実測。`play` で 1 手ずつ確かめた並び）。
#:   b   … b) New character
#:   Enter … 種族 / 家 / 能力値 / 技能 / 生い立ち / 年齢 を既定のまま受ける（6 回）
#:   名前 … ASCII を打って Enter
#:   Enter … 詩の画面を送ると 50 ft から始まる
BIRTH_SCRIPT = "b" + ("\\r" * 6) + "{name}" + "\\r\\r"


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
            # コアが先に落ちた／畳んだ。検査を止めずに先へ進める（結果は checks に出る）。
            self.eof = True


class Session:
    """1 本のコア起動。**必ず `close()` すること**（起動したら殺す）。"""

    def __init__(self, work: Path, tag: str, extra_args=()):
        self.work = work
        self.tag = tag
        self.err_path = work / f"sq_{tag}_stderr.log"
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
    def hello(self, ui_name="sq_protocol_driver.py"):
        self.link.send({"t": "hello", "protocol": 1, "ui_name": ui_name,
                        "ui_version": "0.1.0", "features": []})
        kind, obj = self.link.q.get(timeout=30)
        self.counts[kind] = self.counts.get(kind, 0) + 1
        if kind == "hello_ack":
            self.ack = obj
        return kind, obj

    def ui_state(self, view_w=40, view_h=22, sound_events=False):
        # sound_events … 画面側が音を鳴らす（コアは frame.sounds へ書き留めるだけ）。
        # AUDIO_DESIGN §2.1。既定は偽で、cmd_sfx だけが立てる。
        self.link.send({"t": "ui_state", "view_cells": {"w": view_w, "h": view_h},
                        "cursor_mode": False,
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
                  f" cells={len(f.get('cells', []))}")

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


def screen_signature(session) -> str:
    """画面の署名。**色まで含める。**

    `initial_menu()` の選択位置は**色だけ**で表される（`init2.c:1761-1768` は
    どの行も同じ文言を出し、選ばれている行だけ `TERM_L_BLUE` にする）。だから
    文字列だけを比べると「矢印を押しても画面が変わらない」と誤読する——
    初版の検査が実際にそう出た。
    """
    if not session.frames:
        return ""
    lines = session.frames[-1].get("menu_term_lines", [])
    return json.dumps([[ln.get("text_utf8", ""), ln.get("color_spans", [])] for ln in lines],
                      ensure_ascii=False, sort_keys=True)


def terrain_map_rows() -> int:
    """terrain_map.csv のデータ行数（＝目録の**地形の**件数）。"""
    n = 0
    for raw in TERRAIN_MAP.read_text(encoding="utf-8").splitlines():
        s = raw.strip()
        if not s or s.startswith("#") or s.startswith("feat,"):
            continue
        n += 1
    return n


def entity_tile_rows() -> int:
    """`silq/tilework/128` から目録へ載る**実体の**件数（M2）。

    数え方は `sq_manifest.cpp` の `add_entities()` の写しである。
    **枚数をそのまま数えない**——`SP1000` 以降（誕生画面の家の意匠）は
    地図に出ないので目録へ入れない、という除きがあちらにある。
    **片方を直したら両方直す。**
    """
    n = 0
    for path in ENTITY_TILE_DIR.glob("S*.png"):
        name = path.name
        if len(name) < 7:
            continue
        kind = name[1]
        if kind not in ("R", "K", "P"):
            continue
        digits = name[2:-4]
        if not digits.isdigit():
            continue
        if (kind == "P") and (int(digits) >= 1000):
            continue
        n += 1
    return n


def pick_letter(session, name):
    """セーブ一覧のミラーから `name` の行の letter を読む。無ければ空。

    一覧は **2 列**（`sq_bootstrap.c` の `SQ_SAVE_COLS`）なので 1 行に 2 件並ぶ。
    行頭だけを見ると 2 列目が拾えない。
    """
    for row in session.mirror():
        for hit in re.finditer(r"([a-z])\)\s+(\S+)", row):
            if hit.group(2) == name:
                return hit.group(1)
    return ""


def ingame_frame(session):
    """cells が入っているいちばん新しいフレーム。無ければ None。"""
    for f in reversed(session.frames):
        if f.get("cells"):
            return f
    return None


def has_title_menu(session) -> bool:
    """`initial_menu()` が出ているか（設計 §3.3 の証拠）。

    **英語と日本語の両方を見る。** `--lang=ja` では品書きが訳されているので、
    英語だけを探していると永久に見つからない——`wait_for()` が 180 秒
    まるごと待ってから偽で返り、**その間に送った鍵が落ちる**。
    誕生が始まらない事故の因がこれだった（P5 で 3 回踏んだ）。
    """
    rows = session.mirror()
    def seen(en, ja):
        return any((en in r) or (ja in r) for r in rows)
    return seen("New character", "新しい冒険者") and seen("Tutorial", "チュートリアル")


# =============================================================== handshake（P2）

def cmd_handshake(work: Path):
    ck = Checks()
    s = Session(work, "handshake")
    try:
        kind, ack = s.hello()
        print(f"  <- {kind}: {json.dumps(ack, ensure_ascii=False)}")
        ck.add(kind == "hello_ack", "hello_ack is the core's first message", f"got {kind!r}")
        ck.add(ack.get("protocol") == 1 and ack.get("core_name") == "silq",
               "hello_ack: protocol=1, core_name=silq",
               f"{ack.get('core_name')} {ack.get('core_version')}")
        ck.add("Sil-Q" in str(ack.get("core_version", "")),
               "hello_ack carries the upstream version string", str(ack.get("core_version")))
        roots = ack.get("asset_roots", {})
        ck.add("graf" not in roots, "hello_ack declares no asset_roots.graf (no 8/16px face)",
               str(roots.get("graf", "<absent>")))

        s.ui_state()
        print("[driver] waiting for init_angband + the title menu")
        ok = s.wait_for(lambda x: x.counts.get("pad_commands", 0) > 0 and has_title_menu(x),
                        timeout=180)
        ck.add(ok, "the core reached initial_menu() and mirrored it")
        ck.add(s.counts.get("pad_commands", 0) == 1, "pad_commands arrived exactly once",
               f"count={s.counts.get('pad_commands', 0)}")
        ck.add(s.counts.get("sub_panel_kinds", 0) == 1, "sub_panel_kinds arrived exactly once",
               f"count={s.counts.get('sub_panel_kinds', 0)}")
        # **P3 で目録が握手の列へ入った**（2026-08-23 に直した）。P2 のころは
        # 「まだ来ない」が正しかったが、地形の代表タイルを載せた時点で来るようになり、
        # この行だけ P2 の期待のまま残って FAIL していた。**来ることを見る**のが正しい。
        ck.add(s.counts.get("asset_manifest", 0) == 1,
               "asset_manifest arrived with the handshake (terrain tiles, P3)",
               f"count={s.counts.get('asset_manifest', 0)}")
        if s.pad:
            keymaps = {e.get("id"): e for e in s.pad.get("entries", [])}
            ck.add(len(keymaps) >= 20, "pad_commands carries the minimal table",
                   f"{len(keymaps)} entries, keymap={s.pad.get('current_keymap')}")
        s.show("title menu")

        rows = s.mirror()
        ck.add(any("Sil" in r for r in rows), "the introduction screen is in the mirror",
               next((r.strip() for r in rows if "Sil" in r), ""))

        multi = [ln for f in s.frames for ln in f.get("menu_term_lines", [])
                 if len(ln.get("color_spans", [])) > 1]
        ck.add(bool(multi), "at least one mirror line carries >1 colour span", f"{len(multi)} lines")

        """
        選択を矢印で動かす（'2' = 下 / '8' = 上）。**空白やリターンを混ぜないこと**——
        `initial_menu()` は `
` / `
` / スペースを「いまの選択を決定」として扱う
        （`init2.c:1810`）。初版のスクリプトは `"22 8"` で、空白が c) を選んでしまい、
        続く `8` と `b` が **askfor_aux の名前欄へ打ち込まれた**（Term の 21 行目
        50 桁に `8b` が出た）。誤検出ではなくメニューが正しく動いていた証拠である。
        """
        changed = 0
        for k in parse_script("2288"):
            if k < 0:
                time.sleep(1.0)
                continue
            before = screen_signature(s)
            s.key(k)
            s.settle()
            if screen_signature(s) != before:
                changed += 1
        ck.add(changed >= 3, "injected keys moved the menu highlight", f"{changed}/4")

        # b) = 新規作成へ入る。誕生画面（種族選択）が出れば「キーでゲームが進む」。
        s.key(ord("b"))
        s.settle()
        entered = s.wait_for(lambda x: any(("Noldor" in r) or ("Race" in r) or ("Sindar" in r)
                                           for r in x.mirror()), timeout=60)
        ck.add(entered, "b) New character entered the birth screen")
        s.show("birth screen")

        ids = [f.get("frame_id", -1) for f in s.frames]
        ck.add(all(b > a for a, b in zip(ids, ids[1:])), "frame_id strictly increasing",
               f"{len(ids)} frames")

        bye = s.quit_and_wait()
        ck.add(bye is not None, "exit message arrived after quit_request", json.dumps(bye))
        ck.add(s.proc.poll() is not None, "the core process exited", f"rc={s.proc.returncode}")
        ck.add(s.fatal is None, "no fatal was sent", str(s.fatal))
    finally:
        s.close()

    print("\n[driver] core stderr tail:")
    for line in s.stderr_tail():
        print("   ", line)
    return ck.finish()


# ==================================================================== play

def cmd_play(work: Path, script: str):
    s = Session(work, "play")
    try:
        kind, ack = s.hello()
        print(f"  <- {kind}: {json.dumps(ack, ensure_ascii=False)}")
        s.ui_state()
        s.wait_for(has_title_menu, timeout=180)
        s.show("start")
        for k in parse_script(script):
            if k < 0:
                time.sleep(1.0)
                s.drain(0.5)
                s.show("(waited)")
                continue
            s.key(k)
            s.settle()
            s.show(f"after key {k!r} ({chr(k) if 32 <= k < 127 else '.'})")
        s.quit_and_wait()
    finally:
        s.close()
    print("\n[driver] core stderr tail:")
    for line in s.stderr_tail(30):
        print("   ", line)
    return 0


# ======================================================================= m0（P3）

def cmd_m0(work: Path):
    ck = Checks()
    name = "M0test"

    # **前回の枠を消してから始める。** 残っていると新規作成が名前でぶつかる。
    for leftover in SAVE_DIR.glob(name + "*"):
        leftover.unlink()

    s = Session(work, "m0")
    try:
        kind, ack = s.hello()
        ck.add(kind == "hello_ack" and ack.get("core_name") == "silq",
               "hello_ack", str(ack.get("core_name")) + " " + str(ack.get("core_version")))
        s.ui_state(view_w=40, view_h=22)
        s.wait_for(has_title_menu, timeout=180)

        # **M2 で目録が伸びた**（2026-08-23 に直した。追補 ⑥）。M0 のころは地形 84 件
        # だけで、`k` は `F` しか無く、道はすべて変愚のタイルを指していた。いまは
        # 実体タイル（`silq/tilework/128`）が後ろに並ぶ。数え方はコアと同じ形で組む。
        want_terrain = terrain_map_rows()
        want_entity = entity_tile_rows()
        want = want_terrain + want_entity
        ck.add(s.counts.get("asset_manifest", 0) == 1, "asset_manifest arrived exactly once",
               "count=" + str(s.counts.get("asset_manifest", 0)))
        got = len(s.manifest.get("assets", [])) if s.manifest else 0
        ck.add(got == want, "asset_manifest carries terrain + entities (%d + %d)"
               % (want_terrain, want_entity), "got %d" % got)
        if s.manifest:
            rows = s.manifest["assets"]
            kinds = {a.get("k", "") for a in rows}
            ck.add(kinds <= {"F", "K", "P", "R"}, "every kind is one the ui knows",
                   str(sorted(kinds)))
            ck.add({"F", "K", "P", "R"} <= kinds, "terrain and all three entity kinds are there",
                   str(sorted(kinds)))
            terrain = [a.get("p", "") for a in rows if a.get("k") == "F"]
            entity = [a.get("p", "") for a in rows if a.get("k") != "F"]
            ck.add(len(terrain) == want_terrain, "terrain rows match terrain_map.csv",
                   "%d/%d" % (len(terrain), want_terrain))
            ck.add(all(q.startswith("tilework/sfc/") for q in terrain),
                   "every terrain row reuses a hengband tile", terrain[0] if terrain else "")
            ck.add(all(q.startswith("silq/tilework/128/") for q in entity),
                   "every entity row points at silq's own 128px tiles",
                   entity[0] if entity else "")

        print("[driver] creating a character")
        for k in parse_script(BIRTH_SCRIPT.format(name=name)):
            if k < 0:
                time.sleep(1.0)
                continue
            s.key(k)
            s.settle()

        ok = s.wait_for(lambda x: ingame_frame(x) is not None, timeout=120)
        ck.add(ok, "the game reached the dungeon (cells arrived)")
        f = ingame_frame(s)
        if f is None:
            s.show("last frame")
            return ck.finish()

        cells = f["cells"]
        ck.add(len(cells) == 40 * 22, "cells cover the requested view", "%d cells" % len(cells))
        ck.add(f.get("menu_open") in (None, False),
               "menu_open is off in game (so the ui draws the map, not the mirror)",
               str(f.get("menu_open")))
        ck.add(not f.get("menu_term_lines"), "no term mirror while the map is shown",
               "%d lines" % len(f.get("menu_term_lines", [])))

        known = [c for c in cells if (c[CELL_FF] & FEAT_KNOWN)]
        tiled = [c for c in known if c[CELL_TILE] > 0]
        ck.add(bool(known), "some cells are known", "%d/%d" % (len(known), len(cells)))
        ck.add(len(tiled) == len(known), "every known cell carries a terrain tile_index",
               "%d/%d" % (len(tiled), len(known)))

        walls = [c for c in known if (c[CELL_FF] & FEAT_WALL)]
        floors = [c for c in known if (c[CELL_FF] & FEAT_PASSABLE)]
        ck.add(bool(walls) and bool(floors), "walls and passable floor are both there",
               "%d walls / %d passable" % (len(walls), len(floors)))

        player = [c for c in cells if (c[CELL_FF] & FEAT_PLAYER)]
        ck.add(len(player) == 1, "exactly one cell is flagged as the player", str(len(player)))
        if player:
            ck.add(chr(player[0][CELL_ASCII] & 0xFF) == "@", "the player cell carries '@'",
                   repr(chr(player[0][CELL_ASCII] & 0xFF)))

        lit = [c for c in known if c[CELL_LIGHT] >= 2]
        ck.add(bool(lit), "some cells are lit (the torch reaches them)", "%d lit" % len(lit))

        colours = {c[8] for c in known}
        ck.add(all(0 <= v <= 15 for v in colours), "every colour is folded into 16",
               str(sorted(colours)))

        mm = f.get("minimap", {})
        ck.add(mm.get("width", 0) > 0 and mm.get("height", 0) > 0,
               "minimap carries the whole floor",
               "%sx%s" % (mm.get("width"), mm.get("height")))

        floor = f.get("floor", {})
        ck.add(floor.get("kind") == 2, "floor.kind is Dungeon", str(floor.get("kind")))
        ck.add(floor.get("dun_level", 0) >= 1, "floor.dun_level is set", str(floor.get("dun_level")))
        ck.add(floor.get("place_name", "") != "", "floor.place_name is set",
               str(floor.get("place_name")))

        light = f.get("lighting", {})
        ck.add(light.get("daytime") in (None, False), "lighting says night (Sil-Q is all underground)",
               str(light.get("daytime")))

        hud = f.get("hud", {})
        ck.add(hud.get("hp_max", 0) > 0, "hud carries hp", json.dumps(hud, ensure_ascii=False))
        # **P4 で 0 → -1 に改めた。**負が「この概念が無い」の印（`hud_snapshot.h`）。
        ck.add(hud.get("gold") == -1 and hud.get("level") == -1,
               "hud gold/level say 'absent' with -1 (Sil-Q has neither)",
               "gold=%s level=%s" % (hud.get("gold"), hud.get("level")))

        # セーブして畳む（quit_request は保存経路を通る。設計 §3.2）
        bye = s.quit_and_wait()
        ck.add(bye is not None, "exit after quit_request", json.dumps(bye))
    finally:
        s.close()

    saved = sorted(pp.name for pp in SAVE_DIR.glob(name + "*"))
    ck.add(bool(saved), "a savefile was written", ", ".join(saved))

    # ------------------------------------------------------------ 再起動してロード
    print("[driver] restarting and loading the save")
    s2 = Session(work, "m0_load")
    try:
        s2.hello()
        s2.ui_state(view_w=40, view_h=22)
        s2.wait_for(has_title_menu, timeout=180)
        # c) Open saved character → **一覧から選ぶ**（§9.10。2026-08-23 に直した）。
        # 名前を打つ作りではもう開けない。letter は決め打ちしない——並びは
        # セーブの増減でずれる（記憶「実機へセーブを運ぶスクリプト」）。
        s2.key(ord("c"))
        s2.settle()
        letter = pick_letter(s2, name)
        ck.add(bool(letter), "the save picker lists %s" % name,
               " / ".join(r.strip() for r in s2.mirror() if r.strip())[:160])
        if letter:
            s2.key(ord(letter))
            s2.settle()
        ok = s2.wait_for(lambda x: ingame_frame(x) is not None, timeout=120)
        ck.add(ok, "the save loaded back into the dungeon")
        f2 = ingame_frame(s2)
        if f2:
            ck.add(f2.get("hud", {}).get("name", "") == name, "the loaded character is the same one",
                   json.dumps(f2.get("hud", {}), ensure_ascii=False))
        else:
            s2.show("last frame")
        s2.quit_and_wait()
    finally:
        s2.close()

    print("")
    print("[driver] core stderr tail:")
    for line in s2.stderr_tail():
        print("   ", line)
    return ck.finish()



# =========================================================================== sfx

def collect_sounds(s, start_index: int):
    """frames[start_index:] の sounds を平らに集める。

    codec は既定値を省くので get で読む。**名前の鍵は `n`**
    （`frame_codec.cpp:654`。"name" ではない）。
    """
    out = []
    for f in s.frames[start_index:]:
        for e in f.get("sounds", []):
            out.append({"n": e.get("n", ""), "y": e.get("y", 0), "x": e.get("x", 0)})
    return out


def sfx_catalog_names():
    """画面側の目録（`assets/audio/sfx.jsonc`）に載っている名前。

    jsonc なので行頭の `//` を落としてから読む。読めなければ空集合
    ——目録が無い機でも検査そのものは通す。
    """
    try:
        text = (REPO / "assets" / "audio" / "sfx.jsonc").read_text(encoding="utf-8")
    except OSError:
        return set()
    body = re.sub(r"^\s*//.*$", "", text, flags=re.M)
    try:
        return set(json.loads(body).get("sounds", {}))
    except ValueError:
        return set()


def cmd_sfx(work: Path):
    """効果音の配管（`AUDIO_DESIGN` §2.1・§2.3）の機械検証。

    Sil-Q は音を**鳴らさない**。`sound()` が投げた `TERM_XTRA_SOUND` を名前と
    マスに直して `frame.sounds` へ載せるところまでが受け持ちで、鳴らすのは画面側。
    だからここで見るのは「載ったか」「一度きりか」「旗で止まるか」の 3 つである。

    音の出どころは **`message()` の型**である（`util.c:2960` が
    `sound(message_type)` を呼ぶ）ので、直に `sound()` と書いてある 15 か所より
    広く鳴る（`MSG_STAIRS` `MSG_KILL` `MSG_HIT` `MSG_OPENDOOR` `MSG_HITWALL` など）。
    ここで引くのは**必ず鳴るもの 1 つ**——品を落とす（`MSG_DROP`）である。

    見えていない敵の騒音（`SILQ_SFX_SCENES` の 75 か所）はまだ乗らない
    ——あれは P4 の仕事である。
    """
    ck = Checks()
    name = "SFXtest"
    for leftover in SAVE_DIR.glob(name + "*"):
        leftover.unlink()

    s = Session(work, "sfx")
    try:
        kind, ack = s.hello()
        ck.add(kind == "hello_ack", "hello_ack", str(kind))
        feats = (ack or {}).get("features", [])
        ck.add("audio" in feats, 'core declares "audio"', str(feats))

        s.ui_state(view_w=40, view_h=22, sound_events=True)
        s.wait_for(has_title_menu, timeout=180)
        for k in parse_script(BIRTH_SCRIPT.format(name=name)):
            if k < 0:
                time.sleep(1.0)
                continue
            s.key(k)
            s.settle()
        ck.add(s.wait_for(lambda x: ingame_frame(x) is not None, timeout=120),
               "birth reaches the dungeon")

        # ---- 品を 1 つ落とす（`sound(MSG_DROP)`。`object2.c:4030`）------------
        # **壁にぶつかる筋は採れない。** `do_cmd_walk_test()`（`cmd2.c:3218`）は
        # **既知の**壁を `msg_print`（＝`MSG_GENERIC`＝無音）で断る。音の出る
        # `message(MSG_HITWALL)` は `move_player()` の側で、**まだ知らない**壁に
        # 初めて当たったときにしか通らない（`cmd1.c:4786`）。
        #
        # 落とすほうは確実である——Sil-Q の出だしの持ち物は必ず
        # 「a) Fragments of Lembas」で、`d` `a` Enter で 1 つ落ちる。
        DROP = [ord("d"), ord("a"), 0x0D]

        mark = len(s.frames)
        for k in DROP:
            s.key(k)
            s.settle(quiet_rounds=1, slice_s=0.4, cap_s=8.0)
        heard = collect_sounds(s, mark)
        names = [e["n"] for e in heard]
        ck.add(bool(heard), "sounds reach the frame",
               "%d events: %s" % (len(heard), " ".join(sorted(set(names)))))
        ck.add("drop" in names, 'dropping an item pushes "drop"',
               " ".join(sorted(set(names))))

        # ---- マスは @ のマス（`TERM_XTRA_SOUND` は位置を運ばない）------------
        f = ingame_frame(s)
        if f is not None and heard:
            ck.add(heard[0]["y"] == f.get("player_gy") and heard[0]["x"] == f.get("player_gx"),
                   "the sound sits on the player's own grid",
                   "sound=(%s,%s) player=(%s,%s)" % (heard[0]["x"], heard[0]["y"],
                                                     f.get("player_gx"), f.get("player_gy")))

        # ---- 名前は綴りであって番号ではない（16 バイトに収まる ASCII）--------
        ck.add(all(e["n"] and (len(e["n"]) < 16) and e["n"].isascii() for e in heard),
               "every name is a printable spelling under 16 bytes")

        # ---- 目録に当たるか（変愚の wav をそのまま流用できているか）----------
        catalog = sfx_catalog_names()
        if catalog:
            missing = sorted({n for n in names if n not in catalog})
            ck.add(not missing, "every heard name resolves in assets/audio/sfx.jsonc",
                   ("missing: " + " ".join(missing)) if missing else
                   "%d names" % len(set(names)))


        # ---- 周囲の地形の内訳（環境音の層。`AUDIO_AMBIENCE_FIELDS` §8.1）----
        # **数えていないと丸ごと出ない**（`radius == 0` は codec が畳む）ので、
        # 鍵が在ることそのものが「数えた」の証拠になる。
        # **Sil-Q で埋まるのは壁と瓦礫の 2 つだけ**（草も水も木も存在しない）。
        f = ingame_frame(s) or {}
        sur = f.get("surroundings", {})
        ck.add(sur.get("radius", 0) > 0, "surroundings are counted",
               json.dumps(sur, ensure_ascii=False))
        ck.add(sur.get("counted", 0) > 0, "some grids were known enough to count",
               str(sur.get("counted")))
        ck.add(sur.get("wall", 0) > 0, "the dungeon carries the enclosed layer",
               "wall=%s" % sur.get("wall"))
        empty = [k for k in ("grass", "tree", "water", "deep_water", "lava", "swamp", "dirt")
                 if sur.get(k, 0)]
        ck.add(not empty, "no material Sil-Q does not have is counted", " ".join(empty))

        # ---- 一度きりの縁である（§2.3。汲んだら消える）----------------------
        # **音の出ない手番を渡す。** `5` は pref.prf が `z`（その場に留まる）へ
        # 写しているキーで、ターンは進むが音は鳴らない。`frame.sounds` が
        # 居座っていれば、ここで同じ音がもう一度出てくる。
        mark = len(s.frames)
        for _ in range(3):
            s.key(ord("5"))
            s.settle(quiet_rounds=1, slice_s=0.4, cap_s=6.0)
        again = collect_sounds(s, mark)
        ck.add(not again, "a quiet turn pushes no sound twice",
               " ".join(e["n"] for e in again))

        # ---- 旗を降ろすと止まる（`ui_state.sound_events`）--------------------
        # **`use_sound` は落とさない**ので、旗を上げ直せばまた出る。
        s.ui_state(view_w=40, view_h=22, sound_events=False)
        s.settle(quiet_rounds=1, slice_s=0.5, cap_s=8.0)
        mark = len(s.frames)
        for k in DROP:
            s.key(k)
            s.settle(quiet_rounds=1, slice_s=0.4, cap_s=8.0)
        silent = collect_sounds(s, mark)
        ck.add(not silent, "sound_events=false stops the events",
               " ".join(e["n"] for e in silent))

        bye = s.quit_and_wait()
        ck.add(bye is not None, "exit after quit_request", json.dumps(bye))
    finally:
        s.close()

    for leftover in SAVE_DIR.glob(name + "*"):
        leftover.unlink()
    return ck.finish()


# ================================================================ p4（HUD 充填）

def show_status_col(f):
    """左の状態列・最下段の帯・階層を**人が読める形**で出す。"""
    print("  --- status_col_lines（左 13 桁の写し） ---")
    for i, ln in enumerate(f.get("status_col_lines", [])):
        print("   [%2d] |%-13s| color=%s" % (i, ln.get("text_utf8", ""), ln.get("color", 1)))
    print("  --- bottom_row_runs（最下段の帯。桁は Term のまま） ---")
    for r in f.get("bottom_row_runs", []):
        print("   col=%-3s color=%-3s |%s|" % (r.get("col", 0), r.get("color", 1), r.get("text_utf8", "")))
    print("  bottom_row_cols=%s  depth=%s" % (f.get("bottom_row_cols"),
                                              json.dumps(f.get("depth", {}), ensure_ascii=False)))


def cmd_p4(work: Path):
    """P4 の受け入れ（設計 §8 の P4 行）。

    **表示を組み直さず Term のその区画を読む**方針なので、検査も
    「読んだものがコアの画とも HUD とも食い違わない」で見る。
    """
    ck = Checks()
    name = "P4test"
    for leftover in SAVE_DIR.glob(name + "*"):
        leftover.unlink()

    s = Session(work, "p4")
    try:
        kind, ack = s.hello()
        ck.add(kind == "hello_ack" and ack.get("core_name") == "silq", "hello_ack",
               str(ack.get("core_name")))
        s.ui_state(view_w=40, view_h=22)
        s.wait_for(has_title_menu, timeout=180)

        # タイトルでは状態列を積まない（@ がまだ無い。左端は本文）
        t = s.frames[-1]
        ck.add(not t.get("status_col_lines"), "no status column before the character exists",
               "%d lines" % len(t.get("status_col_lines", [])))

        print("[driver] creating a character")
        for k in parse_script(BIRTH_SCRIPT.format(name=name)):
            if k < 0:
                time.sleep(1.0)
                continue
            s.key(k)
            s.settle()

        ok = s.wait_for(lambda x: ingame_frame(x) is not None, timeout=120)
        ck.add(ok, "the game reached the dungeon (cells arrived)")
        f = ingame_frame(s)
        if f is None:
            s.show("last frame")
            return ck.finish()

        show_status_col(f)

        lines = f.get("status_col_lines", [])
        texts = [ln.get("text_utf8", "") for ln in lines]
        hud = f.get("hud", {})

        ck.add(bool(lines), "the status column is filled in game", "%d lines" % len(lines))
        ck.add(all(len(t) <= 13 for t in texts), "every line stays inside the 13 sidebar columns",
               "max=%d" % max([len(t) for t in texts] or [0]))

        # HP は左の列にも出る（`ROW_HP = 10`）。**HUD の数と一致すること**が P4 の受け入れ。
        # 見出しは `HP` ではなく **`Health`**（`defines.h` の註記は "HP xxxxxxxxx" だが、
        # `prt_hp()` が実際に書くのは `Health nn:nn`。SP は `Voice`）。実物に合わせる。
        hp_rows = [t for t in texts if t.strip().startswith("Health")]
        want_hp = "%d:%d" % (hud.get("hp", -1), hud.get("hp_max", -1))
        got_hp = ""
        if hp_rows:
            nums = [int(v) for v in re.findall(r"\d+", hp_rows[0])]
            got_hp = "%d:%d" % (nums[0], nums[1]) if len(nums) >= 2 else str(nums)
        ck.add(bool(hp_rows) and got_hp == want_hp, "the sidebar Health matches hud.hp",
               "sidebar=%r hud=%s" % (hp_rows[0] if hp_rows else None, want_hp))

        voice_rows = [t for t in texts if t.strip().startswith("Voice")]
        want_sp = "%d:%d" % (hud.get("sp", -1), hud.get("sp_max", -1))
        got_sp = ""
        if voice_rows:
            nums = [int(v) for v in re.findall(r"\d+", voice_rows[0])]
            got_sp = "%d:%d" % (nums[0], nums[1]) if len(nums) >= 2 else str(nums)
        ck.add(bool(voice_rows) and got_sp == want_sp, "the sidebar Voice matches hud.sp",
               "sidebar=%r hud=%s" % (voice_rows[0] if voice_rows else None, want_sp))

        # 技能の 3 つ（近接・射撃・回避）は左の列にしか無い。**出ていること**を見る。
        ck.add(any("(" in t and "," in t for t in texts),
               "the melee/archery/evasion rows are there",
               repr([t for t in texts if "(" in t or "[" in t]))

        # 階層は**右下の 1 か所だけ**。`NNNN ft` で、hud.depth（階）と ×50 で合う。
        depth_text = f.get("depth", {}).get("text_utf8", "")
        want_ft = hud.get("depth", 0) * 50
        ck.add(depth_text == "%d ft" % want_ft, "depth is the core's own wording and matches hud",
               "%r vs %d ft" % (depth_text, want_ft))
        ck.add(depth_text not in texts, "the depth row is not duplicated in the sidebar",
               repr(texts[-3:]))
        # ROW_DEPTH の行は空行のまま残す（下の min が上がらないように）
        ck.add(any(t.strip().startswith("min ") for t in texts),
               "min depth stays in the sidebar where the core drew it",
               repr([t for t in texts if "min" in t]))

        ck.add(f.get("bottom_row_cols", 0) == 80, "bottom_row_cols is the term width",
               str(f.get("bottom_row_cols")))
        # 誕生直後の帯は `Sunlight`（入口のマスは `FEAT_SUNLIGHT`。`prt_terrain()` が
        # `COL_TERRAIN = 72` に書く）。**空とは限らない**ので、桁だけ見て中身は問わない。
        ck.add(all(r.get("col", 0) >= 13 for r in f.get("bottom_row_runs", [])),
               "the bar carries only what the core wrote right of the sidebar",
               repr([(r.get("col"), r.get("text_utf8")) for r in f.get("bottom_row_runs", [])]))

        # 空の帯を「出た」と見なすと何も確かめたことにならないので、**印を 1 つ立てさせる**。
        # 忍び足（`S`）は `prt_state()` が `COL_STATE = 56` に `Stealth` と書く。
        print("[driver] toggling stealth mode (S) to make the bar say something")
        s.key(ord("S"))
        s.settle()
        g = ingame_frame(s)
        show_status_col(g)
        runs = g.get("bottom_row_runs", []) if g else []
        stealth = [r for r in runs if r.get("text_utf8", "").strip() == "Stealth"]
        ck.add(bool(stealth), "the bar carries the core's own status word", repr(runs))
        ck.add(bool(stealth) and stealth[0].get("col") == 56,
               "the run keeps the core's column (COL_STATE = 56)",
               str(stealth[0].get("col")) if stealth else "-")
        ck.add(all(r.get("col", 0) >= 13 for r in runs),
               "no run starts left of COL_MAP=13 (the sidebar is not in the bar)",
               repr([r.get("col") for r in runs]))
        ck.add(not any("min " in r.get("text_utf8", "") for r in runs),
               "'min NNNN ft' did not leak into the bar", repr([r.get("text_utf8") for r in runs]))
        s.key(ord("S"))
        s.settle()

        ck.add(hud.get("gold", 0) == -1 and hud.get("level", 0) == -1,
               "hud says 'this core has neither gold nor level' (-1, not 0)",
               "gold=%s level=%s" % (hud.get("gold"), hud.get("level")))

        # 別画面（キャラクターシート `@`。**`C` ではない**——Sil-Q の `C` は何もしない）を
        # 開いても、状態列が本文で塗り替わらないこと。
        before = [ln.get("text_utf8", "") for ln in (ingame_frame(s) or f).get("status_col_lines", [])]
        s.key(ord("@"))
        s.settle()
        icky = s.frames[-1]
        ck.add(bool(icky.get("menu_term_lines")) and icky.get("menu_open") is True,
               "the character sheet really came up (so the next check means something)",
               "menu_open=%s lines=%d" % (icky.get("menu_open"), len(icky.get("menu_term_lines", []))))
        after = [ln.get("text_utf8", "") for ln in icky.get("status_col_lines", [])]
        ck.add(after == before, "the sidebar keeps its last good value while a full screen is up",
               "%d vs %d lines" % (len(after), len(before)))
        s.key(0x1B)
        s.settle()

        s.quit_and_wait()
    finally:
        s.close()

    for leftover in SAVE_DIR.glob(name + "*"):
        leftover.unlink()
    return ck.finish()


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd = argv[1]
    work = Path(argv[2])
    work.mkdir(parents=True, exist_ok=True)
    if not CORE.is_file():
        print(f"[driver] {CORE} is not there. build it first.")
        return 2
    if cmd == "handshake":
        return cmd_handshake(work)
    if cmd == "m0":
        return cmd_m0(work)
    if cmd == "p4":
        return cmd_p4(work)
    if cmd == "sfx":
        return cmd_sfx(work)
    if cmd == "play":
        return cmd_play(work, argv[3] if len(argv) > 3 else "")
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
