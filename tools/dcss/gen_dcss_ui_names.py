#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""DCSS の画面の部品（`hd2d/dcss/`）が使う日本語の名前の表を作る。

ウェブタイル版の `player` メッセージは種族・神・場所を**英語のまま**送る（`tileweb.cc` の `_send_player`）。
原作のタイル版のステータス欄は日本語版の辞書（`dat/database/ja/jtrans/`）で訳して描くので、同じ訳を表にして画面の側へ持つ。

    python tools/dcss/gen_dcss_ui_names.py [--source <原作の crawl-ref/source>] [--out hd2d/dcss/dcss_ui_names_ja.inc]

出どころ（辞書のファイルと見出し）:
  - 種族   data-species.txt   `[spec name]<英語>`
  - 神     data-gods.txt      見出しなし（`<英語>` だけ）
  - 場所   data-branches.txt  `[branch]<英語>`（短い名前・長い名前・略称の全部）
辞書の形は `%%%%` の区切りのあとに「見出し・空行・訳」の並び。
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE = REPO.parent / "roguelike-cores" / "build-sources" / "dcss" / "crawl-ref" / "source"


def read_entries(path: Path):
    text = path.read_text(encoding="utf-8")
    for block in text.split("%%%%"):
        lines = [l for l in block.strip("\n").split("\n") if not l.startswith("#")]
        lines = [l for l in lines]
        while lines and not lines[0].strip():
            lines.pop(0)
        if not lines:
            continue
        key = lines[0]
        value = "\n".join(l for l in lines[1:]).strip()
        if value:
            yield key, value


def collect(path: Path, tag: str | None):
    out = {}
    for key, value in read_entries(path):
        if tag is None:
            if key.startswith("["):
                continue
            out[key] = value
        elif key.startswith(tag):
            out[key[len(tag):]] = value
    return out


def emit(name: str, table: dict[str, str]) -> str:
    rows = "\n".join(f'    {{ "{k}", "{v}" }},' for k, v in sorted(table.items()))
    return f"inline constexpr DcssNameEntry {name}[] = {{\n{rows}\n}};\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", default=str(DEFAULT_SOURCE))
    ap.add_argument("--out", default=str(REPO / "hd2d" / "dcss" / "dcss_ui_names_ja.inc"))
    args = ap.parse_args()
    jt = Path(args.source) / "dat" / "database" / "ja" / "jtrans"
    species = collect(jt / "data-species.txt", "[spec name]")
    gods = collect(jt / "data-gods.txt", None)
    branches = collect(jt / "data-branches.txt", "[branch]")
    body = ("// 自動生成: tools/dcss/gen_dcss_ui_names.py（原作の日本語版の辞書 dat/database/ja/jtrans/ から）。手で直さない。\n"
            "// 種族・神・場所の英語の名前 → 日本語。画面の部品（hd2d/dcss/）がステータス欄に使う。\n"
            + emit("kDcssSpeciesNamesJa", species) + "\n"
            + emit("kDcssGodNamesJa", gods) + "\n"
            + emit("kDcssBranchNamesJa", branches))
    Path(args.out).write_text(body, encoding="utf-8", newline="\n")
    print(f"種族 {len(species)} 件・神 {len(gods)} 件・場所 {len(branches)} 件 -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
