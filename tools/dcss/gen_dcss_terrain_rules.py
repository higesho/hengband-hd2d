# -*- coding: utf-8 -*-
"""DCSS の地形の規則を作る: 地形の番号 -> ボクセルのプレハブ（画面側の `terrain_prefabs.jsonc`）と、コア側の番号の表。

    python -u tools/dcss/gen_dcss_terrain_rules.py            # 作る（規則・表・プレハブの写し）
    python -u tools/dcss/gen_dcss_terrain_rules.py --check    # 書かずに、いまの物が元から作れる物と一致するか照合する
    python -u tools/dcss/gen_dcss_terrain_rules.py --gen <作業フォルダ>/gen/rltiles   # tilegen の出力の場所（既定: 一時フォルダの dcss_w）

## 何を作るか

元: `dcss/terrain/feature_map.csv`（地形の値 -> 番号 2000+値 とプレハブ）、`dcss/assets/voxel/manifest.tsv`（作ったプレハブ）、
`tilegen` の出力（床・壁のタイル番号。組んだ `DcssCore.exe` と同じ物）。

1. **規則** `assets/voxel/terrain_prefabs.jsonc` の `terrains_extra` の中の目印の間（そこだけを丸ごと書き直す）:
     `// ---- DCSS（tools/dcss/gen_dcss_terrain_rules.py が書く。手で直さない）----` 〜 `// ---- DCSS ここまで ----`
   - 地形の値ごとの規則 218 行（番号 2000〜2220 と派生 2300〜2313）。
   - **ブランチごとの床** 2400〜: 原作は床のマスの背景に、そのマスのブランチ・ヴォールトに合わせて選んだ実際の床のタイル（例 `FLOOR_ORC_3`）を送る。
     タイルの組 -> 床のプレハブごとに 1 つの番号を振る。
   - **ブランチごとの壁** 2500〜: 壁のマスも同じ（例 `WALL_ORC`）。
   - **雲**: 段 M1 では 2650〜 の「雲のある床」にしていたが、段 3（D14）で立体の雲に替えた（番号の帯は使わない。`gen_dcss_clouds.py`・`hd2d/dcss/dcss_clouds.cpp`）。
2. **コア側の表** `dcss/adapter/dcss_tile_variants.inc`: 床・壁の組（タイル番号の範囲）-> 上の番号、派生の地形（店の種別ほか）。
   `dcss/adapter/dcss_frame.cpp` が取り込む。**番号は規則の `id` と同じ表から作る**（片方だけ直さないこと）。
3. **プレハブの写し** 規則が指す `dc_*` を `dcss/assets/voxel/` から `assets/voxel/` へ写す。画面は `assets/voxel/` の 1 か所だけを読む
   （TOband の `tb_*` も同じ。コアごとの置き場を画面は読まない）。同じ中身なので git は 1 つの塊として持つ。

番号の帯（`dcss/terrain/README.md` §3 の続き）: 2000〜2220 地形の値／2300〜2313 派生／**2400〜2499 床の組／2500〜2649 壁の組／2650〜2699 は空き（元は雲の床）**。
他のコアの帯（〜1804・40001）とぶつからない。
"""
from __future__ import annotations

import argparse
import csv
import io
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
REPO = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
import dcss_tilefam as tf  # noqa: E402

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

FEATURE_MAP = REPO / "dcss" / "terrain" / "feature_map.csv"
SRC_VOXEL = REPO / "dcss" / "assets" / "voxel"
DST_VOXEL = REPO / "assets" / "voxel"
PREFABS = DST_VOXEL / "terrain_prefabs.jsonc"
INC_OUT = REPO / "dcss" / "adapter" / "dcss_tile_variants.inc"
BEGIN = "    // ---- DCSS（tools/dcss/gen_dcss_terrain_rules.py が書く。手で直さない）----"
END = "    // ---- DCSS ここまで ----"
TOBAND_BEGIN = "    // ---- TOband（tools/toband/gen_terrain_rules.py が書く。手で直さない）----"

FLOOR_BASE, FLOOR_LIMIT = 2400, 2500
WALL_BASE, WALL_LIMIT = 2500, 2650
#: 物 × 床の組（扉・階段・祭壇など、背景が透ける物の下の床をそのマスの場所の床にする）。番号 = UNDER_BASE + 物の通し番号 * UNDER_STRIDE + (床の番号 - FLOOR_BASE)。
#: 他のコアの帯（〜1804・40001）と、このコアの 2000〜2699 の外。物は 200 個弱なので 3000〜15500 ほど。
UNDER_BASE, UNDER_STRIDE = 3000, 64
# 2650〜2699 は雲のある床の番号だった（段 M1）。雲は立体の雲（`hd2d/dcss/dcss_clouds.cpp`。段 3・D14）に替え、この帯は使わない。

GROUND_DEFAULT = "dc_floor_grey_dirt"
GROUND_TREE = "dc_floor_grass"
#: 光る祭壇・ルーンの灯の、色つきの弱い自発光（線形。床に足される。物には 0.22 倍）。`CELL_FEAT_GLOW`（画面側がマスいっぱいの白い自発光の平板と点光源を足す旗。
#: 店の入口と溶岩のため）は付けない（`dcss/terrain/build_feature_map.py` の注）。キーは規則のキー（`DC_` + 地形の定義名）。値は色味を絵から決めた。
GLOW_EMISSIVE = {
    "DC_ALTAR_SHINING_ONE": (0.09, 0.07, 0.02),   # 金
    "DC_ALTAR_VEHUMET": (0.02, 0.07, 0.07),       # 青緑
    "DC_ALTAR_MAKHLEB": (0.10, 0.04, 0.01),       # 炎の橙
    "DC_ALTAR_IGNIS": (0.09, 0.06, 0.02),         # 蝋燭の暖色
    "DC_RUNELIGHT": (0.02, 0.05, 0.10),           # ルーンの青
}
#: 床を自分で含む物（階段の下りは穴を彫った床の板そのもの。床を先に敷くと穴をふさぐ）
GROUND_OWN = {"dc_stairs_down", "dc_stairs_sealed_down"}

#: 床の色替えの組の名前の末尾（原作は `%hue`・`%desat` で実行時に色を替える。元の絵が同じなので同じプレハブを使う）
COLOUR_SUFFIX = re.compile(r"_(BLUE|GREEN|CYAN|RED|MAGENTA|BROWN|LIGHTGRAY|DARKGRAY|LIGHTBLUE|LIGHTGREEN|LIGHTCYAN|LIGHTRED|LIGHTMAGENTA|YELLOW|WHITE|DARKBROWN|BLACK)$")

#: 派生の地形（地形の値は同じだが、原作が選ぶタイルで絵が変わる物）: (タイルの名前, 地形の値 or -1=どれでも, 番号)
#: 番号は feature_map.csv の派生の行（2300〜2313）。`check_derived` が名前と突き合わせる
DERIVED_TILES = [
    ("TILE_SHOP_WEAPONS", 40, 2300), ("TILE_SHOP_ARMOUR", 40, 2301), ("TILE_SHOP_JEWELLERY", 40, 2302),
    ("TILE_SHOP_BOOKS", 40, 2303), ("TILE_SHOP_SCROLLS", 40, 2304), ("TILE_SHOP_POTIONS", 40, 2305),
    ("TILE_SHOP_GENERAL", 40, 2306), ("TILE_SHOP_FOOD", 40, 2306), ("TILE_SHOP_GADGETS", 40, 2306), ("TILE_SHOP_WANDS", 40, 2306),
    ("TILE_DNGN_STONE_ARCH_HELL", 56, 2307),
    ("TILE_DNGN_ENTER_ZOT_OPEN", 76, 2308),
    ("TILE_DNGN_EXIT_VAULTS_CLOSED", 90, 2309),
    ("TILE_DNGN_ZOT_FIRE_STATUE", 219, 2310), ("TILE_DNGN_ZOT_WINTER_STATUE", 219, 2311), ("TILE_DNGN_ZOT_ENTROPY_STATUE", 219, 2312),
    ("TILE_DNGN_PETRIFIED_TREE", 219, 2313),
]
DERIVED_PREFAB = {2300: "dc_shop_weapon", 2301: "dc_shop_armour", 2302: "dc_shop_jewellery", 2303: "dc_shop_book", 2304: "dc_shop_scroll",
                  2305: "dc_shop_potion", 2306: "dc_shop_general", 2307: "dc_gate_stone_arch_hell", 2308: "dc_gate_enter_zot_open",
                  2309: "dc_gate_enter_vaults_closed", 2310: "dc_statue_zot_fire", 2311: "dc_statue_zot_winter", 2312: "dc_statue_zot_entropy",
                  2313: "dc_tree_petrified"}



# ---------------------------------------------------------------- 入力
def read_csv_rows() -> list[dict]:
    txt = "".join(l for l in open(FEATURE_MAP, encoding="utf-8") if not l.startswith("#"))
    rows = list(csv.reader(io.StringIO(txt)))
    out = []
    for r in rows[1:]:
        m = re.match(r"(object|structure|ground)=(dc_[a-z0-9_]+)", r[8])
        out.append(dict(defn=r[0], num=r[1].strip(), ja=r[2], cls=r[3], tid=int(r[4]) if r[4].strip().isdigit() else 0,
                        tile=r[6], role=m.group(1) if m else "", prefab=m.group(2) if m else ""))
    return out


def read_manifest() -> dict[str, tuple[str, str]]:
    """プレハブ名 -> (種類, 元の絵)。"""
    out = {}
    for line in open(SRC_VOXEL / "manifest.tsv", encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        t = line.rstrip("\n").split("\t")
        out[t[0]] = (t[1], t[2])
    return out


# ---------------------------------------------------------------- 規則
class Rule:
    def __init__(self, key, tid, comment, ground=None, obj=None, obj_ew=None, structure=None, tint=None, emissive=None):
        self.key, self.tid, self.comment = key, tid, comment
        self.ground, self.obj, self.obj_ew, self.structure = ground, obj, obj_ew, structure
        self.tint, self.emissive = tint, emissive

    def text(self) -> str:
        parts = [f'"key": "{self.key}"', f'"id": {self.tid}']
        for col, val in (("ground", self.ground), ("structure", self.structure), ("object", self.obj), ("object_ew", self.obj_ew)):
            if val is not None:
                parts.append('"%s": [%s]' % (col, ", ".join(f'"{n}"' for n in val)))
        if self.tint:
            parts.append('"tint": [%s]' % ", ".join("%.2f" % v for v in self.tint))
        if self.emissive:
            parts.append('"emissive": [%s]' % ", ".join("%.2f" % v for v in self.emissive))
        if not self.comment:
            return "    { %s }," % ", ".join(parts)
        return "    { %s },  // %s" % (", ".join(parts), self.comment)


def feature_rules(rows: list[dict], manifest: dict) -> list[Rule]:
    rules = []
    seen = set()
    for r in rows:
        if r["cls"] != "c" or not r["prefab"]:
            continue
        if r["prefab"] not in manifest:
            raise SystemExit(f"表の行 {r['defn']} が指すプレハブが無い: {r['prefab']}（gen_dcss_terrain.py で作る）")
        if r["tid"] in seen:
            raise SystemExit(f"番号が重なる: {r['tid']} ({r['defn']})")
        seen.add(r["tid"])
        key = "DC_" + re.sub(r"[^A-Z0-9]+", "_", r["defn"].replace("DNGN_", "", 1)).strip("_")
        p, kind = r["prefab"], manifest[r["prefab"]][0]
        comment = r["ja"]
        if r["role"] == "structure":
            rules.append(Rule(key, r["tid"], comment, structure=[p]))
        elif r["role"] == "ground":
            rules.append(Rule(key, r["tid"], comment, ground=[p]))
        else:
            ground = [] if p in GROUND_OWN else [GROUND_TREE if kind == "tree" else GROUND_DEFAULT]
            ew = p + "_ew"
            rules.append(Rule(key, r["tid"], comment, ground=ground, obj=[p], obj_ew=[ew] if ew in manifest else None, emissive=GLOW_EMISSIVE.get(key)))
    return rules


def floor_variants(tiles: tf.Tiles, manifest: dict):
    """床の組 -> プレハブ名。戻り値: ([(先頭, 枚数, プレハブ名)], 申告の行)。"""
    by_png = {}
    for n, (kind, png) in manifest.items():
        if kind == "floor":
            by_png.setdefault(png, n)
    png_of = tf.names_to_png("dc-floor.txt")
    out, unmapped = [], []
    for start, count in tiles.families("floor"):
        canon = tiles.name_of(start)
        if canon.startswith("DNGN_") or "OVERLAY" in canon:
            continue
        cands = [canon] + [a[len("TILE_"):] for a in tiles.aliases_at(start) if a.startswith("TILE_")]
        cands += [COLOUR_SUFFIX.sub("", c) for c in cands if COLOUR_SUFFIX.search(c)]
        pre = None
        for c in cands:
            png = png_of.get(c)
            if png and png in by_png:
                pre = by_png[png]
                break
        if pre is None:
            unmapped.append(canon)
        else:
            out.append((start, count, pre))
    return out, unmapped


def wall_variants(tiles: tf.Tiles, manifest: dict):
    """壁の組 -> プレハブ名。絵が表の壁と同じならその壁、ブランチの壁なら作った `dc_wall_*`。"""
    by_png = {}
    for n, (kind, png) in manifest.items():
        if kind == "wall":
            by_png.setdefault(png, n)
    pngs = tf.wall_family_pngs(tiles)
    wanted = set()
    for n in tf.branch_wall_names():
        wanted.add(tiles.family(tiles.names["TILE_" + n])[0])
    out, skipped = [], []
    for start, count in tiles.families("wall"):
        canon = tiles.name_of(start)
        pre = None
        if start in pngs:
            png = pngs[start][1]
            pre = by_png.get(png)
        if pre is None and start in wanted:
            cand = tf.wall_prefab_name(canon)
            pre = cand if cand in manifest else (cand + "_wall" if cand + "_wall" in manifest else None)
        if pre is None:
            skipped.append((start, count, canon))
        else:
            out.append((start, count, pre))
    # 色替え・松明つきの組（`WALL_HALL_WHITE`・`WALL_BRICK_DARK_2_TORCH`）は、語を落とした元の組のプレハブを使う（形は同じ。色と松明は出ない）
    by_start = {s: p for s, _c, p in out}
    left = []
    for start, count, canon in skipped:
        base = COLOUR_SUFFIX.sub("", canon[:-len("_TORCH")] if canon.endswith("_TORCH") else canon)
        pre = None
        if base != canon and ("TILE_" + base) in tiles.names:
            pre = by_start.get(tiles.family(tiles.names["TILE_" + base])[0])
        if pre is None:
            left.append(canon)
        else:
            out.append((start, count, pre))
    return out, left


def under_objects(rules: list[Rule]) -> list[Rule]:
    """床の組を持たせる物の規則（地面が既定の床の物だけ。階段の下りは床を自分で含む・樹木は草なので外す）。番号の順。"""
    return sorted((r for r in rules if r.obj and r.ground == [GROUND_DEFAULT]), key=lambda r: r.tid)


def under_rules(objs: list[Rule], fl_prefabs: list[str], fl_id: dict) -> list[Rule]:
    """物 × 既定以外の床の規則。既定の床（灰色の土）の組は元の規則そのものなので作らない。"""
    if len(fl_prefabs) > UNDER_STRIDE:
        raise SystemExit("床の種類が物 × 床の番号の幅を超えた")
    out = []
    for oi, o in enumerate(objs):
        for p in fl_prefabs:
            if p == GROUND_DEFAULT:
                continue
            fi = fl_id[p] - FLOOR_BASE
            out.append(Rule("DU%d_%d" % (o.tid, fi), UNDER_BASE + oi * UNDER_STRIDE + fi, "", ground=[p], obj=o.obj, obj_ew=o.obj_ew, emissive=o.emissive))
    return out


def check_derived(rows: list[dict], tiles: tf.Tiles) -> list[tuple[int, int, int, int]]:
    """派生の地形の表を作る。表の行（feature_map.csv の 2300〜2313）が指すプレハブ名と突き合わせる。"""
    by_tid = {r["tid"]: r for r in rows if r["cls"] == "c" and r["tid"]}
    for tid, pre in DERIVED_PREFAB.items():
        row = by_tid.get(tid)
        if row is None or row["prefab"] != pre:
            raise SystemExit(f"派生の番号 {tid} の行が {pre} を指していない: {row and row['prefab']}")
    out = []
    for name, feat, tid in DERIVED_TILES:
        if name not in tiles.names:
            raise SystemExit(f"タイルの名前が見つからない: {name}")
        start, count = tiles.family(tiles.names[name])
        out.append((start, start + count - 1, feat, tid))
    return out


# ---------------------------------------------------------------- 組み立て
def merge_spans(spans: list[tuple[int, int, int]]) -> list[tuple[int, int, int]]:
    """隣り合って同じ番号の範囲をまとめる。"""
    out: list[list[int]] = []
    for a, b, t in sorted(spans):
        if out and out[-1][1] + 1 == a and out[-1][2] == t:
            out[-1][1] = b
        else:
            out.append([a, b, t])
    return [tuple(x) for x in out]


def build(gen: str | None):
    tiles = tf.Tiles(gen)
    manifest = read_manifest()
    rows = read_csv_rows()
    rules = feature_rules(rows, manifest)
    report = []

    fl, unmapped_f = floor_variants(tiles, manifest)
    fl_prefabs = sorted({p for _s, _c, p in fl})
    if len(fl_prefabs) > FLOOR_LIMIT - FLOOR_BASE:
        raise SystemExit("床の種類が帯を超えた")
    fl_id = {p: FLOOR_BASE + i for i, p in enumerate(fl_prefabs)}
    for p in fl_prefabs:
        rules.append(Rule("DC_FLOOR_" + p[len("dc_floor_"):].upper(), fl_id[p], "ブランチの床 " + p, ground=[p]))
    floor_spans = merge_spans([(s, s + c - 1, fl_id[p]) for s, c, p in fl])
    report.append(f"床の組 {len(tiles.families('floor'))} 個のうちプレハブを当てた {len(fl)} 個 -> 床の種類 {len(fl_prefabs)}（番号 {FLOOR_BASE}〜）。当てられなかった {len(unmapped_f)} 個: " + ", ".join(unmapped_f[:12]) + ("…" if len(unmapped_f) > 12 else ""))

    wl, skipped_w = wall_variants(tiles, manifest)
    wl_prefabs = sorted({p for _s, _c, p in wl})
    if len(wl_prefabs) > WALL_LIMIT - WALL_BASE:
        raise SystemExit("壁の種類が帯を超えた")
    wl_id = {p: WALL_BASE + i for i, p in enumerate(wl_prefabs)}
    for p in wl_prefabs:
        rules.append(Rule("DC_WALLSET_" + p[len("dc_wall_"):].upper(), wl_id[p], "ブランチの壁 " + p, structure=[p]))
    wall_spans = merge_spans([(s, s + c - 1, wl_id[p]) for s, c, p in wl])
    report.append(f"壁の組 {len(tiles.families('wall'))} 個のうちプレハブを当てた {len(wl)} 個 -> 壁の種類 {len(wl_prefabs)}（番号 {WALL_BASE}〜）。当てていない {len(skipped_w)} 個（地形の壁の既定に落ちる）")

    objs = under_objects(rules)
    under = under_rules(objs, fl_prefabs, fl_id)
    report.append(f"物 × 床の組: 物 {len(objs)} 個 × 既定以外の床 {len(fl_prefabs) - 1} 種 = 規則 {len(under)} 行（番号 {UNDER_BASE}〜{UNDER_BASE + len(objs) * UNDER_STRIDE - 1}）")
    rules.extend(under)
    derived = check_derived(rows, tiles)
    rules.sort(key=lambda r: r.tid)
    ids = [r.tid for r in rules]
    if len(ids) != len(set(ids)):
        raise SystemExit("規則の番号が重なる")
    return tiles, rules, floor_spans, wall_spans, derived, report, [o.tid for o in objs], fl_id[GROUND_DEFAULT]


def inc_text(tiles, floor_spans, wall_spans, derived, floor_default, under_tids, floor_grey) -> str:
    def arr(name, spans, with_feat=False):
        lines = [f"static const DcssTileSpan {name}[] = {{"]
        for sp in spans:
            if with_feat:
                lines.append(f"    {{ {sp[0]}, {sp[1]}, {sp[3]}, {sp[2]} }},")
            else:
                lines.append(f"    {{ {sp[0]}, {sp[1]}, {sp[2]}, -1 }},")
        lines.append("};")
        return lines

    out = ["// 生成物: tools/dcss/gen_dcss_terrain_rules.py（元: dcss/terrain/feature_map.csv・dcss/assets/voxel/manifest.tsv・tilegen の出力）。手で直さない。",
           "// タイル番号（`t.bg` の下位 16 ビット）の範囲 -> HD2D の地形番号。規則は assets/voxel/terrain_prefabs.jsonc の DCSS の節。",
           "// 範囲は閉区間 {先頭, 末尾, 地形番号, 地形の値（-1 = どれでも）}。床・壁は先頭の番号順で重ならない（二分探索）。派生は順不同（上から探す）。",
           f"static constexpr int kDcssTileFloorMax = {tiles.floor_max};  // 床のタイルは [0, これ)",
           f"static constexpr int kDcssTileWallMax = {tiles.wall_max};    // 壁のタイルは [kDcssTileFloorMax, これ)",
           f"static constexpr std::uint16_t kDcssFloorTerrain = {floor_default};  // 床の既定（DNGN_FLOOR）",
           "// ブランチの床 -> 番号（DNGN_FLOOR のマスだけ）"]
    out += arr("kDcssFloorSpans", floor_spans)
    out.append("// ブランチの壁 -> 番号（壁のマスだけ）")
    out += arr("kDcssWallSpans", wall_spans)
    out.append("// 原作が選ぶタイルで絵が変わる地形（店の種別・ゾットの門と像ほか）")
    out += arr("kDcssDerivedSpans", derived, with_feat=True)
    out += ["// 物 × 床の組（扉・階段・祭壇など、背景が透ける物の下の床）。原作はそのマスの床のタイルを `flv.f` で送る。",
            "// 地形番号 = kDcssUnderBase + (物の規則の地形番号の、kDcssUnderObjects の中の位置) * kDcssUnderStride + (床の地形番号 - kDcssFloorBase)。",
            "// 床が既定（kDcssFloorGrey = 灰色の土）の組は元の物の規則そのもの。",
            f"static constexpr std::uint16_t kDcssUnderBase = {UNDER_BASE};",
            f"static constexpr std::uint16_t kDcssUnderStride = {UNDER_STRIDE};",
            f"static constexpr std::uint16_t kDcssFloorBase = {FLOOR_BASE};",
            f"static constexpr std::uint16_t kDcssFloorGrey = {floor_grey};",
            "static const std::uint16_t kDcssUnderObjects[] = {"]
    for i in range(0, len(under_tids), 16):
        out.append("    " + ", ".join(str(t) for t in under_tids[i:i + 16]) + ",")
    out.append("};")
    return "\n".join(out) + "\n"


def rules_block(rules: list[Rule]) -> list[str]:
    lines = [BEGIN,
             "    //! 正は dcss/terrain/feature_map.csv・dcss/assets/voxel/manifest.tsv・tilegen の出力。番号の帯は tools/dcss/gen_dcss_terrain_rules.py の註。"]
    lines += [r.text() for r in rules]
    lines.append(END)
    return lines


def replace_block(text: str, block: list[str]) -> str:
    src = text.split("\n")
    if BEGIN in src:
        a, b = src.index(BEGIN), src.index(END)
        new = src[:a] + block + src[b + 1:]
    else:
        if TOBAND_BEGIN in src:
            at = src.index(TOBAND_BEGIN)
            new = src[:at] + block + [""] + src[at:]
        else:
            start = next(i for i, l in enumerate(src) if l.strip().startswith('"terrains_extra"'))
            close = next(i for i in range(start, len(src)) if src[i] == "  ]")
            last = close - 1
            while not src[last].strip() or src[last].strip().startswith("//"):
                last -= 1
            if not src[last].rstrip().endswith(","):
                src[last] = src[last].rstrip() + ","
            blk = list(block)
            k = len(blk) - 2
            head, sep, tail = blk[k].partition("  // ")
            blk[k] = head.rstrip().rstrip(",") + (sep + tail if sep else "")        # 配列の最後の規則は , を付けない
            new = src[:close] + [""] + blk + src[close:]
    return "\n".join(new)


def referenced_prefabs(rules: list[Rule]) -> list[str]:
    names = set()
    for r in rules:
        for v in (r.ground, r.obj, r.obj_ew, r.structure):
            if v:
                names.update(v)
    return sorted(names)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--gen", default="", help="tilegen の出力（<作業フォルダ>/gen/rltiles）")
    args = ap.parse_args()
    tiles, rules, floor_spans, wall_spans, derived, report, under_tids, floor_grey = build(args.gen or None)
    text = PREFABS.read_text(encoding="utf-8")
    new_text = replace_block(text, rules_block(rules))
    floor_default = next(r.tid for r in rules if r.key == "DC_FLOOR")
    inc = inc_text(tiles, floor_spans, wall_spans, derived, floor_default, under_tids, floor_grey)
    names = referenced_prefabs(rules)
    missing = [n for n in names if not (SRC_VOXEL / (n + ".vox")).is_file() or not (SRC_VOXEL / (n + ".jsonc")).is_file()]
    if missing:
        print("作ってないプレハブ:", ", ".join(missing))
        return 1
    stale = []
    for n in names:
        for ext in (".vox", ".jsonc"):
            s, d = SRC_VOXEL / (n + ext), DST_VOXEL / (n + ext)
            if not d.is_file() or d.read_bytes() != s.read_bytes():
                stale.append(d)
    have_inc = INC_OUT.read_text(encoding="utf-8") if INC_OUT.is_file() else ""
    for line in report:
        print(line)
    print(f"規則 {len(rules)} 行（地形の値 {sum(1 for r in rules if r.tid < 2400)}・床 {sum(1 for r in rules if FLOOR_BASE <= r.tid < FLOOR_LIMIT)}・"
          f"壁 {sum(1 for r in rules if WALL_BASE <= r.tid < WALL_LIMIT)}・物 × 床 {sum(1 for r in rules if r.tid >= UNDER_BASE)}）、指すプレハブ {len(names)} 個")
    if args.check:
        ok = True
        for what, same in (("terrain_prefabs.jsonc の DCSS の節", new_text == text), ("dcss_tile_variants.inc", have_inc == inc),
                           (f"assets/voxel のプレハブの写し（古い・無い {len(stale)} ファイル）", not stale)):
            print(f"  {what}: {'最新' if same else '古い（作り直しが要る）'}")
            ok = ok and same
        return 0 if ok else 1
    PREFABS.write_text(new_text, encoding="utf-8", newline="\n")
    INC_OUT.write_text(inc, encoding="utf-8", newline="\n")
    for d in stale:
        shutil.copyfile(SRC_VOXEL / d.name, d)
    print(f"書いた: {PREFABS.relative_to(REPO)}・{INC_OUT.relative_to(REPO)}・プレハブの写し {len(stale)} ファイル")
    return 0


if __name__ == "__main__":
    sys.exit(main())
