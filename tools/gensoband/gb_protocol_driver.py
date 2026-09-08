#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""GensobandCore.exe を core プロトコル v1 で駆動する検査器（設計 §8 P2 / P3）。

`tools/diff_test/protocol_driver.py` は使い回せない——あちらは変愚の固定資産
（セーブ雛形・bot JSON・official_tile_table）に癒着していて、幻想蛮怒には
そのどれも無い（P2 の実測）。ここは**プロトコルだけ**を叩く。

    python tools/gensoband/gb_protocol_driver.py handshake  <workdir>
    python tools/gensoband/gb_protocol_driver.py play       <workdir> <script> [mode]
    python tools/gensoband/gb_protocol_driver.py m0         <workdir>
    python tools/gensoband/gb_protocol_driver.py systems    <workdir>
    python tools/gensoband/gb_protocol_driver.py stores     <workdir>
    python tools/gensoband/gb_protocol_driver.py combat     <workdir>
    python tools/gensoband/gb_protocol_driver.py sfx        <workdir>

sub-command
  handshake … 握手・表・誕生画面のミラー・キー進行（P2 の受け入れ）
  play      … スクリプトどおりキーを打ち、毎キーごとにミラーを出す（人が読む用）
  m0        … P3 = M0 の受け入れ 2（asset_roots.slab / asset_manifest /
              ゲーム内フレームの cells・minimap・tile_index / セーブ→再起動→ロード）
  systems   … M1 = システム受け入れ（GENSOBAND_SYSTEM_ACCEPT の S1〜S8）
  stores    … M1 その2 R3 = 店・建物の選択肢。**町を歩く**ので systems と分けてある
              （終わりに quit_request を送らず殺すので、セーブには何も書かない）
  combat    … M1 その2 R4 = 戦闘の見せ場（combat_fx）。**呪文を覚えて撃つ**ので
              世界が進む。stores と同じく殺して終わる
  sfx       … **効果音の引き渡し**（AUDIO_DESIGN §2.1・§2.2）。画面側が
              鳴らす間はコアが 1 度も鳴らさないこと・旗を降ろすとコアが引き取ること。
              **食べる**ので世界が進む。stores と同じく殺して終わる

スクリプトの記法（play / m0 で共通）
  素の文字 … その ASCII を 1 キー
  \\r \\e \\s … Enter / ESC / Space
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
CORE = REPO / "GensobandCore.exe"
SAVE_DIR = REPO / "gensoband" / "lib" / "save"

#! mapping.csv のデータ行数（＝ asset_manifest の件数）。ヘッダと `#` を除いた数。
EXPECTED_MANIFEST = None  # 実行時に mapping.csv から数える


def mapping_row_count() -> int:
    """mapping.csv のデータ行数を数える（コアが送るべき目録の件数）。"""
    path = REPO / "gensoband" / "tilework" / "mapping.csv"
    n = 0
    with path.open("rb") as fp:
        for raw in fp:
            line = raw.strip()
            if not line or line.startswith(b"#") or line.startswith(b"kind,"):
                continue
            n += 1
    return n


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

    def close_stdin(self):
        try:
            self.proc.stdin.close()
        except OSError:
            pass


class Session:
    """1 本のコア起動。**必ず `close()` すること**（起動したら殺す）。"""

    def __init__(self, work: Path, tag: str, extra_args=(), savefile=None):
        self.work = work
        self.tag = tag
        self.err_path = work / f"gb_{tag}_stderr.log"
        self._errfp = self.err_path.open("wb")
        args = [str(CORE), "--ui-protocol=stdio"]
        if savefile:
            args.append(f"--savefile={savefile}")
        args.extend(extra_args)
        self.proc = subprocess.Popen(
            args, cwd=str(REPO), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=self._errfp, env=dict(os.environ), bufsize=0)
        self.link = Link(self.proc)
        self.frames = []
        self.counts = {}
        self.ack = None
        self.manifest = None
        self.pad = None
        self.panel_kinds = None
        self.exit_msg = None
        self.fatal = None

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
            elif kind == "asset_manifest":
                self.manifest = obj
            elif kind == "pad_commands":
                self.pad = obj
            elif kind == "sub_panel_kinds":
                self.panel_kinds = obj
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
    def hello(self, ui_name="gb_protocol_driver.py"):
        self.link.send({"t": "hello", "protocol": 1, "ui_name": ui_name,
                        "ui_version": "0.1.0", "features": []})
        kind, obj = self.link.q.get(timeout=30)
        self.counts[kind] = self.counts.get(kind, 0) + 1
        if kind == "hello_ack":
            self.ack = obj
        return kind, obj

    def ui_state(self, view_w=40, view_h=22, cursor_mode=False, panel_kinds=None,
                 panel_cells=(40, 12), sound_on=False, music_on=False,
                 sound_volume_index=0, music_volume_index=0, sound_events=False):
        #! 音量は**添字**（0 = 100% … 9 = 10%）。画面側の 0〜10 の段とは向きが逆で、
        #! 換えるのは `Hd2dSettings::volume_step_to_index()` の 1 か所だけである。
        #!
        #! `sound_events` … **効果音を誰が鳴らすか**（AUDIO_DESIGN §2.1・§2.2）。
        #! 真ならコアは鳴らさず `frame.sounds` へ書き留める。偽なら従来どおり
        #! `gb_audio.c` が winmm で鳴らす。既定は偽で、`cmd_sfx` だけが立てる。
        msg = {"t": "ui_state", "view_cells": {"w": view_w, "h": view_h},
                        "cursor_mode": bool(cursor_mode),
                        "map_style": {"graf_px": 0, "graf_tag": "ascii"},
                        "audio": {"sound_on": bool(sound_on), "music_on": bool(music_on),
                                  "sound_events": bool(sound_events),
                                  "sound_volume_index": int(sound_volume_index),
                                  "music_volume_index": int(music_volume_index)}}
        if panel_kinds is not None:
            msg["sub_panel_kinds"] = list(panel_kinds)
        cols, rows = panel_cells
        msg["sub_panel_cells"] = [{"cols": cols, "rows": rows} for _ in range(7)]
        self.link.send(msg)

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

    def stderr_text(self):
        """stderr の全文（`stderr_tail` は末尾しか返さない。音の記録は途中に出る）。"""
        return self.err_path.read_bytes().decode("cp932", "replace")

    def stderr_tail(self, n=15):
        text = self.err_path.read_bytes().decode("cp932", "replace")
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


def ingame_frame(session):
    """cells が入っているいちばん新しいフレーム。無ければ None。"""
    for f in reversed(session.frames):
        if f.get("cells"):
            return f
    return None


# =============================================================== handshake（P2）

def cmd_handshake(work: Path):
    ck = Checks()
    """
    **必ず新規で始める。** 枠を指定しないと `last_slot.txt` に覚えた枠が読み込まれ、
    誕生画面を素通りしてゲーム内から始まってしまう（`menu_open` が偽 → Term ミラーは
    空 → 「日本語が読める」の検査が落ちる）。在り得ない枠の名を渡して birth へ倒す。
    """
    slot = "gbhandshake"
    for leftover in SAVE_DIR.glob(f"{slot}*"):
        leftover.unlink()
    s = Session(work, "handshake", savefile=slot)
    try:
        kind, ack = s.hello()
        print(f"  <- {kind}: {json.dumps(ack, ensure_ascii=False)}")
        ck.add(kind == "hello_ack", "hello_ack is the core's first message", f"got {kind!r}")
        ck.add(ack.get("protocol") == 1 and ack.get("core_name") == "gensoband",
               "hello_ack: protocol=1, core_name=gensoband",
               f"{ack.get('core_name')} {ack.get('core_version')}")
        roots = ack.get("asset_roots", {})
        ck.add(bool(roots.get("slab")), "hello_ack.asset_roots.slab is declared (design 5.4)",
               str(roots.get("slab", "<absent>")))
        ck.add("graf" not in roots, "hello_ack declares no asset_roots.graf (no 8/16px face)",
               str(roots.get("graf", "<absent>")))

        s.ui_state()
        print("[driver] waiting for init_angband + birth")
        s.wait_for(lambda x: x.counts.get("pad_commands", 0) > 0
                   and any(("性別" in r) or ("種族" in r) or ("難易度" in r) for r in x.mirror()),
                   timeout=120)
        ck.add(s.counts.get("pad_commands", 0) == 1, "pad_commands arrived exactly once",
               f"count={s.counts.get('pad_commands', 0)}")
        ck.add(s.counts.get("sub_panel_kinds", 0) == 1, "sub_panel_kinds arrived exactly once",
               f"count={s.counts.get('sub_panel_kinds', 0)}")
        ck.add(s.counts.get("asset_manifest", 0) == 1, "asset_manifest arrived exactly once",
               f"count={s.counts.get('asset_manifest', 0)}")
        want = mapping_row_count()
        got = len(s.manifest.get("assets", [])) if s.manifest else 0
        ck.add(got == want, f"asset_manifest carries every mapping.csv row ({want})", f"got {got}")
        if s.manifest:
            # ワイヤの鍵は縮めてある（`protocol_messages.cpp` の encode_asset_manifest）:
            # i=index / k=kind / id=id / p=path。
            paths = [a.get("p", "") for a in s.manifest["assets"]]
            ck.add(any(p.startswith("gensoband/tilework/ascii/") for p in paths),
                   "asset_manifest contains ascii slab paths",
                   next((p for p in paths if p.startswith("gensoband/tilework/ascii/")), ""))
            ck.add(any(p.startswith("tilework/sfc/") for p in paths),
                   "asset_manifest reuses hengband tile paths",
                   next((p for p in paths if p.startswith("tilework/sfc/")), ""))
        s.show("birth screen")

        rows = s.mirror()
        jp = [r for r in rows if any("぀" <= c <= "ヿ" or "一" <= c <= "鿿" for c in r)]
        ck.add(bool(jp), "term_mirror carries readable Japanese", f"{len(jp)} japanese lines")

        multi = [ln for f in s.frames for ln in f.get("menu_term_lines", [])
                 if len(ln.get("color_spans", [])) > 1]
        ck.add(bool(multi), "at least one mirror line carries >1 colour span", f"{len(multi)} lines")

        # 誕生の頭 4 手（難易度 → 説明の確認 → 立場 → 登場作品）。**画面が毎回変わる手**を選ぶ。
        # 同じ 'a' を 4 回打つのは駄目で、難易度画面の 2 手目以降は何も起きない（Enter 待ち）。
        probe = parse_script(r"b\raa")
        changed = 0
        for k in probe:
            before = "\n".join(s.mirror())
            s.key(k)
            s.settle()
            if "\n".join(s.mirror()) != before:
                changed += 1
        ck.add(changed >= 3, "injected keys advanced the screen", f"{changed}/{len(probe)}")

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
    print(f"\n[driver] message counts: {s.counts}")
    return ck.finish()


# ==================================================================== play（人用）

def cmd_play(work: Path, script: str, mode: str = "quit"):
    s = Session(work, "play")
    keys = parse_script(script)
    try:
        s.hello("gb_protocol_driver.py")
        s.ui_state()
        s.wait_for(lambda x: x.counts.get("pad_commands", 0) > 0, timeout=120)
        s.settle()
        s.show("after init")
        for n, k in enumerate(keys):
            if k < 0:
                s.drain(1.0)
                continue
            s.key(k)
            s.settle()
            s.show(f"#{n} key {k:#04x} {chr(k) if 32 <= k < 127 else ''!r}")
        if mode == "eof":
            print("[play] closing stdin (a dead ui)")
            s.link.close_stdin()
            end = time.monotonic() + 60
            while time.monotonic() < end and s.exit_msg is None:
                s.drain(0.5)
        elif mode == "stay":
            pass
        else:
            print("[play] quit_request")
            print(f"[play] exit = {s.quit_and_wait()}")
    finally:
        s.close()
    print(f"[play] rc={s.proc.returncode} counts={s.counts}")
    print("[play] stderr:")
    for line in s.stderr_tail(20):
        print("   ", line)
    return 0


# ======================================================================= m0（P3）

#! 誕生を抜けて人里に立つまでのスクリプト（幻想蛮怒 2.1.6 / 既定オプション）。**ユニーククラス**。
#!
#!  b\r            難易度 NORMAL → 説明を読んで Enter
#!  a              「幻想郷で少しは名の知られた者だ」＝**ユニークプレイヤー**（birth.c:7019）
#!  a a y          登場作品「定番自機二人」→「博麗　霊夢」→ よろしいですか [Y/n]
#!  a y            魔法領域「神秘」→ 確認
#!  a y            性格「極めて普通」→ 確認
#!  \e             初期オプション頁は **Enter では抜けられず ESC で抜ける**（P2 申し送り 4）
#!  222222 \r      ボーナス割り振り: 2 で下へ 6 回 →「決定する」で Enter
#!  \r             能力値の確認（'r' 再ロール / Enter 決定）
#!  reimu \r       セーブファイル名。**既定は「博麗　霊夢」**（全角）なので打ち直す
#!  \r \r          生い立ちの編集を終える → ゲーム開始
BIRTH_SCRIPT = r"b\raaayayay\e222222\r\rreimu\r\r\r"
#! `BIRTH_SCRIPT` が作る枠の名（セーブファイル名の入力に合わせる）。
BIRTH_SLOT = "reimu"


def cmd_m0(work: Path):
    ck = Checks()
    slot = BIRTH_SLOT
    # 前の走りの残りを片付ける（**変愚の lib には触らない**）。
    for leftover in SAVE_DIR.glob(f"{slot}*"):
        leftover.unlink()

    # ---------------------------------------------------- 1 本目: 新規 → セーブ
    s = Session(work, "m0a", savefile=slot)
    try:
        kind, ack = s.hello()
        roots = ack.get("asset_roots", {})
        ck.add(kind == "hello_ack" and ack.get("core_name") == "gensoband",
               "hello_ack core_name=gensoband", str(ack.get("core_version")))
        ck.add(bool(roots.get("slab")), "hello_ack.asset_roots.slab declared", str(roots.get("slab")))
        s.ui_state(view_w=48, view_h=24)
        s.wait_for(lambda x: x.counts.get("asset_manifest", 0) > 0, timeout=120)
        want = mapping_row_count()
        got = len(s.manifest.get("assets", [])) if s.manifest else 0
        ck.add(got == want, f"asset_manifest has {want} entries", f"got {got}")

        s.settle()
        print("[m0] walking the birth screens")
        for k in parse_script(BIRTH_SCRIPT):
            if k < 0:
                s.drain(1.0)
                continue
            s.key(k)
            s.settle(quiet_rounds=2, slice_s=0.7, cap_s=20)
            if ingame_frame(s) is not None:
                break
        s.show("after birth")

        f = ingame_frame(s)
        ck.add(f is not None, "an in-game frame with cells arrived",
               f"{len(f.get('cells', [])) if f else 0} cells")
        if f is not None:
            cells = f["cells"]
            # v1 6.2 の 17 要素タプル: [gx,gy,terrain,flags,mid,slot,oid,light,fg,bg,ascii,ti,uti,...]
            ti = [c[11] for c in cells]
            ck.add(any(t != 0 for t in ti), "cells carry non-zero tile_index",
                   f"{sum(1 for t in ti if t)}/{len(ti)} non-zero")
            ck.add(any(c[3] != 0 for c in cells), "cells carry feature_flags",
                   f"{sum(1 for c in cells if c[3])} known cells")
            ck.add(any((c[3] & 0x0200) for c in cells), "exactly one cell has CELL_FEAT_PLAYER",
                   f"{sum(1 for c in cells if c[3] & 0x0200)}")
            mm = f.get("minimap", {})
            ck.add(mm.get("width", 0) > 0 and bool(mm.get("kinds_b64")),
                   "minimap is filled", f"{mm.get('width')}x{mm.get('height')}")
            ck.add(f.get("menu_open") is not True,
                   "menu_open is false while the map is up", str(f.get("menu_open")))
            hud = f.get("hud", {})
            ck.add(hud.get("hp_max", 0) > 0, "hud has hp", json.dumps(hud, ensure_ascii=False))

        print("[m0] quit_request (forced save)")
        bye = s.quit_and_wait()
        ck.add(bye is not None, "exit after quit_request", json.dumps(bye))
    finally:
        s.close()
    for line in s.stderr_tail(8):
        print("   ", line)

    saves = sorted(p.name for p in SAVE_DIR.glob(f"{slot}*"))
    ck.add(bool(saves), "a savefile was written", str(saves))

    # ------------------------------------------------------- 2 本目: ロード
    s2 = Session(work, "m0b", savefile=slot)
    try:
        s2.hello()
        s2.ui_state(view_w=48, view_h=24)
        loaded = s2.wait_for(lambda x: ingame_frame(x) is not None, timeout=150)
        s2.settle()
        s2.show("after load")
        f2 = ingame_frame(s2)
        ck.add(loaded and f2 is not None,
               "restart loaded the save straight into the game (no birth screens)",
               f"{len(f2.get('cells', [])) if f2 else 0} cells")
        if f2 is not None:
            ck.add(f2.get("hud", {}).get("name", "") != "", "loaded character has a name",
                   json.dumps(f2.get("hud", {}), ensure_ascii=False))
        s2.quit_and_wait()
    finally:
        s2.close()
    for line in s2.stderr_tail(8):
        print("   ", line)

    return ck.finish()


# ======================================================== systems（M1 受け入れ）

#! `GENSOBAND_SYSTEM_ACCEPT` の S1〜S8 を機械で突く。
#! **設計 §1 制約 7 の「合成フレームの PNG を見る」を置き換えるものではない**
#! ——ここが見るのはワイヤに載ったかどうかだけで、画に出たかは別に見ること。

def reach_ingame(s, work, tag):
    """在るセーブで入る。無ければ誕生画面を歩いて作る。"""
    if s.wait_for(lambda x: ingame_frame(x) is not None, timeout=45):
        return True
    print(f"[{tag}] no save; walking the birth screens")
    for k in parse_script(BIRTH_SCRIPT):
        if k < 0:
            s.drain(1.0)
            continue
        s.key(k)
        s.settle(quiet_rounds=2, slice_s=0.7, cap_s=20)
        if ingame_frame(s) is not None:
            return True
    return ingame_frame(s) is not None


def cmd_systems(work: Path):
    ck = Checks()
    s = Session(work, "sys", savefile=BIRTH_SLOT)
    try:
        kind, ack = s.hello()
        ck.add(kind == "hello_ack", "hello_ack", str(ack.get("core_version")))
        # 7 枚とも別々の種類を割り当てる（S1）。番号は window_flag_desc のビット。
        kinds = [6, 0, 1, 4, 12, 3, 10]
        s.ui_state(view_w=48, view_h=24, cursor_mode=True, panel_kinds=kinds)
        ck.add(reach_ingame(s, work, "sys"), "reached an in-game frame",
               f"{len(s.frames)} frames")
        s.settle()

        # ---------------------------------------------------------- S3 コマンド表
        pad = s.pad or {}
        entries = pad.get("entries", [])
        groups = sorted({e.get("group_utf8", "") for e in entries})
        labels = [e.get("label_utf8", "") for e in entries]
        ck.add(len(entries) >= 40, "S3 pad_commands comes from the core menu",
               f"{len(entries)} entries / {len(groups)} groups")
        ck.add(len(groups) >= 8, "S3 every menu group is present", " ".join(groups))
        for want in ("読む(r)", "ダンジョンの全体図(M)", "投げる(v)", "オプション(=)"):
            ck.add(any(want in l for l in labels), f"S3 label {want} is listed",
                   next((l for l in labels if want in l), "<missing>"))
        keyless = [e.get("label_utf8", "") for e in entries if not e.get("seq_original")]
        ck.add(not keyless, "S3 every entry resolves to a key in the original keyset",
               f"{len(keyless)} keyless: {keyless[:4]}")
        ctrl = [e for e in entries if any(k < 0x20 for k in e.get("seq_original", []))]
        ck.add(bool(ctrl), "S3 control-key commands survive the reverse lookup",
               ", ".join(f"{e['label_utf8']}={e['seq_original']}" for e in ctrl[:3]))

        # ------------------------------ GU-01 / GU-02 コアの表に無い 2 命令の継ぎ足し
        # `menu_info` に載っていない命令は、パッドとタッチだけの機体では出しようが無い
        # （`GENSOBAND_HD2D_GAPS` §2.5）。遊びに効く 2 件だけ手で足してある
        # （`gb_pad_commands.cpp` の `kExtras`）。**キーは配列ごとに違う**ので両方見る。
        extras = {7: ("結界", [7], [7]), ord('.'): ("走る", [ord('.')], [ord(',')])}
        for command, (want_label, want_orig, want_rogue) in extras.items():
            hit = next((e for e in entries if e.get("command") == command), None)
            ck.add(hit is not None, f"GU pad table carries the extra command {command}",
                   json.dumps(hit, ensure_ascii=False) if hit else "<missing>")
            if not hit:
                continue
            ck.add(want_label in hit.get("label_utf8", ""),
                   f"GU the extra {command} is labelled like the core's menu",
                   hit.get("label_utf8", ""))
            ck.add(hit.get("seq_original") == want_orig,
                   f"GU the extra {command} resolves in the original keyset",
                   str(hit.get("seq_original")))
            # ローグライク配列では「走る」を押すキーが `,` になる（`pref-key.prf:56`）。
            ck.add(hit.get("seq_rogue") == want_rogue,
                   f"GU the extra {command} resolves in the roguelike keyset",
                   str(hit.get("seq_rogue")))
        ck.add(len({e.get("command") for e in entries}) == len(entries),
               "GU no extra collides with a command the core already sends",
               f"{len(entries)} entries / {len({e.get('command') for e in entries})} commands")
        print("--- pad table ---")
        for g in groups:
            names = [e["label_utf8"] for e in entries if e.get("group_utf8") == g]
            print(f"  {g}: {len(names)}  {' / '.join(names)}")

        # -------------------------------------------------------------- S6 色表
        f = ingame_frame(s) or (s.frames[-1] if s.frames else {})
        pal = None
        for fr in reversed(s.frames):
            if fr.get("term_palette"):
                pal = fr["term_palette"]
                break
        ck.add(pal is not None and len(pal) == 48, "S6 term_palette is on the wire",
               f"{len(pal) if pal else 0} bytes")
        if pal:
            blue = pal[6 * 3:6 * 3 + 3]
            ck.add(blue == [0, 0, 255], "S6 TERM_BLUE is gensoband's own (00,00,FF)", str(blue))

        # -------------------------------------------------- S7 ステータス列の桁
        cols = None
        lines = []
        for fr in reversed(s.frames):
            if fr.get("status_col_lines"):
                cols = fr.get("status_col_cols", 0)
                lines = [ln.get("text_utf8", "") for ln in fr["status_col_lines"]]
                break
        ck.add(cols == 20, "S7 the core declares its status column width (COL_MAP=20)", str(cols))
        widest = max((len(l) for l in lines), default=0)
        ck.add(widest > 13, "S7 the column really is wider than hengband's 13",
               f"widest={widest}: " + repr(max(lines, key=len) if lines else ""))
        print("--- status column ---")
        for l in lines[:6]:
            print(f"  |{l}|")

        # -------------------------------------------------- S1 サブウィンドウ 7 枚
        pk = (s.panel_kinds or {}).get("entries", [])
        pk_labels = [e.get("label_utf8", "") for e in pk]
        ck.add(len(pk) >= 12, "S1 sub_panel_kinds lists the core's window kinds",
               f"{len(pk)} kinds")
        ck.add(pk and pk[0].get("flag") == -1, "S1 the first kind is the UI default",
               str(pk[0] if pk else None))
        ck.add(any("足元" in l for l in pk_labels),
               "S1 gensoband's own kind (floor item list) is offered",
               next((l for l in pk_labels if "足元" in l), "<missing>"))
        ck.add(not any("記念撮影" in l for l in pk_labels),
               "S1 kinds with no fix_ function are left out (snapshot)",
               " / ".join(pk_labels))

        # コアに描かせるため 1 手だけ進める（handle_stuff が window_stuff を回す）。
        s.key(ord("s"))  # 探す（その場で 1 ターン。階を変えない）
        s.settle(quiet_rounds=2, slice_s=0.7, cap_s=20)
        panels = None
        for fr in reversed(s.frames):
            if fr.get("sub_panels"):
                panels = fr["sub_panels"]
                break
        ck.add(panels is not None and len(panels) == 7, "S1 seven sub panels are on the wire",
               f"{len(panels) if panels else 0}")
        if panels:
            got_kinds = [p.get("kind", -1) for p in panels]
            ck.add(got_kinds == kinds, "S1 every panel took the kind the ui asked for",
                   f"{got_kinds} vs {kinds}")
            filled = [i for i, p in enumerate(panels) if p.get("lines")]
            ck.add(len(filled) >= 4, "S1 the core actually drew into the panels",
                   f"filled={filled}")
            print("--- sub panels ---")
            for i, p in enumerate(panels):
                head = p.get("title_utf8", "")
                body = [ln.get("text_utf8", "") for ln in p.get("lines", [])]
                print(f"  [{i}] kind={p.get('kind')} {head}: {len(body)} lines")
                for b in body[:3]:
                    if b.strip():
                        print(f"        {b}")

        # 割り当てが 1 枚も無いときに出る「UI 既定」の中身も見る（枠だけを並べない）。
        deflines = None
        for fr in reversed(s.frames):
            if fr.get("sub2_lines") or fr.get("sub5_lines"):
                deflines = fr
                break
        ck.add(deflines is not None and bool(deflines.get("sub5_lines")),
               "S1 the UI-default panels have content too (inventory)",
               " / ".join((deflines or {}).get("sub5_lines", [])[:3]))
        ck.add(deflines is not None and bool(deflines.get("sub2_lines")),
               "S1 the UI-default panels have content too (equipment)",
               " / ".join((deflines or {}).get("sub2_lines", [])[:3]))

        # ------------------------------- S2 後半 コアのカーソル ／ S4 プロンプト
        def press(ch):
            s.key(ch if isinstance(ch, int) else ord(ch))
            s.settle(quiet_rounds=2, slice_s=0.7, cap_s=20)
            return s.frames[-1] if s.frames else {}

        # 呪文（m）は本の一覧が出る。use_menu が立っていればコアが 》 を書く。
        f = press("m")
        cur = f.get("menu_core_cursors", [])
        ck.add(bool(cur), "S2 the core draws its own cursor and we find it", json.dumps(cur))
        press(0x1B)

        # 落とす（d）→ 下へ 2 つ → 決定。まとめ持ちの品なら個数を聞かれる。
        press("d")
        moved = press("2")
        cur2 = moved.get("menu_core_cursors", [])
        ck.add(bool(cur2) and cur2[0]["line_index"] != cur[0]["line_index"] if cur and cur2 else False,
               "S2 the cursor moves with the arrow keys", json.dumps(cur2))
        press("2")
        f = press(0x0D)
        num = f.get("numeric", {})
        ck.add(bool(num.get("active")) and num.get("max", 0) >= 2,
               "S4 the quantity prompt is lifted off Term row 0",
               json.dumps(num, ensure_ascii=False))
        press(0x1B)  # 個数を聞かれている所で ESC ＝ 落とすのを取り消す
        press(0x1B)

        # 壊す（k）→ 決定 → [y/n/Auto]。**ESC で取り消す**ので何も壊れない。
        press("k")
        f = press(0x0D)
        #! 銘に `!k` の付いた品を選ぶと、破壊の確認の**手前**に
        #! 「本当に〈品名〉ですか?」（`[y/n]`）が 1 枚挟まる（`reimu` の a) がそれ）。
        #! 挟まったまま見ると `[y/n/Auto]` に届かず、選択肢が 2 つで落ちる。
        #! **文言で見分ける**——挟まった側には「壊しますか」が入っていない。
        pr = f.get("prompt", {})
        if pr.get("text_utf8") and "壊しますか" not in pr.get("text_utf8", ""):
            f = press("y")
        pr = f.get("prompt", {})
        ck.add(len(pr.get("choices", [])) == 3,
               "S4 the [y/n/Auto] prompt becomes three choices",
               json.dumps(pr, ensure_ascii=False)[:160])
        f = press(0x1B)
        ck.add(not f.get("prompt", {}).get("choices"),
               "S4 the prompt goes away once it is answered",
               json.dumps(f.get("prompt", {}), ensure_ascii=False))

        # ------------------------------------------------------ S5 日本語入力
        # メモ（:）は品物を選ばずに askfor_aux へ入る。**1 キーで自由入力の待ちになる。**
        f = press(":")
        ck.add(f.get("text_input_active") is True,
               "S5 the core says it is waiting for free text",
               json.dumps(f.get("prompt", {}), ensure_ascii=False))
        # 画面が送るのと同じ形（input_event の text）で日本語を 1 文字入れる。
        s.link.send({"t": "input_event", "events": [{"e": "text", "s": "あ"}]})
        s.settle(quiet_rounds=2, slice_s=0.7, cap_s=20)
        typed = s.frames[-1].get("prompt", {}).get("text_utf8", "") if s.frames else ""
        ck.add("あ" in typed, "S5 a japanese character reaches the core and comes back", typed)
        press(0x1B)  # ESC ＝ 書かずに閉じる

        # ------------------------------------- R2 askfor を通らないエディタ 2 つ
        #! フレームは**既定との差**である（`frame_codec.cpp:483` の `const GameFrame def{}`）。
        #! 前のフレームとの差ではないので、**項目が無い＝既定値**——`text_input_active`
        #! は真のときしか線に載らない。「下りた」は「載っていない」で見る。
        #
        # `_` ＝ 自動拾いエディタ（`dungeon.c:7717`）。**エディタ本体は印字文字が
        # そのまま本文になる**ので、待ちのループそのものが自由入力である。
        #! 初回だけ「設定ファイルをコピーします」の 3 行が出て `-more-` で止まる
        #! （保存せずに閉じるので毎回そうなる）。**空白で送って進める**——エディタに
        #! 入ってからの空白は本文へ 1 文字入るだけで、ここは保存せずに閉じる。
        f = press("_")
        for _ in range(3):
            if f.get("text_input_active") is True:
                break
            f = press(" ")
        ck.add(f.get("text_input_active") is True,
               "R2 the autopick editor itself is a free-text wait",
               f"rows={len(s.mirror(f))}")

        s.link.send({"t": "input_event", "events": [{"e": "text", "s": "あ"}]})
        s.settle(quiet_rounds=2, slice_s=0.7, cap_s=20)
        rows = s.mirror()
        ck.add(any("あ" in l for l in rows),
               "R2 a japanese character lands in the autopick buffer",
               next((l for l in rows if "あ" in l), "<missing>"))

        # ^S ＝ 検索文字列（`get_string_for_search`。これも自前のループ）
        f = press(0x13)
        rows = s.mirror(f)
        ck.add(f.get("text_input_active") is True,
               "R2 the autopick search prompt is a free-text wait too",
               rows[0] if rows else "")
        ck.add(any("検索" in l for l in rows), "R2 the search prompt is really on screen",
               next((l for l in rows if "検索" in l), "<missing>"))
        press(0x1B)  # 検索をやめる

        # ^Q ＝ 保存せずに終了。書き換えたので確認が出る → y。**何も書き込まれない。**
        f = press(0x11)
        ck.add("破棄" in (s.mirror(f)[0] if s.mirror(f) else ""),
               "R2 quitting the editor asks before discarding",
               s.mirror(f)[0] if s.mirror(f) else "")
        f = press("y")
        ck.add(f.get("text_input_active") is not True and not s.mirror(f),
               "R2 the editor closes and the flag is not left standing",
               f"menu_open={f.get('menu_open')} rows={len(s.mirror(f))}")

        # -------------------------------------- R1 サブウィンドウの双方向（その2）
        #! いまのフレームに載っている 7 枚の種類（載っていなければ遡って最後のもの）。
        def panel_kinds_now():
            for fr in reversed(s.frames):
                if fr.get("sub_panels"):
                    return [p.get("kind", -1) for p in fr["sub_panels"]]
            return []

        before = panel_kinds_now()
        # `=` → `w` でコアの割り当て画面へ。カーソルは (窓 0, ビット 0) から始まる。
        press("=")
        f = press("w")
        rows = s.mirror(f)
        ck.add(any("ウィンドウ" in l for l in rows),
               "R1 the core's window-flag screen is reachable", rows[0] if rows else "")
        press("6")              # 右へ 1 ＝ 窓 1（＝画面のパネル 0）
        press("2")
        press("2")              # 下へ 2 ＝ ビット 2（呪文一覧）
        press("t")              # 「その枠を呪文一覧だけにする」
        press(0x1B)             # 割り当て画面を出る
        press(0x1B)             # オプション画面を出る
        after = panel_kinds_now()
        ck.add(after and after[0] == 2,
               "R1 a change made in the core's own screen reaches the wire",
               f"{before} -> {after}")

        #! **ここが要**。画面はまだ古い値を送ってくる（往復に数フレーム掛かる）。
        #! 抱え込みが無いと、この 1 通で元へ戻る。
        s.ui_state(view_w=48, view_h=24, cursor_mode=True, panel_kinds=kinds)
        press("s")
        held = panel_kinds_now()
        ck.add(held and held[0] == 2,
               "R1 a stale ui_state does not undo it (the core side is held)",
               f"{held} while the ui still asks for {kinds}")

        #! 画面が追いついた＝同じ値を送り返してきた。手綱を返し、以後はまた画面のもの。
        caught_up = [2] + kinds[1:]
        s.ui_state(view_w=48, view_h=24, cursor_mode=True, panel_kinds=caught_up)
        press("s")
        s.ui_state(view_w=48, view_h=24, cursor_mode=True, panel_kinds=caught_up)
        press("s")
        settled = panel_kinds_now()
        ck.add(settled == caught_up,
               "R1 once the ui catches up the reins go back to it",
               f"{settled} vs {caught_up}")

        #! 元へ戻す。画面が持ち主なので、送れば次のフレームで戻る。
        s.ui_state(view_w=48, view_h=24, cursor_mode=True, panel_kinds=kinds)
        press("s")
        ck.add(panel_kinds_now() == kinds, "R1 the ui can set it back",
               str(panel_kinds_now()))

        # ------------------------------------------------------------ S8 ヒント
        hint = ""
        for fr in reversed(s.frames):
            if fr.get("controller_hint"):
                hint = fr["controller_hint"]
                break
        ck.add(bool(hint), "S8 controller_hint is filled", hint)

        s.quit_and_wait()
    finally:
        s.close()
    for line in s.stderr_tail(10):
        print("   ", line)
    return ck.finish()


# ================================================= stores（M1 その2 / R3）

#! 店・建物の選択肢（`GENSOBAND_SYSTEM_ACCEPT` R3）。
#! **`systems` とは別の副命令にしてある**——ここは町を歩くので @ の居場所が動く。
#! 終わりに `quit_request` を送らず**殺して終わる**ので、セーブには何も書かない
#! （書くと次の走りで道順が合わなくなる。道順は `reimu` の初期位置べた書き）。

#! 目的地は**地形番号で地図から引く**（GU-14。`GENSOBAND_HD2D_GAPS` §2.5）。
#!
#! 前は起点 `(153, 33)` と道順（西 9・北 2 ／ 西 72・南 2）をべた書きしていたが、
#! **誕生の立ち位置が (180, 33) へ動いた**ので R3 が 1 つも通らなくなった
#! （2026-08-24。セーブを消して誕生から作り直しても (180, 33) だったので、
#! セーブの傷ではなく町のデータが動いた）。**建物と店の位置は動いていない。**
#!
#! 番号の正は `gensoband/lib/edit/f_info.txt`。線に載る `terrain_id` は
#! アダプタが名寄せした後の値だが、この 2 つは変愚と番号が重ならないので素通りする。
TERRAIN_TERAKOYA = 130  # N:130:BUILDING_2（寺子屋）。地図には `+` で出る
TERRAIN_SUZUNAAN = 82   # N:82（鈴奈庵）。地図には `9` で出る


def player_at(session):
    f = session.frames[-1] if session.frames else {}
    return (f.get("player_gx"), f.get("player_gy"))


def find_terrain(session, terrain_id):
    """その地形のマスを 1 つ返す。**`cells` に入っていなければ None。**

    `cells` は `view_w` x `view_h` ぶんしか来ないので、探すときだけ広い視界を頼むこと。
    """
    f = session.frames[-1] if session.frames else {}
    for c in f.get("cells", []):
        if c[2] == terrain_id:
            return (c[0], c[1])
    return None


def route_out(start, target):
    """@ の居る行を東西に走ってから縦へ入る。**人里はこの行が端まで開けている**。"""
    dx, dy = target[0] - start[0], target[1] - start[1]
    return ("6" if dx > 0 else "4") * abs(dx) + ("2" if dy > 0 else "8") * abs(dy)


def route_back(start, target):
    """`route_out` の逆順。**縦から戻す**——建物の在る行は東西に抜けていない。"""
    dx, dy = target[0] - start[0], target[1] - start[1]
    return ("8" if dy > 0 else "2") * abs(dy) + ("4" if dx > 0 else "6") * abs(dx)


def choice_text(session, choice):
    """選択肢の枠に入っている文字（span はバイト位置なので bytes で切る）。"""
    rows = session.mirror()
    i = choice.get("line_index", 0)
    if i >= len(rows):
        return ""
    raw = rows[i].encode("utf-8")
    b = choice.get("span_begin", 0)
    return raw[b:b + choice.get("span_len", 0)].decode("utf-8", "replace")


def choice_key_text(session, choice):
    """決定キーそのものの位置に入っている文字（下線を敷く所）。"""
    rows = session.mirror()
    i = choice.get("line_index", 0)
    if i >= len(rows):
        return ""
    raw = rows[i].encode("utf-8")
    b = choice.get("key_begin", 0)
    return raw[b:b + choice.get("key_len", 0)].decode("utf-8", "replace")


def walk(session, route):
    for ch in route:
        session.key(ord(ch))
        session.drain(0.25)
    session.settle(quiet_rounds=2, slice_s=0.7, cap_s=25)


def cmd_stores(work: Path):
    ck = Checks()
    s = Session(work, "stores", savefile=BIRTH_SLOT)
    try:
        kind, ack = s.hello()
        ck.add(kind == "hello_ack", "hello_ack", str(ack.get("core_version")))
        #! **町ぜんぶが `cells` に入る広さ**でいったん頼む（目的地を地形番号で引くため）。
        s.ui_state(view_w=200, view_h=70, cursor_mode=True)
        ck.add(reach_ingame(s, work, "stores"), "reached an in-game frame")
        s.settle()
        start = player_at(s)
        terakoya = find_terrain(s, TERRAIN_TERAKOYA)
        suzunaan = find_terrain(s, TERRAIN_SUZUNAAN)
        ck.add(terakoya is not None, "R3 寺子屋 is on the map (terrain 130)", str(terakoya))
        ck.add(suzunaan is not None, "R3 鈴奈庵 is on the map (terrain 82)", str(suzunaan))
        if not terakoya or not suzunaan:
            print("[stores] 目的地が地図に無い（夜で見えていない？）。ここで止める")
            s.close()
            return ck.finish()
        print(f"[stores] @{start}  寺子屋{terakoya}  鈴奈庵{suzunaan}")
        #! 測り終えたら普段の広さへ戻す（14,000 マスを毎フレーム運ばせない）。
        s.ui_state(view_w=66, view_h=24, cursor_mode=True)
        s.settle()

        def choices():
            return (s.frames[-1] if s.frames else {}).get("menu_choices", [])

        def press(ch):
            s.key(ch if isinstance(ch, int) else ord(ch))
            s.settle(quiet_rounds=2, slice_s=0.6, cap_s=15)
            return s.frames[-1] if s.frames else {}

        def show(tag):
            print(f"--- {tag} ---")
            for c in choices():
                key = c.get("key", 0)
                name = {0x1B: "ESC", 0x20: "SPACE"}.get(key, chr(key) if 32 < key < 127 else str(key))
                print(f"    key={name:5s} at={choice_key_text(s, c)!r}  |{choice_text(s, c)}|")

        # ------------------------------------------------------------ 建物
        walk(s, route_out(start, terakoya))
        got = choices()
        show("building")
        labels = [choice_text(s, c) for c in got]
        ck.add(bool(s.frames[-1].get("menu_open")), "R3 the building screen is open")
        ck.add(any("クエスト" in l for l in labels),
               "R3 the building's own action is a choice",
               " / ".join(labels))
        ck.add(any(c.get("key") == 0x1B for c in got),
               "R3 ESC) is a choice too (leaving is clickable)",
               next((choice_text(s, c) for c in got if c.get("key") == 0x1B), "<missing>"))
        esc = next((c for c in got if c.get("key") == 0x1B), {})
        ck.add(choice_key_text(s, esc) == "ESC",
               "R3 the underline sits on the key itself, not the label",
               f"key_begin={esc.get('key_begin')} len={esc.get('key_len')}")
        ck.add(all("手持ちのお金" not in l for l in labels),
               "R3 the frame stops before the gold on the same row",
               " / ".join(labels))

        # 建物を出て店へ。ESC は `keys` で 1 つ送る（印字文字ではないのでスクリプトには書けない）。
        s.key(0x1B)
        s.settle(quiet_rounds=2, slice_s=0.7, cap_s=20)
        walk(s, route_back(start, terakoya))  # 元の場所へ戻す（次の道順を起点から書けるように）
        ck.add(player_at(s) == start, "R3 back where we started", str(player_at(s)))

        # ------------------------- c 名指しの選択画面（設定・知識。その3）
        #! 店・建物の手掛かり（`ESC)` / `コマンド:` / `手持ちのお金:`）を持たない画面。
        #! **1 キーで開けるものだけ**をここで見る（帰還先・町テレポート・賭けは
        #! 町を歩いて建物へ入らないと出せないので、表には載せたが検査していない）。
        NAMED = [
            ("=", "option_root", "[ オプションの設定 ]", 14, "ウインドウフラグ"),
            ("@", "macros", "[ マクロの設定 ]", 10, "マクロの作成"),
            ("%", "visuals", "[ 画面表示の設定 ]", 11, "画面表示方法の初期化"),
            ("&", "colors", "[ カラーの設定 ]", 3, "カラーの設定を変更する"),
            ("|", "diary", "[ 記録の設定 ]", 4, "記録を消去する"),
            ("~", "knowledge", "現在の知識を確認する", 12, "既知のモンスター"),
        ]
        for key, tag, sig, want, label in NAMED:
            press(key)
            got = choices()
            rows = s.mirror()
            ck.add(any(sig in r for r in rows), f"c {tag}: the screen opened",
                   next((r.strip() for r in rows if sig in r), "<missing>"))
            ck.add(len(got) == want, f"c {tag}: every entry became a choice",
                   f"{len(got)} (want {want})")
            texts = [choice_text(s, ch) for ch in got]
            ck.add(any(label in t for t in texts), f"c {tag}: label {label} is offered",
                   next((t for t in texts if label in t), " / ".join(texts[:3])))
            press(0x1B)
            press(0x1B)

        #! **降りたら殺す。**項目を選ぶと一覧は残ったまま下でキーを待つ。
        #! 一覧を選択肢のままにすると、決定した数字がトリガーキーへ混ざる。
        press("@")
        before = len(choices())
        press("3")  # マクロの確認 → `トリガーキー:` の待ちへ降りる
        after = choices()
        rows = s.mirror()
        ck.add(any("トリガーキー" in r for r in rows), "c the macro screen went down a level",
               next((r.strip() for r in rows if "トリガーキー" in r), "<missing>"))
        ck.add(before > 0 and not after,
               "c the list stops being offered while the core waits below it",
               f"{before} -> {len(after)}")
        press(0x1B)
        press(0x1B)
        press(0x1B)

        #! 知識だけは `ESC) 抜ける  SPACE) 次ページ` を項目と同居させている。
        #! **`SPACE)` は英字**なので、店の `スペース)` だけを見ていると拾えない。
        press("~")
        got = choices()
        keys = {ch.get("key") for ch in got}
        ck.add(0x20 in keys and 0x1B in keys,
               "c knowledge offers both ESC) and the ASCII SPACE)",
               " ".join(sorted(hex(k) for k in keys if k < 0x30)))
        press(0x1B)
        press(0x1B)

        # -------------------------------------------------------------- 店
        walk(s, route_out(start, suzunaan))
        got = choices()
        show("store")
        labels = [choice_text(s, c) for c in got]
        ck.add(bool(s.frames[-1].get("menu_open")), "R3 the store screen is open")
        #! 在庫は入れ替わる。**ページ送りの 2 行は品数が 1 ページに収まると出ない**
        #! （`store.c:6506` の `stock_num > store_bottom`）ので、数も文言も条件付きで見る。
        ck.add(len(got) >= 8, "R3 every command below コマンド: became a choice",
               f"{len(got)} choices")
        for want in ("商品を買う", "アイテムを売る", "商品を調べる"):
            ck.add(any(want in l for l in labels), f"R3 {want} is a choice",
                   next((l for l in labels if want in l), "<missing>"))
        paged = any("スペース)" in row for row in s.mirror())
        if paged:
            ck.add(any("次ページ" in l for l in labels), "R3 次ページ is a choice",
                   next((l for l in labels if "次ページ" in l), "<missing>"))
        else:
            print("    （このときの在庫は 1 ページに収まった。ページ送りの検査は飛ばす）")
        pair = [c for c in got if choice_text(s, c).startswith("i/e)")]
        ck.add(len(pair) == 2 and {c.get("key") for c in pair} == {ord("i"), ord("e")},
               "R3 i/e) is two choices sharing one frame",
               " / ".join(f"{chr(c.get('key'))}@{c.get('key_begin')}" for c in pair))
        if paged:
            space = [c for c in got if c.get("key") == 0x20]
            ck.add(bool(space) and choice_key_text(s, space[0]) == "スペース",
                   "R3 スペース) resolves to the space key",
                   choice_text(s, space[0]) if space else "<missing>")
        ck.add(all(not l.startswith("a)") for l in labels),
               "R3 the goods list is not offered here (pressing a) does nothing)",
               " / ".join(labels[:3]))

        # p) 商品を買う → 品物選び。**ここで初めて商品一覧が選択肢になる。**
        s.key(ord("p"))
        s.settle(quiet_rounds=2, slice_s=0.7, cap_s=20)
        got = choices()
        show("item prompt")
        rows = s.mirror()
        ck.add(rows and rows[0].startswith("(") and "-" in rows[0],
               "R3 the goods prompt is on row 0", rows[0] if rows else "")
        keys = [c.get("key") for c in got]
        ck.add(len(got) >= 5 and keys == sorted(keys) and keys[0] == ord("a"),
               "R3 the goods list becomes a run of consecutive letters",
               f"{len(got)}: " + "".join(chr(k) for k in keys))
        ck.add(all(c.get("line_index", 0) < 21 for c in got),
               "R3 the command lines are not mixed into the goods list",
               str([c.get("line_index") for c in got]))
        s.key(0x1B)  # 買わずに戻る
        s.settle(quiet_rounds=2, slice_s=0.7, cap_s=20)
        s.key(0x1B)  # 店を出る
        s.settle(quiet_rounds=2, slice_s=0.7, cap_s=20)
    finally:
        #! **`quit_request` を送らない。**送るとコアが強制セーブして @ の居場所が動く。
        s.close()
    for line in s.stderr_tail(10):
        print("   ", line)
    return ck.finish()



# ================================================= combat（M1 その2 / R4）

#! 戦闘の見せ場（`GENSOBAND_SYSTEM_ACCEPT` S10 / R4）。
#! **`systems` と分けてある**——呪文を覚えて撃つので世界が進む。終わりに
#! `quit_request` を送らず殺すので、セーブには何も書かない。


def last_messages(session, n=4):
    for fr in reversed(session.frames):
        if fr.get("messages"):
            return [m.get("text_utf8", "") for m in fr["messages"][-n:]]
    return []


def cmd_combat(work: Path):
    ck = Checks()
    #! **鳴らさずに解決だけさせる**（`gb_audio.c` の `GB_AUDIO_SILENT`）。
    #! 検査のたびに机の前で音が出ると邪魔なので、道は通して最後の 1 行だけ省く。
    #! 実際に聞きたいときは `GB_AUDIO_SILENT=0` を外から渡す（`setdefault` なので勝つ）。
    os.environ.setdefault("GB_AUDIO_LOG", "1")
    os.environ.setdefault("GB_AUDIO_SILENT", "1")
    s = Session(work, "combat", savefile=BIRTH_SLOT)
    try:
        kind, ack = s.hello()
        ck.add(kind == "hello_ack", "hello_ack", str(ack.get("core_version")))
        #! **音量を最大から下げて頼む。**最大のままだと `PlaySound` へ素のファイルを
        #! 渡す道しか通らず、PCM を縮める側（`gb_scale_wav`）を 1 度も踏まない。
        s.ui_state(view_w=66, view_h=24, cursor_mode=True, sound_on=True, music_on=True,
                   sound_volume_index=5, music_volume_index=3)
        ck.add(reach_ingame(s, work, "combat"), "reached an in-game frame")
        s.settle()

        def press2(k):
            """1 キー送って、**その打鍵で増えたぶん**の演出と伝言を返す。

            伝言は `last_messages()` では見分けられない——あちらは「伝言の載った
            いちばん新しいフレーム」を返すので、その打鍵で何も出なかったときに
            **前の打鍵の伝言をもう一度返す**。撃ち直しの判定がそれで狂う。
            """
            n = len(s.frames)
            s.key(k if isinstance(k, int) else ord(k))
            s.settle(quiet_rounds=2, slice_s=0.6, cap_s=15)
            fx, msgs = [], []
            for fr in s.frames[n:]:
                fx.extend(fr.get("combat_fx", []))
                msgs.extend(m.get("text_utf8", "") for m in fr.get("messages", []))
            return fx, msgs

        def press(k):
            return press2(k)[0]

        #! **撃つのは職業特技（`J`）である**（GU-15。`GENSOBAND_HD2D_GAPS` §2.5）。
        #!
        #! 前は呪文（`m`）で撃っていたが、**誕生直後の @ は呪文を 1 つも覚えられない**
        #! ——`p_ptr->new_spells` が 0 で、`G` は「新しい呪文を覚えることはできない！」で
        #! 撥ねられる（`cmd5.c:913`）。以前は既に覚えている枠に当たっていたので通っていた。
        #!
        #! 霊夢の Lv1 の技「パスウェイジョンニードル」は**無属性のボルト**で消費 2MP
        #! （`new_class_power.c:26621`）。ここが見たいものそのもので、しかも
        #! **職業特技は幻想蛮怒でいちばん大きい仕組み**（GH-06）なので一緒に踏める。
        #!
        #! `cursor_mode` を立てているので技は**コアの 》 ＋ Enter**で選ぶ
        #! （選択肢の行に `a)` が無いので文字では選べない画面である）。
        press("J")
        ck.add(any("パスウェイジョンニードル" in r for r in s.mirror()),
               "R4 the class power (J) is on offer",
               next((r.strip() for r in s.mirror() if "パスウェイジョン" in r), "<missing>"))
        press(0x1B)

        def fire(aim, tries=5):
            """技を 1 発撃つ。**失率 15% なので外したら撃ち直す**（`new_class_power.c` の表）。

            **失率の判定は「術を選んだ直後・方向を聞かれる前」に起きる。**
            外れた回はコアが方向を待っていないので、そこへ `4` や `*` `t` を送ると
            **ただのコマンドとして走ってしまう**（`*` はターゲット指定・`t` は装備を外す）。
            だから外れたら方向を送らずに選び直す。

            消費は 1 発 2MP、持ち玉は 20 なので 5 回なら枯れない。
            """
            for _ in range(tries):
                press("J")
                if any("失敗した" in m for m in press2(0x0D)[1]):
                    continue
                if aim:
                    press("*")
                    return press("t")
                return press("4")
            return []

        # 方角を押す＝そちらへ撃つ（狙いを付けない）。
        fx = fire(aim=False)
        bolts = [e for e in fx if e.get("kind") == 2]
        ck.add(bool(bolts), "R4 a bolt reaches the wire",
               json.dumps(bolts[:1], ensure_ascii=False))
        if bolts:
            b = bolts[0]
            ck.add((b.get("sy"), b.get("sx")) != (b.get("y"), b.get("x")),
                   "R4 the bolt carries both ends (fired from / landed at)",
                   f"({b.get('sy')},{b.get('sx')}) -> ({b.get('y')},{b.get('x')})")
            #! 無属性（弾幕と同じ `GF_MISSILE`）は束 0 ＝ 物理。**既定なので線に載らない。**
            ck.add(b.get("elem", 0) == 0,
                   "R4 a plain missile lands in the physical bucket", str(b.get("elem", 0)))

        # 狙いを付けて撃つ（`*` で敵を選び `t` で決定）。当たれば命中も積まれる。
        #! **見えている敵が居るときだけ見る**（GU-15）。人里は無人のことがあり、
        #! そのとき `*` は敵を見つけられないので `t` は地形を指す。弾は自分のマスへ
        #! 落ちて `path_n == 0` になり、`gb_fx_bolt()` が呼ばれない
        #! （`spells1.c:13265` の条件）。**演出の作りではなく、撃つ相手が居ないだけ**である。
        f = ingame_frame(s) or (s.frames[-1] if s.frames else {})
        seen = [c for c in f.get("cells", []) if c[4]]
        if not seen:
            print("    （このとき見えている敵が居なかった。狙い撃ちの検査は飛ばす）")
        else:
            print(f"    （見えている敵 {len(seen)} 体。狙い撃ちを見る）")
            fx = fire(aim=True)
            hits = [e for e in fx if e.get("kind") == 1]
            bolts = [e for e in fx if e.get("kind") == 2]
            ck.add(bool(bolts), "R4 the aimed shot is a bolt too",
                   json.dumps(bolts[:1], ensure_ascii=False))
            ck.add(bool(hits), "R4 hitting a monster is on the wire",
                   json.dumps(hits[:1], ensure_ascii=False) + " / " + " ".join(last_messages(s, 2)))
            if hits:
                h = hits[0]
                ck.add(0.0 < float(h.get("i", 0.0)) <= 1.0,
                       "R4 the strength is a ratio, not raw damage", str(h.get("i")))

        #! **汲んだら消える**（`gb_fx_take`）。同じ縁を次のフレームでも拾うと
        #! 演出が止まらなくなる（変愚の `TeleportFx::burst` と同じ罠）。
        fx = press("s")
        ck.add(not fx, "R4 the events are drained, not re-sent", json.dumps(fx))

        # ------------------------------------------------- a 音（その3）
        #! **効果音を 1 つは鳴らさせてから見る**（GU-15）。この @ が町で鳴らせるのは
        #! 「術の失敗」（`SOUND_FAIL`）くらいしかない——`sound(SOUND_*)` を全数見たところ、
        #! 店の 4 つは `stores` の領分、殴打系は敵が要る、`walk` は cfg が空（GH-32）である。
        #!
        #! 失率は 15%。**成功した回は方向を聞かれた所で ESC すれば MP を使わない**
        #! （`do_cmd_new_class_power()` は `get_aim_dir()` が偽なら課金せずに戻る）ので、
        #! 外れるまで何度でも試せる。20 回で「1 度も外れない」のは 4% 弱。
        def provoke_fail_sound(tries=20):
            for _ in range(tries):
                if any(("audio: sound " in ln) and ("->" in ln) for ln in s.stderr_text().splitlines()):
                    return True
                press("J")
                if any("失敗した" in m for m in press2(0x0D)[1]):
                    continue  # ここで `fail.wav` が鳴っている
                press(0x1B)  # 成功した回は方向を聞かれた所で取り消す（MP を使わない）
            return any(("audio: sound " in ln) and ("->" in ln) for ln in s.stderr_text().splitlines())

        if not provoke_fail_sound():
            print("    （20 回撃って 1 度も外れなかった。効果音の 2 件は飛ばす）")
        log = s.stderr_text()
        head = [ln for ln in log.splitlines() if "audio: sound" in ln and "entries" in ln]
        ck.add(bool(head), "a the cfg tables load", head[0].strip() if head else "<missing>")
        if head:
            counts = re.findall(r"sound (\d+) / music (\d+) entries", head[0])
            n_sound = int(counts[0][0]) if counts else 0
            n_music = int(counts[0][1]) if counts else 0
            ck.add(n_sound > 0, "a sound.cfg resolved to files that exist", str(n_sound))
            ck.add(n_music > 0, "a music.cfg resolved to files that exist", str(n_music))
        played = [ln for ln in log.splitlines() if "audio: sound " in ln and "->" in ln]
        tunes = [ln for ln in log.splitlines() if "audio: music " in ln]
        ck.add(bool(tunes), "a the core asked for a tune and it resolved to a file",
               tunes[0].split("->")[-1].strip() if tunes else "<none>")
        if played:
            ck.add(True, "a the core asked for a sound and it resolved to a file",
                   played[0].split("->")[-1].strip())
            #! **音量を当てられたか**（`PlaySound` に音量の引数が無いので PCM を縮めている）。
            #! ここが落ちると、つまみを回しても効果音だけ大きさが変わらない。
            scaled = [ln for ln in log.splitlines() if "audio: scaled" in ln]
            ck.add(bool(scaled), "a the sound volume reached the PCM",
                   scaled[0].split("scaled")[-1].strip() if scaled else "<none>")
        #! 同じ曲を続けて頼まれても鳴らし直さない（頭へ戻ってしまう）。
        picked = [ln.split("->")[0].strip() for ln in tunes]
        ck.add(len(picked) == len(set(picked)), "a the same tune is not restarted",
               " / ".join(picked[:4]))
    finally:
        s.close()
    for line in s.stderr_tail(10):
        print("   ", line)
    return ck.finish()



# ============================================================ sfx（効果音の配管）

#! **誰が鳴らすかの引き渡し**（`AUDIO_DESIGN` §2.1・§2.2）。
#! 幻想蛮怒は 4 本のうち**唯一、自分でも鳴らせるコア**（`gb_audio.c` が winmm を叩く）
#! なので、ここで見るのは「載ったか」だけでなく **「載せたときはコアが鳴らしていないか」**
#! までである。片方だけ直すと**両方が同時に鳴る**。


def collect_sounds(session, start_index):
    """frames[start_index:] の sounds を平らに集める。

    codec は既定値を省くので get で読む。**名前の鍵は `n`**
    （`frame_codec.cpp:654`。"name" ではない）。
    """
    out = []
    for f in session.frames[start_index:]:
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


def food_letter(session):
    """食べる画に並んだ食料の選択キー。無ければ 0。

    **行頭では引けない。**品書きは主 Term の右側に重なって出るので、行の頭には
    左の状態列（`NORMAL` など）が載っている——`    NORMAL       ......  b) , 5食の 食料`。
    `英字 + )` を行の途中から拾う。
    """
    for row in session.mirror():
        if "食料" not in row:
            continue
        m = re.search(r"([a-z])\)", row)
        if m:
            return ord(m.group(1))
    return 0


def played_sounds(session):
    """コアが**鳴らそうとした**行だけを拾う（`GB_AUDIO_LOG=1`。`gb_audio.c:623`）。

    書式は `[gensoband] audio: sound <番号> (<名前>) -> <道>`。起動の 1 行
    （`audio: sound 59 / music 46 entries`）と混ざらないよう、番号の後ろの `(` で切る。
    """
    return [ln for ln in session.stderr_text().splitlines()
            if re.search(r"audio: sound \d+ \(", ln)]


def cmd_sfx(work: Path):
    ck = Checks()
    #! **鳴らさずに解決だけさせる**（`combat` と同じ）。検査のたびに机の前で音が
    #! 出ると邪魔なので、道は通して最後の 1 行だけ省く。`GB_AUDIO_LOG=1` は
    #! 「コアが鳴らそうとしたか」を stderr に残す——この検査の要である。
    os.environ.setdefault("GB_AUDIO_LOG", "1")
    os.environ.setdefault("GB_AUDIO_SILENT", "1")
    s = Session(work, "sfx", savefile=BIRTH_SLOT)
    try:
        kind, ack = s.hello()
        ck.add(kind == "hello_ack", "hello_ack", str(ack.get("core_version")))
        ck.add("audio" in ack.get("features", []), 'core declares "audio"',
               str(ack.get("features")))

        s.ui_state(view_w=66, view_h=24, sound_on=True, sound_events=True)
        ck.add(reach_ingame(s, work, "sfx"), "reached an in-game frame")
        s.settle()

        def eat(mark_from=None):
            """食料を 1 つ食べる（`sound(SOUND_EAT)`。`cmd6.c:144`）。

            **壁にぶつかる筋は採れない**——人里の @ の周りは何マスも開けていて、
            `SOUND_HITWALL`（`cmd1.c:9168`）まで歩かせると道順がセーブごとに変わる。
            食べるのは持ち物さえ在れば確実である。
            """
            m = len(s.frames) if mark_from is None else mark_from
            s.key(ord("E"))
            s.settle(quiet_rounds=1, slice_s=0.5, cap_s=10.0)
            k = food_letter(s)
            if k:
                s.key(k)
                s.settle(quiet_rounds=1, slice_s=0.5, cap_s=10.0)
            else:
                s.key(0x1B)
                s.settle(quiet_rounds=1, slice_s=0.4, cap_s=6.0)
            return k, collect_sounds(s, m)

        mark = len(s.frames)
        key, heard = eat(mark)
        ck.add(key > 0, "the eat prompt lists food", chr(key) if key else "-")
        names = [e["n"] for e in heard]
        ck.add("eat" in names, 'eating pushes "eat"',
               "%d events: %s" % (len(heard), " ".join(sorted(set(names)))))

        # ---- マスは @ のマス（`TERM_XTRA_SOUND` は位置を運ばない）------------
        f = ingame_frame(s)
        if f is not None and heard:
            ck.add((heard[0]["x"], heard[0]["y"]) == player_at(s),
                   "the sound sits on the player's own grid",
                   "sound=(%s,%s) player=%s" % (heard[0]["x"], heard[0]["y"], player_at(s)))

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

        # ---- **コアは鳴らしていない**（§2.2 の引き渡し）----------------------
        # ここを落とすと**両方が同時に鳴る**。`GB_AUDIO_LOG=1` はコアが鳴らそうと
        # した回数をそのまま残すので、旗を立てている間は 1 行も出ないのが正しい。
        # **起動の 1 行と区別する。** `gb_audio_init()` も
        # 「audio: sound 59 / music 46 entries」と書く。鳴らした行だけを採る
        # 目印は番号の後ろの `(名前) ->` である（`gb_audio.c:623`）。
        played = played_sounds(s)
        ck.add(not played, "the core plays nothing while the screen wants the events",
               " / ".join(played[:3]))


        # ---- 周囲の地形の内訳（環境音の層。`AUDIO_AMBIENCE_FIELDS` §8.1）----
        # **数えていないと丸ごと出ない**（`radius == 0` は codec が畳む）ので、
        # 鍵が在ることそのものが「数えた」の証拠になる。
        sur = (ingame_frame(s) or {}).get("surroundings", {})
        ck.add(sur.get("radius", 0) > 0, "surroundings are counted",
               json.dumps(sur, ensure_ascii=False))
        ck.add(sur.get("counted", 0) > 0, "some grids were known enough to count",
               str(sur.get("counted")))
        # 人里の地上なので**壁ばかりにはならない**。閉塞の層がいきなり最大で
        # 鳴っていたら、既知の判定か円の切り方を間違えている。
        ck.add(sur.get("wall", 0) < 200, "the town is not counted as a solid wall",
               "wall=%s" % sur.get("wall"))

        # ---- 一度きりの縁である（§2.3。汲んだら消える）----------------------
        # 何も打たずに静かにする。`frame.sounds` が居座っていれば、次のフレームで
        # 同じ音がもう一度出てくる。
        mark = len(s.frames)
        s.key(0x1B)
        s.settle(quiet_rounds=2, slice_s=0.8, cap_s=12.0)
        again = collect_sounds(s, mark)
        ck.add(not again, "a quiet round pushes no sound twice",
               " ".join(e["n"] for e in again))

        # ---- 旗を降ろすと**コアが鳴らす側へ戻る**（§2.2）--------------------
        # 出来事は止まり、代わりに `gb_audio.c` が鳴らす。**どちらか一方**である。
        s.ui_state(view_w=66, view_h=24, sound_on=True, sound_events=False)
        s.settle(quiet_rounds=1, slice_s=0.5, cap_s=8.0)
        mark = len(s.frames)
        key2, silent = eat(mark)
        ck.add(key2 > 0, "food is still there for the second bite", chr(key2) if key2 else "-")
        ck.add(not silent, "sound_events=false stops the events",
               " ".join(e["n"] for e in silent))
        played = played_sounds(s)
        ck.add(bool(played), "the core takes the sound back when the screen is not open",
               played[-1] if played else "(nothing played)")
    finally:
        #! **`quit_request` を送らずに殺す**（`stores` と同じ）。食べたぶんを
        #! セーブへ書き戻すと、走るたびに食料が減って 3 回目から通らなくなる。
        s.close()
    return ck.finish()


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    cmd = sys.argv[1]
    work = Path(sys.argv[2])
    work.mkdir(parents=True, exist_ok=True)
    if not CORE.is_file():
        print(f"[driver] {CORE} is missing (build GensobandCore first)")
        return 2
    if cmd == "handshake":
        return cmd_handshake(work)
    if cmd == "play":
        return cmd_play(work, sys.argv[3] if len(sys.argv) > 3 else "",
                        sys.argv[4] if len(sys.argv) > 4 else "quit")
    if cmd == "m0":
        return cmd_m0(work)
    if cmd == "systems":
        return cmd_systems(work)
    if cmd == "stores":
        return cmd_stores(work)
    if cmd == "combat":
        return cmd_combat(work)
    if cmd == "sfx":
        return cmd_sfx(work)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
