#!/usr/bin/env python3
"""NetHack の地形の表 nethack/adapter/nh_terrain.cpp を、原作の defsym.h と変愚の地形定義に突き合わせる。

確かめること:
  1. 英語版（nethack）と日本語版（nethackjp）の defsym.h の PCHAR が、番号 0 から途切れず、記号の名前も順も同じ。
  2. 表の各行の頭の註釈（番号と記号の名前）が、defsym.h の PCHAR の番号と名前に一致し、行の数も同じ。
  3. nh_terrain.h の kCmapCount が記号の数に等しい。
  4. nh_terrain.cpp の変愚の地形の定数（番号と末尾の key）が lib/edit/TerrainDefinitions.jsonc の id と key に一致する。

原作（roguelike-cores）は読むだけで、書き込まない。
使い方: python tools/nethack/check_terrain_table.py [--cores <build-sources のフォルダ>] [--table <表の cpp>]
終了コード: 通れば 0、食い違いがあれば 1。
"""

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEFAULT_CORES = REPO.parent / "roguelike-cores" / "build-sources"
TABLE_CPP = REPO / "nethack" / "adapter" / "nh_terrain.cpp"
TABLE_H = REPO / "nethack" / "adapter" / "nh_terrain.h"
TERRAIN_JSONC = REPO / "lib" / "edit" / "TerrainDefinitions.jsonc"

# defsym.h の PCHAR / PCHAR2 の行頭。2 番目の引数は字（'\\' や ' ' を含む）なので、番号と名前だけを取る。
PCHAR_RE = re.compile(r"^\s*PCHAR2?\(\s*(\d+)\s*,\s*'(?:\\.|[^'\\])'\s*,\s*(S_\w+)", re.M)
ROW_RE = re.compile(r"^\s*/\*\s*(\d+)\s+(S_\w+)\s*\*/", re.M)
ENUM_RE = re.compile(r"^\s*(k\w+)\s*=\s*(\d+)\s*,\s*//\s*(\w+)\s*$", re.M)
COUNT_RE = re.compile(r"kCmapCount\s*=\s*(\d+)\s*;")


def read_defsym(path: Path) -> list[tuple[int, str]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    return [(int(i), name) for i, name in PCHAR_RE.findall(text)]


def read_terrain_keys(path: Path) -> dict[int, str]:
    # 変愚の定義は SJIS。id と key は ASCII なので、読めない字は無視してよい。
    text = path.read_text(encoding="cp932", errors="replace")
    return {int(i): key for i, key in re.findall(r'"id":\s*(\d+),\s*"key":\s*"(\w+)"', text)}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--cores", type=Path, default=DEFAULT_CORES, help="原作の build-sources のフォルダ")
    ap.add_argument("--table", type=Path, default=TABLE_CPP, help="照合する表（試験用に差し替えられる）")
    args = ap.parse_args()

    errors: list[str] = []

    # 1. 英日の defsym.h
    syms: dict[str, list[tuple[int, str]]] = {}
    for variant in ("nethack", "nethackjp"):
        path = args.cores / variant / "include" / "defsym.h"
        if not path.is_file():
            errors.append(f"{path} が読めない")
            continue
        entries = read_defsym(path)
        syms[variant] = entries
        gaps = [i for i, (num, _) in enumerate(entries) if num != i]
        if not entries:
            errors.append(f"{variant}: PCHAR が 1 つも取れない")
        elif gaps:
            errors.append(f"{variant}: 番号が 0 から連続していない（最初のずれは並びの {gaps[0]} 番目）")
        print(f"{variant}: PCHAR {len(entries)} 個（{entries[0][1]} 〜 {entries[-1][1]}）" if entries else f"{variant}: 0 個")
    if len(syms) == 2 and syms["nethack"] != syms["nethackjp"]:
        diff = [(a, b) for a, b in zip(syms["nethack"], syms["nethackjp"]) if a != b]
        errors.append(f"英語版と日本語版の PCHAR が違う（個数 {len(syms['nethack'])} / {len(syms['nethackjp'])}、最初の違い {diff[:1]}）")
    else:
        print("英語版と日本語版: 番号・名前・順が同じ")

    # 2. 表の行
    cpp = args.table.read_text(encoding="utf-8")
    rows = [(int(i), name) for i, name in ROW_RE.findall(cpp)]
    ref = syms.get("nethack", [])
    if ref:
        if len(rows) != len(ref):
            errors.append(f"表の行数 {len(rows)} が defsym.h の PCHAR の数 {len(ref)} と違う")
        for pos, (row, expect) in enumerate(zip(rows, ref)):
            if row != expect:
                errors.append(f"表の {pos} 行目が違う: 表 {row} / defsym.h {expect}")
                break
    print(f"表の行: {len(rows)} 行")

    # 3. kCmapCount
    m = COUNT_RE.search(TABLE_H.read_text(encoding="utf-8"))
    if not m:
        errors.append("nh_terrain.h に kCmapCount が見つからない")
    elif ref and int(m.group(1)) != len(ref):
        errors.append(f"kCmapCount {m.group(1)} が PCHAR の数 {len(ref)} と違う")
    else:
        print(f"kCmapCount: {m.group(1)}")

    # 4. 変愚の地形の番号
    keys = read_terrain_keys(TERRAIN_JSONC)
    consts = ENUM_RE.findall(cpp)
    if not keys:
        errors.append("TerrainDefinitions.jsonc から id と key が取れない")
    for name, num, key in consts:
        got = keys.get(int(num))
        if got != key:
            errors.append(f"{name} = {num} の key は {key} と書いてあるが、定義では {got}")
    print(f"変愚の地形の定数: {len(consts)} 個を {len(keys)} 件の定義と照合")

    if errors:
        print("\n通らない:")
        for e in errors:
            print("  - " + e)
        return 1
    print("\n通った")
    return 0


if __name__ == "__main__":
    sys.exit(main())
