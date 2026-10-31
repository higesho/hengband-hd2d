#!/usr/bin/env python3
"""NetHack の原作のタイルの絵から、16×16 の絵を並べた PNG と、glyph の番号 → 絵の番号の表を作る。

 （D16・T6）。入力は tools/core_sources のレシピで再構成した原作
（`../roguelike-cores/build-sources/nethack`（英語版）・`nethackjp`（日本語版））で、何も書き換えない。

作る物（`<出力先>/<言語>/`。言語は en・ja）:
  tiles.png   16×16 の絵を 1 行 40 個で並べた RGBA の PNG。絵の番号 n は (n % 40, n // 40) の位置。
              原作の GUI 版が埋め込む tiles.bmp（640 幅・8 ビット）と同じ並びと同じ色で、絵の透過色
              RGB(71,108,108)（原作の `TILE_BK_COLOR`）の画素だけ alpha を 0 にしてある（RGB は元のまま残す）。
  tiles.json  絵の番号の表と、場所を知るための情報（下の「表の形」）。
  tile_names.json  （`--names` のときだけ）絵の番号ごとの名前。原作の .txt の `# tile N (名前)` の名前で、確かめ用。

表の形（tiles.json）:
  format 1 / source / tile_size 16 / columns 40 / rows / tile_count / image "tiles.png" / transparent_rgb
  nummons・num_objects          原作の NUMMONS・NUM_OBJECTS（絵のファイルから数えて、include の怪物の表と照合済み）
  max_glyph                     原作の MAX_GLYPH。glyph2tile の長さと同じ
  glyph_offsets                 include/display.h の `enum glyph_offsets` の値（GLYPH_*_OFF）
  special_tiles                 stone・unexplored・nothing・corridor・delimiter・petmark（ペットの印）・pilemark（物の山の印）・invisible
  tile_ranges                   monsters・objects・other・statues・decals の絵の番号の範囲 [最初, 最後]
  glyph2tile                    glyph の番号 → 絵の番号。原作の tilemap.c が作る glyphmap[].tileidx と同じ
  どの glyph も 0 以上の絵を指す（原作の tilemap は割り当てが漏れると失敗する）。

絵の並び（原作の tile2bmp と同じ）: monsters.txt、objects.txt、other.txt、monsters.txt（灰色にしたもの＝石像）、
decals.txt（区切り・ペットの印・物の山の印の 3 個）。怪物は 1 体ごとに男・女の 2 個。

作り方: 原作の道具（tilemap.c・tiletext.c・tile2bmp.c）と同じ規則を Python で読む。
  - コンパイラが要らず、Linux・Android の組み立てでも同じに作れる。tilemap は MSVC の組み立てと
    `../src/tile.c` への書き込みが要り、原作の作業ツリーを汚すため、組み立てのたびには走らせない。
  - 代わりに `--verify-original` が、原作の tilemap と tile2bmp を MSVC で組んで走らせ（作業用の写しの中で）、
    この道具の出力と全 glyph・全画素を突き合わせる（Windows と Visual Studio が要る）。
  - 規則（glyph の並び＝display.h、条件付きの絵の飛ばし＝tilemap.c の conditionals、絵の数＝各 .txt）は
    原作のファイルから読み、数字を直書きしない。

使い方:
    python tools/nethack/build_tiles.py                              # 英語版・日本語版を nethack/data/<言語>/ へ
    python tools/nethack/build_tiles.py --lang en --out <フォルダ>   # <フォルダ>/en/ へ
    python tools/nethack/build_tiles.py --source-root <build-sources> --names
    python tools/nethack/build_tiles.py --verify-original --out <作業フォルダ>
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCES = ROOT.parent / "roguelike-cores" / "build-sources"
DEFAULT_OUT = ROOT / "nethack" / "data"
VCVARS = r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

VARIANTS = {"en": "nethack", "ja": "nethackjp"}

TILE_X = TILE_Y = 16
COLUMNS = 40  # 原作の tile2bmp の max_tiles_in_row
# tile2bmp.c の tilefilenames と同じ順（4 番目の monsters.txt は灰色にして石像の絵にする）
TILE_FILES = ("monsters.txt", "objects.txt", "other.txt", "monsters.txt", "decals.txt")
# tiletext.c の graymappings（パレットの順番 → 灰色の段階の番号）
GRAYMAPPINGS = (0, 1, 17, 18, 19, 20, 27, 22, 23, 24, 25, 26, 21, 15, 13, 14, 14,
                1, 17, 18, 19, 20, 27, 22, 23, 24, 25, 20)
# 原作の組み立てで定義される物（global.h の MAIL_STRUCTURES は無条件に定義。CHARON はどこでも定義しない）
DEFINED_MACROS = {"MAIL_STRUCTURES"}
# 表の中の `win/share` の道具が glyphmap を作るときの、爆発の種類の並び（display.h の GLYPH_EXPLODE_*_OFF の並びと同じ）
EXPLOSION_KINDS = ("DARK", "NOXIOUS", "MUDDY", "WET", "MAGICAL", "FIERY", "FROSTY")
WALL_SETS = ("MAIN", "MINES", "GEH", "KNOX", "SOKO")


class TileError(Exception):
    pass


# ---------------------------------------------------------------- 小さな C の読み取り

def strip_c_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def active_text(text: str, defined=frozenset(DEFINED_MACROS)) -> str:
    """#if 0・#ifdef・#ifndef・#else・#endif だけを解いて、有効な行を返す。
    ほかの #if・#elif は式を評価せず、有効として扱う（怪物の表の頭の定義用のマクロだけが使う）。"""
    out = []
    stack = []  # 各段の (今の枝が有効か, 親が有効か, すでに有効な枝があったか)
    live = True
    for line in text.splitlines():
        s = line.strip()
        m = re.match(r"#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", s)
        if not m:
            out.append(line if live else "")
            continue
        kind, rest = m.group(1), m.group(2).strip()
        if kind in ("if", "ifdef", "ifndef"):
            if kind == "if":
                cond = rest != "0"
            elif kind == "ifdef":
                cond = rest in defined
            else:
                cond = rest not in defined
            stack.append((live, cond))
            live = live and cond
        elif kind == "elif":
            parent, _ = stack[-1]
            stack[-1] = (parent, True)
            live = parent
        elif kind == "else":
            parent, cond = stack[-1]
            stack[-1] = (parent, not cond)
            live = parent and not cond
        else:  # endif
            parent, _ = stack.pop()
            live = parent
        out.append("")
    return "\n".join(out)


def eval_c_int(expr: str, env: dict) -> int:
    py = expr.strip()
    if re.search(r"[^\w\s()+\-*<>]", py):
        raise TileError(f"読めない式: {expr}")
    return int(eval(py, {"__builtins__": {}}, env))


def read_text(path: Path) -> str:
    return path.read_bytes().decode("utf-8", errors="replace").replace("\r\n", "\n")


# ---------------------------------------------------------------- タイルの .txt（tiletext.c の読み方）

class TileFile:
    def __init__(self, name):
        self.name = name
        self.palette = []      # [(r, g, b)]（パレットの並びの順）
        self.color_index = {}  # 文字 → パレットの番号
        self.entries = []      # [(コメント, [16 行の文字列])]


HEADER_RE = re.compile(r"#\s*(\S+)\s+(-?\d+)\s*\((.*)\)")
PALETTE_RE = re.compile(r"([A-Za-z0-9.])\s*=\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)")


def parse_tile_file(path: Path) -> TileFile:
    tf = TileFile(path.name)
    lines = read_text(path).split("\n")
    i = 0

    def next_line():
        """tiletext.c の get_next_line: 空行と、`# tile ` 以外の # の行を飛ばす。先頭の空白は捨てる。"""
        nonlocal i
        while i < len(lines):
            s = lines[i].lstrip(" ")
            i += 1
            if not s.strip():
                continue
            if s[0] != "#" or s.startswith("# tile ") or s.startswith("# placeholder "):
                return s
        return None

    # パレット: `X = (r, g, b)` が続く間
    pending = None
    while True:
        s = next_line()
        if s is None:
            break
        m = PALETTE_RE.match(s)
        if not m:
            pending = s
            break
        tf.color_index[m.group(1)] = len(tf.palette)
        tf.palette.append((int(m.group(2)), int(m.group(3)), int(m.group(4))))
    if not tf.palette:
        raise TileError(f"{path}: パレットがありません")
    while pending is not None:
        m = HEADER_RE.match(pending)
        if not m or m.group(1) not in ("tile", "placeholder"):
            raise TileError(f"{path}: 絵の見出しが読めません: {pending!r}")
        comment = m.group(3)
        s = next_line()
        if s is None or s[0] != "{":
            raise TileError(f"{path}: '{{' がありません（{comment}）")
        rows = []
        for _ in range(TILE_Y):
            s = next_line()
            if s is None:
                raise TileError(f"{path}: 絵の途中で終わりました（{comment}）")
            row = s.rstrip("\n")
            rows.append((row + "." * TILE_X)[:TILE_X])
        s = next_line()
        if s is None or s[0] != "}":
            raise TileError(f"{path}: '}}' がありません（{comment}）")
        tf.entries.append((comment, rows))
        pending = next_line()
    return tf


def render_tile(tf: TileFile, rows, gray: bool, warn):
    """16 行の文字 → RGBA（alpha は透過色だけ 0）の bytes。tiletext.c の read_txttile と同じ。"""
    key = tf.palette[tf.color_index["."]] if "." in tf.color_index else None
    out = bytearray()
    for row in rows:
        for ch in row:
            k = tf.color_index.get(ch, -1)
            if k < 0:
                raise TileError(f"{tf.name}: パレットに無い色 {ch!r}")
            if gray:
                if k > len(GRAYMAPPINGS) - 1:
                    warn(f"{tf.name}: 灰色の対応表にない色 {ch!r}（元の色のまま）")
                else:
                    k = GRAYMAPPINGS[k]
            r, g, b = tf.palette[k]
            out += bytes((r, g, b, 0 if (r, g, b) == key else 255))
    return bytes(out)


# ---------------------------------------------------------------- 原作のヘッダからの定数

def parse_defsym(source: Path) -> dict:
    text = strip_c_comments(read_text(source / "include" / "defsym.h"))
    syms = {}
    for m in re.finditer(r"PCHAR2?\(\s*(\d+)\s*,\s*'(?:\\.|[^'\\])'\s*,\s*(S_\w+)", text):
        syms[m.group(2)] = int(m.group(1))
    values = sorted(syms.values())
    if not values or values != list(range(len(values))):
        raise TileError("defsym.h の PCHAR の番号が 0 からの連番になっていません")
    return syms


def parse_conditionals(source: Path) -> dict:
    """tilemap.c の conditionals[]（コンパイルされていない怪物・アイテムのぶんの絵を飛ばす表）→ {MON_GLYPH: 名前の集合, ...}。"""
    text = read_text(source / "win" / "share" / "tilemap.c")
    m = re.search(r"struct conditionals_t \{.*?\} conditionals\[\] = \{(.*?)\{ TERMINATOR", text, re.S)
    if not m:
        raise TileError("tilemap.c の conditionals[] が見つかりません")
    body = active_text(strip_c_comments(m.group(1)))
    result = {"MON_GLYPH": [], "OBJ_GLYPH": [], "OTH_GLYPH": []}
    for e in re.finditer(r"\{\s*(MON_GLYPH|OBJ_GLYPH|OTH_GLYPH)\s*,\s*[^,{}]+?,\s*\"([^\"]+)\"\s*\}", body):
        result[e.group(1)].append(e.group(2))
    return result


def parse_monster_names(source: Path) -> list:
    """include/monsters.h の有効な怪物の、性別を問わない名前（原作の pmnames[NEUTRAL]）の並び。"""
    text = active_text(strip_c_comments(read_text(source / "include" / "monsters.h")))
    names = []
    for m in re.finditer(r"\bMON\(\s*(NAMS?)\(([^)]*)\)", text):
        strings = re.findall(r"\"((?:[^\"\\]|\\.)*)\"", m.group(2))
        names.append(strings[-1] if m.group(1) == "NAMS" else strings[0])
    return names


def parse_glyph_offsets(source: Path, env: dict) -> dict:
    """include/display.h の enum glyph_offsets を読んで評価する。"""
    text = strip_c_comments(read_text(source / "include" / "display.h"))
    m = re.search(r"enum glyph_offsets \{(.*?)\n\};", text, re.S)
    if not m:
        raise TileError("display.h の enum glyph_offsets が見つかりません")
    env = dict(env)
    offsets = {}
    prev = -1
    for item in m.group(1).split(","):
        item = item.strip()
        if not item:
            continue
        if "=" in item:
            name, expr = (p.strip() for p in item.split("=", 1))
            value = eval_c_int(expr, env)
        else:
            name, value = item, prev + 1
        env[name] = value
        offsets[name] = value
        prev = value
    return offsets


# ---------------------------------------------------------------- 絵の番号の表（tilemap.c の init_tilemap と同じ）

def build_tables(source: Path, files: dict, warn):
    """files: 名前 → TileFile。戻り値: (glyph2tile, 情報の辞書)。"""
    cond = parse_conditionals(source)
    mon_entries = files["monsters.txt"].entries
    obj_entries = files["objects.txt"].entries
    oth_entries = files["other.txt"].entries
    n_m, n_o, n_t = len(mon_entries), len(obj_entries), len(oth_entries)

    # 怪物: 2 個（男・女）ずつ。条件付きで飛ばす怪物の対は、名前が conditionals にある物
    skip_mon = set(cond["MON_GLYPH"])
    m_base = []     # 怪物 i（有効な物だけ）の男の絵の番号
    m_names = []
    p = 0
    while p < n_m - 1:
        name_m, name_f = (e[0].split(",")[0] for e in mon_entries[p:p + 2])
        gend_m = mon_entries[p][0].split(",")[1:]
        gend_f = mon_entries[p + 1][0].split(",")[1:]
        if name_m != name_f or gend_m != ["male"] or gend_f != ["female"]:
            raise TileError(f"monsters.txt の {p} 番が男・女の対になっていません: {mon_entries[p][0]} / {mon_entries[p + 1][0]}")
        if name_m not in skip_mon:
            m_base.append(p)
            m_names.append(name_m)
        p += 2
    if mon_entries[n_m - 1][0].split(",")[0] != "invisible monster":
        raise TileError(f"monsters.txt の最後が invisible monster ではありません: {mon_entries[n_m - 1][0]}")
    nummons = len(m_base)
    invisible_tile = n_m - 1
    header_names = parse_monster_names(source)
    if header_names != m_names:
        diff = [(i, a, b) for i, (a, b) in enumerate(zip(header_names, m_names)) if a != b][:3]
        raise TileError(f"include/monsters.h の怪物（{len(header_names)} 種）と monsters.txt（{nummons} 種）が合いません: {diff}")

    # アイテム
    skip_obj = set(cond["OBJ_GLYPH"])
    o_tile = []     # アイテム i の絵の番号（全体の通し番号）
    o_names = []
    for q, (comment, _) in enumerate(obj_entries):
        if comment in skip_obj:
            continue
        o_tile.append(n_m + q)
        o_names.append(comment)
    num_objects = len(o_tile)
    corpse = [i for i, n in enumerate(o_names) if n == "corpse"]
    if len(corpse) != 1:
        raise TileError("objects.txt に corpse が 1 つではありません")
    corpse_tile = o_tile[corpse[0]]

    # 爆発・壁などの数
    sym_h = strip_c_comments(read_text(source / "include" / "sym.h"))
    disp_h = strip_c_comments(read_text(source / "include" / "display.h"))
    trap_h = strip_c_comments(read_text(source / "include" / "trap.h"))
    syms = parse_defsym(source)
    env = dict(syms)
    env["MAXPCHARS"] = len(syms)
    env["NUMMONS"] = nummons
    env["NUM_OBJECTS"] = num_objects
    env["MAXEXPCHARS"] = int(re.search(r"#define\s+MAXEXPCHARS\s+(\d+)", sym_h).group(1))
    env["WARNCOUNT"] = int(re.search(r"#define\s+WARNCOUNT\s+(\d+)", sym_h).group(1))
    env["NUM_ZAP"] = int(re.search(r"#define\s+NUM_ZAP\s+(\d+)", disp_h).group(1))
    env["TRAPNUM"] = int(re.search(r"\bTRAPNUM\s*=\s*(\d+)", trap_h).group(1))
    env["MAXTCHARS"] = env["TRAPNUM"] - 1
    offsets = parse_glyph_offsets(source, env)
    max_glyph = offsets["MAX_GLYPH"]

    g2t = [-1] * max_glyph

    def put(glyph, tile):
        if not 0 <= glyph < max_glyph:
            raise TileError(f"glyph {glyph} が範囲外")
        if g2t[glyph] != -1:
            raise TileError(f"glyph {glyph} に絵を二重に割り当てました")
        g2t[glyph] = tile

    # 怪物（男・女の 4 組: 通常・ペット・見つけた・乗っている）と死体
    for i in range(nummons):
        male, female = m_base[i], m_base[i] + 1
        for name in ("MON", "PET", "DETECT", "RIDDEN"):
            put(offsets[f"GLYPH_{name}_MALE_OFF"] + i, male)
            put(offsets[f"GLYPH_{name}_FEM_OFF"] + i, female)
        put(offsets["GLYPH_BODY_OFF"] + i, corpse_tile)
        put(offsets["GLYPH_BODY_PILETOP_OFF"] + i, corpse_tile)
    put(offsets["GLYPH_INVIS_OFF"], invisible_tile)
    # アイテム
    for i in range(num_objects):
        put(offsets["GLYPH_OBJ_OFF"] + i, o_tile[i])
        put(offsets["GLYPH_OBJ_PILETOP_OFF"] + i, o_tile[i])
    # other.txt（地形・効果）: 並びは tilemap.c の init_tilemap の通り
    base = n_m + n_o
    pos = [base]

    def nxt():
        v = pos[0]
        pos[0] += 1
        return v

    def span(count, glyph_off):
        for i in range(count):
            put(glyph_off + i, nxt())

    walls = env["S_trwall"] - env["S_vwall"] + 1
    put(offsets["GLYPH_CMAP_STONE_OFF"], nxt())
    span(walls, offsets["GLYPH_CMAP_MAIN_OFF"])
    span(env["S_brdnladder"] - env["S_ndoor"] + 1, offsets["GLYPH_CMAP_A_OFF"])
    span(5, offsets["GLYPH_ALTAR_OFF"])
    span(env["S_arrow_trap"] + env["MAXTCHARS"] - env["S_grave"], offsets["GLYPH_CMAP_B_OFF"])
    span(env["NUM_ZAP"] << 2, offsets["GLYPH_ZAP_OFF"])
    span(env["S_goodpos"] - env["S_digbeam"] + 1, offsets["GLYPH_CMAP_C_OFF"])
    swallow = env["S_sw_br"] - env["S_sw_tl"] + 1
    swallow_base = pos[0]
    for k in range(nummons):
        for i in range(swallow):
            put(offsets["GLYPH_SWALLOW_OFF"] + k * swallow + i, swallow_base + i)
    pos[0] += swallow
    for kind in EXPLOSION_KINDS:
        span(env["MAXEXPCHARS"], offsets[f"GLYPH_EXPLODE_{kind}_OFF"])
    span(env["WARNCOUNT"], offsets["GLYPH_WARNING_OFF"])
    put(offsets["GLYPH_UNEXPLORED_OFF"], nxt())
    put(offsets["GLYPH_NOTHING_OFF"], nxt())
    for name in WALL_SETS[1:]:
        span(walls, offsets[f"GLYPH_CMAP_{name}_OFF"])
    if pos[0] - base != n_t:
        raise TileError(f"other.txt の絵は {n_t} 個ですが、規則からは {pos[0] - base} 個になります（地形の記号の数が合いません）")
    # 石像: monsters.txt を灰色にした 4 番目の絵（条件付きの怪物の対も含めて同じ並び）
    statue_base = base + n_t
    for i in range(nummons):
        male = statue_base + m_base[i]
        for kind in ("STATUE", "STATUE_PILETOP"):
            put(offsets[f"GLYPH_{kind}_MALE_OFF" if kind == "STATUE" else "GLYPH_STATUE_MALE_PILETOP_OFF"] + i, male)
            put(offsets[f"GLYPH_{kind}_FEM_OFF" if kind == "STATUE" else "GLYPH_STATUE_FEM_PILETOP_OFF"] + i, male + 1)
    decal_base = statue_base + n_m
    missing = [g for g, t in enumerate(g2t) if t < 0]
    if missing:
        raise TileError(f"絵が割り当てられていない glyph が {len(missing)} 個あります（例 {missing[:5]}）")
    if len(files["decals.txt"].entries) != 3:
        raise TileError("decals.txt が 3 個ではありません")

    # 区切り・ペットの印・物の山の印は decals.txt（glyph には割り当てない）
    stone = g2t[offsets["GLYPH_CMAP_STONE_OFF"]]
    corr = g2t[offsets["GLYPH_CMAP_A_OFF"] + env["S_corr"] - env["S_ndoor"]]
    info = {
        "nummons": nummons,
        "num_objects": num_objects,
        "max_glyph": max_glyph,
        "glyph_offsets": offsets,
        "special_tiles": {
            "stone": stone,
            "corridor": corr,
            "unexplored": g2t[offsets["GLYPH_UNEXPLORED_OFF"]],
            "nothing": g2t[offsets["GLYPH_NOTHING_OFF"]],
            "invisible": invisible_tile,
            "delimiter": decal_base,
            "petmark": decal_base + 1,
            "pilemark": decal_base + 2,
        },
        "tile_ranges": {
            "monsters": [0, n_m - 1],
            "objects": [n_m, n_m + n_o - 1],
            "other": [base, base + n_t - 1],
            "statues": [statue_base, statue_base + n_m - 1],
            "decals": [decal_base, decal_base + 2],
        },
    }
    return g2t, info


# ---------------------------------------------------------------- 画像

def png_rgba(width: int, height: int, rgba: bytes) -> bytes:
    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    stride = width * 4
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += rgba[y * stride:(y + 1) * stride]
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def compose(tiles: list) -> tuple:
    count = len(tiles)
    rows = (count + COLUMNS - 1) // COLUMNS
    width, height = COLUMNS * TILE_X, rows * TILE_Y
    canvas = bytearray(width * height * 4)  # 余りのマスは透明
    for n, t in enumerate(tiles):
        x0, y0 = (n % COLUMNS) * TILE_X, (n // COLUMNS) * TILE_Y
        for y in range(TILE_Y):
            dst = ((y0 + y) * width + x0) * 4
            canvas[dst:dst + TILE_X * 4] = t[y * TILE_X * 4:(y + 1) * TILE_X * 4]
    return width, height, bytes(canvas)


def load_tiles(source: Path, warn):
    """原作のタイルの .txt を tile2bmp と同じ順に読み、全部の絵（RGBA の bytes）の並びと、名前の並びを返す。"""
    share = source / "win" / "share"
    files = {}
    for name in sorted(set(TILE_FILES)):
        path = share / name
        if not path.is_file():
            raise TileError(f"{path} がありません。python tools/core_sources/prepare.py で原作を再構成してください")
        files[name] = parse_tile_file(path)
    tiles, names = [], []
    for pass_no, name in enumerate(TILE_FILES):
        tf = files[name]
        gray = pass_no == 3
        for comment, rows in tf.entries:
            tiles.append(render_tile(tf, rows, gray, warn))
            names.append(("statue: " if gray else "") + comment)
    return files, tiles, names


# ---------------------------------------------------------------- 出力

def format_json(data: dict) -> str:
    """glyph2tile は 1 行 24 個で並べ、ほかは 2 字下げの普通の JSON にする。"""
    g2t = data["glyph2tile"]
    head = dict(data)
    head["glyph2tile"] = "@@G2T@@"
    text = json.dumps(head, ensure_ascii=False, indent=2)
    # 数だけの短い列（透過色・絵の範囲）は 1 行にまとめる
    text = re.sub(r"\[\n\s+(-?\d+(?:,\n\s+-?\d+)*)\n\s+\]",
                  lambda m: "[" + re.sub(r",\n\s+", ", ", m.group(1)) + "]", text)
    rows = [", ".join(str(v) for v in g2t[i:i + 24]) for i in range(0, len(g2t), 24)]
    body = "[\n    " + ",\n    ".join(rows) + "\n  ]"
    return text.replace('"@@G2T@@"', body) + "\n"


def build_one(lang: str, source_root: Path, out_root: Path, with_names: bool, log) -> dict:
    source = source_root / VARIANTS[lang]
    if not (source / "include" / "display.h").is_file():
        raise TileError(f"{source} がありません。python tools/core_sources/prepare.py で原作を再構成してください")
    warnings = []
    files, tiles, names = load_tiles(source, warnings.append)
    g2t, info = build_tables(source, files, warnings.append)
    total = len(tiles)
    expect = info["special_tiles"]["pilemark"] + 1
    if total != expect:
        raise TileError(f"絵の数 {total} と規則から出る数 {expect} が違います")
    width, height, rgba = compose(tiles)
    out = out_root / lang
    out.mkdir(parents=True, exist_ok=True)
    (out / "tiles.png").write_bytes(png_rgba(width, height, rgba))
    key = files["monsters.txt"].palette[files["monsters.txt"].color_index["."]]
    data = {
        "format": 1,
        "source": VARIANTS[lang],
        "tile_size": TILE_X,
        "columns": COLUMNS,
        "rows": height // TILE_Y,
        "tile_count": total,
        "image": "tiles.png",
        "transparent_rgb": list(key),
        "nummons": info["nummons"],
        "num_objects": info["num_objects"],
        "max_glyph": info["max_glyph"],
        "special_tiles": info["special_tiles"],
        "tile_ranges": info["tile_ranges"],
        "glyph_offsets": info["glyph_offsets"],
        "glyph2tile": g2t,
    }
    (out / "tiles.json").write_text(format_json(data), encoding="utf-8", newline="\n")
    if with_names:
        (out / "tile_names.json").write_text(json.dumps(names, ensure_ascii=False, indent=0) + "\n",
                                             encoding="utf-8", newline="\n")
    for w in sorted(set(warnings)):
        log(f"  警告: {w}")
    log(f"[{lang}] {out / 'tiles.png'} {width}x{height} 絵 {total} 個 / {out / 'tiles.json'} glyph {len(g2t)} 個")
    return {"tiles": tiles, "names": names, "g2t": g2t, "info": info, "data": data,
            "first_palette": files["monsters.txt"].palette}


# ---------------------------------------------------------------- 原作の道具との突き合わせ

def run_bat(bat: Path, lines: list, log: Path, timeout: int):
    body = ["@echo off", "set VSLANG=1033", f'call "{VCVARS}" >nul', "if errorlevel 1 exit /b 1"] + lines
    body.append("if defined FAILED exit /b 1")
    bat.write_text("\r\n".join(body) + "\r\n", encoding="utf-8")
    result = subprocess.run(["cmd", "/c", str(bat)], capture_output=True, text=True, errors="replace", timeout=timeout)
    text = result.stdout + result.stderr
    log.write_text(text, encoding="utf-8")
    return result.returncode, text


def build_original(lang: str, source: Path, work: Path, log) -> tuple:
    """原作の tilemap と tile2bmp を組んで走らせ、(tile.c の本文, tiles.bmp のバイト列) を返す。
    原作の写しは work に必要な部分だけ複製して使う（tilemap が ../src/tile.c を書くため）。"""
    ja = lang == "ja"
    tree = work / "tree"
    if tree.exists():
        shutil.rmtree(tree)
    for rel in ("include", "sys/windows", "win/share"):
        shutil.copytree(source / rel, tree / rel)
    (tree / "nhlua/lua/src").mkdir(parents=True)
    for h in (source / "nhlua/lua/src").glob("*.h"):
        shutil.copy2(h, tree / "nhlua/lua/src" / h.name)
    (tree / "src").mkdir()
    for n in ("monst", "objects", "drawing", "alloc", "hacklib"):
        shutil.copy2(source / "src" / f"{n}.c", tree / "src" / f"{n}.c")
    (tree / "util").mkdir()
    shutil.copy2(source / "util" / "panic.c", tree / "util" / "panic.c")
    obj = work / "obj"
    if obj.exists():
        shutil.rmtree(obj)
    obj.mkdir(parents=True)
    charset = "-DUNICODE -D_UNICODE" if ja else "-D_MBCS"
    game = "-DSHIM_GRAPHICS -DNOTTYGRAPHICS" + (" -DNH_LANG_JA" if ja else "")
    cflags = (f"-c -nologo -D_CRT_NONSTDC_NO_DEPRECATE -D_CRT_SECURE_NO_DEPRECATE -D_LIB -D_SCL_SECURE_NO_DEPRECATE {charset} "
              "-DCRTAPI1=_cdecl -DCRTAPI2=_cdecl -DNDEBUG -DYY_NO_UNISTD_H -DHAS_STDINT_H -DHAS_INLINE -EHsc -fp:precise "
              "-Gd -GF -GS -Gy -MT -O2 -D_AMD64_=1 -DWIN64 -D_WIN64 -DWIN32 -D_WIN32 -W3 -wd4244 -wd4245 -wd4310 -wd4706 "
              "/utf-8 /std:clatest")
    inc = f'/I"{tree}\\include" /I"{tree}\\sys\\windows" /I"{tree}\\win\\share" /I"{tree}\\nhlua\\lua\\src"'

    def cl(extra, out, src):
        return f'cl {cflags} {game} {extra} {inc} -Fo"{out}" "{src}" || set FAILED=1'

    lines = [
        cl("", obj / "tilemap.obj", tree / "win/share/tilemap.c"),
        cl("/DTILETEXT", obj / "tiletxt.obj", tree / "win/share/tilemap.c"),
        cl("", obj / "tiletext.obj", tree / "win/share/tiletext.c"),
        cl("/DPACKED_FILE -D_CONSOLE -DWIN32CON", obj / "tile2bmp.obj", tree / "win/share/tile2bmp.c"),
    ]
    for n in ("monst", "objects", "drawing", "alloc", "hacklib"):
        lines.append(cl("", obj / f"{n}.obj", tree / f"src/{n}.c"))
    lines.append(cl("", obj / "panic.obj", tree / "util/panic.c"))
    lines.append("if defined FAILED exit /b 1")
    common = " ".join(f'"{obj}\\{n}.obj"' for n in ("monst", "objects", "drawing", "hacklib", "alloc", "panic"))
    lines.append(f'link /nologo /OUT:"{work}\\tilemap.exe" "{obj}\\tilemap.obj" {common} || set FAILED=1')
    lines.append(f'link /nologo /OUT:"{work}\\tile2bmp.exe" "{obj}\\tile2bmp.obj" "{obj}\\tiletext.obj" "{obj}\\tiletxt.obj" {common} || set FAILED=1')
    code, text = run_bat(work / "build.bat", lines, work / "build.log", timeout=900)
    errors = [l for l in text.splitlines() if " error " in l or "fatal error" in l or "LNK" in l]
    if code != 0 or errors:
        raise TileError("原作の道具を組めませんでした: " + "; ".join(errors[:5]) + f"（{work / 'build.log'}）")
    util = tree / "util"
    for exe, args in (("tilemap.exe", []), ("tile2bmp.exe", [str(work / "tiles.bmp")])):
        r = subprocess.run([str(work / exe)] + args, cwd=str(util), capture_output=True, text=True, errors="replace", timeout=300)
        if r.returncode != 0:
            raise TileError(f"{exe} が失敗しました: {r.stdout}{r.stderr}")
        others = [l for l in r.stderr.splitlines() if l.strip() and not l.startswith("Total of")]
        if others:
            log(f"  原作の {exe} の出力: {others[:5]}")
    return (tree / "src" / "tile.c").read_text(encoding="utf-8", errors="replace"), (work / "tiles.bmp").read_bytes()


def read_bmp(data: bytes):
    """8 ビット・下から上へ並ぶ BMP → (幅, 高さ, パレット, 画素の番号を上から並べた行のリスト)。"""
    off = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    if bits != 8 or height <= 0:
        raise TileError("想定外の BMP")
    palette = [(data[54 + 4 * i + 2], data[54 + 4 * i + 1], data[54 + 4 * i]) for i in range(256)]
    stride = ((width * 8 + 31) // 32) * 4
    rows = [data[off + (height - 1 - y) * stride: off + (height - 1 - y) * stride + width] for y in range(height)]
    return width, height, palette, rows


def verify_original(lang: str, source_root: Path, work_root: Path, mine: dict, log) -> bool:
    work = work_root / f"orig_{lang}"
    work.mkdir(parents=True, exist_ok=True)
    tile_c, bmp = build_original(lang, source_root / VARIANTS[lang], work, log)
    ok = True
    # 1. 全 glyph の絵の番号（tile.c の glyphmap[] の tileidx）
    theirs = {}
    for m in re.finditer(r"NO_CUSTOMCOLOR,\s*(\d+)(?:,\s*\d+)?\s*\},\s*/\*\s*\[(\d+)\]", tile_c):
        theirs[int(m.group(2))] = int(m.group(1))
    g2t = mine["g2t"]
    bad = [g for g in range(len(g2t)) if theirs.get(g) != g2t[g]]
    log(f"[{lang}] 表: 原作 {len(theirs)} glyph / 自作 {len(g2t)} glyph / 食い違い {len(bad)}")
    if bad or len(theirs) != len(g2t):
        ok = False
        for g in bad[:10]:
            log(f"    glyph {g}: 原作 {theirs.get(g)} 自作 {g2t[g]}")
    # 2. 特別な絵の番号と範囲
    sp = mine["info"]["special_tiles"]
    rng = mine["info"]["tile_ranges"]
    pairs = [("TILE_CORR", "corridor"), ("TILE_STONE", "stone"), ("TILE_UNEXPLORED", "unexplored"),
             ("TILE_DELIMITER", "delimiter"), ("TILE_PETMARK", "petmark"), ("TILE_PILEMARK", "pilemark")]
    for cname, key in pairs:
        v = int(re.search(rf"\b{cname}\s*=\s*(\d+)", tile_c).group(1))
        flag = "一致" if v == sp[key] else "不一致"
        ok = ok and v == sp[key]
        log(f"    {cname} 原作 {v} 自作 {sp[key]} {flag}")
    for cname, val in (("total_tiles_used", mine["data"]["tile_count"]), ("maxmontile", rng["monsters"][1]),
                       ("maxobjtile", rng["objects"][1]), ("maxothtile", rng["other"][1])):
        v = int(re.search(rf"\b{cname}\s*=\s*(\d+)", tile_c).group(1))
        ok = ok and v == val
        log(f"    {cname} 原作 {v} 自作 {val} {'一致' if v == val else '不一致'}")
    # 3. 画素。原作の tile2bmp は BMP のパレットを最初のファイル（monsters.txt）の色だけで作るので、decals.txt だけにある色
    #    （区切りの印の緑 (0,255,0)）は BMP では黒（パレットの未使用の枠）になる。原作の BMP の側の癖なので、
    #    「自作の色がその先頭のパレットに無く、BMP の色が (0,0,0)」の画素だけは許し、数えて報告する。
    width, height, palette, rows = read_bmp(bmp)
    first_palette = set(mine["first_palette"])
    tiles = mine["tiles"]
    diff_tiles, excused_pixels, bad_pixels = [], 0, 0
    for n, t in enumerate(tiles):
        x0, y0 = (n % COLUMNS) * TILE_X, (n // COLUMNS) * TILE_Y
        tile_bad = False
        for y in range(TILE_Y):
            row = rows[y0 + y]
            for x in range(TILE_X):
                theirs_rgb = palette[row[x0 + x]]
                o = (y * TILE_X + x) * 4
                my_rgb = (t[o], t[o + 1], t[o + 2])
                if my_rgb == theirs_rgb:
                    continue
                if my_rgb not in first_palette and theirs_rgb == (0, 0, 0):
                    excused_pixels += 1
                    if n not in diff_tiles:
                        diff_tiles.append(n)
                else:
                    bad_pixels += 1
                    tile_bad = True
        if tile_bad:
            log(f"    絵 {n} の色が違います")
    log(f"[{lang}] 画素: BMP {width}x{height} / 自作 {len(tiles)} 個（{len(tiles) * TILE_X * TILE_Y} 画素）を比べて、"
        f"食い違い {bad_pixels} 画素。ほかに原作の BMP の癖で黒になっている画素 {excused_pixels}（絵 {diff_tiles}）")
    if bad_pixels:
        ok = False
    return ok


def main() -> int:
    ap = argparse.ArgumentParser(description="NetHack の原作のタイルの絵から tiles.png と tiles.json を作る")
    ap.add_argument("--lang", choices=["en", "ja", "both"], default="both")
    ap.add_argument("--source-root", type=Path,
                    default=Path(os.environ.get("HENGBAND_CORE_SOURCE_ROOT", DEFAULT_SOURCES)),
                    help="tools/core_sources/prepare.py の出力先（nethack/・nethackjp/ を含む）")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT, help="出力先の根。<out>/<言語>/ に置く（既定 nethack/data）")
    ap.add_argument("--names", action="store_true", help="絵の番号ごとの名前の一覧 tile_names.json も作る（確かめ用）")
    ap.add_argument("--verify-original", action="store_true",
                    help="原作の tilemap・tile2bmp を MSVC で組んで走らせ、表と画素を突き合わせる（<out>/verify/ を使う）")
    args = ap.parse_args()
    langs = ["en", "ja"] if args.lang == "both" else [args.lang]
    source_root = args.source_root.resolve()
    out_root = args.out.resolve()
    log = print
    ok = True
    try:
        for lang in langs:
            mine = build_one(lang, source_root, out_root, args.names, log)
            if args.verify_original:
                if not Path(VCVARS).exists():
                    raise TileError(f"vcvars64.bat がありません: {VCVARS}")
                ok = verify_original(lang, source_root, out_root / "verify", mine, log) and ok
    except TileError as e:
        print(f"エラー: {e}", file=sys.stderr)
        return 1
    if args.verify_original:
        print("原作の道具との突き合わせ: " + ("すべて一致" if ok else "不一致あり"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
