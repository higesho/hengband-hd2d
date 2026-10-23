#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""変愚蛮怒・短愚蛮怒: 名前が長い敵を見ても、見る帯（`l`）が出ることの実機の検査（日本語）。

    python tools/hd2d_verify/look_band_long_name_check.py --heng-dir <HengbandCore.exe の置き場> --tang-dir <TangbandCore.exe の置き場> [--core hengband|tangband] [--work <作業フォルダ>]

**サンドボックスの中で流す**（`tools/gate/sandbox.py`。開発ツリーのセーブ・cfg は書かない）。音は鳴らさない。
**起動したコアは自分で終わらせる**。コア 1 本あたり上限 `DEADLINE_S` 秒。

## 何を確かめるか

原作の見るコマンドは行 0 に `[??]ターゲット:<名前>(レベル???, <状態>, <属性>) [r思 q止 t決 p自 o現 +次 -前]` を出す。
Term は 80 桁なので、名前が長いと行の終わりの操作の案内（`q止`・`<dir>`）が落ちる。以前の共用の判定
（`presentation/bridge/prompt_semantics.h` の `analyze_command_prompt()`）は案内を手がかりにしていたので、
落ちると見る帯が出ず、パッドの案内が消えた。直した判定（`look_row_was_cut()`）が実機で働くことを、次で確かめる。

敵を 2 種呼び（`^A n`）、敵の行動を指定する入口（`presentation/bridge/hengband_test_monster_act.h`）で止めて
（`hold all on`）プレイヤーの隣へ置き（`place`）、`l` で見て `+` で進み、帯（頭が `見る: `）の説明に敵の名前が読める段を探す:

  - 山猫（r_info 62。対照。名前が短く、案内が落ちない）… 帯が出る。名前が読める
  - サルマンの間者『ヘビの舌』（r_info 137。ユニーク。全角 13 字＝26 桁。行が 80 桁を超えて案内が落ちる）… 帯が出る。名前が読める

どちらも帯に `+`（次の注目地点）と `p`（自分の位置へ戻る）の選択肢があること。

英語は実機の検査を書いていない（変愚・短愚の実機の駆動は日本語の画面の文言に頼っている）。英語の切れた行は
`HengbandHd2d.exe` の入力の自己検査（`hd2d/app/checks/input_checks.cpp` の `analyze_command_prompt` の節）で押さえる。

終了コード 0 = 全部通った。
"""
from __future__ import annotations

import argparse
import os
import sys
import time
import traceback
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import monster_act_check as M  # noqa: E402  (サンドボックス・セーブの写し・敵の呼び出し・命令ファイルの道具をそのまま使う)

sbx = M.sbx
CoreError = M.CoreError

#: コア 1 本あたりの上限（秒）
DEADLINE_S = 420.0
#: 帯の頭（日本語）
BAND_HEAD = "見る: "
#: (種族番号, 説明, 帯の説明に読めるはずの名前)
CASES = [
    (62, "山猫（短い名前・対照）", "山猫"),
    (137, "サルマンの間者『ヘビの舌』（長い名前）", "サルマンの間者『ヘビの舌』"),
]


def look_band_for(s: "M.Session", needle: str, max_steps: int) -> tuple[bool, dict]:
    """`l` で見て `+` で進み、帯の説明に `needle` が読める段を探す。(見つかったか, その段の帯の情報) を返す。
    帯の情報: text（帯の字）・keys（選択肢のキー）・head_ok（頭が `見る: `）。見終わったら Esc で抜ける。"""
    c = s.core
    c.keys("l")
    first = c.prompt_text()
    info: dict = {}
    found = False
    for step in range(max_steps):
        p = (c.frame or {}).get("prompt") or {}
        text = p.get("text_utf8") or ""
        if needle in text:
            info = {"text": text, "keys": [ch.get("key") for ch in p.get("choices", [])], "head_ok": text.startswith(BAND_HEAD)}
            found = True
            break
        c.keys("+")
        if step > 0 and c.prompt_text() == first:
            break
    c.keys("\\e")
    s.tidy()
    return found, info


def run_core(core_key: str, exe_dir: Path, work: Path) -> M.Checks:
    ck = M.Checks()
    print(f"=== {core_key} ===", flush=True)
    sb = sbx.Sandbox.create(work / f"lb_{core_key}", exe_dir=exe_dir)
    sb.force_silent_cfg()
    s = None
    t0 = time.monotonic()
    try:
        entry = M.install(sb, core_key, "dungeon")
        stale = sb.root / "scratch_old" / "monster_act.txt"
        if stale.exists():
            stale.unlink()
        s = M.Session(sb, core_key, with_env=True, tag=f"lookband_{core_key}")
        s.core_key_for_manifest = core_key
        s.load(entry)
        s.ready()
        s.dbg("w")
        s.ready()
        new, _ = s.act("hold all on\n", wait_verdicts=1)
        ck.add(M.verdict_kinds(new) == ["ok"], "検査用の入口で敵を止められた（hold all on）", f"{new}")
        px, py = s.player()
        used: set[tuple[int, int]] = set()
        for race, what, needle in CASES:
            if time.monotonic() - t0 > DEADLINE_S:
                ck.add(False, f"上限時間（{DEADLINE_S:.0f} 秒）")
                break
            before = set(s.slot_map())
            s.summon(race)
            fresh = [slot for slot, (r, _x, _y) in s.slot_map().items() if r == race and slot not in before]
            if not ck.add(len(fresh) == 1, f"{what}を呼べた（m_idx が画面から分かる）", f"{s.slot_map()}"):
                continue
            slot = fresh[0]
            placed = None
            # 呼んだ敵が最初から隣にいるなら、置き直さない（廊下で隣のマスが 2 つしか無いとき、先に置いた敵のほかに空きが無い）。
            _r, cx, cy = s.slot_map()[slot]
            if max(abs(cx - px), abs(cy - py)) == 1 and (cx - px, cy - py) not in used:
                placed = (cx - px, cy - py)
            for dx, dy in ([] if placed else [c for c in s.candidates(1, 1) if c not in used][:8]):
                res, _ = s.act(f"place {slot} {dx} {dy}\n", wait_verdicts=1)
                if res and M.is_ok(res[0]) and s.slot_map().get(slot, (0, -1, -1))[1:] == (px + dx, py + dy):
                    placed = (dx, dy)
                    break
            if not ck.add(placed is not None, f"{what}をプレイヤーの隣へ置けた"):
                continue
            used.add(placed)
            found, info = look_band_for(s, needle, 12)
            keys = info.get("keys", [])
            ck.add(found and info.get("head_ok") and (ord("+") in keys) and (ord("p") in keys),
                   f"{what}: 見る帯が出て、名前が読める",
                   f"needle={needle!r} band={info.get('text', '')[:100]!r} keys={keys}")
        ck.add(s.core.proc.poll() is None, "検査の終わりまでコアが生きている")
    except Exception as e:  # noqa: BLE001
        ck.add(False, f"例外 {type(e).__name__}: {e}", traceback.format_exc(limit=4))
    finally:
        tail = ""
        if s is not None:
            tail = s.core.stderr_tail(8)
            s.core.close()
        if ck.ng:
            print("--- 標準エラーの末尾 ---\n" + tail, flush=True)
        try:
            sb.destroy()
        except OSError:
            pass
    print(f"  => {core_key}: OK {ck.ok} / NG {ck.ng}", flush=True)
    return ck


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--heng-dir", help="HengbandCore.exe の置き場")
    ap.add_argument("--tang-dir", help="TangbandCore.exe の置き場")
    ap.add_argument("--core", action="append", choices=sorted(M.EXES))
    ap.add_argument("--work", default=str(Path(os.environ.get("TEMP", str(M.REPO / ".taskwork"))) / "hb_lb"), help="作業フォルダ。データ置き場のパスが長いと読み込みで -続く- が出るので短めに")
    args = ap.parse_args()
    dirs = {"hengband": args.heng_dir, "tangband": args.tang_dir}
    cores = args.core or [c for c in sorted(M.EXES) if dirs[c]]
    work = Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    total_ng = 0
    for core in cores:
        exe_dir = Path(dirs[core]) if dirs[core] else M.REPO
        if not (exe_dir / M.EXES[core]).is_file():
            print(f"[NG] 実行体が無い: {exe_dir / M.EXES[core]}")
            total_ng += 1
            continue
        total_ng += run_core(core, exe_dir, work).ng
    print("RESULT", "FAIL" if total_ng else "PASS", flush=True)
    return 1 if total_ng else 0


if __name__ == "__main__":
    sys.exit(main())
