# -*- coding: utf-8 -*-
"""DCSS のタイルの画像ページ（main・player）と、タイルごとの切り出しの表を作る。

アダプタ（`dcss/adapter/dcss_slab.cpp`）が実行時に、原作が送るタイル番号（`t.fg`・`t.doll`・`t.mcache`）から
絵を取り出して板にするための材料。**原作のタイルの絵は 8,000 枚を超え、番号ごとに 1 枚ずつ配るのは重い**ので、
原作のタイル版が使うのと同じ画像ページ（`tilegen` が作る `main.png`・`player.png`）をそのまま持つ。

出力（`--out`、既定 `dcss/assets/atlas`）:
  - `main.png`     … アイテム・死体・効果などのページ（タイル番号 TILE_FEAT_MAX 〜 TILE_MAIN_MAX - 1）
  - `player.png`   … 怪物・プレイヤーの人形の部品のページ（TILE_MAIN_MAX 〜 TILEP_PLAYER_MAX - 1）
  - `tiles.tsv`    … 1 行目から `#` の行は定数（`const <名前> <値>`）。それ以降は 1 タイル 1 行
                     `<番号> <ページ m|p> <w> <h> <ox> <oy> <sx> <sy> <ex> <ey>`（原作の `tile_info`）

なぜ作り直すか: 原作の組み立ては `USE_TILE` 無しの `tilegen`（画像を読まない）で番号の表だけ作るので、`tile_info`（ページ内の位置）が
全部 0 になる。ここでは `USE_TILE` 付き（libpng）の `tilegen` を**このツリーの外の作業フォルダ**で別に組み、
画像のページと位置の表を作る。**原作の写しには書き込まない**（作業フォルダへ複製して使う）。番号は組み立てに使った表と
同じでなければならないので、作った `tiledef-*.h` が `<gen>/rltiles` の物とバイト単位で一致することを確かめる。

`tilegen`（原作 `rltiles/tool`）を作業用の複製で直す点（原作の行は書き換えない）:
  1. 画像ページを PNG で書く所（`tile_page.cc` の `write_png`）を、素の RGBA の書き出しに替える。新しい libpng（1.6.58）では
     原作の `write_png` が `png_write_png` のあとにもう一度 `png_write_end` を呼ぶため異常終了する。
  2. 入力の PNG は、付属の情報（`iCCP`・`eXIf` など）を外した RGBA に直してから読ませる（同じ libpng で、`iCCP` を持つ絵を読むと異常終了する）。
     画素は 1 つも変えない。

画面（`hd2d/dcss/dcss_tabs.*`）が使う画像ページ（`--ui-out`。段 U2）:
  タブの欄の持ち物・呪文・能力・技能・命令・怪物の絵は、原作のタイル版が使うのと同じ画像ページから切り出して描く。
  main・player に加え、床（floor）・壁（wall）・地形（feat）・画面の部品（gui。呪文・能力・技能・命令の絵）・アイコン（icons。数字・網・カーソル）を
  作り、全部のタイル番号（原作は 0 から TILEI_ICONS_MAX まで 1 つの番号の空間）を 1 つの表 `tiles.tsv` にまとめる。
  ページの記号: f floor / w wall / e feat / m main / p player / g gui / i icons。
  作る先は `assets/ui/dcss/atlas/`（画面は実行ファイルの隣の `assets/` から読む）。コアが使う `dcss/assets/atlas/` の物は変えない。

要る物: MSVC（vcvars64.bat）・vcpkg の libpng と zlib（`C:\\vcpkg\\installed\\x64-windows`）・Pillow・
`dcss/adapter/prepare_gen.py` の作業フォルダ（`DCSS_WORK`。原作の複製と生成物が入っている）。

使い方:
  python -u tools/dcss/dcss_tile_atlas.py                      # 作る（作業フォルダは Temp の短い道）
  python -u tools/dcss/dcss_tile_atlas.py --check              # 追跡している物と作り直した物が一致するか
"""
from __future__ import annotations

import argparse
import hashlib
import os
import tempfile
import re
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

REPO = Path(__file__).resolve().parents[2]
DEFAULT_GEN_WORK = Path(os.environ.get("DCSS_WORK", os.path.join(tempfile.gettempdir(), "dcss_w")))
DEFAULT_ATLAS_WORK = Path(os.environ.get("DCSS_ATLAS_WORK", os.path.join(tempfile.gettempdir(), "dcss_atlas")))
DEFAULT_VCVARS = Path(r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat")
DEFAULT_VCPKG = Path(r"C:\vcpkg\installed\x64-windows")
DEFAULT_OUT = REPO / "dcss" / "assets" / "atlas"

PAGES = ("main", "player")  # ページの名前（dc-main.txt・dc-player.txt）
PAGE_CODE = {"main": "m", "player": "p"}
# 画面（hd2d/dcss/）が使うページ。番号は原作の 1 つの空間: floor 0〜 / wall / feat（〜TILE_FEAT_MAX）/ main / player / gui / icons。
UI_PAGES = ("floor", "wall", "feat", "main", "player", "gui", "icons")
UI_PAGE_CODE = {"floor": "f", "wall": "w", "feat": "e", "main": "m", "player": "p", "gui": "g", "icons": "i"}
DEFAULT_UI_OUT = REPO / "assets" / "ui" / "dcss" / "atlas"


def run(cmd, cwd, env=None, shell=False):
    r = subprocess.run(cmd, cwd=str(cwd), env=env, shell=shell, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if r.returncode != 0:
        raise SystemExit(f"失敗（終了コード {r.returncode}）: {cmd}\n{r.stdout[-2000:]}\n{r.stderr[-2000:]}")
    return r.stdout


def sanitize_pngs(root: Path) -> int:
    """作業用の複製の PNG を、付属の情報なしの RGBA に直す（画素は変えない）。"""
    n = 0
    for dp, _d, fs in os.walk(root):
        for f in fs:
            if not f.lower().endswith(".png"):
                continue
            p = Path(dp) / f
            try:
                im = Image.open(p).convert("RGBA")
            except Exception:  # 空のページなど（tilegen が前に書いた 0 バイトの物）
                p.unlink()
                continue
            Image.frombytes("RGBA", im.size, im.tobytes()).save(p)
            n += 1
    return n


PAGE_PATCH_OLD = "bool success = write_png(filename, pixels, m_width, m_height);"
PAGE_PATCH_NEW = (
    'bool success = false; { std::string fn = std::string(filename) + ".rgba"; FILE *rf = fopen(fn.c_str(), "wb");'
    " if (rf) { int hd[2] = { m_width, m_height }; fwrite(hd, sizeof(int), 2, rf); fwrite(pixels, 4, (size_t)m_width * m_height, rf);"
    " fclose(rf); success = true; } }"
)


def build_tilegen(src_tool: Path, work: Path, vcvars: Path, vcpkg: Path) -> Path:
    tg = work / "tg"
    exe = tg / "tilegen_png.exe"
    if exe.is_file():
        return exe
    if tg.exists():
        shutil.rmtree(tg)
    tg.mkdir(parents=True)
    for f in ("main.cc", "tile.cc", "tile_colour.cc", "tile_list_processor.cc", "tile_page.cc",
              "tile.h", "tile_colour.h", "tile_list_processor.h", "tile_page.h"):
        shutil.copy2(src_tool / f, tg / f)
    page = (tg / "tile_page.cc").read_text(encoding="utf-8")
    if PAGE_PATCH_OLD not in page:
        raise SystemExit("tile_page.cc に直す箇所が無い（原作の版が替わった）")
    (tg / "tile_page.cc").write_text(page.replace(PAGE_PATCH_OLD, PAGE_PATCH_NEW), encoding="utf-8")
    (tg / "obj").mkdir()
    bat = tg / "build.bat"
    bat.write_text(
        "@echo off\r\ncall \"%s\" >nul 2>&1\r\ncd /d \"%s\"\r\n"
        "cl /nologo /O2 /EHsc /std:c++17 /DUSE_TILE /D_CRT_SECURE_NO_WARNINGS /utf-8 /I\"%s\" "
        "main.cc tile.cc tile_colour.cc tile_list_processor.cc tile_page.cc /Fe:tilegen_png.exe /Fo:obj\\ "
        "/link /LIBPATH:\"%s\" libpng16.lib z.lib\r\n" % (vcvars, tg, vcpkg / "include", vcpkg / "lib"),
        encoding="ascii")
    run(["cmd", "/c", str(bat)], tg)
    if not exe.is_file():
        raise SystemExit("tilegen_png.exe ができない")
    for dll in ("libpng16.dll", "z.dll"):
        shutil.copy2(vcpkg / "bin" / dll, tg / dll)
    return exe


def parse_enum(path: Path, names: dict[str, int]) -> None:
    val = 0
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("//")[0].strip().rstrip(",")
        m = re.match(r"(\w+)\s*(?:=\s*(\w+))?$", line)
        if not m or line.startswith("enum") or line in ("{", "}"):
            continue
        n, v = m.group(1), m.group(2)
        if v is not None:
            val = int(v) if v.isdigit() else names[v]
        names[n] = val
        val += 1


def parse_infos(cc: Path) -> list[tuple[int, ...]]:
    text = cc.read_text(encoding="utf-8")
    return [tuple(int(x) for x in m) for m in re.findall(r"tile_info\((-?\d+), (-?\d+), (-?\d+), (-?\d+), (-?\d+), (-?\d+), (-?\d+), (-?\d+)\),", text)]


def make(work: Path, gen_work: Path, out: Path, vcvars: Path, vcpkg: Path, refresh: bool, ui_out: Path | None = None) -> dict[str, str]:
    src_rl = gen_work / "mirror" / "crawl-ref" / "source" / "rltiles"
    gen_rl = gen_work / "gen" / "rltiles"
    if not (src_rl / "dc-unrand.txt").is_file() or not (gen_rl / "tiledef-main.h").is_file():
        raise SystemExit(f"prepare_gen.py の作業フォルダが無い: {gen_work}（python dcss/adapter/prepare_gen.py --work ... を先に）")
    work.mkdir(parents=True, exist_ok=True)
    rl = work / "rl"
    if refresh and rl.exists():
        shutil.rmtree(rl)
    if not rl.exists():
        print("複製: %s -> %s" % (src_rl, rl), flush=True)
        shutil.copytree(src_rl, rl, ignore=shutil.ignore_patterns("tiledef-*", "tile-*.html", "tileinfo-*.js", "*.d", "*.png.rgba", "dngn.png"))
        print("PNG を整える: %d 枚" % sanitize_pngs(rl), flush=True)
    exe = build_tilegen(src_rl / "tool", work, vcvars, vcpkg)
    env = dict(os.environ)
    env["PATH"] = str(exe.parent) + os.pathsep + env["PATH"]
    for page in PAGES:
        (rl / f"{page}.png.rgba").unlink(missing_ok=True)
        run([str(exe), f"dc-{page}.txt"], rl, env)
    # 番号が組み立てに使った表と同じか（ヘッダがバイト単位で一致）
    for name in ("main", "player"):
        a = (rl / f"tiledef-{name}.h").read_bytes()
        b = (gen_rl / f"tiledef-{name}.h").read_bytes()
        if a != b:
            raise SystemExit(f"tiledef-{name}.h が組み立ての物と違う。番号がずれている")
    names: dict[str, int] = {}
    for name in ("floor", "wall", "feat", "main", "player"):
        parse_enum(gen_rl / f"tiledef-{name}.h", names)
    feat_max, main_max, player_max = names["TILE_FEAT_MAX"], names["TILE_MAIN_MAX"], names["TILEP_PLAYER_MAX"]
    consts = {
        "TILE_FEAT_MAX": feat_max,
        "TILE_MAIN_MAX": main_max,
        "TILEP_PLAYER_MAX": player_max,
        "TILEP_PLAYER": names["TILEP_PLAYER"],
        "TILEP_MCACHE_START": names["TILEP_MCACHE_START"],
        "TILEP_MONS_UNKNOWN": names["TILEP_MONS_UNKNOWN"],
        "TILE_PARCHMENT_LOW": names.get("TILE_PARCHMENT_LOW", 0),
        "TILE_PARCHMENT_HIGH": names.get("TILE_PARCHMENT_HIGH", 0),
    }
    out.mkdir(parents=True, exist_ok=True)
    rows: list[str] = []
    for page, start, end in (("main", feat_max, main_max), ("player", main_max, player_max)):
        infos = parse_infos(rl / f"tiledef-{page}.cc")
        if len(infos) != end - start:
            raise SystemExit(f"{page}: tile_info の数が合わない {len(infos)} != {end - start}")
        raw = (rl / f"{page}.png.rgba").read_bytes()
        w, h = int.from_bytes(raw[0:4], "little"), int.from_bytes(raw[4:8], "little")
        im = Image.frombytes("RGBA", (w, h), raw[8:])
        im.save(out / f"{page}.png", optimize=True)
        kept = 0
        for i, (tw, th, ox, oy, sx, sy, ex, ey) in enumerate(infos):
            if tw == 0 and th == 0:
                continue
            rows.append(f"{start + i}\t{PAGE_CODE[page]}\t{tw}\t{th}\t{ox}\t{oy}\t{sx}\t{sy}\t{ex}\t{ey}")
            kept += 1
        print(f"{page}.png {w}x{h} / タイル {kept} / {(out / (page + '.png')).stat().st_size} バイト", flush=True)
    lines = ["# DCSS タイルの切り出しの表。tools/dcss/dcss_tile_atlas.py が作る（手で直さない）。",
             "# const <名前> <値> / 以降 <番号> <ページ m|p> <w> <h> <ox> <oy> <sx> <sy> <ex> <ey>（原作の tile_info）"]
    lines += [f"const {k} {v}" for k, v in consts.items()]
    lines += rows
    (out / "tiles.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    if ui_out is not None:
        make_ui(rl, gen_rl, exe, env, names, ui_out)
        write_core_icons(ui_out, out, names, gen_rl)
    return {p.name: hashlib.sha1(p.read_bytes()).hexdigest() for p in (out / "tiles.tsv", out / "main.png", out / "player.png")}


#: 画面が名前で引くタイル番号（数字・網・カーソル・枠・ページ送り・地図の上の印）。`tiles.tsv` の `const` の行になる。
UI_WANTED = [
    "TILEI_CURSOR", "TILEI_MESH", "TILEI_NUM0", "TILEI_ITEM_SLOT_SELECTED", "TILEI_NUM_MINUS5", "TILEI_NUM_ZERO", "TILEI_NUM_PLUS1", "TILEI_NUM_PLUS11",
    "TILE_ITEM_SLOT", "TILE_ITEM_SLOT_EQUIP", "TILE_ITEM_SLOT_CURSED", "TILE_ITEM_SLOT_EQUIP_CURSED", "TILE_ITEM_SLOT_VEHUMET",
    "TILE_ITEM_PAGE_NEXT", "TILE_ITEM_PAGE_PREVIOUS", "TILE_MODIFIER_CHAOTIC",
    "TILE_HALO_FRIENDLY", "TILE_HALO_GD_NEUTRAL", "TILE_HALO_NEUTRAL",
    # 地図の上の印（段 3。`hd2d/dcss/dcss_marks.cpp`。原作 `cell_renderer.js` の前景のアイコン）
    "TILEI_TRAP_NET", "TILEI_TRAP_WEB", "TILEI_SOMETHING_UNDER", "TILEI_FRIENDLY", "TILEI_GOOD_NEUTRAL", "TILEI_NEUTRAL",
    "TILEI_PARALYSED", "TILEI_STAB_BRAND", "TILEI_UNAWARE", "TILEI_FLEEING", "TILEI_POISON", "TILEI_MORE_POISON", "TILEI_MAX_POISON",
    "TILEI_MDAM_LIGHTLY_DAMAGED", "TILEI_MDAM_MODERATELY_DAMAGED", "TILEI_MDAM_HEAVILY_DAMAGED", "TILEI_MDAM_SEVERELY_DAMAGED", "TILEI_MDAM_ALMOST_DEAD",
    "TILEI_RAMPAGE", "TILEI_CONFUSED", "TILEI_SLOWED", "TILEI_HASTED",
    "TILEI_DEMON_NUM1", "TILEI_DEMON_NUM2", "TILEI_DEMON_NUM3", "TILEI_DEMON_NUM4", "TILEI_DEMON_NUM5",
]


def ui_const_lines(names: dict[str, int], consts: dict[str, int]) -> list[str]:
    out = [f"const {k} {v}" for k, v in consts.items()]
    out += [f"const {k} {names[k]}" for k in UI_WANTED if k in names]
    return out


def write_status_icon_sizes(gen_rl: Path, names: dict[str, int], ui_out: Path) -> int:
    """原作の `status_icon_size`（`rltiles/status-icon-sizes.h`）を `status_icons.tsv`（`<タイル番号>	<幅>`）に写す。表に無い番号は -1（描かない）。"""
    text = (gen_rl / "status-icon-sizes.h").read_text(encoding="utf-8")
    rows: list[tuple[int, int]] = []
    pending: list[str] = []
    for line in text.splitlines():
        m = re.match(r"\s*case (TILEI_\w+):", line)
        if m:
            pending.append(m.group(1))
            continue
        m = re.match(r"\s*return (-?\d+);", line)
        if m and pending:
            rows += [(names[n], int(m.group(1))) for n in pending]
            pending = []
    rows.sort()
    lines = ["# 状態のアイコンの幅（原作 rltiles/status-icon-sizes.h の status_icon_size）。<タイル番号> <幅>。0 は位置をずらさず重ねる、表に無い番号は描かない。",
             "# tools/dcss/dcss_tile_atlas.py（--ui-out / --marks-data-only）が作る（手で直さない）。"]
    lines += [f"{n}\t{w}" for n, w in rows]
    (ui_out / "status_icons.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    return len(rows)


def write_core_icons(ui_out: Path, core_out: Path, names: dict[str, int], gen_rl: Path) -> int:
    """コアのアダプタが怪物の板に焼き込む状態のアイコンの材料（段 3。`dcss/adapter/dcss_slab.h`）を、画面用の表から作る。

    `core_out`（`dcss/assets/atlas`）に `icons.png`（アイコンのページ。画面用と同じ絵）・`icons.tsv`（`const` と切り出しの行。ページの記号は i）・
    `status_icons.tsv`（状態のアイコンの幅）を作る。main・player の表（`tiles.tsv`）には触らない（`--check` の一致を保つ）。
    """
    consts = {k: names[k] for k in ("TILEG_GUI_MAX", "TILEI_ICONS_MAX")}
    rows = [l for l in (ui_out / "tiles.tsv").read_text(encoding="utf-8").splitlines() if l and not l.startswith(("#", "const ")) and l.split("\t")[1] == "i"]
    lines = ["# DCSS のアイコンのページの切り出しの表（コア用。怪物の板に焼き込む）。tools/dcss/dcss_tile_atlas.py が作る（手で直さない）。",
             "# const <名前> <値> / 以降 <番号> i <w> <h> <ox> <oy> <sx> <sy> <ex> <ey>（原作の tile_info）"]
    lines += ui_const_lines(names, consts)
    lines += rows
    core_out.mkdir(parents=True, exist_ok=True)
    (core_out / "icons.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    shutil.copyfile(ui_out / "icons.png", core_out / "icons.png")
    write_status_icon_sizes(gen_rl, names, core_out)
    return len(rows)


def marks_data_only(gen_rl: Path, ui_out: Path, core_out: Path) -> None:
    """既にある `tiles.tsv` の切り出しの行は変えず、`const` の行と `status_icons.tsv` だけ作り直す（画像ページの再生成を伴わない）。"""
    names: dict[str, int] = {}
    for name in ("floor", "wall", "feat", "main", "player", "gui", "icons"):
        parse_enum(gen_rl / f"tiledef-{name}.h", names)
    consts = {k: names[k] for k in ("TILE_FLOOR_MAX", "TILE_WALL_MAX", "TILE_FEAT_MAX", "TILE_MAIN_MAX", "TILEP_PLAYER_MAX", "TILEG_GUI_MAX", "TILEI_ICONS_MAX")}
    path = ui_out / "tiles.tsv"
    old = path.read_text(encoding="utf-8").splitlines()
    head = [l for l in old if l.startswith("#")]
    rows = [l for l in old if l and not l.startswith("#") and not l.startswith("const ")]
    path.write_text("\n".join(head + ui_const_lines(names, consts) + rows) + "\n", encoding="utf-8", newline="\n")
    print(f"tiles.tsv の const を作り直した / status_icons.tsv {write_status_icon_sizes(gen_rl, names, ui_out)} 行")
    print(f"コア用 icons.tsv {write_core_icons(ui_out, core_out, names, gen_rl)} 行（{core_out}）")


def make_ui(rl: Path, gen_rl: Path, exe: Path, env: dict, names: dict[str, int], ui_out: Path) -> None:
    """画面が使う全ページ（UI_PAGES）の画像と、1 つにまとめた切り出しの表を作る。"""
    for name in ("gui", "icons"):
        parse_enum(gen_rl / f"tiledef-{name}.h", names)
    ui_out.mkdir(parents=True, exist_ok=True)
    consts = {k: names[k] for k in ("TILE_FLOOR_MAX", "TILE_WALL_MAX", "TILE_FEAT_MAX", "TILE_MAIN_MAX", "TILEP_PLAYER_MAX", "TILEG_GUI_MAX", "TILEI_ICONS_MAX")}
    rows: list[str] = []
    bounds = {
        "floor": (0, consts["TILE_FLOOR_MAX"]),
        "wall": (consts["TILE_FLOOR_MAX"], consts["TILE_WALL_MAX"]),
        "feat": (consts["TILE_WALL_MAX"], consts["TILE_FEAT_MAX"]),
        "main": (consts["TILE_FEAT_MAX"], consts["TILE_MAIN_MAX"]),
        "player": (consts["TILE_MAIN_MAX"], consts["TILEP_PLAYER_MAX"]),
        "gui": (consts["TILEP_PLAYER_MAX"], consts["TILEG_GUI_MAX"]),
        "icons": (consts["TILEG_GUI_MAX"], consts["TILEI_ICONS_MAX"]),
    }
    for page in UI_PAGES:
        (rl / f"{page}.png.rgba").unlink(missing_ok=True)
        run([str(exe), f"dc-{page}.txt"], rl, env)
        a = (rl / f"tiledef-{page}.h").read_bytes()
        b = (gen_rl / f"tiledef-{page}.h").read_bytes()
        if a != b:
            raise SystemExit(f"tiledef-{page}.h が組み立ての物と違う。番号がずれている")
        start, end = bounds[page]
        infos = parse_infos(rl / f"tiledef-{page}.cc")
        if len(infos) != end - start:
            raise SystemExit(f"{page}: tile_info の数が合わない {len(infos)} != {end - start}")
        raw = (rl / f"{page}.png.rgba").read_bytes()
        w, h = int.from_bytes(raw[0:4], "little"), int.from_bytes(raw[4:8], "little")
        Image.frombytes("RGBA", (w, h), raw[8:]).save(ui_out / f"{page}.png", optimize=True)
        kept = 0
        for i, (tw, th, ox, oy, sx, sy, ex, ey) in enumerate(infos):
            if tw == 0 and th == 0:
                continue
            rows.append(f"{start + i}\t{UI_PAGE_CODE[page]}\t{tw}\t{th}\t{ox}\t{oy}\t{sx}\t{sy}\t{ex}\t{ey}")
            kept += 1
        print(f"[ui] {page}.png {w}x{h} / タイル {kept}", flush=True)
    # 名前つきの番号（画面が直に使うもの。数字・網・カーソル・枠・ページ送り・地図の上の印など）
    lines = ["# DCSS タイルの切り出しの表（画面用。全ページ）。tools/dcss/dcss_tile_atlas.py --ui-out が作る（手で直さない）。",
             "# const <名前> <値> / 以降 <番号> <ページ f w e m p g i> <w> <h> <ox> <oy> <sx> <sy> <ex> <ey>（原作の tile_info）"]
    lines += ui_const_lines(names, consts)
    lines += rows
    (ui_out / "tiles.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--work", default=str(DEFAULT_ATLAS_WORK), help="作業フォルダ（短い道。原作の最長のパスが 118 文字）")
    ap.add_argument("--gen-work", default=str(DEFAULT_GEN_WORK), help="prepare_gen.py の作業フォルダ")
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    ap.add_argument("--vcvars", default=str(DEFAULT_VCVARS))
    ap.add_argument("--vcpkg", default=str(DEFAULT_VCPKG))
    ap.add_argument("--refresh", action="store_true", help="作業用の複製を作り直す")
    ap.add_argument("--ui-out", nargs="?", const=str(DEFAULT_UI_OUT), default=None, help="画面が使う全ページ（floor・wall・feat・main・player・gui・icons）と表を作る先（既定 assets/ui/dcss/atlas）")
    ap.add_argument("--marks-data-only", action="store_true", help="--ui-out の先の tiles.tsv の const の行と status_icons.tsv だけ作り直す（tilegen を回さない）")
    ap.add_argument("--check", action="store_true", help="作り直した物が --out の物と一致するかだけ見る（--out は書かない）")
    args = ap.parse_args()
    out = Path(args.out)
    if args.marks_data_only:
        marks_data_only(Path(args.gen_work) / "gen" / "rltiles", Path(args.ui_out) if args.ui_out else DEFAULT_UI_OUT, out)
        return 0
    if args.check:
        tmp = Path(args.work) / "check_out"
        if tmp.exists():
            shutil.rmtree(tmp)
        got = make(Path(args.work), Path(args.gen_work), tmp, Path(args.vcvars), Path(args.vcpkg), False)
        bad = [n for n, h in got.items() if not (out / n).is_file() or hashlib.sha1((out / n).read_bytes()).hexdigest() != h]
        print("一致" if not bad else "食い違い: " + ", ".join(bad))
        return 1 if bad else 0
    got = make(Path(args.work), Path(args.gen_work), out, Path(args.vcvars), Path(args.vcpkg), args.refresh, Path(args.ui_out) if args.ui_out else None)
    for n, h in got.items():
        print(f"  {n}  {h}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
