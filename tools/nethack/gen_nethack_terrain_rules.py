# -*- coding: utf-8 -*-
"""NetHack の地形の規則を作る: 地形の番号（表示記号 × 壁の系統・祭壇の属性）-> ボクセルのプレハブの名前。

    python tools/nethack/gen_nethack_terrain_rules.py            # 規則を assets/voxel/terrain_prefabs.jsonc へ書き、コア側の表を .inc へ書く
    python tools/nethack/gen_nethack_terrain_rules.py --check    # 書かずに、いまの物が作れる物と一致するか照合する（終了コード 0 が一致）
    python tools/nethack/gen_nethack_terrain_rules.py --check --files   # さらに、規則が指すプレハブが assets/voxel にあることも見る

## 何を作るか

1. **規則** `assets/voxel/terrain_prefabs.jsonc` の `terrains_extra` の中の目印の間（そこだけを丸ごと書き直す）:
     `// ---- NetHack（tools/nethack/gen_nethack_terrain_rules.py が書く。手で直さない）----` 〜 `// ---- NetHack ここまで ----`
   DCSS・TOband の節と同じ書き方（`key`・`id`・`ground`・`structure`・`object`・`object_ew`・`emissive`）に、全行へ `"optional": true` を付ける。番号と名前だけで絵は含まない（追跡してよい）。
   指すプレハブ `nh_*` は `gen_nethack_terrain.py` が作る（追跡しない。組み立てのときに作る）。`optional` の行は、指すプレハブが 1 つでも読めなければ画面が
   その行を飛ばす（`PrefabLibrary::load`）ので、`nh_*` を作る前の作業ツリー・配布物でも、NetHack 以外のコアの画面は起動する（NetHack の地形は役割の既定の絵になる）。
2. **コア側の表** `nh_terrain_ids.inc`（既定は `.taskwork/nethack_terrain2/out/`。アダプタへの置き場は `nethack/adapter/`）:
   表示記号 `cmap`（0〜104）-> 地形番号、壁の系統・祭壇の属性から番号を引く関数。**番号は規則の `id` と同じ表から作る**（片方だけ直さないこと）。

## 番号の帯（NetHack は 20000〜20999）

他のコアの帯とぶつからない所を、`terrain_prefabs.jsonc` 全体の `id` を数えて選んだ: 変愚 1〜238／Sil-Q・Frox ほか 1302〜1544／TOband 1603〜1804／
DCSS 2001〜2561 と 3000〜（物 × 床の組。いまの上限は 3000 + 196 × 64 = 15544）／Angband 40001。20000〜20999 は誰も使っていない。

| 番号 | 中身 |
| --- | --- |
| 20000 + cmap | 表示記号 `cmap`（`defsym.h` の PCHAR の番号 0〜104）。壁（1〜11）と祭壇（33）以外の地形。33 は中立の祭壇（`cmap_to_glyph(S_altar)` と同じ） |
| 20100 + 系統 × 16 + (cmap - 1) | 壁 11 種 × 5 系統（系統 0 通常・1 鉱山・2 ゲヘナ・3 ノックス・4 ソコバン。glyph の `GLYPH_CMAP_*_OFF` が持つ）。壁のプレハブは系統 × 向きごと（`nh_wall_<系統>_<表示記号の名前>`。側面は横の壁の絵で統一〔D23〕、上面の縁の柵の位置だけが向きで変わる〔D26〕） |
| 20200 + 属性 | 祭壇 5 種（`altar_types`: 0 無属性・1 混沌・2 中立・3 秩序・4 その他。glyph が持つ） |

罠・設備の下の床は、通路・部屋を区別できないので部屋の床（`nh_floor_room`）にする。扉は向き（南北・東西）の対を持ち、どちらを使うかは画面が隣の壁から決める
（`terrain_prefabs.jsonc` の表頭の説明）。
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
REPO = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
sys.dont_write_bytecode = True

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

PREFABS = REPO / "assets" / "voxel" / "terrain_prefabs.jsonc"
TERRAIN_CPP = REPO / "nethack" / "adapter" / "nh_terrain.cpp"
INC_DEFAULT = REPO / ".taskwork" / "nethack_terrain2" / "out" / "nh_terrain_ids.inc"
BEGIN = "    // ---- NetHack（tools/nethack/gen_nethack_terrain_rules.py が書く。手で直さない）----"
END = "    // ---- NetHack ここまで ----"
TOBAND_BEGIN = "    // ---- TOband（tools/toband/gen_terrain_rules.py が書く。手で直さない）----"

BASE = 20000
WALL_BASE = 20100
ALTAR_BASE = 20200
LIMIT = 21000
CMAP_COUNT = 105
WALL_FIRST, WALL_LAST = 1, 11           # S_vwall … S_trwall
WALL_SYSTEMS = ("main", "mines", "gehennom", "knox", "sokoban")
ALTAR_TYPES = ("unaligned", "chaotic", "neutral", "lawful", "other")
ROOM = "nh_floor_room"
LAVA_EMISSIVE = (2.10, 0.72, 0.22)      # TOband の深い溶岩と同じ

#: cmap -> (種類, 指すプレハブ)。種類: ground / structure / object（床は ROOM）/ door（対）/ own（床を自分で含む物。ground だけ）
CMAP_RULES = {
    12: ("ground", "nh_floor_doorway"),
    13: ("door", "nh_door_open"), 14: ("door", "nh_door_open"),
    15: ("door", "nh_door_closed"), 16: ("door", "nh_door_closed"),
    17: ("object", "nh_bars"), 18: ("object", "nh_tree"),
    19: ("ground", "nh_floor_room"), 20: ("ground", "nh_floor_dark"), 21: ("ground", "nh_floor_room_engr"),
    22: ("ground", "nh_floor_corridor"), 23: ("ground", "nh_floor_corridor_lit"), 24: ("ground", "nh_floor_corridor_engr"),
    25: ("object", "nh_stairs_up"), 26: ("ground", "nh_stairs_down"),
    27: ("object", "nh_ladder_up"), 28: ("ground", "nh_ladder_down"),
    29: ("object", "nh_stairs_up_branch"), 30: ("ground", "nh_stairs_down_branch"),
    31: ("object", "nh_ladder_up_branch"), 32: ("ground", "nh_ladder_down_branch"),
    33: ("object", "nh_altar_neutral"),
    34: ("object", "nh_grave"), 35: ("object", "nh_throne"), 36: ("object", "nh_sink"), 37: ("object", "nh_fountain"),
    38: ("ground", "nh_pool"), 39: ("ground", "nh_ice"), 40: ("ground", "nh_lava"), 41: ("ground", "nh_lava_wall"),
    42: ("ground", "nh_bridge_open_v"), 43: ("ground", "nh_bridge_open_h"),
    44: ("structure", "nh_bridge_raised_v"), 45: ("structure", "nh_bridge_raised_h"),
    46: ("ground", "nh_air"), 47: ("ground", "nh_cloud"), 48: ("ground", "nh_water"),
    49: ("object", "nh_trap_arrow"), 50: ("object", "nh_trap_dart"), 51: ("object", "nh_trap_falling_rock"),
    52: ("object", "nh_trap_squeaky_board"), 53: ("object", "nh_trap_bear"), 54: ("object", "nh_trap_land_mine"),
    55: ("object", "nh_trap_rolling_boulder"), 56: ("object", "nh_trap_sleeping_gas"), 57: ("object", "nh_trap_rust"),
    58: ("object", "nh_trap_fire"),
    59: ("ground", "nh_trap_pit"), 60: ("ground", "nh_trap_spiked_pit"), 61: ("ground", "nh_trap_hole"), 62: ("ground", "nh_trap_trapdoor"),
    63: ("object", "nh_trap_teleport"), 64: ("object", "nh_trap_level_teleporter"), 65: ("object", "nh_trap_magic_portal"),
    66: ("object", "nh_trap_web"), 67: ("object", "nh_trap_statue"), 68: ("object", "nh_trap_magic"),
    69: ("object", "nh_trap_anti_magic"), 70: ("object", "nh_trap_polymorph"), 71: ("object", "nh_trap_vibrating_square"),
    72: ("door", "nh_door_trapped"), 73: ("object", "nh_trap_chest"),
}
EMISSIVE = {40: LAVA_EMISSIVE, 41: LAVA_EMISSIVE}
#: 規則を持たない（描かない）cmap: 0 = S_stone、74〜104 = 効果（ビーム・閃光・盾・毒雲・位置の指定・呑み込み・爆発）
HIDDEN = [0] + list(range(74, CMAP_COUNT))


class Rule:
    def __init__(self, key, tid, comment, ground=None, obj=None, obj_ew=None, structure=None, emissive=None):
        self.key, self.tid, self.comment = key, tid, comment
        self.ground, self.obj, self.obj_ew, self.structure, self.emissive = ground, obj, obj_ew, structure, emissive

    def text(self):
        parts = [f'"key": "{self.key}"', f'"id": {self.tid}']
        for col, val in (("ground", self.ground), ("structure", self.structure), ("object", self.obj), ("object_ew", self.obj_ew)):
            if val is not None:
                parts.append('"%s": [%s]' % (col, ", ".join(f'"{n}"' for n in val)))
        if self.emissive:
            parts.append('"emissive": [%s]' % ", ".join("%.2f" % v for v in self.emissive))
        parts.append('"optional": true')    # 指す nh_* が無い作業ツリーでも画面が起動できるよう、読めなければこの行を飛ばす（PrefabLibrary::load）
        return "    { %s },  // %s" % (", ".join(parts), self.comment)


def cmap_names():
    """表示記号の名前（`nh_terrain.cpp` の各行の頭の註釈 `/* 12 S_ndoor */`）。原作のヘッダの順と同じ（`check_terrain_table.py` が照合する）。"""
    names = {}
    for m in re.finditer(r"/\*\s*(\d+)\s+(S_[a-z_0-9]+)\s*\*/", TERRAIN_CPP.read_text(encoding="utf-8")):
        names[int(m.group(1))] = m.group(2)
    if sorted(names) != list(range(CMAP_COUNT)):
        raise SystemExit(f"nh_terrain.cpp の註釈から表示記号 {CMAP_COUNT} 個が読めない（{len(names)} 個）")
    return names


def build_rules():
    names = cmap_names()
    rules = []
    for cmap, (kind, prefab) in sorted(CMAP_RULES.items()):
        sname = names[cmap]
        key = "NH_" + sname[2:].upper()
        tid = BASE + cmap
        if WALL_FIRST <= cmap <= WALL_LAST:
            raise SystemExit("壁は CMAP_RULES に書かない")
        comment = sname
        if kind == "ground":
            rules.append(Rule(key, tid, comment, ground=[prefab], emissive=EMISSIVE.get(cmap)))
        elif kind == "structure":
            rules.append(Rule(key, tid, comment, structure=[prefab]))
        elif kind == "object":
            rules.append(Rule(key, tid, comment, ground=[ROOM], obj=[prefab]))
        elif kind == "door":
            rules.append(Rule(key, tid, comment, ground=[ROOM], obj=[prefab], obj_ew=[prefab + "_ew"]))
        else:
            raise SystemExit(f"種類が違う: {kind}")
    for cmap in range(WALL_FIRST, WALL_LAST + 1):
        for si, sysname in enumerate(WALL_SYSTEMS):
            tid = WALL_BASE + si * 16 + (cmap - WALL_FIRST)
            rules.append(Rule("NH_WALL_%s_%s" % (sysname.upper(), names[cmap][2:].upper()), tid, "%s（%s の迷宮）" % (names[cmap], sysname),
                              structure=["nh_wall_%s_%s" % (sysname, names[cmap][2:])]))      # 向きごとのブロック（D26）
    for ai, atype in enumerate(ALTAR_TYPES):
        rules.append(Rule("NH_ALTAR_" + atype.upper(), ALTAR_BASE + ai, "祭壇（%s）" % atype, ground=[ROOM], obj=["nh_altar_" + atype]))
    rules.sort(key=lambda r: r.tid)
    ids = [r.tid for r in rules]
    keys = [r.key for r in rules]
    if len(set(ids)) != len(ids) or len(set(keys)) != len(keys):
        raise SystemExit("規則の番号か key が重なる")
    if not all(BASE <= i < LIMIT for i in ids):
        raise SystemExit("番号が帯の外")
    return names, rules


def referenced(rules):
    out = set()
    for r in rules:
        for v in (r.ground, r.obj, r.obj_ew, r.structure):
            if v:
                out.update(v)
    return sorted(out)


def rules_block(rules):
    lines = [BEGIN,
             "    //! 番号の帯は 20000〜20999（20000 + 表示記号／20100 + 壁の系統 × 16 + 壁の種類／20200 + 祭壇の属性）。"
             "指す `nh_*` は tools/nethack/gen_nethack_terrain.py が組み立てのときに作る（追跡しない）。"]
    lines += [r.text() for r in rules]                                      # 後ろに TOband の節が続くので、最後の規則にも , を付ける
    lines.append(END)
    return lines


def apply_block(text, block):
    src = text.split("\n")
    if BEGIN in src:
        a, b = src.index(BEGIN), src.index(END)
        return "\n".join(src[:a] + block + src[b + 1:])
    at = src.index(TOBAND_BEGIN)                                            # 初回は TOband の節の前（DCSS の節の後ろ）へ足す。既存の行は 1 行も変えない
    return "\n".join(src[:at] + block + [""] + src[at:])


def inc_text(names):
    lines = [
        "// 生成物: tools/nethack/gen_nethack_terrain_rules.py。手で直さない。規則（assets/voxel/terrain_prefabs.jsonc の NetHack の節）の `id` と同じ表から作る。",
        "// NetHack のコアが画面へ送る地形番号（`cell.terrain_id`）。表示記号 `cmap`（`glyph_to_cmap()` の値。defsym.h の PCHAR の番号 0〜104）から引く。",
        "// 壁（cmap 1〜11）は迷宮の系統（glyph が持つ。GLYPH_CMAP_MAIN/MINES/GEH/KNOX/SOKO_OFF）で、祭壇（cmap 33）は属性（altar_types）で番号が変わる。",
        f"static constexpr std::uint16_t kNhTerrainBase = {BASE};",
        f"static constexpr std::uint16_t kNhWallBase = {WALL_BASE};    // + 系統 * 16 + (cmap - S_vwall)",
        f"static constexpr std::uint16_t kNhAltarBase = {ALTAR_BASE};   // + altar_types（unaligned 0・chaotic 1・neutral 2・lawful 3・other 4）",
        "enum NhWallSystem : int { kNhWallMain = 0, kNhWallMines = 1, kNhWallGehennom = 2, kNhWallKnox = 3, kNhWallSokoban = 4 };",
        "constexpr std::uint16_t nh_wall_terrain(int system, int cmap) { return static_cast<std::uint16_t>(kNhWallBase + system * 16 + (cmap - 1)); }",
        "constexpr std::uint16_t nh_altar_terrain(int altar_type) { return static_cast<std::uint16_t>(kNhAltarBase + altar_type); }",
        "// 表示記号 -> 地形番号。壁（1〜11）は通常の系統の番号。祭壇（33）は中立の祭壇。0 は描かない（S_stone と効果の記号）。",
        f"static const std::uint16_t kNhCmapTerrain[{CMAP_COUNT}] = {{",
    ]
    for cmap in range(CMAP_COUNT):
        if cmap in HIDDEN:
            tid = 0
        elif WALL_FIRST <= cmap <= WALL_LAST:
            tid = WALL_BASE + (cmap - WALL_FIRST)
        else:
            tid = BASE + cmap
        lines.append(f"    /* {cmap:3d} {names[cmap]} */ {tid},")
    lines.append("};")
    return "\n".join(lines) + "\n"


def other_ids(text):
    """NetHack の節の外の id（ほかのコアの番号）。"""
    src = text.split("\n")
    if BEGIN in src:
        a, b = src.index(BEGIN), src.index(END)
        src = src[:a] + src[b + 1:]
    return [int(x) for x in re.findall(r'"id":\s*(\d+)', "\n".join(src))]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--files", action="store_true", help="規則が指すプレハブが assets/voxel にあることも見る（--check と使う）")
    ap.add_argument("--inc", default=str(INC_DEFAULT), help="コア側の表の出力先")
    args = ap.parse_args()
    names, rules = build_rules()
    sys.path.insert(0, str(HERE.parent))
    import gen_nethack_terrain as G  # 名前の集合だけを使う（タイルは読まない）
    known = set(G.NAMES)
    prefabs = referenced(rules)
    unknown = [n for n in prefabs if n not in known]
    if unknown:
        print("gen_nethack_terrain.py が作らない名前を指している:", ", ".join(unknown))
        return 1
    unused = sorted(known - set(prefabs))
    text = PREFABS.read_text(encoding="utf-8")
    clash = sorted(set(other_ids(text)) & {r.tid for r in rules})
    if clash:
        print("ほかのコアの番号とぶつかる:", clash)
        return 1
    new_text = apply_block(text, rules_block(rules))
    inc = inc_text(names)
    print(f"規則 {len(rules)} 行（表示記号 {len(CMAP_RULES)} 行〔祭壇の中立を含む〕・壁 {(WALL_LAST - WALL_FIRST + 1) * len(WALL_SYSTEMS)} 行・"
          f"祭壇 {len(ALTAR_TYPES)} 行）、指すプレハブ {len(prefabs)} 個（作る物 {len(known)} 個のうち使われない {len(unused)} 個: {', '.join(unused) or 'なし'}）")
    if args.check:
        ok = True
        have_inc = Path(args.inc).read_text(encoding="utf-8") if Path(args.inc).is_file() else ""
        for what, same in (("terrain_prefabs.jsonc の NetHack の節", new_text == text), ("nh_terrain_ids.inc", have_inc == inc)):
            print(f"  {what}: {'最新' if same else '古い（作り直しが要る）'}")
            ok = ok and same
        if unused:
            print("  NG: 作るのに使われないプレハブがある")
            ok = False
        if args.files:
            miss = [n for n in prefabs if not (PREFABS.parent / (n + ".vox")).is_file() or not (PREFABS.parent / (n + ".jsonc")).is_file()]
            print(f"  assets/voxel のプレハブ: {'そろっている' if not miss else '無い ' + ', '.join(miss)}")
            ok = ok and not miss
        return 0 if ok else 1
    PREFABS.write_text(new_text, encoding="utf-8", newline="\n")
    Path(args.inc).parent.mkdir(parents=True, exist_ok=True)
    Path(args.inc).write_text(inc, encoding="utf-8", newline="\n")
    print(f"書いた: {PREFABS.relative_to(REPO)}・{args.inc}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
