#!/usr/bin/env python3
"""ゲーム内ヘルプの訳（angband/lang/ja/help/*.ja.txt）から angband/lib-ja/help/*.txt を作成する。

 #12・§10 P5・§18。前例は frox の fc_build_ja_help.py・Sil-Q。

原作の show_file()（ui-help.c）は 1 行を Term の 0 桁目から書き、折り返さない。Term の幅（80）を
超えた字は Term の段で黙って切られ、どの検査にも映らない（term-truncation-is-invisible-to-checks）。
だから作成する段で**桁**を数え、78 桁に収める。桁は an_wide.c の an_wcwidth() と同じ規則で数える
（全角 2・それ以外 1）。

訳の正本の書き方:

  - 1 行がそのまま 1 行になる。78 桁を超える行だけ折る（折り目は全角どうしの境か半角の空白。
    行頭の禁則「、。」）』ー・小書きのかな ほか」と行末の禁則「「『（ ほか」を守る）。続きの行は
    元の行の頭の空白と同じだけ字下げする。
  - `#!` で始まる行は道具への指示か註釈で、作成物には出ない。`#! stops 39` のように書くと、以降の行の
    タブ（\\t）を「次の桁まで空白で埋める」意味に読む（表の欄をそろえる）。欄の中身と次の欄の間は
    2 桁以上あける（画面側は 2 つ以上の空白で欄を分ける）。足りなければ exit 1。
  - `===` や `---` だけの行は、直前の行の桁数に合わせて長さを直す（見出しの下線）。
  - `.. ` で始まる行（RST の指示。目次の `.. menu:: [a] commands.txt`）は英語と 1 字も違えない。
    英語のファイルと同じ指示が同じ順で並んでいなければ exit 1（ファイル名を訳すと開けなくなる）。

    python tools/angband/an_build_ja_help.py build    # 作成する（78 桁を超える行が残れば exit 1）
    python tools/angband/an_build_ja_help.py check    # 作成物を数え直す（80 桁超え・78 桁超え・英字の残り）

改行は英語の lib/help の同じファイルに合わせる（原作の file_getl() はどちらも読む）。
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "angband" / "lang" / "ja" / "help"
EN = ROOT / "angband" / "lib" / "help"
OUT = ROOT / "angband" / "lib-ja" / "help"
FILES = ["index", "r_index", "commands", "r_comm", "symbols"]
WIDTH = 78
TERM = 80

KINSOKU_HEAD = set("、。，．」』）】〕］｝〉》！？ー・：；ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ)]},.:;!?%")
KINSOKU_TAIL = set("「『（【〔［｛〈《([{")


def wcwidth(ch: str) -> int:
    """angband/adapter/an_wide.c の an_wcwidth() の写し。"""
    wc = ord(ch)
    if wc < 0x1100:
        return 1
    if (wc <= 0x115F or 0x2E80 <= wc <= 0x303E or 0x3041 <= wc <= 0x33FF or 0x3400 <= wc <= 0x4DBF
            or 0x4E00 <= wc <= 0x9FFF or 0xA000 <= wc <= 0xA4CF or 0xAC00 <= wc <= 0xD7A3
            or 0xF900 <= wc <= 0xFAFF or 0xFE30 <= wc <= 0xFE4F or 0xFF00 <= wc <= 0xFF60
            or 0xFFE0 <= wc <= 0xFFE6):
        return 2
    return 1


def cols(s: str) -> int:
    return sum(wcwidth(c) for c in s)


def odd_chars(s: str) -> list[str]:
    """an_wcwidth() が 1 と数えるのに画では幅を取りうる字（… ― ※ など）。訳に使わない。"""
    return [c for c in s if ord(c) >= 0x1100 and wcwidth(c) == 1] + [c for c in s if 0x80 <= ord(c) < 0x1100]


def wrap(line: str, width: int = WIDTH) -> list[str]:
    """78 桁に折る。全角の境か半角の空白で折り、禁則を守る。"""
    if cols(line) <= width:
        return [line]
    indent = len(line) - len(line.lstrip(" "))
    out: list[str] = []
    rest = line
    while cols(rest) > width:
        # width に収まる最長の頭
        c = 0
        fit = 0
        for i, ch in enumerate(rest):
            if c + wcwidth(ch) > width:
                break
            c += wcwidth(ch)
            fit = i + 1
        cut = -1
        lo = indent + 1
        for k in range(fit, lo - 1, -1):
            if k >= len(rest):
                continue
            a, b = rest[k - 1], rest[k]
            if b == " ":
                cut = k
                break
            if a == " ":
                cut = k
                break
            if (wcwidth(a) == 2 or wcwidth(b) == 2) and b not in KINSOKU_HEAD and a not in KINSOKU_TAIL:
                cut = k
                break
        if cut <= indent:
            raise ValueError(f"折れる所が無い: {line!r}")
        head = rest[:cut].rstrip(" ")
        out.append(head)
        rest = " " * indent + rest[cut:].lstrip(" ")
    out.append(rest)
    return out


def detab(line: str, stops: list[int], where: str) -> str:
    parts = line.split("\t")
    s = parts[0].rstrip(" ")
    for i, part in enumerate(parts[1:]):
        if i >= len(stops):
            raise ValueError(f"{where}: タブが欄の数（stops）より多い")
        stop = stops[i]
        if cols(s) > stop - 2:
            raise ValueError(f"{where}: 欄が次の欄（{stop} 桁目）まで 2 桁あかない（{cols(s)} 桁）: {s!r}")
        s = s + " " * (stop - cols(s)) + part.rstrip(" ")
    return s


def directives(text: str) -> list[str]:
    return [ln.rstrip() for ln in text.splitlines() if ln.startswith(".. ")]


def build_one(name: str) -> tuple[str, list[str]]:
    src = (SRC / f"{name}.ja.txt").read_text(encoding="utf-8")
    en_bytes = (EN / f"{name}.txt").read_bytes()
    eol = "\r\n" if b"\r\n" in en_bytes else "\n"
    en_text = en_bytes.decode("utf-8")
    errors: list[str] = []
    stops: list[int] = []
    lines: list[str] = []
    for no, raw in enumerate(src.splitlines(), 1):
        where = f"{name}.ja.txt:{no}"
        if raw.startswith("#!"):
            m = re.match(r"#!\s*stops\s*([0-9, ]*)$", raw)
            if not m:
                continue  # 註釈
            stops = [int(x) for x in m.group(1).replace(" ", "").split(",") if x]
            continue
        line = raw.rstrip(" ")
        for c in odd_chars(line):
            errors.append(f"{where}: 桁の数え方が画と食い違いうる字 {c!r}（U+{ord(c):04X}）")
        if line.startswith(".. "):
            lines.append(line)
            continue
        if "\t" in line:
            try:
                line = detab(line, stops, where)
            except ValueError as e:
                errors.append(str(e))
                continue
            if cols(line) > WIDTH:  # 表の行は折らない（折ると欄が崩れる）
                errors.append(f"{where}: 表の行が {cols(line)} 桁（上限 {WIDTH}）: {line!r}")
                continue
            lines.append(line)
            continue
        bare = line.strip()
        if len(bare) >= 3 and set(bare) in ({"="}, {"-"}) and lines:
            line = bare[0] * cols(lines[-1])
        try:
            lines.extend(wrap(line))
        except ValueError as e:
            errors.append(f"{where}: {e}")
    if directives("\n".join(lines)) != directives(en_text):
        errors.append(f"{name}: `.. ` の指示が英語と違う: ja={directives(chr(10).join(lines))} en={directives(en_text)}")
    for no, ln in enumerate(lines, 1):
        if cols(ln) > WIDTH:
            errors.append(f"{name}.txt:{no}: {cols(ln)} 桁（上限 {WIDTH}）: {ln!r}")
    return eol.join(lines) + eol, errors


def check() -> int:
    bad = 0
    total = 0
    for name in FILES:
        path = OUT / f"{name}.txt"
        if not path.is_file():
            print(f"無い: {path}")
            bad += 1
            continue
        for no, ln in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            total += 1
            c = cols(ln)
            if c > TERM:
                print(f"{name}.txt:{no}: 80 桁超え {c}: {ln}")
                bad += 1
            elif c > WIDTH:
                print(f"{name}.txt:{no}: 78 桁超え {c}: {ln}")
                bad += 1
    print(f"lib-ja/help: {len(FILES)} 本・{total} 行を数えた。80 桁超え・78 桁超え {bad}")
    return 1 if bad else 0


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["build", "check"])
    args = ap.parse_args()
    if args.cmd == "check":
        return check()
    OUT.mkdir(parents=True, exist_ok=True)
    all_errors: list[str] = []
    for name in FILES:
        text, errors = build_one(name)
        all_errors.extend(errors)
        if not errors:
            (OUT / f"{name}.txt").write_bytes(text.encode("utf-8"))
            print(f"{name}.txt: {text.count(chr(10))} 行")
    for e in all_errors:
        print(e, file=sys.stderr)
    if all_errors:
        print(f"失敗 {len(all_errors)} 件（そのファイルは作成していない）", file=sys.stderr)
        return 1
    return check()


if __name__ == "__main__":
    sys.exit(main())
