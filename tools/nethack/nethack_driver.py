#!/usr/bin/env python3
"""NetHackCore.exe / NetHackJpCore.exe をプロトコル v1（stdio）で動かし、届いた frame の要点を表示する。

tools/umoria/umoria_driver.py と同じ形。
画面側（HengbandHd2d.exe）は起こさず、音も出さない。セーブなどは一時フォルダの HENGBAND_DATA_ROOT へ書く
（--data-root で同じフォルダを渡すと、続きから始まる）。

使い方:
    python tools/nethack/nethack_driver.py --lang en --keys "probe<cr>" --keys y --keys y --keys n
    python tools/nethack/nethack_driver.py --lang ja --data-root C:\\temp\\nh_ja --keys S --keys y

キーの書き方: 字はそのまま 1 キー。<esc> <cr> <bs> <sp> <tab>、<^X>（Ctrl+X）、<up> <down> <left> <right>
（move の入力イベントで送る）。--keys は 1 回ごとに 1 手として送り、そのあとに届いた frame を表示する。

専用の画面（features: nethack-ui。、プロトコルの仕様書 §8.7）:
    python tools/nethack/nethack_driver.py --nethack-ui --lang ja --data-root C:\\temp\\nh_ja \\
        --reply "text:たろう" --reply "select:random" --reply dismiss --reply "menu:cancel" --keys "i" --reply dismiss
--nethack-ui を付けると hello に nethack-ui を挙げ、届いた nethack_ui の知らせを読みやすく表示する（--ui-raw で生の JSON も）。
--keys と --reply と --msgwin は書いた順に 1 手ずつ送る。--reply は nethack_reply を送る。r は直前に届いた要求の番号を自動で入れる
（"@7:menu:1" のように先頭へ @番号: を付けると指定できる）。書き方:
    menu:1,3x5     メニューで項目 1 と、項目 3 を個数 5 で選ぶ（番号は 1 から。見出しの行は選べない）
    menu:cancel    メニューを取り消す（cancel だけでも同じ）
    dismiss        テキストの窓・墓標・見るだけのメニューを閉じる
    yn:y           yn の問いにキー y（yn:esc yn:sp yn:cr も使える）
    text:内容      文字入力・名前・拡張コマンド（UTF-8）
    select:役割,種族,性別,属性[,名前]   キャラクタ作成（番号。-1 はおまかせ）。select:random・select:quit も使える
    pos:x,y[,mod]  位置の選択のクリック（mod 1 左・2 右）
    {…}            生の JSON（a と必要な欄。r は自動）
--msgwin 行数x桁数 は msgwin の返事（メッセージの窓の大きさ。--More-- の判定に使われる）を送る。
"""
from __future__ import annotations

import argparse
import json
import os
import queue
import struct
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NAMED = {"<esc>": 27, "<cr>": 13, "<bs>": 8, "<sp>": 32, "<tab>": 9}
MOVES = {"<left>": (-1, 0), "<right>": (1, 0), "<up>": (0, -1), "<down>": (0, 1)}
EXES = {"en": "NetHackCore.exe", "ja": "NetHackJpCore.exe"}


def parse_keys(text: str) -> list[object]:
    """キーの列（int）と、move の入力イベント（(dx, dy) の組）の並びにする。"""
    items: list[object] = []
    i = 0
    while i < len(text):
        if text[i] == "<":
            end = text.find(">", i)
            token = text[i:end + 1] if end >= 0 else ""
            if token in NAMED:
                items.append(NAMED[token])
                i = end + 1
                continue
            if token in MOVES:
                items.append(MOVES[token])
                i = end + 1
                continue
            if token.startswith("<^") and len(token) == 4:
                items.append(ord(token[2].upper()) - 64)
                i = end + 1
                continue
        for byte in text[i].encode("utf-8"):
            items.append(byte)
        i += 1
    return items


class Core:
    def __init__(self, exe: Path, data_root: Path, lang: str, timeout: float, modern: bool = False, ui: bool = False):
        self.ui = ui
        #: 届いた nethack_ui の知らせ（まだ表示していない物）と、いま答えを待っている要求（r つきの最後の物）
        self.ui_new: list[dict] = []
        self.ui_count = 0
        self.pending: dict | None = None
        self.last_wait: dict | None = None
        self.proc = subprocess.Popen(
            [str(exe), "--ui-protocol=stdio"], cwd=str(data_root),
            env={**os.environ, "HENGBAND_DATA_ROOT": str(data_root), "HENGBAND_MUTE": "1"},
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.messages: queue.Queue[dict | None] = queue.Queue()
        self.frame: dict | None = None
        self.frame_count = 0
        self.exit_code: int | None = None
        self.hello_ack: dict | None = None
        self.other: list[str] = []
        self.stderr_text = ""
        threading.Thread(target=self._reader, daemon=True).start()
        threading.Thread(target=self._stderr_reader, daemon=True).start()
        # 上限時間。超えたら強制的に終わらせる（コアが入力を待って止まったままにしない）
        # デーモンスレッドにする（そうしないと、確かめが終わっても上限時間まで python が終わらない）
        timer = threading.Timer(timeout, self._kill)
        timer.daemon = True
        timer.start()
        self.send({"t": "hello", "protocol": 1, "ui_name": "nethack-driver", "ui_version": "1",
                   "features": ["nethack-ui"] if ui else []})
        state = {"t": "ui_state", "lang": lang}
        if modern:
            state["modern_ui"] = True  # 新しい画面。コアは窓のモデル（frame.modern）を送る
        self.send(state)

    def _kill(self) -> None:
        if self.proc.poll() is None:
            print("!! time limit: killing the core")
            self.proc.kill()

    def _reader(self) -> None:
        out = self.proc.stdout
        assert out is not None
        while True:
            head = out.read(4)
            if len(head) < 4:
                self.messages.put(None)
                return
            size = struct.unpack("<I", head)[0]
            self.messages.put(json.loads(out.read(size).decode("utf-8")))

    def _stderr_reader(self) -> None:
        err = self.proc.stderr
        assert err is not None
        for line in err:
            self.stderr_text += line.decode("utf-8", errors="replace")

    def send(self, value: dict) -> None:
        body = json.dumps(value, separators=(",", ":")).encode("utf-8")
        assert self.proc.stdin is not None
        try:
            self.proc.stdin.write(struct.pack("<I", len(body)) + body)
            self.proc.stdin.flush()
        except OSError:
            pass  # コアがもう終わっている（終了の後の送り）

    def settle(self, quiet: float = 0.8, limit: float = 20.0) -> None:
        """quiet 秒、何も届かなくなるまで読む。"""
        deadline = time.monotonic() + limit
        while time.monotonic() < deadline:
            try:
                message = self.messages.get(timeout=quiet)
            except queue.Empty:
                return
            if message is None:
                return
            kind = message.get("t")
            if kind == "frame":
                self.frame = message
                self.frame_count += 1
            elif kind == "hello_ack":
                self.hello_ack = message
            elif kind == "exit":
                self.exit_code = int(message.get("code", 0))
            elif kind == "nethack_ui":
                self.ui_new.append(message)
                self.ui_count += 1
                k = message.get("k")
                if k == "wait":
                    self.last_wait = message
                if k in ("menu", "text", "ask", "select", "rip") and message.get("r"):
                    self.pending = message
            else:
                self.other.append(kind or "?")

    def keys(self, items: list[object], quiet: float) -> None:
        keys: list[int] = []
        events: list[dict] = []
        for item in items:
            if isinstance(item, tuple):
                events.append({"e": "move", "dx": item[0], "dy": item[1]})
            else:
                keys.append(int(item))  # type: ignore[arg-type]
        if keys:
            self.send({"t": "keys", "keys": keys})
        if events:
            self.send({"t": "input_event", "events": events})
        self.settle(quiet)

    def close(self) -> None:
        if self.proc.poll() is None:
            try:
                self.proc.stdin.close()  # type: ignore[union-attr]  # EOF: 原作の hangup（保存して終わる道）
                self.proc.wait(timeout=15)
            except Exception:
                self.proc.kill()
                self.proc.wait(timeout=5)
        # 終わった後に届いた知らせ（exit）を拾う
        self.settle(0.3, 2.0)


def clip(text: str, width: int = 110) -> str:
    return text if len(text) <= width else text[:width - 1] + "…"


def describe(frame: dict | None) -> str:
    if frame is None:
        return "(no frame)"
    lines = []
    flags = [k for k in ("menu_open", "pre_game_menu", "awaiting_command", "text_input_active", "menu_over_map") if frame.get(k)]
    lines.append(f"frame #{frame.get('frame_id')} flags: {' '.join(flags) or '-'}")
    hud = frame.get("hud") or {}
    lines.append("hud: name={name!r} HP {hp}/{hp_max} Pw {sp}/{sp_max} gold {gold} depth {depth} level {level}".format(
        name=hud.get("name"), hp=hud.get("hp", 0), hp_max=hud.get("hp_max", 0), sp=hud.get("sp", 0),
        sp_max=hud.get("sp_max", 0), gold=hud.get("gold", 0), depth=hud.get("depth", 0), level=hud.get("level", 0)))
    if hud.get("status_line"):
        lines.append("status_line: " + clip(hud["status_line"]))
    for message in frame.get("messages") or []:
        lines.append("  msg: " + clip(message.get("text_utf8", "")))
    prompt = frame.get("prompt") or {}
    if prompt.get("text_utf8") or prompt.get("choices"):
        choices = " ".join(f"[{chr(c.get('key', 0)) if 32 < c.get('key', 0) < 127 else c.get('key', 0)}:{c.get('label_utf8', '')}]"
                           for c in prompt.get("choices") or [])
        lines.append("prompt: " + clip(prompt.get("text_utf8", "")) + ("  " + choices if choices else ""))
    term = frame.get("menu_term_lines") or []
    if term:
        lines.append(f"menu_term_lines ({len(term)}):")
        for i, line in enumerate(term[:30]):
            lines.append(f"  {i:2}|" + clip(line.get("text_utf8", "")))
        if len(term) > 30:
            lines.append(f"  ... {len(term) - 30} more")
    modern = frame.get("modern_ui") or {}
    if modern.get("stage"):
        rows = " ".join(f"[{r.get('key') and chr(r['key']) if 32 < (r.get('key') or 0) < 127 else r.get('key')}:{r.get('label', '')}]"
                        for r in (modern.get("rows") or [])[:20])
        lines.append("modern: kind={kind} stage={stage} title={title!r} rev={rev} cursor={cur} input={inp!r}".format(
            kind=modern.get("kind"), stage=modern.get("stage"), title=modern.get("title"), rev=modern.get("revision"),
            cur=modern.get("cursor"), inp=clip(modern.get("input", ""), 60)))
        if rows:
            lines.append(f"   rows ({len(modern.get('rows') or [])}): " + clip(rows, 300))
        if modern.get("body"):
            lines.append("   body: " + clip(modern["body"].replace(chr(10), " / "), 200))
    choices = frame.get("menu_choices") or []
    if choices:
        shown = " ".join(f"[{chr(c.get('key', 0)) if 32 < c.get('key', 0) < 127 else c.get('key', 0)}:{c.get('label_utf8', '')}{'*' if c.get('selected') else ''}]"
                         for c in choices[:20])
        lines.append(f"menu_choices ({len(choices)}): " + clip(shown, 300))
    return "\n".join(lines)


CELL_FIELDS = ("gx", "gy", "terrain_id", "feature_flags", "monster_id", "monster_slot", "object_id", "light_level",
               "fg_color", "bg_color", "ascii_fallback", "tile_index", "under_tile_index",
               "graf_fg_row", "graf_fg_col", "graf_bg_row", "graf_bg_col")


def cell_dict(cell: object) -> dict:
    """frame の cells の 1 マス（17 要素の配列。presentation/frame/frame_codec.cpp の enc_cell）を名前つきにする。"""
    if isinstance(cell, dict):
        return cell
    result = dict(zip(CELL_FIELDS, cell))  # type: ignore[arg-type]
    ch = result.get("ascii_fallback", 0)
    result["ascii_fallback"] = chr(ch) if isinstance(ch, int) and 0x20 <= ch < 0x7F else ""
    return result


def b64_bytes(text: str | None) -> bytes:
    import base64
    return base64.b64decode(text) if text else b""


def render_map(frame: dict | None, light: bool = False) -> str:
    """frame の cells から字の地図（80 x 21）を描く。確かめ用。

    cells の並びは gy → gx。ascii_fallback の字をそのまま置く（無いマスは空白）。
    light=True なら、字の代わりに light_level（. 知らない・1 覚えている・2 見えている）を置く。
    CELL_FEAT_PLAYER のマスは常に '@'。"""
    if frame is None or not frame.get("cells"):
        return "(no map: cells is empty)"
    cols = int(frame.get("view_w") or 80)
    rows = int(frame.get("view_h") or 21)
    grid = [[" "] * cols for _ in range(rows)]
    for raw in frame["cells"]:
        cell = cell_dict(raw)
        gx, gy = int(cell.get("gx", 0)), int(cell.get("gy", 0))
        if not (0 <= gx < cols and 0 <= gy < rows):
            continue
        if light:
            grid[gy][gx] = str(cell.get("light_level", 0)) if cell.get("light_level") else "."
        else:
            ch = cell.get("ascii_fallback") or " "
            grid[gy][gx] = ch if isinstance(ch, str) and ch else " "
        if int(cell.get("feature_flags", 0)) & 0x0200:
            grid[gy][gx] = "@"
    border = "+" + "-" * cols + "+"
    body = ["|" + "".join(row).ljust(cols) + "|" for row in grid]
    return "\n".join([border] + body + [border])


def describe_map(frame: dict | None) -> str:
    """地図の要点（位置・窓・地形の数・ミニマップ）。"""
    if frame is None or not frame.get("cells"):
        return "map: (none)"
    cells = [cell_dict(c) for c in frame["cells"]]
    known = [c for c in cells if int(c.get("feature_flags", 0)) & 0x0400]
    terrain: dict[int, int] = {}
    for c in known:
        key = int(c.get("terrain_id", 0))
        terrain[key] = terrain.get(key, 0) + 1
    levels = {0: 0, 1: 0, 2: 0}
    for c in cells:
        level = int(c.get("light_level", 0))
        levels[level] = levels.get(level, 0) + 1
    mm = frame.get("minimap") or {}
    kinds = b64_bytes(mm.get("kinds_b64"))
    markers = b64_bytes(mm.get("markers_b64"))
    ids = [c for c in cells if int(c.get("monster_id", 0)) or int(c.get("object_id", 0))]
    id_text = " ".join(f"({c['gx']},{c['gy']}){c.get('ascii_fallback', '')}:m{c.get('monster_id', 0)}/o{c.get('object_id', 0)}" for c in ids[:12])
    return ("map: player=({px},{py}) cam=({cx},{cy}) view={vw}x{vh} cells={n} known={k} light(0/1/2)={l0}/{l1}/{l2}\n"
            "     terrain_id counts={t}\n"
            "     minimap {mw}x{mh} nonzero kinds={nk} markers={nm} memory_dim={md}\n"
            "     monster_id/object_id のあるマス={ni}: {idt}").format(
        px=frame.get("player_gx"), py=frame.get("player_gy"), cx=frame.get("cam_x"), cy=frame.get("cam_y"),
        vw=frame.get("view_w"), vh=frame.get("view_h"), n=len(cells), k=len(known),
        l0=levels.get(0, 0), l1=levels.get(1, 0), l2=levels.get(2, 0),
        t=dict(sorted(terrain.items())), mw=mm.get("width"), mh=mm.get("height"),
        nk=sum(1 for k in kinds if k), nm=sum(1 for m in markers if m),
        md=(frame.get("lighting") or {}).get("memory_dim"), ni=len(ids), idt=id_text)


# ---- 専用の画面（nethack-ui） ----

KEY_NAMES = {"esc": 27, "cr": 13, "sp": 32, "bs": 8, "tab": 9, "nl": 10}
ATTR_NAMES = {0: "", 1: "bold", 2: "dim", 3: "italic", 4: "uline", 5: "blink", 7: "inverse"}


def parse_reply(spec: str, pending: dict | None) -> dict:
    """--reply の書き方から nethack_reply の本体（r つき）を作る。"""
    r = None
    if spec.startswith("@"):
        head, _, spec = spec.partition(":")
        r = int(head[1:])
    if spec.startswith("{"):
        body = json.loads(spec)
    elif spec in ("cancel", "dismiss"):
        body = {"a": spec}
    else:
        kind, _, rest = spec.partition(":")
        if kind == "menu":
            if rest == "cancel":
                body = {"a": "cancel"}
            else:
                sel = []
                for part in rest.split(",") if rest else []:
                    num, _, count = part.partition("x")
                    entry = {"i": int(num), "n": int(count) if count else -1}
                    sel.append(entry)
                body = {"a": "menu", "sel": sel}
        elif kind == "yn":
            key = KEY_NAMES.get(rest.lower()) if rest.lower() in KEY_NAMES else ord(rest[0])
            body = {"a": "yn", "key": key}
        elif kind == "text":
            body = {"a": "text", "text": rest}
        elif kind == "select":
            if rest in ("random", "quit"):
                body = {"a": rest}
            else:
                parts = rest.split(",")
                body = {"a": "select", "role": int(parts[0]), "race": int(parts[1]),
                        "gender": int(parts[2]), "align": int(parts[3])}
                if len(parts) > 4:
                    body["name"] = ",".join(parts[4:])
        elif kind == "msgwin":
            rows, _, cols = rest.partition("x")
            body = {"a": "msgwin", "rows": int(rows), "cols": int(cols), "r": 0}
        elif kind == "pos":
            parts = [int(v) for v in rest.split(",")]
            body = {"a": "pos", "x": parts[0], "y": parts[1], "mod": parts[2] if len(parts) > 2 else 1, "r": 0}
        else:
            raise SystemExit(f"--reply の書き方が分かりません: {spec!r}")
    body.setdefault("r", r if r is not None else (pending.get("r", 0) if pending else 0))
    if r is not None:
        body["r"] = r
    body["t"] = "nethack_reply"
    return body


def ui_item_text(item: dict) -> str:
    ch = item.get("ch") or " "
    num = item.get("i", 0)
    attr = ATTR_NAMES.get(item.get("attr", 0), str(item.get("attr")))
    mark = "*" if item.get("sel") else " "
    glyph = item.get("glyph", -1)
    extra = []
    if attr:
        extra.append(attr)
    if item.get("clr", -1) not in (-1, 8):
        extra.append(f"clr={item.get('clr')}")
    if glyph is not None and glyph >= 0:
        extra.append(f"glyph={glyph}")
    if item.get("gch"):
        extra.append(f"gch={item['gch']}")
    head = f"#{num}" if num else "--"
    return f"    {head:>4} {ch} {mark}{item.get('text', '')}" + (f"   <{' '.join(extra)}>" if extra else "")


def describe_ui(message: dict, raw: bool = False) -> str:
    """nethack_ui の知らせ 1 通を、読みやすい数行にする。"""
    if raw:
        return "  ui " + clip(json.dumps(message, ensure_ascii=False, separators=(",", ":")), 3000)
    k = message.get("k")
    r = f" r={message['r']}" if message.get("r") else ""
    if k == "win":
        return f"  ui win {message.get('op')} id={message.get('id')} type={message.get('type')}" + (" blocking" if message.get("blocking") else "")
    if k == "putstr":
        flags = ("" if not message.get("attr") else f" attr={message['attr']}") + (" nohistory" if message.get("nohistory") else "")
        return f"  ui putstr id={message.get('id')}{flags} {message.get('text')!r}"
    if k == "menu":
        items = message.get("items") or []
        lines = [f"  ui menu{r} id={message.get('id')} how={message.get('how')} prompt={message.get('prompt')!r} items={len(items)}"]
        lines += [ui_item_text(item) for item in items[:40]]
        if len(items) > 40:
            lines.append(f"    ... {len(items) - 40} more")
        return "\n".join(lines)
    if k in ("text", "rip"):
        lines_ = message.get("lines") or []
        head = f"  ui {k}{r} id={message.get('id')} lines={len(lines_)}"
        if k == "rip":
            head += f" name={message.get('name')!r} gold={message.get('gold')} killer={message.get('killer')!r} year={message.get('year')}"
        out = [head]
        out += [f"    | {line.get('text', '')}" for line in lines_[:30]]
        if len(lines_) > 30:
            out.append(f"    ... {len(lines_) - 30} more")
        return "\n".join(out)
    if k == "status":
        parts = []
        for f in message.get("fields") or []:
            pct = f" {f['pct']}%" if "pct" in f else ""
            parts.append(f"f{f.get('f')}={f.get('text')!r}(c{f.get('color')}/a{f.get('attr')}{pct})")
        cond = message.get("cond")
        cond_text = "" if cond is None else " cond=" + ",".join(f"{c.get('name')}(c{c.get('color')}/a{c.get('attr')})" for c in cond)
        tag = "reset" if message.get("reset") else ("flush" if message.get("flush") else "")
        return f"  ui status {tag} " + clip(" ".join(parts), 360) + cond_text
    if k == "ask":
        extra = ""
        if message.get("choices") is not None:
            extra += f" choices={message['choices']!r}"
        if message.get("def"):
            extra += f" def={message['def']!r}"
        if message.get("cmds"):
            extra += f" cmds={len(message['cmds'])}"
        return f"  ui ask{r} kind={message.get('kind')} query={message.get('query')!r}{extra}"
    if k == "select":
        names = lambda key: ",".join(f"{e.get('i')}:{e.get('name')}" for e in message.get(key) or [])
        return (f"  ui select{r} name={message.get('name')!r} pick={message.get('pick')} ok={len(message.get('ok') or [])} combos\n"
                f"    roles   {clip(names('roles'), 200)}\n    races   {names('races')}\n    genders {names('genders')}\n    aligns  {names('aligns')}")
    if k == "wait":
        return f"  ui wait reason={message.get('reason')}{r}" + (f" text={message['text']!r}" if message.get("text") else "")
    if k == "exit":
        return f"  ui exit text={message.get('text')!r}"
    if k in ("curs", "cliparound"):
        return f"  ui {k} " + " ".join(f"{key}={message[key]}" for key in ("id", "x", "y") if key in message)
    return "  ui " + clip(json.dumps(message, ensure_ascii=False, separators=(",", ":")), 300)


class StepAction(argparse.Action):
    """--keys・--reply・--msgwin を、書いた順に 1 つの列へ積む。"""

    def __call__(self, parser, namespace, values, option_string=None):
        steps = getattr(namespace, "steps", None)
        if steps is None:
            steps = []
            setattr(namespace, "steps", steps)
        steps.append((self.dest, values))


def main() -> None:
    sys.stdout.reconfigure(encoding="utf-8")  # type: ignore[attr-defined]
    parser = argparse.ArgumentParser()
    parser.add_argument("--lang", choices=["en", "ja"], default="en", help="ui_state の言語と、既定の実行ファイル")
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--data-root", type=Path, help="HENGBAND_DATA_ROOT。同じフォルダを渡すと続きから始まる")
    parser.add_argument("--keys", action=StepAction, default=[], help="1 手ぶんのキー。送ったあとの frame を表示する")
    parser.add_argument("--reply", action=StepAction, default=[],
                        help="nethack_reply を 1 通送る（--nethack-ui のとき。書き方は冒頭の説明）")
    parser.add_argument("--msgwin", action=StepAction, default=[], help="msgwin の返事（例 2x40。行数x桁数）を送る（--nethack-ui のとき）")
    parser.add_argument("--quiet", action="store_true", help="最後の frame だけ表示する")
    parser.add_argument("--map", action="store_true", help="表示する frame ごとに、cells から描いた字の地図（80 x 21）と地図の要点を出す")
    parser.add_argument("--map-light", action="store_true", help="--map に加えて、light_level の地図も出す")
    parser.add_argument("--modern", action="store_true", help="ui_state に modern_ui を付けて握手する（新しい画面。窓のモデルが frame.modern に出る）")
    parser.add_argument("--nethack-ui", action="store_true",
                        help="hello に nethack-ui を挙げる。窓の字は frame ではなく nethack_ui の知らせで届く（--reply で答える）")
    parser.add_argument("--ui-raw", action="store_true", help="nethack_ui の知らせを生の JSON で表示する")
    parser.add_argument("--no-frame", action="store_true", help="--nethack-ui のとき、frame の要約を出さない（知らせだけ）")
    parser.add_argument("--first-msgwin", help="握手のすぐ後に msgwin の返事（例 2x40）を送る（--nethack-ui のとき）")
    parser.add_argument("--wait", type=float, default=0.8, help="何も届かなくなったとみなす秒数")
    parser.add_argument("--timeout", type=float, default=120.0, help="全体の上限時間（秒）。超えたらコアを終わらせる")
    args = parser.parse_args()
    steps: list[tuple[str, str]] = getattr(args, "steps", [])
    if not args.nethack_ui and any(kind in ("reply", "msgwin") for kind, _ in steps):
        raise SystemExit("--reply と --msgwin は --nethack-ui と一緒に使います")
    exe = (args.exe or ROOT / EXES[args.lang]).resolve()
    with tempfile.TemporaryDirectory(prefix="nethack-driver-") as temp:
        data_root = (args.data_root or Path(temp)).resolve()
        data_root.mkdir(parents=True, exist_ok=True)
        core = Core(exe, data_root, args.lang, args.timeout, args.modern, args.nethack_ui)
        try:
            if args.first_msgwin:
                core.send(parse_reply(f"msgwin:{args.first_msgwin}", None))
            core.settle(quiet=max(args.wait, 1.5), limit=30.0)
            if core.hello_ack:
                print("hello_ack: core_name={core_name} core_version={core_version} features={features}".format(
                    **{**core.hello_ack, "features": core.hello_ack.get("features")}))

            def show_ui() -> None:
                for message in core.ui_new:
                    print(describe_ui(message, args.ui_raw))
                core.ui_new.clear()

            def show_map() -> None:
                if args.map or args.map_light:
                    print(describe_map(core.frame))
                    print(render_map(core.frame))
                    if args.map_light:
                        print(render_map(core.frame, light=True))

            def show(title: str) -> None:
                print(title)
                if args.nethack_ui:
                    show_ui()
                if not (args.nethack_ui and args.no_frame):
                    print(describe(core.frame))
                    show_map()

            if not args.quiet or not steps:
                show("== start")
            for i, (kind, value) in enumerate(steps):
                if kind == "keys":
                    core.keys(parse_keys(value), args.wait)
                    label = repr(value)
                else:
                    if kind == "msgwin":
                        body = parse_reply(f"msgwin:{value}", None)
                    else:
                        body = parse_reply(value, core.pending)
                    core.send(body)
                    if body.get("a") not in ("msgwin", "pos") and core.pending and body.get("r") == core.pending.get("r"):
                        core.pending = None
                    core.settle(args.wait)
                    label = "reply " + json.dumps(body, ensure_ascii=False, separators=(",", ":"))
                if not args.quiet or i == len(steps) - 1:
                    show(f"== {label} (exit={core.exit_code})")
                else:
                    core.ui_new.clear()
                if core.proc.poll() is not None:
                    print("(core has exited)")
                    break
        finally:
            core.close()
            if args.nethack_ui and core.ui_new:
                print("== after close")
                for message in core.ui_new:
                    print(describe_ui(message, args.ui_raw))
        print(f"exit {core.exit_code} alive {core.proc.poll() is None} frames {core.frame_count} ui_messages {core.ui_count} returncode {core.proc.returncode}")
        if core.stderr_text.strip():
            print("-- stderr --" + chr(10) + clip(core.stderr_text.strip(), 1500))


if __name__ == "__main__":
    main()
