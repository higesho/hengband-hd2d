#!/usr/bin/env python3
"""新 UI のコマンドメニューのタブを 1 巡させる間に、別の画面が挟まらないかを全コアで確かめる。

気づいたこと（2026-09-26）「メニューのタブを切り替える際に一度別の画面を一瞬挟んでないか」を
受けて作った。タブの切り替えのうちコアと往復するのは次の 2 つだけで、ほかのタブ（魔法・行動・
道具・…・機能）は画面側の中だけで切り替わる（`hd2d/ui/feature_menu.cpp`）。

  * 持ち物一覧のタブへ入る … `inventory_open`。出るとき … `cancel`
  * Rogue 系の「設定」タブへ入る … 原作の `o`（`settings_list_rogue`）。出るとき … `cancel`

この往復を、inventory_actions_audit.py と同じ起こし方（作業フォルダのセーブ・上限時間）で
2 巡させ、要求を送ってから目当ての画（入るとき＝入力を受ける一覧、出るとき＝コマンド待ち）が
届くまでにコアが送ったフレームを 1 枚ずつ仕分けて数える:

  * 同じ窓（前の窓・目当ての窓） … よい
  * 窓の無いフレーム … よい（画面側は前のタブの画か、マップを描き続ける。hd2d_app.cpp の
    `menu_handoff` と ModernStoreUi::opening_placeholder()）。数だけ報告する
  * **別の窓**（ほかの種類の窓・中身の無い一覧・入力を受けない一覧・原作の Term の画）… FAIL

    python tools/hd2d_verify/menu_tabs_audit.py all
    python tools/hd2d_verify/menu_tabs_audit.py rogue36,heng

終了コード 0 = 全部 PASS。
"""
from __future__ import annotations

import argparse
import shutil
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import inventory_actions_audit as ia  # noqa: E402

ROGUE_SETTINGS = {"rogue2", "rogue36", "rogue54"}


def classify(frame: dict, allowed: set[str]) -> str:
    """フレームを「same / blank / foreign:<何>」に分ける。allowed は許す窓（kind か content_type）。"""
    m = frame.get("modern_ui") or {}
    stage = m.get("stage") or ""
    if stage:
        ident = m.get("content_type") if m.get("content_type") == "settings_list_rogue" else m.get("kind")
        if ident not in allowed:
            return f"foreign:{m.get('kind')}/{m.get('content_type')}/{stage}"
        if m.get("kind") == "inventory" and stage == "browse" and not m.get("rows"):
            return "foreign:empty-inventory"
        return "same"
    if (frame.get("menu_term_lines") or []) and not frame.get("menu_over_map") and frame.get("menu_open"):
        return "foreign:term"
    return "blank"


def watch(c: ia.Core, start: int, done, allowed: set[str], timeout: float = 8.0) -> tuple[bool, dict]:
    """start 以降のフレームを、done(frame) が真になるまで仕分ける。"""
    counts: dict[str, int] = {}
    end = time.monotonic() + timeout
    i = start
    while time.monotonic() < end and not c.eof:
        c.drain(0.1)
        while i < len(c.frames):
            f = c.frames[i]
            i += 1
            counts["frames"] = counts.get("frames", 0) + 1
            if done(f):
                return True, counts
            kind = classify(f, allowed)
            counts[kind] = counts.get(kind, 0) + 1
    return False, counts


def inventory_ready(rid: int):
    def done(f: dict) -> bool:
        m = f.get("modern_ui") or {}
        return (m.get("kind") == "inventory" and m.get("stage") == "browse" and bool(m.get("rows"))
                and m.get("input_ready", True) and int(m.get("request_id") or 0) >= rid)
    return done


def settings_ready(f: dict) -> bool:
    m = f.get("modern_ui") or {}
    return m.get("content_type") == "settings_list_rogue" and m.get("stage") == "cursor" and m.get("input_ready", True)


def audit_core(spec: ia.CoreSpec, work: Path, rounds: int) -> list[dict]:
    root = work / f"root-{spec.name}"
    root.mkdir(parents=True, exist_ok=True)
    ia.build_root(spec, root)
    pristine = work / f"pristine-{spec.name}"
    if not pristine.exists():
        for rel in spec.copy:
            if (root / rel / "save").is_dir():
                shutil.copytree(root / rel / "save", pristine / rel / "save")
        pristine.mkdir(parents=True, exist_ok=True)
    ia.reset_saves(spec, root, pristine)
    results: list[dict] = []
    c = ia.boot(spec, root, work / f"{spec.name}-tabs.stderr.log")
    try:
        def step(label: str, send, done, allowed: set[str]) -> None:
            start = len(c.frames)
            t0 = time.monotonic()
            send()
            ok, counts = watch(c, start, done, allowed)
            foreign = {k: v for k, v in counts.items() if k.startswith("foreign")}
            status = "PASS" if ok and not foreign else "FAIL"
            detail = "" if ok else f"target never came ({ia.summary(c.f)})"
            if foreign:
                detail = (detail + " " if detail else "") + "foreign " + ",".join(f"{k}={v}" for k, v in foreign.items())
            results.append({"core": spec.name, "step": label, "status": status, "blank": counts.get("blank", 0),
                            "same": counts.get("same", 0), "detail": detail, "ms": int((time.monotonic() - t0) * 1000)})
            print(f"  [{status}] {spec.name:8} {label:22} blank={counts.get('blank', 0):2} same={counts.get('same', 0):2} "
                  f"frames={counts.get('frames', 0):2} {results[-1]['ms']:5}ms {detail}", flush=True)
            c.settle()

        for r in range(rounds):
            holder: dict = {}

            def open_inv() -> None:
                holder["rid"] = c.act("inventory_open", revision=1)
            start = len(c.frames)
            open_inv()
            ok, counts = watch(c, start, inventory_ready(holder["rid"]), {"inventory"})
            foreign = {k: v for k, v in counts.items() if k.startswith("foreign")}
            status = "PASS" if ok and not foreign else "FAIL"
            detail = ("" if ok else f"target never came ({ia.summary(c.f)}) ") + \
                ("foreign " + ",".join(f"{k}={v}" for k, v in foreign.items()) if foreign else "")
            results.append({"core": spec.name, "step": f"#{r + 1} enter inventory tab", "status": status,
                            "blank": counts.get("blank", 0), "same": counts.get("same", 0), "detail": detail.strip(), "ms": 0})
            print(f"  [{status}] {spec.name:8} {'#%d enter inventory tab' % (r + 1):22} blank={counts.get('blank', 0):2} "
                  f"same={counts.get('same', 0):2} frames={counts.get('frames', 0):2} {detail}", flush=True)
            c.settle()
            if not ok:
                break
            step(f"#{r + 1} leave inventory tab", lambda: c.act("cancel"), ia.in_game, {"inventory"})
            if spec.name in ROGUE_SETTINGS:
                step(f"#{r + 1} enter settings tab", lambda: c.keys(ord("o")), settings_ready, set())
                step(f"#{r + 1} leave settings tab", lambda: c.act("cancel"), ia.in_game, {"settings_list_rogue"})
    finally:
        c.close()
    return results


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("cores", help="comma list or 'all': " + ",".join(ia.CORES))
    ap.add_argument("--rounds", type=int, default=2)
    ap.add_argument("--work", type=Path, default=None)
    a = ap.parse_args()
    names = [n for n in ia.CORES if n != "heng-dbg"] if a.cores == "all" else a.cores.split(",")
    temp = None
    work = a.work
    if work is None:
        temp = tempfile.TemporaryDirectory(prefix="menu-tabs-audit-")
        work = Path(temp.name)
    work.mkdir(parents=True, exist_ok=True)
    results: list[dict] = []
    try:
        for n in names:
            print(f"== {n}", flush=True)
            try:
                results += audit_core(ia.CORES[n], work, a.rounds)
            except Exception as exc:  # noqa: BLE001
                results.append({"core": n, "step": "*", "status": "FAIL", "detail": f"setup: {exc}"})
                print(f"  [FAIL] {n} setup: {exc}", flush=True)
    finally:
        if temp is not None:
            temp.cleanup()
    fails = [r for r in results if r["status"] == "FAIL"]
    print(f"\n{len(results) - len(fails)} / {len(results)} passed")
    for r in fails:
        print(f"FAIL {r['core']} {r['step']}: {r['detail']}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
