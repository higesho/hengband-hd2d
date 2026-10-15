#!/usr/bin/env python3
"""町の検査（`--town-check`）を、関門で別コアの町と「検査の検査」にも通す。

関門の `town-check` は既定の町（変愚の辺境の地）だけを読む。2026-10-10 に足した直しの再発防止に、次も流す:

* **Frox の町**: アナンバール（`t_ana`）とタロス（`t_thalos`）。`tools/frox/fc_town_to_hengband.py` で
  変愚の形へ起こし、`HD2D_STYLE_CORE=frox` で読む。期待は `RESULT: PASS`。
* **検査の検査**: 捨てた規則へ戻す `HD2D_BREAK_TOWN` を付けると、対応する検査が落ちること。
  検査が何も見ていない（数えるだけで落とさない）状態になっていないかを確かめる。

  | 町 | `HD2D_BREAK_TOWN` | 落ちるべき検査 |
  | --- | --- | --- |
  | 変愚のモリバント | `gate_dir`（門の向きを重心からの遠さだけで選ぶ） | (11) |
  | 変愚のモリバント | `crag`（階段に接した塊を岩山にする） | (10) |
  | 幻想蛮怒 12 番 | `alley`（別の敷地の壁に挟まれた路地を門にする） | (12) |

  幻想蛮怒 12 番は、捨てた規則を付けない読みで (8)（門の前が道）と (12) が落ちないことも見る
  （町全体の `RESULT` は、前からの看板ほかの不足で落ちるので見ない）。

使い方（関門が呼ぶ。手で流すときも同じ）::

    python tools/hd2d_verify/town_meta_check.py <作業フォルダ> [HengbandHd2d.exe のパス]

実行体を省くと、このスクリプトの 2 つ上（開発ツリーかサンドボックスの根）の `HengbandHd2d.exe` を使う。
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXE = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "HengbandHd2d.exe"


def run_town_check(data: str, env_extra: dict[str, str]) -> tuple[int, str]:
    env = dict(os.environ)
    env.pop("HD2D_BREAK_TOWN", None)
    env.update({"HENGBAND_MUTE": "1"})
    env.update(env_extra)
    proc = subprocess.run([str(EXE), "--town-check", f"--town-data={data}"], cwd=ROOT, env=env,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
    return proc.returncode, proc.stdout.decode("utf-8", errors="replace")


def flagged(text: str, num: int) -> bool:
    """検査 (num) が落ちた印（`  (num) **…**`）があるか。"""
    return re.search(rf"^\s*\({num}\) \*\*", text, re.M) is not None


def main() -> int:
    work = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "scratch_old" / "town_meta_check"
    work.mkdir(parents=True, exist_ok=True)
    if not EXE.exists():
        print(f"FAIL 実行体が無い: {EXE}")
        print("RESULT: FAIL")
        return 1
    failures = 0

    def report(ok: bool, title: str, text: str) -> None:
        nonlocal failures
        print(f"{'PASS' if ok else 'FAIL'} {title}")
        if not ok:
            failures += 1
            tail = [l for l in text.splitlines() if l.strip() and not l.startswith("[hd2d][gl]")][-12:]
            for line in tail:
                print(f"    | {line}")

    # ---- Frox の町（変愚の形へ起こしてから読む） ----
    for name, out_name in (("t_ana", "05_Anambar.txt"), ("t_thalos", "06_Thalos.txt")):
        out = work / out_name
        conv = subprocess.run([sys.executable, str(ROOT / "tools" / "frox" / "fc_town_to_hengband.py"), name, "-o", str(out)],
                              cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        if conv.returncode != 0 or not out.exists():
            report(False, f"Frox {name} を変愚の形へ起こせない", conv.stdout.decode("utf-8", errors="replace"))
            continue
        _, text = run_town_check(str(out), {"HD2D_STYLE_CORE": "frox"})
        report("RESULT: PASS" in text, f"Frox {name}（{out_name}）の町の検査が通る", text)

    # ---- 検査の検査 ----
    morivant = "lib/edit/towns/03_Morivant.jsonc"
    for brk, num in (("gate_dir", 11), ("crag", 10)):
        _, text = run_town_check(morivant, {"HD2D_BREAK_TOWN": brk})
        report(flagged(text, num) and "RESULT: FAIL" in text,
               f"変愚のモリバントで HD2D_BREAK_TOWN={brk} にすると ({num}) が落ちる", text)
    _, text = run_town_check(morivant, {})
    report(not flagged(text, 10) and not flagged(text, 11) and not flagged(text, 12),
           "変愚のモリバントは捨てた規則を付けなければ (10)(11)(12) が落ちない", text)

    genso12 = "gensoband/lib/edit/t0000012.txt"
    _, text = run_town_check(genso12, {"HD2D_STYLE_CORE": "gensoband", "HD2D_BREAK_TOWN": "alley"})
    report(flagged(text, 12), "幻想蛮怒 12 番で HD2D_BREAK_TOWN=alley にすると (12) が落ちる", text)
    _, text = run_town_check(genso12, {"HD2D_STYLE_CORE": "gensoband"})
    report(not flagged(text, 8) and not flagged(text, 12), "幻想蛮怒 12 番は (8)（門の前が道）と (12)（路地）が落ちない", text)

    print(f"RESULT: {'PASS' if failures == 0 else 'FAIL'}")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
