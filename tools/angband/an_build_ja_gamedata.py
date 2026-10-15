#!/usr/bin/env python3
"""Angband の説明文の日本語化。

angband/lang/ja/desc/<file>.ja.tsv（訳文の元データ）を読み、angband/lib/gamedata/*.txt の
**説明の行だけ**を日本語に置き換えた写しを angband/lib-ja/gamedata/ へ作成する。

  * 英語の写し（angband/lib/gamedata/）は 1 バイトも変えない（制約 3）。
  * `name:` を含むほかの行は 1 バイトも変えない。作成のたびに「置き換える行を除いた行の並びが
    英語と同じ」ことを確かめ、違えば exit 1（§6.4「desc: 以外 0 行」）。
  * 訳の鍵はレコードの英語名（§6.1 と同じ。アイテムは `<type>:<name>`、職業の呪文は
    `<職業>/<呪文>`）。2 列目は英語原文の SHA-1 の頭 10 桁で、**上流が原文を変えたら訳は
    使わず英語のまま出す**（制約 5。`count` が「古い訳」として数える）。
  * 変愚の説明文は写さない（J7・制約 8）。

    python tools/angband/an_build_ja_gamedata.py count            # 出どころと訳の数を数える
    python tools/angband/an_build_ja_gamedata.py build            # lib-ja/gamedata を作成する
    python tools/angband/an_build_ja_gamedata.py dump FILE OUT    # 未訳を「鍵 \\t 英語」で書き出す
    python tools/angband/an_build_ja_gamedata.py import FILE IN   # 「鍵 \\t 日本語」を元データへ足す

TSV の書式: `鍵 \\t 原文のハッシュ \\t 訳文`。`#` で始まる行は註釈。
"""
from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GD = ROOT / "angband" / "lib" / "gamedata"
OUT = ROOT / "angband" / "lib-ja" / "gamedata"
DESC = ROOT / "angband" / "lang" / "ja" / "desc"

# 1 行の上限。parser は 1024 バイトの領域で 1 行を読む（z-file.c の file_getl）。
LINE_BYTES = 900


@dataclass
class FieldSpec:
    kind: str          # desc（説明文）／frag（英語の文へ差し込まれる句）／msg（メッセージ）
    join: bool         # True: 続く同じ指示の行を 1 つに繋ぐ（string_append）
    prefix: int = 0    # 値の前に残すコロン区切りの欄の数（monster.txt の message-vis:SPELL:…）


@dataclass
class FileSpec:
    record: tuple[str, ...]                 # レコードの頭になる指示
    key: str                                # 鍵の型（{name} {type} {spell} {class} など）
    fields: dict[str, FieldSpec]


D = FieldSpec("desc", True)
F = FieldSpec("frag", True)

# 設計 §6.4 の対象。ここに無い指示は `count` の「見ていないもの」に出る。
SPECS: dict[str, FileSpec] = {
    "monster.txt": FileSpec(("name",), "{name}", {
        "desc": D,
        "message-vis": FieldSpec("msg", False, 1),
        "message-invis": FieldSpec("msg", False, 1),
    }),
    "monster_base.txt": FileSpec(("name",), "{name}", {"desc": F}),
    # ここから下の msg 系は §6.4 の列挙に無かったが、gamedata の字で P2 のカタログからは
    # 引きにくい（`msg("%s", …)` で丸ごと来る）ので P3 で足した（本書 §15 に記録）。
    "chest_trap.txt": FileSpec(("name",), "{name}", {"msg": FieldSpec("msg", False), "msg-death": FieldSpec("frag", False)}),
    "shape.txt": FileSpec(("name",), "{name}", {"effect-msg": FieldSpec("frag", False)}),
    "object.txt": FileSpec(("name",), "{type}:{name}", {"desc": D, "msg": FieldSpec("msg", False), "vis-msg": FieldSpec("msg", False)}),
    "artifact.txt": FileSpec(("name",), "{name}", {"desc": D, "msg": FieldSpec("msg", False)}),
    "ego_item.txt": FileSpec(("name",), "{name}", {"desc": D}),
    "activation.txt": FileSpec(("name",), "{name}", {"desc": D, "msg": FieldSpec("msg", False)}),
    # `title:`（称号。左の状態列・人物表・店主の挨拶・墓に出る。画面の札なので P4 で足した。設計 §17）。
    # 鍵は `<職業>/?|title`・`#2`〜`#10`（呪文より前に並ぶので spell は無い）。状態列の 13 桁に収める。
    "class.txt": FileSpec(("name", "spell"), "{class}/{spell}", {"desc": D, "effect-msg": FieldSpec("frag", False),
                                                                 "title": FieldSpec("label", False)}),
    "curse.txt": FileSpec(("name",), "{name}", {"desc": F, "msg": FieldSpec("msg", False)}),
    "trap.txt": FileSpec(("name",), "{name}", {"desc": D, "msg": FieldSpec("msg", False), "msg-good": FieldSpec("msg", False),
                                               "msg-bad": FieldSpec("msg", False), "msg-xtra": FieldSpec("msg", False)}),
    "terrain.txt": FileSpec(("name",), "{name}", {"desc": D, "walk-msg": FieldSpec("msg", False), "run-msg": FieldSpec("msg", False),
                                                  "hurt-msg": FieldSpec("msg", False), "die-msg": FieldSpec("frag", False),
                                                  "confused-msg": FieldSpec("msg", False)}),
    "player_property.txt": FileSpec(("type",), "{type}:{code}", {"desc": D}),
    "player_timed.txt": FileSpec(("name",), "{name}", {"desc": F, "on-end": FieldSpec("msg", False), "on-increase": FieldSpec("msg", False),
                                                        "on-decrease": FieldSpec("msg", False)}),
    "object_property.txt": FileSpec(("type",), "{type}:{code}", {"desc": F, "msg": FieldSpec("msg", False)}),
    "projection.txt": FileSpec(("code",), "{type}:{code}", {
        "desc": F, "player-desc": F, "blind-desc": F, "lash-desc": F,
    }),
    "summon.txt": FileSpec(("name",), "{name}", {"desc": F}),
    "blow_effects.txt": FileSpec(("name",), "{name}", {"desc": F}),
    "blow_methods.txt": FileSpec(("name",), "{name}", {"desc": F, "act": FieldSpec("msg", False)}),
    "monster_spell.txt": FileSpec(("name",), "{name}", {
        "lore": FieldSpec("frag", False),
        "message-vis": FieldSpec("msg", False),
        "message-invis": FieldSpec("msg", False),
        "message-miss": FieldSpec("msg", False),
        "message-save": FieldSpec("msg", False),
    }),
    "pain.txt": FileSpec(("type",), "{type}", {"message": FieldSpec("msg", False)}),
    "hints.txt": FileSpec((), "hint", {"H": FieldSpec("desc", False)}),
    "history.txt": FileSpec(("chart",), "{chart}", {"phrase": FieldSpec("desc", False)}),
}

# 数えるが訳さない（理由つき）。`count` が表に出す。
NOT_SHOWN = {
    ("ui_entry.txt", "desc"): "ui-entry.c:2228 が読み捨てる（画に出ない）",
    ("ui_entry_base.txt", "desc"): "ui-entry.c:2228 が読み捨てる（画に出ない）",
    ("old_class.txt", "*"): "どこからも読まれない（init.c に登録が無い）",
}
_ID = "名前か識別子（別のレコードを名前で引く・P1 の名前の表の範囲）"
_UI = "画面の札（P4。桁の上限がある）"
_P2 = "英語の文へ差し込まれる語（P2 のカタログで引く。本表は訳さない）"
for _f, _ds, _why in [
    ("artifact.txt", ("base-object", "curse"), _ID),
    ("blow_effects.txt", ("lore-color-base", "lore-color-immune", "lore-color-resist"), _ID),
    ("monster_spell.txt", ("lore-color-base", "lore-color-immune", "lore-color-resist"), _ID),
    ("body.txt", ("slot",), _UI),
    ("class.txt", ("book", "equip", "spell"), _ID),
    ("curse.txt", ("conflict", "type"), _ID),
    ("dungeon_profile.txt", ("room",), _ID),
    ("ego_item.txt", ("item", "type"), _ID),
    ("flavor.txt", ("fixed", "flavor"), _ID),
    ("monster.txt", ("base", "drop", "drop-base", "friends", "friends-base", "mimic", "shape"), _ID),
    ("monster.txt", ("plural",), "群れの名。日本語では単数と同じ字（設計 §6.2）"),
    ("object.txt", ("type",), _ID),
    ("object_base.txt", ("graphics",), _ID),
    ("object_property.txt", ("id-type", "subtype", "type-mult"), _ID),
    ("pit.txt", ("mon-ban", "mon-base"), _ID),
    ("player_timed.txt", ("grade",), _UI),
    ("projection.txt", ("color",), _ID),
    ("quest.txt", ("race",), _ID),
    ("realm.txt", ("book-noun",), _P2),
    ("room_template.txt", ("D", "tval"), _ID),
    ("vault.txt", ("D", "type"), _ID),
    ("slay.txt", ("melee-verb", "range-verb"), _P2),
    ("store.txt", ("always", "buy", "normal", "owner"), _ID),
    ("summon.txt", ("base",), _ID),
    ("terrain.txt", ("look-prefix",), _P2),
    ("trap.txt", ("graphics",), _ID),
    ("ui_entry.txt", ("label",), _UI),
    ("ui_knowledge.txt", ("mcat-include-base", "monster-category"), _ID),
    ("visuals.txt", ("flicker",), _ID),
]:
    for _d in _ds:
        NOT_SHOWN[(_f, _d)] = _why


def text_hash(s: str) -> str:
    return hashlib.sha1(s.encode("utf-8")).hexdigest()[:10]


@dataclass
class Entry:
    file: str
    key: str
    directive: str
    kind: str
    prefix: str             # 値の前に残す字（"desc:" や "message-vis:BR_FIRE:"）
    lines: list[int] = field(default_factory=list)
    parts: list[str] = field(default_factory=list)

    @property
    def en(self) -> str:
        return "".join(self.parts)


def read_lines(path: Path) -> tuple[list[str], str]:
    raw = path.read_bytes().decode("utf-8")
    eol = "\r\n" if "\r\n" in raw else "\n"
    lines = raw.split(eol)
    return lines, eol


def split_directive(line: str) -> tuple[str, str] | None:
    if not line or line.startswith("#"):
        return None
    if ":" not in line:
        return None
    d, v = line.split(":", 1)
    return d, v


def parse_entries(fname: str, lines: list[str]) -> list[Entry]:
    spec = SPECS[fname]
    entries: list[Entry] = []
    state: dict[str, str] = {}
    pending: list[Entry] = []      # 鍵が決まる前（レコードの途中）の項目
    counts: dict[str, int] = {}
    prev: Entry | None = None
    prev_line = -2

    def close_record():
        nonlocal pending
        for e in pending:
            base = spec.key.format_map(_Default(state_snap[id(e)]))
            base = f"{base}|{e.directive}"
            n = counts.get(base, 0) + 1
            counts[base] = n
            e.key = base if n == 1 else f"{base}#{n}"
            entries.append(e)
        pending = []

    state_snap: dict[int, dict[str, str]] = {}

    for i, line in enumerate(lines):
        sd = split_directive(line)
        if sd is None:
            continue
        d, v = sd
        if d in spec.record:
            if d == spec.record[0]:
                close_record()
                # 上位のレコードが替わったら、それより下の状態（下位のレコード・code・type）を消す
                keep = {"class"} if fname == "class.txt" else set()
                for r in list(state):
                    if r not in keep:
                        state.pop(r, None)
            else:
                close_record()
            state[d] = v.split(":", 1)[0] if d in ("spell", "type", "code") else v
            if fname == "class.txt" and d == "name":
                state["class"] = v
            if fname == "trap.txt" and d == "name":
                state["name"] = v.split(":", 1)[0]
            prev = None
            continue
        if d in ("type",) and "type" not in spec.record:
            state["type"] = v
            for e in pending:
                state_snap[id(e)]["type"] = v
        if d == "code" and "code" not in spec.record:
            state["code"] = v
            for e in pending:
                state_snap[id(e)]["code"] = v
        fs = spec.fields.get(d)
        if fs is None:
            prev = None
            continue
        pre_fields = v.split(":", fs.prefix) if fs.prefix else [v]
        if fs.prefix and len(pre_fields) <= fs.prefix:
            prev = None
            continue
        value = pre_fields[-1]
        prefix = d + ":" + "".join(p + ":" for p in pre_fields[:-1])
        if fs.join and prev is not None and prev.directive == d and prev_line == i - 1:
            prev.lines.append(i)
            prev.parts.append(value)
            prev_line = i
            continue
        e = Entry(fname, "", d, fs.kind, prefix, [i], [value])
        if fs.prefix:
            e.directive = d + ":" + pre_fields[0]
        state_snap[id(e)] = dict(state)
        pending.append(e)
        prev = e
        prev_line = i
    close_record()
    return entries


class _Default(dict):
    def __missing__(self, k):
        return "?"


def load_tsv(fname: str) -> dict[str, tuple[str, str]]:
    path = DESC / (Path(fname).stem + ".ja.tsv")
    out: dict[str, tuple[str, str]] = {}
    if not path.exists():
        return out
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        cols = line.split("\t")
        if len(cols) != 3:
            raise SystemExit(f"{path}:{n}: 列が 3 つでない")
        if cols[0] in out:
            raise SystemExit(f"{path}:{n}: 鍵が重なっている: {cols[0]}")
        out[cols[0]] = (cols[1], cols[2])
    return out


def chunk(prefix: str, text: str) -> list[str]:
    """1 行が LINE_BYTES を超えないよう字の境で割る（parser は同じ指示の行を繋ぐ）。"""
    out: list[str] = []
    cur = ""
    for ch in text:
        if len((prefix + cur + ch).encode("utf-8")) > LINE_BYTES:
            out.append(prefix + cur)
            cur = ""
        cur += ch
    out.append(prefix + cur)
    return out


def all_files() -> list[str]:
    return sorted(p.name for p in GD.glob("*.txt"))


def cmd_count(args) -> int:
    rows = []
    tot = {}
    for fname in SPECS:
        lines, _ = read_lines(GD / fname)
        entries = parse_entries(fname, lines)
        tsv = load_tsv(fname)
        by = {}
        for e in entries:
            k = (fname, e.directive.split(":")[0], e.kind)
            r = by.setdefault(k, [0, 0, 0, 0, 0])
            r[0] += 1
            r[1] += len(e.en)
            t = tsv.get(e.key)
            if t is None:
                r[4] += 1
            elif t[0] != text_hash(e.en):
                r[3] += 1
            else:
                r[2] += 1
        keys = {e.key for e in entries}
        orphan = [k for k in tsv if k not in keys]
        for k, r in by.items():
            rows.append((*k, *r))
            t = tot.setdefault(k[2], [0, 0, 0, 0, 0])
            for j in range(5):
                t[j] += r[j]
        if orphan:
            print(f"!! {fname}: 元データにあって gamedata に無い鍵 {len(orphan)}: {orphan[:5]}")
    print(f"{'file':22} {'field':14} {'kind':5} {'entries':>7} {'en_chars':>8} {'ja':>6} {'stale':>6} {'missing':>7}")
    for r in rows:
        print(f"{r[0]:22} {r[1]:14} {r[2]:5} {r[3]:7} {r[4]:8} {r[5]:6} {r[6]:6} {r[7]:7}")
    print("-- 種類ごと（desc＝説明文・frag＝英語の文へ差し込まれる句・msg＝メッセージ）")
    for k, t in tot.items():
        print(f"   {k:5} 項目 {t[0]:5}  英字 {t[1]:7}  訳 {t[2]:5}  古い訳 {t[3]:4}  未訳 {t[4]:5}")

    # 見ていないもの: 対象外のファイル・指示のうち、英語の文らしい値を持つもの
    print("-- この道具が見ていないもの（対象外の指示で、英語の句を持つ行）")
    for fname in all_files():
        lines, _ = read_lines(GD / fname)
        spec = SPECS.get(fname)
        seen: dict[str, int] = {}
        for line in lines:
            sd = split_directive(line)
            if sd is None:
                continue
            d, v = sd
            if spec and d in spec.fields:
                continue
            if re.search(r"[A-Za-z]{2,}[ ,.!?']+[A-Za-z]", v) and d not in ("name",):
                seen[d] = seen.get(d, 0) + 1
        for d, n in sorted(seen.items()):
            why = NOT_SHOWN.get((fname, d)) or NOT_SHOWN.get((fname, "*")) or ""
            print(f"   {fname:22} {d:18} {n:5}  {why}")
    print("   ＋ コードの側で英語の文を組んで句を差し込む所（frag の前後の字）は P2 のカタログの範囲")
    print("   ＋ 名前（name: など）は P1 の names/*.ja.tsv の範囲")
    return 0


def build_file(fname: str, report: list[str]) -> tuple[bytes, int, int]:
    src = GD / fname
    raw = src.read_bytes()
    if fname not in SPECS:
        return raw, 0, 0
    lines, eol = read_lines(src)
    entries = parse_entries(fname, lines)
    tsv = load_tsv(fname)
    replace: dict[int, list[str]] = {}
    drop: set[int] = set()
    used = 0
    for e in entries:
        t = tsv.get(e.key)
        if t is None or t[0] != text_hash(e.en):
            continue
        ja = t[1]
        new_lines = chunk(e.prefix, ja) if FIELD_JOIN(fname, e) else [e.prefix + ja]
        replace[e.lines[0]] = new_lines
        for li in e.lines[1:]:
            drop.add(li)
        used += 1
    out: list[str] = []
    for i, line in enumerate(lines):
        if i in drop:
            continue
        if i in replace:
            out.extend(replace[i])
        else:
            out.append(line)
    # 確かめ: 置き換えの対象の指示を除けば、行の並びは英語と同じ
    touched = {e.lines[0] for e in entries} | {li for e in entries for li in e.lines}
    a = [l for i, l in enumerate(lines) if i not in touched]
    ja_entries_lines = set()
    fields = SPECS[fname].fields
    b = []
    for l in out:
        sd = split_directive(l)
        if sd and sd[0] in fields:
            continue
        b.append(l)
    if a != b:
        for x, y in zip(a, b):
            if x != y:
                report.append(f"{fname}: 説明以外の行が違う: {x!r} / {y!r}")
                break
        else:
            report.append(f"{fname}: 説明以外の行の数が違う {len(a)} / {len(b)}")
    return eol.join(out).encode("utf-8"), used, len(entries)


def FIELD_JOIN(fname: str, e: Entry) -> bool:
    return SPECS[fname].fields[e.directive.split(":")[0]].join


def cmd_build(args) -> int:
    report: list[str] = []
    OUT.mkdir(parents=True, exist_ok=True)
    total_used = total = 0
    for p in sorted(GD.iterdir()):
        if p.is_dir():
            continue
        data, used, n = build_file(p.name, report)
        dst = OUT / p.name
        if not dst.exists() or dst.read_bytes() != data:
            dst.write_bytes(data)
        total_used += used
        total += n
    # 英語側に無くなった物を消す（作成物の置き場の中だけ）
    for q in OUT.iterdir():
        if q.is_file() and not (GD / q.name).exists():
            q.unlink()
    check_history_bytes(report)
    for r in report:
        print("!!", r)
    print(f"lib-ja/gamedata: 項目 {total} のうち訳を当てた {total_used}")
    return 1 if report else 0


def check_history_bytes(report: list[str]) -> None:
    """生い立ち（history.txt の phrase を表の順に継いだ字）がセーブに収まるか。

    セーブは `rd_string(player->history, 250)`（load.c:663）で読むので、継いだ字は 249 バイトまで。
    日本語は 1 字 3 バイトなので、最も長くなる道筋を種族ごとに数える。
    """
    import functools
    path = OUT / "history.txt"
    if not path.exists():
        return
    charts: dict[int, list[tuple[int, str]]] = {}
    cur = None
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("chart:"):
            a = line.split(":")
            cur = (int(a[1]), int(a[2]))
        elif line.startswith("phrase:") and cur:
            charts.setdefault(cur[0], []).append((cur[1], line[len("phrase:"):]))

    @functools.lru_cache(None)
    def longest(c: int) -> int:
        if c == 0 or c not in charts:
            return 0
        return max(len(v.encode("utf-8")) + longest(n) for n, v in charts[c])

    races = (OUT / "p_race.txt").read_text(encoding="utf-8")
    for start in sorted({int(x) for x in re.findall(r"^history:(\d+)", races, re.M)}):
        n = longest(start)
        if n > 249:
            report.append(f"history.txt: 表 {start} から継ぐ生い立ちが最長 {n} バイト（セーブは 249 まで）")


def cmd_dump(args) -> int:
    fname = args.file
    lines, _ = read_lines(GD / fname)
    tsv = load_tsv(fname)
    n = 0
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        for e in parse_entries(fname, lines):
            t = tsv.get(e.key)
            if t is not None and t[0] == text_hash(e.en):
                continue
            f.write(f"{e.key}\t{e.en}\n")
            n += 1
    print(f"{fname}: 未訳 {n}")
    return 0


def cmd_import(args) -> int:
    fname = args.file
    lines, _ = read_lines(GD / fname)
    entries = {e.key: e for e in parse_entries(fname, lines)}
    path = DESC / (Path(fname).stem + ".ja.tsv")
    DESC.mkdir(parents=True, exist_ok=True)
    cur = load_tsv(fname)
    bad = 0
    for n, line in enumerate(Path(args.inp).read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        if "\t" not in line:
            print(f"!! {args.inp}:{n}: タブが無い: {line[:60]}")
            bad += 1
            continue
        k, ja = line.split("\t", 1)
        ja = ja.strip()
        if k not in entries:
            print(f"!! {args.inp}:{n}: 鍵が gamedata に無い: {k}")
            bad += 1
            continue
        if "\t" in ja or not ja:
            print(f"!! {args.inp}:{n}: 訳が空かタブを含む: {k}")
            bad += 1
            continue
        cur[k] = (text_hash(entries[k].en), ja)
    order = [e for e in entries]  # gamedata の並び
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"# {path.name} — {fname} の説明の訳（変愚の説明文は写していない）。\n")
        f.write("# 鍵 \\t 英語原文の SHA-1 の頭 10 桁 \\t 訳文。原文が変われば訳は使われない（tools/angband/an_build_ja_gamedata.py count が「古い訳」と数える）。\n")
        for k in order:
            if k in cur:
                f.write(f"{k}\t{cur[k][0]}\t{cur[k][1]}\n")
        for k in cur:
            if k not in entries:
                f.write(f"{k}\t{cur[k][0]}\t{cur[k][1]}\n")
    print(f"{fname}: 元データ {len(cur)} 件（取り込みの傷 {bad}）")
    return 1 if bad else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("count")
    sub.add_parser("build")
    p = sub.add_parser("dump"); p.add_argument("file"); p.add_argument("out")
    p = sub.add_parser("import"); p.add_argument("file"); p.add_argument("inp")
    args = ap.parse_args()
    return {"count": cmd_count, "build": cmd_build, "dump": cmd_dump, "import": cmd_import}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
