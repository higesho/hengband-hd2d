# -*- coding: utf-8 -*-
"""DCSS のタイル番号（`tilegen` が作る `tiledef-floor/wall/feat.h`）から、床・壁・地形のタイルの「同じ組」を読む共通部分。

原作のウェブタイル版は、マスの背景を `t.bg`（下位 16 ビットがタイル番号 = `TILE_FLAG_MASK`）で送る。
床と壁は、そのマスのブランチ（枝のダンジョン）・ヴォールトの指定に合わせて**原作がすでに選んだ実際のタイル**
（例 `TILE_FLOOR_ORC_3`）で届く。番号は [0, FLOOR_MAX) が床、[FLOOR_MAX, WALL_MAX) が壁、[WALL_MAX, FEAT_MAX) が地形の物。

タイル番号は `tilegen` の出力で決まる（原作の `rltiles/dc-*.txt` の並び順）。**組んだ DcssCore.exe と同じ生成物**
（`dcss/adapter/prepare_gen.py` の作業フォルダの `gen/rltiles`）から読むこと。

使うもの: `tools/dcss/gen_dcss_terrain_rules.py`（アダプタの表と規則）・`gen_dcss_terrain.py`（壁の組のプレハブ）。
"""
from __future__ import annotations

import os
import tempfile
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEFAULT_WORK = Path(os.environ.get("DCSS_WORK", os.path.join(tempfile.gettempdir(), "dcss_w")))
#: 原作の写し。`rltiles` の定義（`dc-*.txt`）・`tileview.cc`・`tilepick.cc` を読むだけ（書き換えない）
#: 原作の写しを入れた `roguelike-cores` の場所。既定は開発ツリーの隣。関門のサンドボックスの中（親が違う）から流すときは、環境変数 HENGBAND_CORES_ROOT で指す。
CORES_ROOT = Path(os.environ["HENGBAND_CORES_ROOT"]) if os.environ.get("HENGBAND_CORES_ROOT") else REPO.parent / "roguelike-cores"
SOURCE_CANDIDATES = (
    CORES_ROOT / "build-sources" / "dcss" / "crawl-ref" / "source",
    CORES_ROOT / "upstream" / "crawl-ja" / "crawl-ref" / "source",
)


def source_root() -> Path:
    for p in SOURCE_CANDIDATES:
        if (p / "rltiles" / "dc-wall.txt").is_file():
            return p
    raise SystemExit("原作の写し（rltiles/dc-wall.txt）が見つからない: " + ", ".join(map(str, SOURCE_CANDIDATES)))


class Tiles:
    """床・壁・物のタイルの番号表。`gen` は `<作業フォルダ>/gen/rltiles`。"""

    KINDS = ("floor", "wall", "feat")

    def __init__(self, gen_rltiles: Path | str | None = None):
        gen = Path(gen_rltiles) if gen_rltiles else DEFAULT_WORK / "gen" / "rltiles"
        if not (gen / "tiledef-floor.h").is_file():
            raise SystemExit(f"tilegen の出力が無い: {gen}（python tools/dcss/build_core.py か dcss/adapter/prepare_gen.py で作る）")
        self.gen = gen
        self.names: dict[str, int] = {}
        self.first_name: dict[int, str] = {}
        for fn in ("tiledef-floor.h", "tiledef-wall.h", "tiledef-feat.h", "tiledef-main.h"):
            self._parse_enum(gen / fn)
        self.floor_max = self.names["TILE_FLOOR_MAX"]
        self.wall_max = self.names["TILE_WALL_MAX"]
        self.feat_max = self.names["TILE_FEAT_MAX"]
        self.offset = {"floor": 0, "wall": self.floor_max, "feat": self.wall_max}
        self.limit = {"floor": self.floor_max, "wall": self.wall_max, "feat": self.feat_max}
        self.count: dict[str, list[int]] = {}
        self.base: dict[str, list[int]] = {}
        self.tile_name: dict[str, list[str]] = {}
        for k in self.KINDS:
            text = (gen / f"tiledef-{k}.cc").read_text(encoding="utf-8")
            self.count[k] = self._array(text, f"_tile_{k}_count")
            self.base[k] = self._array(text, f"_tile_{k}_basetiles")
            m = re.search(r"_tile_" + k + r"_name\[[^\]]*\]\s*=\s*\{(.*?)\};", text, re.S)
            self.tile_name[k] = re.findall(r'"([^"]*)"', m.group(1)) if m else []
            n = self.limit[k] - self.offset[k]
            if len(self.count[k]) != n or len(self.base[k]) != n:
                raise SystemExit(f"tiledef-{k}.cc の表の長さが合わない: {len(self.count[k])}/{len(self.base[k])} != {n}")

    # ---- 読み取り
    def _parse_enum(self, path: Path) -> None:
        val = 0
        for line in path.read_text(encoding="utf-8").splitlines():
            line = line.split("//")[0].strip().rstrip(",")
            m = re.match(r"(\w+)\s*(?:=\s*(\w+))?$", line)
            if not m or line.startswith("enum") or line in ("{", "}"):
                continue
            n, v = m.group(1), m.group(2)
            if v is not None:
                val = int(v) if v.isdigit() else self.names[v]
            self.names[n] = val
            self.first_name.setdefault(val, n)
            val += 1

    @staticmethod
    def _array(text: str, name: str) -> list[int]:
        m = re.search(re.escape(name) + r"\[[^\]]*\]\s*=\s*\{([^}]*)\}", text)
        if not m:
            raise SystemExit(f"{name} の表が読めない")
        return [int(x) for x in re.findall(r"-?\d+", m.group(1))]

    # ---- 問い合わせ
    def kind_of(self, tile: int) -> str:
        if 0 <= tile < self.floor_max:
            return "floor"
        if tile < self.wall_max:
            return "wall"
        if tile < self.feat_max:
            return "feat"
        return ""

    def family(self, tile: int) -> tuple[int, int]:
        """タイル番号 -> (組の先頭の番号, 組の枚数)。"""
        k = self.kind_of(tile)
        if not k:
            raise KeyError(tile)
        i = tile - self.offset[k]
        start = self.base[k][i] + self.offset[k]            # 表の値は種別の先頭からの相対
        c = self.count[k][start - self.offset[k]]
        return start, c

    def families(self, kind: str) -> list[tuple[int, int]]:
        """種別の全部の組 [(先頭, 枚数)]（番号順）。"""
        out, i, off = [], 0, self.offset[kind]
        cs = self.count[kind]
        while i < len(cs):
            out.append((off + i, cs[i]))
            i += max(1, cs[i])
        return out

    def name_of(self, tile: int) -> str:
        """タイルの名前（`tilegen` の名前の表のもの。`TILE_` は付かない。例 `WALL_ORC`、`FLOOR_GREY_DIRT_3`）。"""
        k = self.kind_of(tile)
        names = self.tile_name.get(k, [])
        i = tile - self.offset[k]
        return names[i] if 0 <= i < len(names) else f"#{tile}"

    def aliases_at(self, tile: int) -> list[str]:
        return [n for n, v in self.names.items() if v == tile]


# ---------------------------------------------------------------- 定義ファイル（rltiles/dc-*.txt）から絵の元を引く
def first_png_of_names(defs_file: str, wanted: set[str]) -> dict[str, str]:
    """`dc-wall.txt` などの定義から、列挙名（`WALL_ORC` など）-> 最初の絵（`rltiles` からの相対。拡張子なし）。
    列挙名を持つ行の最初の絵だけを見る。行の書き方は `<絵> [<列挙名> ...]`、`%sdir <置き場>` で置き場が替わる。"""
    root = source_root() / "rltiles"
    out: dict[str, str] = {}
    sdir = ""
    for line in (root / defs_file).read_text(encoding="utf-8", errors="replace").splitlines():
        t = line.split()
        if not t or t[0].startswith("#"):
            continue
        if t[0] == "%sdir":
            sdir = t[1] if len(t) > 1 else ""
            continue
        if t[0].startswith("%"):
            continue
        for n in t[1:]:
            if n in wanted and n not in out:
                png = (sdir + "/" + t[0]) if sdir else t[0]
                if (root / (png + ".png")).is_file():
                    out[n] = png
    return out


def branch_wall_names() -> list[str]:
    """ブランチごとの壁として原作が選ぶタイルの名前（`TILE_` を除く）。
    読む所は 2 か所: `tileview.cc` の `tile_default_flv`（ブランチの既定の壁・床）と、
    `tilepick.cc` の `_apply_branch_tile_overrides`（積み石・金属の壁の差し替え）。"""
    src = source_root()
    t1 = (src / "tileview.cc").read_text(encoding="utf-8", errors="replace")
    a = t1.index("void tile_default_flv")
    seg = t1[a:t1.index("\n}\n", a)]
    t2 = (src / "tilepick.cc").read_text(encoding="utf-8", errors="replace")
    a = t2.index("static tileidx_t _apply_branch_tile_overrides")
    seg += t2[a:t2.index("\n}\n", a)]
    return sorted({m[len("TILE_"):] for m in re.findall(r"TILE_(?:WALL|ROCK_WALL)_\w+", seg)}
                  | {"DNGN_CRYSTAL_WALL", "DNGN_METAL_WALL", "DNGN_STONE_WALL"} | {"DNGN_METAL_IRON", "DNGN_METAL_ZOT"}
                  # 迷宮・深層の壁の別の積み方、ヴォールトの石の壁、蔦の煉瓦（実機で出た。`dc_m1_check.py` の「表に無いタイル」の申告から足した）
                  | {"WALL_BRICK_DARK_2", "WALL_BRICK_DARK_3", "WALL_BRICK_DARK_4", "WALL_BRICK_DARK_5", "WALL_BRICK_DARK_6",
                     "STONE_WALL_VAULT", "WALL_BRICK_VINES"})


def names_to_png(defs_file: str) -> dict[str, str]:
    """定義ファイルの全部の列挙名 -> 最初の絵（`rltiles` からの相対。拡張子なし）。"""
    root = source_root() / "rltiles"
    out: dict[str, str] = {}
    sdir = ""
    for line in (root / defs_file).read_text(encoding="utf-8", errors="replace").splitlines():
        t = line.split()
        if not t or t[0].startswith("#"):
            continue
        if t[0] == "%sdir":
            sdir = t[1] if len(t) > 1 else ""
            continue
        if t[0].startswith("%"):
            continue
        png = (sdir + "/" + t[0]) if sdir else t[0]
        if not (root / (png + ".png")).is_file():
            continue
        for n in t[1:]:
            out.setdefault(n, png)
    return out


def wall_prefab_name(canonical: str) -> str:
    """壁の組の名前（`tilegen` の名前の表のもの。例 `WALL_ORC`）-> 新しく作るプレハブの名前 `dc_wall_orc`。"""
    n = canonical
    for p in ("ROCK_WALL_", "WALL_", "DNGN_"):
        if n.startswith(p):
            n = n[len(p):] if p != "ROCK_WALL_" else "rock_" + n[len(p):]
            break
    return "dc_wall_" + n.lower()


def wall_family_pngs(tiles: Tiles) -> dict[int, tuple[str, str]]:
    """壁の組の先頭のタイル番号 -> (代表の名前, 最初の絵)。絵が見つからない組（色替えで作る物）は入れない。"""
    by_name = names_to_png("dc-wall.txt")
    out: dict[int, tuple[str, str]] = {}
    for start, _count in tiles.families("wall"):
        canon = tiles.name_of(start)
        for cand in [canon] + [a[len("TILE_"):] for a in tiles.aliases_at(start) if a.startswith("TILE_")]:
            if cand in by_name:
                out[start] = (canon, by_name[cand])
                break
    return out
