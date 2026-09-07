#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""**どの色が何に使われているか**を材質ごとにまとめて出す。

2026-08-23 に決めた:「まずは各オブジェクトごとにどの色が何に使われているのかを
確認し、その情報を持つようにして。そのうえで、材質ごとに汚しをかけるか否か決める。」

情報そのものは**プレハブごとの `.jsonc` の `palette`**に入っている（`gen_prefabs.py` が焼く）。
ここはそれを**人が読んで決められる形**に畳むだけの道具である。

    python tools/voxel/report_palette_uses.py

出るもの。

  * 材質ごとの色数・使っているプレハブ数
  * 色ごとの代表的な用途（最大 3 件）と、それを使うプレハブの例
  * **用途が分からない色**（宣言はあるが註釈が採れなかったもの）

`--material stone` で 1 材質だけ、`--stdout` で書かずに画面へ。
"""
from __future__ import annotations

import argparse
import json
import re
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VOXEL_DIR = ROOT / 'assets' / 'voxel'
OUT = ROOT / 'docs' / ''

#: `hd2d/render/surface_wear.h` の `MaterialClass` と同じ並び。
ORDER = ['stone', 'soil', 'wood', 'metal', 'plaster', 'fabric', 'plant', 'leaf', 'clean', 'default']

LABEL = {
    'stone': '石（切石・敷石・瓦・煉瓦）',
    'soil': '土（砂・灰・炭）',
    'wood': '木（材木・板・樹皮）',
    'metal': '金（鉄・鋼・鉛・錆）',
    'plaster': '漆喰（紙・障子・骨・大理石）',
    'fabric': '布（幟・藁・縄・毛）',
    'plant': '草木（草・苔・蔓・花）',
    'leaf': '木の葉',
    'clean': '水・炎・光・硝子（**汚さない**）',
    'default': '推定できなかった色',
}


def read_palettes():
    """`.jsonc` の `palette` を全部読む。"""
    books = {}
    for path in sorted(VOXEL_DIR.glob('*.jsonc')):
        try:
            body = json.loads(re.sub(r'^\s*//.*$', '', path.read_text(encoding='utf-8'), flags=re.M))
        except (ValueError, OSError):
            continue
        entries = body.get('palette')
        if entries:
            books[path.stem] = entries
    return books


def collect(books):
    """材質 → 色 → {用途, 使っているプレハブ} に畳む。"""
    by_material = defaultdict(lambda: defaultdict(lambda: {'uses': [], 'books': [], 'rgb': ''}))
    for book, entries in books.items():
        for entry in entries:
            material = entry.get('material', 'default')
            name = entry.get('name') or ('#' + entry.get('rgb', '??????'))
            slot = by_material[material][name]
            slot['rgb'] = entry.get('rgb', '')
            slot['books'].append(book)
            use = entry.get('use')
            if use and (use not in slot['uses']):
                slot['uses'].append(use)
    return by_material


def render(by_material, only=None):
    lines = []
    lines.append('# 色の使われ方（材質ごと）')
    lines.append('')
    lines.append('**この文書は焼いたものである**（`tools/voxel/report_palette_uses.py`）。手で直さない。')
    lines.append('元は各プレハブの `.jsonc` の `palette`（`tools/voxel/gen_prefabs.py` が焼く）。')
    lines.append('')
    lines.append('材質ごとに**汚しを掛けるか否か**を決めるための一覧である')
    lines.append('（2026-08-23 に決めた）。掛け方の実装は `hd2d/render/surface_wear.h`。')
    lines.append('')
    total_books = len({b for slots in by_material.values() for s in slots.values() for b in s['books']})
    lines.append('| 材質 | 色数 | 使っているプレハブ |')
    lines.append('|---|---:|---:|')
    for material in ORDER:
        slots = by_material.get(material)
        if not slots:
            continue
        books = {b for s in slots.values() for b in s['books']}
        lines.append('| {} | {} | {} |'.format(LABEL.get(material, material), len(slots), len(books)))
    lines.append('')
    lines.append('プレハブは全部で {} 個。'.format(total_books))
    lines.append('')
    for material in ORDER:
        slots = by_material.get(material)
        if not slots or (only and (material != only)):
            continue
        lines.append('## {}（`{}`）'.format(LABEL.get(material, material), material))
        lines.append('')
        lines.append('| 色 | RGB | プレハブ数 | 何に使っているか |')
        lines.append('|---|---|---:|---|')
        for name in sorted(slots, key=lambda k: -len(slots[k]['books'])):
            slot = slots[name]
            uses = ' / '.join(slot['uses'][:3]) if slot['uses'] else '**（分からない）**'
            example = slot['books'][0] if slot['books'] else ''
            lines.append('| `{}` | `{}` | {} | {}<br><sub>例: {}</sub> |'.format(
                name, slot['rgb'], len(slot['books']), uses.replace('|', '\\|'), example))
        lines.append('')
    return '\n'.join(lines) + '\n'


def main(argv=None):
    ap = argparse.ArgumentParser(description='色の使われ方を材質ごとに出す')
    ap.add_argument('--material', help='1 材質だけ出す（stone / wood / …）')
    ap.add_argument('--stdout', action='store_true', help='書かずに画面へ')
    args = ap.parse_args(argv)

    books = read_palettes()
    if not books:
        print('`.jsonc` に palette がありません。先に tools/voxel/gen_prefabs.py を回してください。')
        return 1
    text = render(collect(books), args.material)
    if args.stdout:
        print(text)
        return 0
    #: 改行は LF で書く（記憶 `hengband-python-edit-flips-line-endings`）。
    OUT.write_bytes(text.encode('utf-8'))
    print('焼いた: {}（プレハブ {} 個）'.format(OUT.relative_to(ROOT), len(books)))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
