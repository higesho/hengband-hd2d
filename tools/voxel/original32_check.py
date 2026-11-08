#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""変愚蛮怒・短愚蛮怒: 実体の板を公式の 32 画素のタイル（Original32）の組へ切り替えられることの検査（コアを標準入出力で駆動する）。

    python tools/voxel/original32_check.py [--core hengband|tangband] [--exe-dir DIR] [--work DIR]

必ずサンドボックス（tools/gate/sandbox.py）で流す（開発ツリーのセーブ・cfg は書かない）。
音は鳴らさない（HENGBAND_MUTE=1）。1 コアごとに上限時間（DEADLINE_S）。起こしたプロセスは自分で終わらせる。

## 何を確かめるか（コアごと）

  1. 握手で `entity-tiles-original32` を申告する。目録に 32 画素の板（種類 `G`・`H32_<枠>`・番号 40000 ＋枠）が
     `kOriginal32Frames` の数だけ載り、どの板のファイル（`assets/voxel/slab/H32_<枠>.vox` / `.jsonc`）も在る
  2. `entity_tiles` を送らない（画面が 64 画素を選んだ）ときは、プレイヤー・敵とも 32 画素の板でない
  3. `entity_tiles=original32` を送ると、プレイヤーが表のプレイヤーの枠（職業 × 2 ＋ 性別の白の枠）、呼んだ敵（大白ヘビ）が
     表の怪物の枠（本体と同じく見かけの種族といまの表示色で引いた枠）、足元に落としたアイテムが表のアイテムの枠になる
  4. `entity_tiles=official8` を送ると 8 画素の板（`H8_`）になる（同時に 32 画素にはならない）
  5. `entity_tiles` を戻すと 64 画素の組に戻る
終了コード 0 = 全部通った。
"""
from __future__ import annotations

import argparse
import re
import sys
import time
import traceback
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / "tools" / "gate" / "fixtures"))
sys.path.insert(0, str(REPO / "tools" / "gate"))
sys.path.insert(0, str(REPO / "tools" / "hd2d_verify"))

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

import monster_act_check as M  # noqa: E402  (Session・install・Checks をそのまま使う)
import sandbox as sbx  # noqa: E402
from fxdriver import C_FG, C_GX, C_GY, C_MONSTER, C_OBJECT, C_TILE, default_ui_state, player_pos  # noqa: E402

TABLE = REPO / "presentation" / "bridge" / "original32_tile_table.h"
SLAB_DIR = REPO / "assets" / "voxel" / "slab"
DEADLINE_S = 900.0
R_SNAKE = 21


def read_table() -> dict:
    text = TABLE.read_text(encoding="utf-8")

    def const(name: str) -> int:
        return int(re.search(rf"{name} = (\d+);", text).group(1))

    def block(name: str) -> str:
        return text.split(f"{name}[] = {{", 1)[1].split("};", 1)[0]

    monsters = [tuple(int(v) for v in m) for m in re.findall(r"\{ (-?\d+), (\d+), (\d+) \}, // \d+", block("kOriginal32Monsters"))]
    return {
        "base": const("kOriginal32IndexBase"),
        "monster_base": const("kOriginal32MonsterBaseFrame"),
        "item_base": const("kOriginal32ItemBaseFrame"),
        "player_frame": const("kOriginal32PlayerFrame"),
        "player_icons": const("kOriginal32PlayerIcons"),
        "monsters": monsters,
        "item_icons": len(re.findall(r"-?\d+", block("kOriginal32ItemColorStarts"))),
        "frames": [int(v) for v in re.findall(r"\d+", block("kOriginal32Frames"))],
    }


def expected_monster_frame(tab: dict, race: int, fg: int) -> int:
    """本体の monster_catalog_tile_symbol と同じ（presentation_bridge.cpp の original32_monster）。"""
    color_start, entry_color, base_frame = tab["monsters"][race]
    color = fg & 0x0F
    extra = color if color < entry_color else color - 1
    payload = base_frame if (color_start < 0 or color == entry_color) else color_start + extra
    frame = tab["monster_base"] + payload
    return frame if frame in set(tab["frames"]) else tab["monster_base"] + base_frame


class Session(M.Session):
    """asset_manifest の中身も溜める（fxdriver の Core は数えるだけなので）。"""

    def __init__(self, *a, **k) -> None:
        super().__init__(*a, **k)
        self.manifest: list[dict] = []
        core = self.core
        original = core.drain

        def drain(timeout: float) -> int:
            n = original(timeout)
            return n

        # 受け取りの列から asset_manifest を拾う（Core._reader が q へ入れた物を drain が捨てる前に見る）
        q_get = core.q.get

        def get(*args, **kwargs):
            kind, obj = q_get(*args, **kwargs)
            if kind == "asset_manifest":
                self.manifest.extend(obj.get("assets") or [])
            return kind, obj

        core.q.get = get  # type: ignore[assignment]
        core.drain = drain  # type: ignore[assignment]

    def send_tiles(self, tiles: str | None) -> None:
        st = default_ui_state()
        if tiles:
            st["entity_tiles"] = tiles
        self.core.send(st)
        self.core.settle(0.6, 10)


def tile_of(f: dict, x: int, y: int) -> int:
    for c in (f or {}).get("cells") or []:
        if c[C_GX] == x and c[C_GY] == y:
            return int(c[C_TILE])
    return -1


def run_core(core_key: str, exe_dir: Path, work: Path, tab: dict) -> M.Checks:
    ck = M.Checks()
    print(f"=== {core_key} ===", flush=True)
    sb = sbx.Sandbox.create(work / core_key, exe_dir=exe_dir)
    sb.force_silent_cfg()
    s = None
    base = tab["base"]
    frames = set(tab["frames"])
    try:
        entry = M.install(sb, core_key, "dungeon")
        s = Session(sb, core_key, with_env=False, tag=f"o32_{core_key}")
        s.core_key_for_manifest = core_key
        s.load(entry)
        s.ready()

        # 1. 申告と目録
        feats = (s.core.ack or {}).get("features") or []
        ck.add("entity-tiles-original32" in feats, "[1] 握手で entity-tiles-original32 を申告する", str(feats))
        h32 = [a for a in s.manifest if str(a.get("p", "")).startswith("assets/voxel/slab/H32_")]
        ok_entries = all(a.get("k") == "G" and int(a.get("i", -1)) == base + int(a.get("id", -2)) for a in h32)
        ck.add(len(h32) == len(frames) and ok_entries, "[1] 目録に 32 画素の板が表の枠の数だけ・種類 G・番号 40000＋枠で載る",
               f"{len(h32)} / {len(frames)} 件 / 形 {ok_entries}")
        missing = [f for f in frames if not (SLAB_DIR / f"H32_{f}.vox").is_file() or not (SLAB_DIR / f"H32_{f}.jsonc").is_file()]
        ck.add(not missing, "[1] 目録の板のファイルが全部在る", f"無い {len(missing)} 件 {missing[:5]}")

        # 敵を呼び、持ち物を 1 つ足元に落として 1 歩離れる
        s.summon(R_SNAKE)
        f = s.core.ingame_frame() or {}
        start = player_pos(f)
        s.core.keys("d")
        s.core.keys("a")
        s.core.keys("\\r")
        s.ready()
        for step in "64281937":
            s.core.keys(step)
            s.ready()
            if player_pos(s.core.ingame_frame()) != start:
                break
        f = s.core.ingame_frame() or {}
        dropped = next(((c[C_GX], c[C_GY]) for c in f.get("cells") or [] if c[C_OBJECT] and (c[C_GX], c[C_GY]) == tuple(start)), None)
        ck.add(dropped is not None and player_pos(f) != start, "持ち物を 1 つ足元に落として離れた", f"{start} → {player_pos(f)}")

        def snapshot():
            s.core.keys(",")  # 1 ターン過ごしてフレームを出させる
            s.ready()
            fr = s.core.ingame_frame() or {}
            px, py = player_pos(fr)
            snake = next(((c[C_TILE], c[C_FG], c[C_MONSTER]) for c in fr.get("cells") or [] if c[C_MONSTER] == R_SNAKE), None)
            item = tile_of(fr, start[0], start[1]) if dropped else -1
            return tile_of(fr, px, py), snake, item

        def is32(t: int) -> bool:
            return base <= t < base + 0x10000 and (t - base) in frames

        # 2. 送らない（64 画素）
        s.send_tiles(None)
        p, snake, item = snapshot()
        ck.add(p > 0 and not is32(p), "[2] 送らないときプレイヤーは 32 画素の板でない", str(p))
        ck.add(snake is not None and not is32(snake[0]), "[2] 送らないとき敵は 32 画素の板でない", str(snake))

        # 3. original32
        s.send_tiles("original32")
        p, snake, item = snapshot()
        lo = tab["player_frame"]
        ck.add(is32(p) and lo <= p - base < lo + tab["player_icons"], "[3] プレイヤーが表のプレイヤーの枠（白）", f"{p} → 枠 {p - base}")
        if snake is not None:
            want = base + expected_monster_frame(tab, R_SNAKE, snake[1])
            ck.add(snake[0] == want, "[3] 大白ヘビが本体と同じ引き方の枠（見かけの種族・いまの表示色）", f"{snake[0]} / 期待 {want}（色 {snake[1]}）")
        else:
            ck.add(False, "[3] 大白ヘビが見える")
        if dropped:
            lo_i = tab["item_base"]
            ck.add(is32(item) and item - base >= lo_i, "[3] 落としたアイテムが表のアイテムの枠", f"{item} → 枠 {item - base}")

        # 4. official8
        s.send_tiles("official8")
        p, snake, item = snapshot()
        ck.add(30000 <= p < base and not is32(p), "[4] 8 画素を送るとプレイヤーは 8 画素の板（32 画素でない）", str(p))

        # 5. 戻す
        s.send_tiles(None)
        p, snake, item = snapshot()
        ck.add(p > 0 and not is32(p) and not (30000 <= p < base), "[5] 戻すと 64 画素の組", str(p))
        if snake is not None:
            ck.add(not is32(snake[0]), "[5] 戻すと敵も 64 画素の組", str(snake))
    except Exception as exc:  # noqa: BLE001
        ck.add(False, "検査が途中で止まった", f"{exc}\n{traceback.format_exc()}")
    finally:
        if s is not None:
            try:
                s.core.close()
            except Exception:  # noqa: BLE001
                pass
        sb.destroy()
    return ck


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--core", choices=["hengband", "tangband"], action="append")
    ap.add_argument("--exe-dir", type=Path, default=REPO)
    ap.add_argument("--work", type=Path, default=REPO / ".taskwork" / "original32_check")
    args = ap.parse_args()
    tab = read_table()
    failed = 0
    t0 = time.monotonic()
    for core in args.core or ["hengband", "tangband"]:
        ck = run_core(core, args.exe_dir, args.work, tab)
        print(f"  {core}: OK {ck.ok} / NG {ck.ng}", flush=True)
        failed += ck.ng
    print(f"所要 {time.monotonic() - t0:.0f} 秒 / 落ち {failed}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
