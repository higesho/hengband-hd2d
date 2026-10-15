#!/usr/bin/env python3
"""レベル 10 の能力値の選択と、その手前の -続く- を、変愚・短愚蛮怒・幻想蛮怒で確かめる。

気づいたこと（2026-10-07、Frox の FH-24）「レベル 10 の能力値の画面に入る際に操作を受け付けなくなった」
を受けて、同じ形の穴が変愚系・幻想蛮怒にもないかを確かめる道具。次の 3 つを見る。

  (a) 原作が -続く- で待っているとき、frame の prompt に -続く- が載っているか
      （画面側がこれを見て決定のボタンを出す。載らないと、画面に何も出ないまま原作が待つ）。
  (b) 能力値の選択（「どの能力値を上げますか」）で、選択肢が `a`〜`f` の 6 つだけで、
      括弧の中の数字を選択肢と読まず、名札が「(現在値 …)」の後ろまで残っているか。
  (c) -続く- を決定で送ったあと、能力値の選択の画面へ進み、決定→「はい」→ コマンド待ちまで通るか。

1 回ごとにコアを起こし直す。データは一時フォルダへ写す（開発ツリーのセーブには触らない）。
音は鳴らさない（`HENGBAND_MUTE=1`）。上限時間は 1 本あたり `--timeout` 秒（既定 150）。

    python tools/hd2d_verify/more_stat_choice_audit.py heng,tang,genso
    python tools/hd2d_verify/more_stat_choice_audit.py heng --lang en

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

STAT_PROMPTS = ("どの能力値を上げますか", "Which stat do you want to raise")


def keys_from(text: str) -> list[int]:
    return [ord(c) for c in text]


def frame_text(frame: dict) -> str:
    return "\n".join(x.get("text_utf8", "") for x in frame.get("menu_term_lines") or [])


def brief(frame: dict) -> str:
    prompt = (frame.get("prompt") or {})
    msgs = frame.get("messages") or []
    return "aw=%s menu=%s prompt=%r choices=%d msg=%r" % (
        frame.get("awaiting_command"), frame.get("menu_open"), (prompt.get("text_utf8") or "")[:50],
        len(prompt.get("choices") or []), (msgs[-1].get("text_utf8", "") if msgs else "")[:50])


def more_visible(frame: dict) -> bool:
    """-続く- が画面側に伝わる形で載っているか（prompt か、行 0 のミラー）。"""
    from_prompt = (frame.get("prompt") or {}).get("text_utf8") or ""
    if any(m in from_prompt for m in ("-続く-", "-more-", "［続く］")):
        return True
    for line in frame.get("menu_term_lines") or []:
        if line.get("source_row", 0) == 0 and any(m in line.get("text_utf8", "") for m in ("-続く-", "-more-", "［続く］")):
            return True
    return False


def run_core(name: str, work: Path, timeout: float, verbose: bool) -> list[tuple[str, bool, str]]:
    spec = ia.CORES[name]
    root = work / f"root-{name}"
    root.mkdir(parents=True, exist_ok=True)
    ia.build_root(spec, root)
    log = work / f"{name}.stderr.log"
    results: list[tuple[str, bool, str]] = []

    def check(label: str, ok: bool, detail: str = "") -> None:
        results.append((label, ok, detail))
        print(f"  [{'PASS' if ok else 'FAIL'}] {name:6} {label} {detail}", flush=True)

    c = ia.boot(spec, root, log)
    t_end = time.monotonic() + timeout
    try:
        # 画面側と同じく新 UI を立てたまま、原作のキーを直接送る（-続く- とメニューの見え方は frame の中身で見る）。
        # デバッグ（ウィザード）コマンドに入る。^A → 確認 y でメニューが開く（以後は ^A でメニューが開く）。
        # メニューの x ＝「経験値を得る」。経験値を倍々に増やして、レベル 10 を越えさせる。
        if name == "genso":
            # 幻想蛮怒は新規キャラでデバッグ・ウィザードが使えない。オプション（= → 4 ページ → 14 番目の
            # 「デバッグ/詐欺オプションを許可する」を y）を立ててから ^W（案内の -続く- が 2 回出る）→ y。
            for k in [ord("="), ord("4")] + [ord("2")] * 13 + [ord("y"), 27, 27, 23]:
                c.keys(k)
                c.settle(0.4, 6)
            for _ in range(6):
                f = c.f
                if (f.get("prompt") or {}).get("choices"):
                    c.keys(ord("y"))
                    c.settle(0.5, 6)
                    break
                if more_visible(f):
                    c.keys(13)
                    c.settle(0.5, 6)
                    continue
                break
        else:
            if name == "tang":
                # 短愚蛮怒（3.0.2.4 の写し）は、オプション（= → 4 ページ → 12 番目の「デバッグ/詐欺オプションを
                # 許可する」を y）を立てないと ^A が「許可されていません」で返る。
                for k in [ord("="), ord("4")] + [ord("2")] * 11 + [ord("y"), 27, 27]:
                    c.keys(k)
                    c.settle(0.4, 6)
            # 変愚・短愚: ^A → （案内の -続く- があれば Enter）→ 確認 y でデバッグのメニューが開く。
            c.keys(1)
            c.settle(0.6, 8)
            for _ in range(6):
                f = c.f
                if (f.get("prompt") or {}).get("choices"):
                    c.keys(ord("y"))
                    c.settle(0.6, 8)
                    break
                if more_visible(f):
                    c.keys(13)
                    c.settle(0.5, 6)
                    continue
                break
        seen_more = 0
        seen_more_visible = 0
        stat_frame = None
        first = (name != "genso")
        for step in range(80):
            if time.monotonic() > t_end:
                check("level 10 reached within time", False, f"timeout level={(c.f.get('hud') or {}).get('level')}")
                return results
            if not first:
                c.keys(1)
                c.settle(0.4, 6)
            first = False
            c.keys(ord("x"))
            c.settle(0.5, 8)
            for _ in range(12):
                f = c.f
                if any(p in frame_text(f) for p in STAT_PROMPTS):
                    stat_frame = f
                    break
                if f.get("awaiting_command") and not f.get("menu_open"):
                    break
                if more_visible(f):
                    seen_more += 1
                    seen_more_visible += 1
                    if verbose:
                        print("   -more- visible:", brief(f))
                else:
                    # コマンド待ちの印も、-続く- も、問いも見えないのに原作が待っている。
                    seen_more += 1
                    if verbose:
                        print("   waiting with nothing visible:", brief(f), "level=", (f.get("hud") or {}).get("level"))
                c.keys(13)
                c.settle(0.5, 8)
            if stat_frame is not None:
                break
        check("-more- visible while the core waits", seen_more == seen_more_visible,
              f"{seen_more_visible}/{seen_more}")
        if stat_frame is None:
            check("stat choice screen reached", False, brief(c.f))
            return results
        check("stat choice screen reached", True)

        # (b) 選択肢
        choices = stat_frame.get("menu_choices") or []
        keys = [ch.get("key") for ch in choices]
        check("stat choices are exactly a-f", keys == [ord(k) for k in "abcdef"], f"keys={keys}")
        lines = stat_frame.get("menu_term_lines") or []
        bad_label = []
        for ch in choices:
            li = ch.get("line_index")
            if li is None or li >= len(lines):
                continue
            text = lines[li].get("text_utf8", "")
            # 切り出しは UTF-8 のバイト位置
            raw = text.encode("utf-8")
            label = raw[ch.get("span_begin", 0):ch.get("span_begin", 0) + ch.get("span_len", 0)].decode("utf-8", "replace")
            # 名札は閉じ括弧の後ろまで残る（開き括弧と閉じ括弧の数が同じ）
            if verbose:
                print("   label:", repr(label), "key=", ch.get("key"))
            body = label.split(") ", 1)[1] if ") " in label[:6] else label   # 先頭の札 `a)` の閉じ括弧は数えない
            if body.count("(") != body.count(")"):
                bad_label.append(label)
        check("stat labels keep the closing parenthesis", not bad_label, f"{bad_label[:2]}")
        if verbose:
            print("   stat mirror:\n" + "\n".join("     " + l.get("text_utf8", "") for l in lines[:10]))

        # (c) 選択して確認まで通す。カーソル（cursor ui）無しの原作のキーで a → はい。
        c.keys(ord("d"))
        c.settle(0.6, 8)
        asked = frame_text(c.f) + "\n" + ((c.f.get("prompt") or {}).get("text_utf8") or "")
        yn_labels = [(ch.get("label_utf8") or "") for ch in ((c.f.get("prompt") or {}).get("choices") or [])]
        if verbose:
            print("   after pick:", brief(c.f), "labels=", yn_labels)
        check("confirm labels match the language",
              (ia.UI_LANG == "ja") == any(("はい" in l) for l in yn_labels) if yn_labels else False, f"labels={yn_labels}")
        c.keys(ord("y"))
        c.settle(0.8, 10)
        end = time.monotonic() + 15
        stale = []
        while time.monotonic() < end and not ia.in_game(c.f):
            if more_visible(c.f):
                # 選んだ後の -続く- を原作が待っている間は、後ろに残った能力値の画面の選択肢（d など）を
                # 出し続けてはいけない（決定のキーが -続く- ではなく選択肢へ行く）。
                if c.f.get("menu_choices"):
                    stale.append(len(c.f.get("menu_choices")))
                c.keys(13)
            c.settle(0.5, 6)
        check("no stale stat choices while -more- waits", not stale, f"{stale}")
        check("back at the command prompt", ia.in_game(c.f), brief(c.f))
        if verbose and not ia.in_game(c.f):
            print("   final mirror:")
            for l in (c.f.get("menu_term_lines") or [])[:12]:
                print("     |%s| %s" % (l.get("source_row"), l.get("text_utf8", "")[:90]))
    finally:
        c.close()
    return results


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("cores", help="heng,tang,genso のコンマ区切り")
    ap.add_argument("--lang", choices=("ja", "en"), default="ja")
    ap.add_argument("--timeout", type=float, default=150.0)
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    ia.UI_LANG = a.lang
    failed = 0
    with tempfile.TemporaryDirectory(prefix="more-stat-audit-") as tmp:
        for name in a.cores.split(","):
            print(f"== {name} (lang={a.lang})", flush=True)
            try:
                res = run_core(name, Path(tmp), a.timeout, a.verbose)
            except Exception as exc:  # noqa: BLE001
                print(f"  [FAIL] {name} exception: {exc}")
                failed += 1
                continue
            failed += sum(1 for _, ok, _ in res if not ok)
    print("\nFAILED:", failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
