#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""**色の名前 → 材質**を決める 1 か所（面の汚し・木の葉が使う）。

## なぜ 1 か所に集めるのか

材質は 2 か所で要る。

  * `gen_prefabs.py` の書き出し … プレハブごとのパレットに**材質を書き込む**（正）
  * `gen_material_table.py` … `.jsonc` に宣言の無い物のための**色 → 材質の予備表**

同じ判断を 2 本持つと、片方だけ直したときに「宣言と予備表で材質が違う」になる。

## 何を根拠にしているか

**`gen_prefabs.py` に書いてある言葉**である（2026-08-23 に決めたことの流れ）。

  1. `OVERRIDE` … 名指しの直し（いちばん強い）
  2. 名前の断片（`timber` `leaf` `iron` …）
  3. **すぐ上の註釈**（「退色した灰白の切石」「錆びた鉄（門扉・柵・棟飾り）」）

勘で当てるのではなく、木に書いてある言葉を読む（記憶 `hengband-ai-notes-are-not-rules`）。
どれでも当たらなければ `default`——**弱い汚しだけ**が掛かる方へ倒す。
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

#: `hd2d/render/surface_wear.h` の `MaterialClass` と**同じ順・同じ番号**であること。
CLASSES = ['Default', 'Stone', 'Soil', 'Wood', 'Metal', 'Plaster', 'Fabric', 'Plant', 'Clean', 'Leaf']

#: 名前の断片 → 材質。**上から順に当てる**（先に書いたものが勝つ）。
KEYWORDS = [
    # 汚さないもの（水・火・光・氷・ガラス・霧）。**最初に外す**——
    # 「炎に苔が生える」たぐいの事故は、他のどれよりも目に付く。
    ('Clean', (
        'wa', 'lava', 'fl', 'lit', 'glass', 'pane', 'snow', 'yuki', 'foam', 'spray', 'jetwa',
        'plume', 'bub', 'gas', 'cloud', 'sky', 'iris', 'slime', 'mgl', 'mim', 'nacr', 'emb',
        'crimson', 'sq_sun', 'sq_lit', 'sq_ench', 'sq_cold', 'git_hi', 'hig_okibi',
        'moy_niji', 'hig_kaza', 'sulf', 'damp', 'murk', 'dark', 'ink', 'hig_yami',
        #: FroxComposband の雪と氷・毒の液・放電（`gen_prefabs.py` の `fc_*`）。
        'fc_snow', 'fc_ice', 'fc_slush', 'fc_waste', 'fc_acid', 'fc_zap',
    )),
    # 木の葉（`Leaf`）は草・苔（`Plant`）と**別に採る**——葉には葉の形を描くが、
    # 苔に葉脈が生えると嘘になる。**`Plant` より前**に置くこと。
    ('Leaf', (
        'leaf', 'alp', 'moy_sasa', 'git_matsu', 'git_ha', 'sasa',
    )),
    ('Plant', (
        'grass', 'moss', 'lichen', 'stem', 'verd', 'kusa', 'wilt', 'turf',
        'stalk', 'cor', 'sq_wd_g', 'sq_wd_v', 'git_meisai', 'rot',
        'git_kusa', 'hig_shibe', 'hig_kuki',
    )),
    ('Wood', (
        'timber', 'oak', 'bark', 'log', 'beam', 'board', 'shin', 'ridge', 'nuki', 'sugi',
        'ita', 'maru', 'matsu', 'miki', 'mbr', 'oshi', 'sq_wd', 'lash', 'moy_log',
        'git_ita', 'git_maru', 'git_matsu', 'git_miki', 'git_oshi',
    )),
    ('Metal', (
        'iron', 'steel', 'anv', 'rust', 'sq_anv', 'sq_oro', 'git_kin', 'gold', 'coin',
    )),
    ('Fabric', (
        'cloth', 'rag', 'drape', 'flag', 'tent', 'web', 'straw', 'hay', 'fur', 'nawa',
        'shime', 'shide', 'moy_shime', 'moy_shide', 'git_nawa', 'sq_web',
    )),
    ('Plaster', (
        'plas', 'cream', 'shoji', 'git_shoji', 'bone', 'marb', 'qz', 'moy_w', 'hig_shiro',
    )),
    ('Soil', (
        'dirt', 'soil', 'ash', 'soot', 'silt', 'clay', 'suna', 'tsuchi', 'ana', 'pit',
        'sco', 'grit', 'pack', 'sand', 'char', 'git_yuki', 'hig_suna', 'hig_tsuchi',
        'moy_grit', 'git_ana', 'sq_pit', 'sq_soot',
    )),
    ('Stone', (
        'wall', 'mortar', 'rock', 'band', 'obs', 'crag', 'tomb', 'lair', 'cob',
        'tile', 'pav', 'gravel', 'crack', 'pebble', 'mrl', 'cyc', 'joint', 'reki', 'koma',
        'koha', 'mag', 'roof', 'ai', 'git_kuro', 'crust', 'ward', 'moy_hi', 'moy_ao',
        'moy_e', 'hig_kon', 'hig_yaki', 'hig_koro', 'git_reki', 'git_koma', 'git_koha',
        'fc_rune',  #: 盗賊の罠の印。床へ刻んだもの
    )),
]

#: 意匠ごとの色（`reg_local`）は `asa_` `mor_` `sen_` … と**町や洞の頭**が付く。
#: 頭を落とした残りが材を言っていることが多いので、**確かなものだけ**を 2 度目に拾う。
STEM_KEYWORDS = {
    'Stone': ('ish', 'roc', 'rk', 'bas', 'st'),
    'Wood': ('wood', 'bark', 'ki'),
    'Metal': ('met', 'brass', 'foil', 'kin'),
    'Fabric': ('carp',),
    'Plant': ('moss', 'root'),
    'Clean': ('pool', 'smk', 'void', 'gl'),
}

#: 名前で名指しの直し（`KEYWORDS` より**強い**）。理由を隣に書くこと。
OVERRIDE = {
    #: **註釈が「その物」を言っていない色**。ここは推し当てなので、作った人の確認待ち。
    'sen_pea': 'Plant',    # 「桃（不老長寿の記号）と鶴の丹頂」。桃の樹冠に使う＝花と見た
    'sen_fru': 'Plant',    # 同じ木の実
    'sen_crown': 'Plant',  # 鶴の丹頂（同じ色群）
    'kyuri': 'Plant',      # 竹の稈（`bamboo_ein_*` が使う）。緑だが水でも光でもない
    'rub': 'Stone',        # 乱石積みの瓦礫。註釈が無いが manor の壁がこれで出来ている
    'alp_fw': 'Plant',     # 高山の花。葉ではない
    'alp_fy': 'Plant',
    'alp_fr': 'Plant',
    'crust': 'Stone',      # 溶岩の殻。土ではなく固まった岩
    'char': 'Soil',        # 焦げ。木ではなく炭
    'bone': 'Plaster',     # 骨。石より白茶けた乾いた面
    'hay': 'Fabric',       # 藁。編んだものとして扱う
    'straw': 'Fabric',
    'gold': 'Metal',       # 磨いてあっても縁は摩耗する
    'coin': 'Metal',
    'rust': 'Metal',
    'ward': 'Stone',       # 結界の石
    'hig_ki': 'Wood',      # 彼岸の木
    'git_ki': 'Wood',
    'hig_beni': 'Plant',   # 彼岸花の紅
    'hig_shiro': 'Plant',  # 白い彼岸花
    'git_ha': 'Plant',
    'moy_ao': 'Plant',     # 迷い竹の青竹
    'fc_peel': 'Plant',    # バナナの皮（Frog の罠）。果皮なので葉でも布でもない
}

#: 註釈の言葉 → 材質。**いちばん先に出てきた言葉を採る**——註釈は「その物」から
#: 書き始まり、比較の相手は後ろに来る（「魔法の森の苔より濃く青い」の苔は比較の相手）。
#: 1 文字の当てにならない語は入れない（「灰」は灰白・灰色に化ける）。
COMMENT_WORDS = (
    ('Leaf', ('落ち葉', '松葉', '笹', '葉')),
    ('Plant', ('苔', '蔓', '蔦', '草', '芝', '花', '茎', '藻')),
    ('Metal', ('錆', '鉄', '鋼', '鉛', '銅', '真鍮', '金具', '金属', '鎖')),
    ('Clean', ('硝子', 'ガラス', '窓', '灯り', '灯火', '炎', '焔', '水面', '氷', '雪', '霧', '湯気', '光')),
    ('Fabric', ('布', '幟', '旗', '幕', '縄', '藁', '畳', '絨毯', '毛皮', '提灯')),
    ('Plaster', ('漆喰', '障子', '和紙', '大理石', '白壁', '骨')),
    ('Wood', ('樹皮', '木材', '木肌', '板', '梁', '幹', '杉', '檜', '丸太', '木')),
    ('Stone', ('切石', '敷石', '石畳', 'レンガ', '煉瓦', 'スレート', '瓦', '舗装', '岩', '石')),
    ('Soil', ('土', '砂', '泥', '炭', '煤')),
)

def first_clause(text):
    """註釈の**頭の一言**だけを採る。

    色の登録簿の註釈は「木（P10 レビュー 2）。葉は**塊ごとに 1 色**なので…」のように、
    頭に物の名前が来て、後ろに理由が続く。**用途として持つのは頭だけ**でよい。
    """
    if not text:
        return ''
    for stop in ('。', '——', '(', '（'):
        at = text.find(stop)
        if at > 0:
            text = text[:at]
            break
    return text.strip().strip('*').strip()


def looks_like_a_use(text):
    """用途の見出しらしいか。**説明文を弾く**ための当たり判定。"""
    if not text or (len(text) > 40):
        return False
    if text.startswith(('---', '!')) or ('**' in text):
        return False
    #: 作り方の話（理由・手順）は用途ではない。
    for word in ('greedy', '三角形', 'ボクセル', '罠', 'ため', 'ので', 'こと'):
        if word in text:
            return False
    return True


#: 組み立ての中で色を置いている行（`v[...] = C['timber']`）。
_USE_RE = re.compile(r"C\[\s*'([a-z0-9_]+)'\s*\]")


def mine_uses(builder, lookback=3):
    """**その物の中でその色が何に使われているか**を、組み立ての註釈から拾う。

    2026-08-23 に決めた:「まずは各オブジェクトごとにどの色が何に使われているのかを
    確認し、その情報を持つようにして」。

    生成器は置く場所の行末に用途を書いている——`v[8:24, 18:20, z0:z0+4] = C['timber']  # 貫（横帯）`。
    行末に無ければ**直前の註釈**（節の見出し）まで 3 行さかのぼる。
    実測で（プレハブ × 色）の 47% が採れる。残りは色の登録簿の註釈で補う。

    @return `{色の名前: [用途, …]}`
    """
    import inspect
    try:
        src = inspect.getsource(builder).splitlines()
    except (OSError, TypeError):
        return {}
    out = {}
    for i, line in enumerate(src):
        names = _USE_RE.findall(line)
        if not names:
            continue
        note = line.split('#', 1)[1].strip() if ('#' in line) else ''
        if not note:
            for back in range(1, lookback + 1):
                if i - back < 0:
                    break
                prev = src[i - back].strip()
                if prev.startswith('#'):
                    candidate = prev.lstrip('#').strip()
                    #: **見出しだけを採る。**組み立ての註釈には「なぜそう作るか」の
                    #: 説明文も混じっていて（「greedy が面ごとにまとめるので…」）、
                    #: それを用途として持つと**読む人が毎回選り分ける羽目になる**。
                    if looks_like_a_use(candidate):
                        note = candidate
                    break
                #: 値を置く行が続いている間はさかのぼってよい（節の見出しへ届く）。
                if prev and not prev.startswith(('v[', 'vol[', 'for ', 'if ', 'else', 'elif')):
                    break
        for name in names:
            notes = out.setdefault(name, [])
            if note and (note not in notes):
                notes.append(note)
    return out


_NAME_RE = re.compile(r"(?:reg|reg_local)\(\s*'([a-z0-9_]+)'|\(\s*'([a-z0-9_]+)'\s*,\s*\(")
_COMMENTS = None


def read_comments(path=None):
    """`gen_prefabs.py` から**色の名前 → すぐ上（と行末）の註釈**を拾う。

    2 通りの書き方があるので両方見る。

      * `reg('bark', (74, 54, 38))` / `reg_local(...)`
      * `for _name, _rgb in (...)` の中の `('hai_w', (168, 164, 155)),`

    **註釈は続く宣言行にも掛ける**（登録簿は 1 つの見出しの下に何行も並べる）。
    ただし**新しい註釈が来たら前のは捨てる**——捨て忘れると、遠くの見出しが
    下の色まで引きずられる（実際に「地面は茶系」が葉の色の説明になっていた）。
    """
    global _COMMENTS
    if (_COMMENTS is not None) and (path is None):
        return _COMMENTS
    src = Path(path) if path else (ROOT / 'tools' / 'voxel' / 'gen_prefabs.py')
    text = src.read_text(encoding='utf-8')
    found = {}
    pending = []
    after_declaration = False
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith('#'):
            if after_declaration:
                pending = []   #: 宣言をまたいだら**新しい見出しの始まり**
                after_declaration = False
            pending.append(stripped.lstrip('#').strip())
            continue
        names = [a or b for a, b in _NAME_RE.findall(line)]
        if not names:
            if stripped != '' or not after_declaration:
                pending = []
                after_declaration = False
            continue
        quote = line.find("'")
        hash_at = line.find('#')
        trailing = line[hash_at + 1:].strip() if (hash_at > quote >= 0) else ''
        note = ' '.join(pending + ([trailing] if trailing else []))
        for name in names:
            found.setdefault(name, note)
        after_declaration = True
    if path is None:
        _COMMENTS = found
    return found


def classify_by_comment(name):
    """註釈から材質を当てる。**当てられなければ `None`。**"""
    note = read_comments().get(name)
    if not note:
        return None
    best = None
    for cls, words in COMMENT_WORDS:
        for word in words:
            at = note.find(word)
            if at >= 0 and ((best is None) or (at < best[0])):
                best = (at, cls)
    return best[1] if best else None


def classify(name):
    """色の名前 → 材質（`CLASSES` の綴り）。当てられなければ `'Default'`。"""
    if name in OVERRIDE:
        return OVERRIDE[name]
    #: 接尾（_l / _d / _dd / _g …）は明暗の別なので落としてから当てる。
    stem = name
    for suffix in ('_dd', '_gd', '_ld', '_l', '_d', '_g', '_h', '_b', '_m', '_j', '_v', '_p', '_s'):
        if stem.endswith(suffix):
            stem = stem[: -len(suffix)]
            break
    if stem in OVERRIDE:
        return OVERRIDE[stem]
    for cls, keys in KEYWORDS:
        for key in keys:
            if stem == key or stem.startswith(key + '_') or name.startswith(key + '_'):
                return cls
    if '_' in stem:
        rest = stem.split('_', 1)[1]
        for cls, keys in STEM_KEYWORDS.items():
            if rest in keys:
                return cls
    from_note = classify_by_comment(name)
    if from_note is not None:
        return from_note
    return 'Default'


def material_name(cls):
    """`CLASSES` の綴り → `.jsonc` に書く小文字の綴り（C++ の `material_class_name` と同じ）。"""
    return cls.lower()


def names_by_index(name_to_index):
    """`C`（名前 → 索引）を裏返す。**別名で 1 つの索引に複数の名前が乗る。**"""
    out = {}
    for name, index in name_to_index.items():
        out.setdefault(int(index), []).append(name)
    for names in out.values():
        names.sort()
    return out


def material_for_index(names):
    """1 つの索引に乗った名前から材質を 1 つ選ぶ。

    別名（`C['kawara'] = C['obs']`）があるので**名前は 1 つとは限らない**。
    見立ての付いたものを優先し、それでも割れたら**名前順で先のもの**を採る
    （どちらを採ったかは呼ぶ側が数えられるよう、割れたかどうかも返す）。
    """
    seen = []
    for name in names:
        cls = classify(name)
        if cls != 'Default':
            seen.append(cls)
    if not seen:
        return 'Default', False
    split = len(set(seen)) > 1
    return seen[0], split
