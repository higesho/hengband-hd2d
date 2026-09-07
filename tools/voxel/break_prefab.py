# -*- coding: utf-8 -*-
"""プレハブの宣言を**わざと壊した**写しを作る。検査が実際に反応するかを見るための道具。


> 「検出したいものを検出できない検査は、無いより悪い。」

過去に「四隅の欠けチェックが 0.0% と報告し続けていたが、実際は下塗り色を数えておらず
**そもそも数えていなかった**」という前例がある。だから検査を書いたら、**壊れた入力を
食わせて落ちることを確かめる**までが 1 組である。

## 使い方
```
python tools\\voxel\\break_prefab.py <プレハブ名> <出力先> <壊し方>
HengbandHd2d.exe --prefab-check=<プレハブ名> --voxel-dir=<出力先>   # 落ちれば検査は生きている
```

| 壊し方 | 何を偽るか | 反応するべき検査 |
|---|---|---|
| `footprint-drop` | 接地しているマスを 1 つ宣言から**消す** | footprint 照合 |
| `footprint-add` | 接地していないマスを宣言に**足す** | footprint 照合 |
| `pivot` | ピボットをパーツの外へ出す | ピボットの範囲 |
| `cycle` | パーツの親子を輪にする | 親子の循環 |
| `offset` | `.vox` と食い違う `offset` を書く | 配置の照合 |
| `none` | 何も壊さない（**対照実験**。これは通らなければならない） | — |
"""
import json
import os
import re
import shutil
import sys


def load_jsonc(path):
    with open(path, 'r', encoding='utf-8') as fp:
        text = fp.read()
    # 行頭の `//` 注釈だけを落とす（この道具が読むのは自分たちが書いた `.jsonc` だけ）。
    stripped = '\n'.join(line for line in text.splitlines() if not line.lstrip().startswith('//'))
    return json.loads(stripped)


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    name, out_dir, mode = sys.argv[1], sys.argv[2], sys.argv[3]
    src_dir = os.path.join('assets', 'voxel')
    os.makedirs(out_dir, exist_ok=True)
    shutil.copyfile(os.path.join(src_dir, name + '.vox'), os.path.join(out_dir, name + '.vox'))

    body = load_jsonc(os.path.join(src_dir, name + '.jsonc'))

    if mode == 'footprint-drop':
        if not body['footprint']:
            print('footprint が空なので落とせない')
            return 2
        dropped = body['footprint'].pop(0)
        note = '接地しているマス %s を宣言から消した' % (dropped,)
    elif mode == 'footprint-add':
        far = [999, 999]
        body['footprint'].append(far)
        note = '接地していないマス %s を宣言に足した' % (far,)
    elif mode == 'pivot':
        body['parts'][-1]['pivot'] = [-500.0, -500.0, -500.0]
        note = 'ピボットをパーツの外（-500,-500,-500）へ出した'
    elif mode == 'cycle':
        if len(body['parts']) < 2:
            print('パーツが 1 つしかないので輪が作れない（mill を使うこと）')
            return 2
        body['parts'][0]['parent'] = body['parts'][1]['name']
        body['parts'][1]['parent'] = body['parts'][0]['name']
        note = 'パーツ %s と %s を互いの親にした' % (body['parts'][0]['name'], body['parts'][1]['name'])
    elif mode == 'offset':
        body['parts'][-1]['offset'] = [12345.0, 0.0, 0.0]
        note = '`.vox` と食い違う offset を書いた'
    elif mode == 'none':
        note = '何も壊していない（対照実験）'
    else:
        print('知らない壊し方: %s' % mode)
        return 2

    out_path = os.path.join(out_dir, name + '.jsonc')
    with open(out_path, 'w', encoding='utf-8') as fp:
        fp.write('// わざと壊した写し（tools/voxel/break_prefab.py %s）\n' % mode)
        fp.write('// %s\n' % note)
        json.dump(body, fp, ensure_ascii=False, indent=2)
        fp.write('\n')
    print('%s -> %s  (%s)' % (name, out_path, note))
    return 0


if __name__ == '__main__':
    sys.exit(main())
